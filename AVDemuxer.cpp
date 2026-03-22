#include "AVDemuxer.h"
#include "CodecWrapper.h"
#include <QDebug>

#include <algorithm>
#include <cctype>

namespace {

std::string toLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool startsWithScheme(const std::string& url, const char* scheme)
{
    const std::string lowerUrl = toLowerCopy(url);
    const std::string prefix = std::string(scheme) + "://";
    return lowerUrl.rfind(prefix, 0) == 0;
}

bool isRtspUrl(const std::string& url)
{
    return startsWithScheme(url, "rtsp");
}

bool isRtmpUrl(const std::string& url)
{
    return startsWithScheme(url, "rtmp");
}

bool isNetworkStreamUrl(const std::string& url)
{
    return isRtspUrl(url) || isRtmpUrl(url);
}

void applyNetworkOptions(const std::string& url, AVDictionary** dict)
{
    if (!isNetworkStreamUrl(url))
    {
        return;
    }

    av_dict_set(dict, "rw_timeout", "15000000", 0);
    av_dict_set(dict, "timeout", "15000000", 0);
    av_dict_set(dict, "buffer_size", "1048576", 0);

    if (isRtspUrl(url))
    {
        // VLC stream could not to be play if set it as tcp in local test
        // av_dict_set(dict, "rtsp_transport", "tcp", 0);
        av_dict_set(dict, "max_delay", "500000", 0);
    }

    if (isRtmpUrl(url))
    {
        av_dict_set(dict, "tcp_nodelay", "1", 0);
    }
}

void freeFormatContext(AVFormatContext*& formatCtx)
{
    if (!formatCtx)
    {
        return;
    }

    if (formatCtx->iformat || formatCtx->pb)
    {
        avformat_close_input(&formatCtx);
        return;
    }

    avformat_free_context(formatCtx);
    formatCtx = nullptr;
}

}  // namespace

AVDemuxer::AVDemuxer() = default;

AVDemuxer::~AVDemuxer() 
{
    closeInput();
}

void AVDemuxer::unLoad()
{
    abortRequest_.store(false);
    closeInput();
}

bool AVDemuxer::getAbortRequest()
{
    return abortRequest_.load();
}

bool AVDemuxer::isEof() const
{
    return eof_.load();
}

void AVDemuxer::abort(bool abortRequest)
{
    abortRequest_.store(abortRequest);
}

void AVDemuxer::closeInput()
{
    availableStream_.clear();
    currentStream_.clear();
    freeFormatContext(formatCtx_);
}

static int decode_interrupt_cb(void* ctx)
{
    auto d = reinterpret_cast<AVDemuxer *>(ctx);
    return d ? int(d->getAbortRequest()) : 0;
}

int AVDemuxer::doLoad(std::string url)
{
    closeInput();

    formatCtx_ = avformat_alloc_context();
    if (!formatCtx_)
    {
        qWarning() << "avformat_alloc_context failed";
        return AVERROR(ENOMEM);
    }

    abortRequest_.store(false);
    eof_.store(false);

    //设置回调，只要ffmpeg进入和可能需要等待的操作，就会反复调用回调。回调返回0，继续等待，返回1，中断操作。
    formatCtx_->flags |= AVFMT_FLAG_GENPTS;
    formatCtx_->interrupt_callback.callback = decode_interrupt_cb;
    formatCtx_->interrupt_callback.opaque = this;

    // 类似于ffmpeg -f,指定输入流的类型，从而避免ffmpeg 通过 avformat_open_input 和 avformat_find_stream_info
    // 自动检测带来的耗时过长等问题。
    const AVInputFormat* inputFormat = nullptr;
    if (!inputFormat_.empty())
    {
        qDebug() << "Loading -f " << inputFormat_;
        inputFormat = av_find_input_format(inputFormat_.c_str());
        if (!inputFormat)
        {
            qDebug() << "could not find input format:" << inputFormat;
        }
    }

    DictionaryStruct dict;

    if (isNetworkStreamUrl(url))
    {
        applyNetworkOptions(url, &dict.dict);
        qDebug() << "Opening network stream:" << url.c_str();
    }

    int ret = avformat_open_input(&formatCtx_, url.c_str(), inputFormat, &dict.dict);

    if (ret < 0)
    {
        qWarning() << "avformat_open_input failed, ret is " << ret;
        closeInput();
        return ret;
    }

    ret = avformat_find_stream_info(formatCtx_, nullptr);
    if (ret < 0)
    {
        qWarning() << "avformat_find_stream_info failed, ret is " << ret;
        closeInput();
        return ret;
    }
    collectStreamInfo(formatCtx_);
    return 1;
}

void AVDemuxer::collectStreamInfo(AVFormatContext* formatCtx)
{
    availableStream_.clear();
    currentStream_.clear();

    for (std::size_t i = 0; i < formatCtx->nb_streams; i++)
    {
        if (!formatCtx->streams[i]->codecpar)
        {
            qWarning() << "could not find codec, index = " << i;
            return;
        }

        const AVMediaType type = formatCtx->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO || type == AVMEDIA_TYPE_AUDIO || type == AVMEDIA_TYPE_SUBTITLE)
        {
            qDebug() << "insert decoder to Map, type is " << type << ",index is " << i;
            auto vedioCodec = std::make_shared<CodecWrapper>(formatCtx->streams[i], "", type);
            availableStream_.insert(std::make_pair(i, vedioCodec));
        }
    }

    const int videoStreamIndex = av_find_best_stream(
        formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex >=0)
        currentStream_[AVMEDIA_TYPE_VIDEO] = availableStream_[videoStreamIndex];
    else
        qWarning() << "could not find VIDEO stream ";

    const int audioStreamIndex = av_find_best_stream(
        formatCtx, AVMEDIA_TYPE_AUDIO, -1, videoStreamIndex, nullptr, 0);
    if (audioStreamIndex >=0)
    {
        currentStream_[AVMEDIA_TYPE_AUDIO] = availableStream_[audioStreamIndex];
        qDebug() << "time base is " << av_q2d(currentStream_[AVMEDIA_TYPE_AUDIO]->getStream()->time_base);
    }
    else
        qWarning() << "could not find AUDIO stream ";

    const int subtitleStreamIndex = av_find_best_stream(
        formatCtx, AVMEDIA_TYPE_SUBTITLE, -1,
        audioStreamIndex >= 0 ? audioStreamIndex : videoStreamIndex, nullptr, 0);
    if (subtitleStreamIndex >=0)
        currentStream_[AVMEDIA_TYPE_SUBTITLE] = availableStream_[subtitleStreamIndex];
    else
        qWarning() << "could not find SUBTITLE stream ";

    qDebug() << "collectStreamInfo finished ";
}

const std::map<int, std::shared_ptr<CodecWrapper>>& AVDemuxer::getAvailableStream() const
{
    return availableStream_;
}

const std::map<int, std::shared_ptr<CodecWrapper>>& AVDemuxer::getCurrentStream() const
{
    return currentStream_;
}

AVFormatContext* AVDemuxer::getFormatCtx()
{
    return formatCtx_;
}

void AVDemuxer::clearEofFlag()
{
    eof_ = false;
}

AVPacketPtr AVDemuxer::readPacket()
{
    if (!formatCtx_)
    {
        return nullptr;
    }

    AVPacketPtr pkt(av_packet_alloc());
    if (!pkt)
    {
        qDebug() << "av_packet_alloc failed";
        return nullptr;
    }

    for (;;)
    {
        int ret = av_read_frame(formatCtx_, pkt.get());

        if (ret == AVERROR_EXIT || abortRequest_.load())
        {
            qDebug() << "av_read_frame interrupted";
            return nullptr;
        }

        if (ret == AVERROR_EOF || (formatCtx_->pb && avio_feof(formatCtx_->pb))) {
            eof_.store(true);
            qDebug() << "eof set as true";
            return nullptr;
        }
        else if (ret < 0)
        {
            qDebug() << "av_read_frame: unexpected result:" << ret;
            return nullptr;
        }
        return pkt;
    }
    return nullptr;
}

AVFramePtr AVDemuxer::decodePacket(int streamIndex, AVPacketPtr packet)
{
    std::lock_guard<std::mutex> locker(mutex_);

    const auto streamIt = availableStream_.find(streamIndex);
    if (streamIt == availableStream_.end() || !streamIt->second)
    {
        qWarning() << "decodePacket invalid stream index:" << streamIndex;
        return nullptr;
    }

    AVCodecContext* ctx = streamIt->second->getAvctx();
    if (!ctx)
    {
        qWarning() << "decodePacket invalid codec context, stream index:" << streamIndex;
        return nullptr;
    }

    AVFramePtr frame(av_frame_alloc());
    if (!frame)
    {
        qWarning() << "av_frame_alloc failed in decodePacket";
        return nullptr;
    }

    int ret = avcodec_send_packet(ctx, packet ? packet.get() : nullptr);

    if (ret == AVERROR(EAGAIN))
    {
        return nullptr;
    }
    if (ret < 0) {
        qWarning() << "avcodec_send_packet unexpected result:" << ret;
        return nullptr;
    }

    for (;;)
    {
        ret = avcodec_receive_frame(ctx, frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            return nullptr;
        }
        if (ret < 0)
        {
            qWarning() << "avcodec_receive_frame unexpected result:" << ret;
            return nullptr;
        }
        return frame;
    }
    qWarning() << "decodePacket failed" << ret;
    return nullptr;
}

void AVDemuxer::flushCodecBuffers(const std::map<int, std::shared_ptr<CodecWrapper>>& streams)
{
    std::lock_guard<std::mutex> locker(mutex_);
    for (const auto& streamEntry : streams)
    {
        const auto& codecWrapper = streamEntry.second;
        if (!codecWrapper)
        {
            continue;
        }

        AVCodecContext* ctx = codecWrapper->getAvctx();
        if (!ctx)
        {
            continue;
        }

        avcodec_flush_buffers(ctx);
    }
}
