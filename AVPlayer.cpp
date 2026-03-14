#include "AVPlayer.h"
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <QCoreApplication>
#include <QDir>
#include <OnnxFrameProcessor.h>

namespace {

inline bool isSeekStale(uint64_t itemSerial, uint64_t currentSerial, bool isSeeking)
{
    return isSeeking || itemSerial != currentSerial;
}

inline bool shouldAbortForSeekOrQuit(uint64_t itemSerial, uint64_t currentSerial, bool isSeeking, bool isQuitting)
{
    return isQuitting || isSeekStale(itemSerial, currentSerial, isSeeking);
}

std::string readOnnxModelFromEnv()
{
    const char* path = std::getenv("MYPLAYER_ONNX_MODEL");
    if (!path)
    {
        return std::string();
    }
    return std::string(path);
}

}  // namespace

AVPlayer::AVPlayer()
    : demuxer_(std::make_shared<AVDemuxer>())
    , videoFilter_(nullptr)
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString modelPath = QDir(appDir).filePath("models/yunet_n_640_640.onnx");
    onnxModelPath_ = modelPath.toStdString();
    qDebug() << "appDir : " << appDir << ", onnxModelPath_ is " << onnxModelPath_;
    setFaceDetectionInputMode(OnnxFrameProcessor::FaceInputMode::FixedSize);
    setFaceDetectionFixedInputSize(640, 640);
}

void AVPlayer::waitForSeekIfNeeded()
{
    if (!isSeek_)
    {
        return;
    }
    std::unique_lock<std::mutex> lock(seekStateMutex_);
    seekCv_.wait(lock, [this] {
        return !isSeek_ || quit_;
    });
}

AVPlayer::~AVPlayer()
{
    stop();
    // 等待所有线程完成
    if (demuxFuture_.isValid()) demuxFuture_.waitForFinished();
    if (videoDecodeFuture_.isValid()) videoDecodeFuture_.waitForFinished();
    if (videoPlayFuture_.isValid()) videoPlayFuture_.waitForFinished();
    if (audioDecodeFuture_.isValid()) audioDecodeFuture_.waitForFinished();
    if (audioPlayFuture_.isValid()) audioPlayFuture_.waitForFinished();
    if (subtitlePlayFuture_.isValid()) subtitlePlayFuture_.waitForFinished();
}

void AVPlayer::setUrl(std::string url)
{
    url_ = url;
}

void AVPlayer::setVideoOnnxModel(const std::string& modelPath)
{
    onnxModelPath_ = modelPath;
}

const std::string& AVPlayer::getVideoOnnxModel() const
{
    return onnxModelPath_;
}

void AVPlayer::setFaceDetectionInputMode(OnnxFrameProcessor::FaceInputMode mode)
{
    faceInputMode_ = mode;
    if (onnxProcessor_)
    {
        onnxProcessor_->setFaceDetectionInputMode(mode);
    }
}

OnnxFrameProcessor::FaceInputMode AVPlayer::faceDetectionInputMode() const
{
    return faceInputMode_;
}

void AVPlayer::setFaceDetectionFixedInputSize(int width, int height)
{
    faceFixedInputWidth_ = std::max(1, width);
    faceFixedInputHeight_ = std::max(1, height);

    if (onnxProcessor_)
    {
        onnxProcessor_->setFaceDetectionFixedInputSize(faceFixedInputWidth_, faceFixedInputHeight_);
    }
}

std::pair<int, int> AVPlayer::faceDetectionFixedInputSize() const
{
    return std::pair<int, int>(faceFixedInputWidth_, faceFixedInputHeight_);
}

double AVPlayer::getCurrentTimeSec()
{
    const double videoSec = currentVideoPtsSec_.load();
    if (audioPlayer_ && audioPlayer_->isAudioClockValid())
    {
        const double audioSec = audioPlayer_->getAudioTime();
        if (audioSec >= 0.0)
        {
            constexpr double kMaxClockGapSec = 3.0;
            if (videoSec > 0.0 && std::fabs(audioSec - videoSec) > kMaxClockGapSec)
            {
                return videoSec;
            }
            return audioSec;
        }
    }
    return videoSec;
}
double AVPlayer::getDurationSec()
{
    AVFormatContext* fmtCtx = demuxer_ ? demuxer_->getFormatCtx() : nullptr;
    if (!fmtCtx || fmtCtx->duration <= 0)
    {
        return 0.0;
    }
    return static_cast<double>(fmtCtx->duration) / AV_TIME_BASE;
}

void AVPlayer::doload()
{
    quit_ = false;
    videoPacketQueue_.setActive(true);
    audioPacketQueue_.setActive(true);
    subtitlePacketQueue_.setActive(true);
    videoFrameQueue_.setActive(true);
    audioFrameQueue_.setActive(true);
    subtitleFrameQueue_.setActive(true);

    if (url_.empty())
    {
        qDebug() << "url is empty , please set url firstly";
        return;
    }

    auto ret = demuxer_->doLoad(url_);
    if (ret != 1)
    {
        qDebug() << "demuxer doLoad failed, ret is " << ret;
        return;
    }

    demuxFuture_ = QtConcurrent::run(&threadPool_, &AVPlayer::doDemux, this);

    currentStreams_ = demuxer_->getCurrentStream();

    if (currentStreams_.find(AVMEDIA_TYPE_VIDEO) != currentStreams_.end())
    {
        videoFilter_.reset();
        onnxProcessor_.reset();

        if (onnxModelPath_.empty())
        {
            onnxModelPath_ = readOnnxModelFromEnv();
        }

        if (!onnxModelPath_.empty())
        {
            auto onnxProcessor = std::make_unique<OnnxFrameProcessor>();
            onnxProcessor->setFaceDetectionInputMode(faceInputMode_);
            onnxProcessor->setFaceDetectionFixedInputSize(faceFixedInputWidth_, faceFixedInputHeight_);
            if (onnxProcessor->loadModel(onnxModelPath_))
            {
                onnxProcessor_ = std::move(onnxProcessor);
                qDebug() << "ONNX video processing enabled:" << onnxModelPath_.c_str();
            }
            else
            {
                qDebug() << "Failed to load ONNX model, fallback to FFmpeg filter:" << onnxModelPath_.c_str();
            }
        }

        if (!onnxProcessor_)
        {
            videoFilter_ = std::make_shared<FrameFilter>(currentStreams_.find(AVMEDIA_TYPE_VIDEO)->second->getAvctx(), "hue=s=0");
        }

        videoDecodeFuture_ = QtConcurrent::run(&threadPool_, &AVPlayer::doVideoDecode, this);
        videoPlayFuture_ = QtConcurrent::run(&threadPool_, &AVPlayer::doPlayVideo, this);
    }

    if (currentStreams_.find(AVMEDIA_TYPE_AUDIO) != currentStreams_.end())
    {
        audioPlayer_ = std::make_unique<AudioPlayer>(currentStreams_.find(AVMEDIA_TYPE_AUDIO)->second);
        audioDecodeFuture_ = QtConcurrent::run(&threadPool_, &AVPlayer::doAudioDecode, this);
        audioPlayFuture_ = QtConcurrent::run(&threadPool_, &AVPlayer::doPlayAudio, this);
    }

    if (currentStreams_.find(AVMEDIA_TYPE_SUBTITLE) != currentStreams_.end())
    {
        subtitlePlayFuture_ = QtConcurrent::run(&threadPool_, &AVPlayer::doPlaySubtitle, this);
    }
}

void AVPlayer::doSeek(double pos)
{
    std::lock_guard<std::mutex> locker(seekMutex_);
    if(pos < 0 || pos > 1.0)
    {
        qDebug() << "seek pos should be between 0 and 1.0";
        return;
    }

    AVFormatContext* fmtCtx = demuxer_ ? demuxer_->getFormatCtx() : nullptr;
    if (!fmtCtx || fmtCtx->duration <= 0)
    {
        qDebug() << "seek failed: invalid format context or duration";
        return;
    }

    isSeek_ = true;
    const int64_t seekTarget = static_cast<int64_t>(std::llround(pos * static_cast<double>(fmtCtx->duration)));
    qDebug() << "Seeking to position: " << pos << ", target timestamp: " << seekTarget;
    int ret = avformat_seek_file(fmtCtx, -1, INT64_MIN, seekTarget, INT64_MAX, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        qDebug() << "Error seeking: " << ret;
        isSeek_ = false;
        seekCv_.notify_all();
        return;
    } else {
        qDebug() << "Seek successful";
    }

    // Mark all in-flight packets/frames from previous timeline as stale.
    seekSerial_.fetch_add(1);

    demuxer_->clearEofFlag();

    videoPacketQueue_.setActive(false);
    audioPacketQueue_.setActive(false);
    subtitlePacketQueue_.setActive(false);
    videoFrameQueue_.setActive(false);
    audioFrameQueue_.setActive(false);
    subtitleFrameQueue_.setActive(false);

    demuxer_->flushCodecBuffers(currentStreams_);

    videoPacketQueue_.clear();
    audioPacketQueue_.clear();
    subtitlePacketQueue_.clear();
    videoFrameQueue_.clear();
    audioFrameQueue_.clear();
    subtitleFrameQueue_.clear();

    videoPacketQueue_.setActive(true);
    audioPacketQueue_.setActive(true);
    subtitlePacketQueue_.setActive(true);
    videoFrameQueue_.setActive(true);
    audioFrameQueue_.setActive(true);
    subtitleFrameQueue_.setActive(true);

    if (audioPlayer_)
    {
        audioPlayer_->resetForSeek();
    }

    syncTimer_.clear();
    audioStart_ = false;
    isSeek_ = false;
    seekCv_.notify_all();
}

void AVPlayer::stop()
{
    quit_ = true;
    videoPacketQueue_.setActive(false);
    audioPacketQueue_.setActive(false);
    subtitlePacketQueue_.setActive(false);
    videoFrameQueue_.setActive(false);
    audioFrameQueue_.setActive(false);
    subtitleFrameQueue_.setActive(false);
    seekCv_.notify_all();
}

void AVPlayer::doDemux()
{
    const auto videoIt = currentStreams_.find(AVMEDIA_TYPE_VIDEO);
    const auto audioIt = currentStreams_.find(AVMEDIA_TYPE_AUDIO);
    const auto subtitleIt = currentStreams_.find(AVMEDIA_TYPE_SUBTITLE);

    while (!quit_)
    {
        waitForSeekIfNeeded();
        if (quit_)
        {
            return;
        }

        const uint64_t packetSerial = seekSerial_.load();

        AVPacketPtr pkt;
        {
            std::lock_guard<std::mutex> lock(seekMutex_);
            pkt = demuxer_->readPacket();
        }

        if (isSeekStale(packetSerial, seekSerial_.load(), isSeek_))
        {
            continue;
        }

        if (pkt == nullptr)
        {
            if (demuxer_->isEof())
            {
                if (videoIt != currentStreams_.end())
                {
                    videoPacketQueue_.enqueue(QueuedPacket::makeEof(packetSerial));
                }
                if (audioIt != currentStreams_.end())
                {
                    audioPacketQueue_.enqueue(QueuedPacket::makeEof(packetSerial));
                }
                if (subtitleIt != currentStreams_.end())
                {
                    subtitlePacketQueue_.enqueue(QueuedPacket::makeEof(packetSerial));
                }
                qDebug() << "return here";
            }
            else
            {
                qDebug() << "doDemux readPacket abnormal";
            }
            return;
        }

        const int pktStreamIndex = pkt->stream_index;

        if (videoIt != currentStreams_.end() && pktStreamIndex == videoIt->second->getStream()->index)
        {
            videoPacketQueue_.enqueue(QueuedPacket(std::move(pkt), packetSerial));
        }
        else if (audioIt != currentStreams_.end() && pktStreamIndex == audioIt->second->getStream()->index)
        {
            audioPacketQueue_.enqueue(QueuedPacket(std::move(pkt), packetSerial));
        }
        else if (subtitleIt != currentStreams_.end() && pktStreamIndex == subtitleIt->second->getStream()->index)
        {
            subtitlePacketQueue_.enqueue(QueuedPacket(std::move(pkt), packetSerial));
        }
    }
}

void AVPlayer::doAudioDecode()
{
    qDebug() << "doAudioDecode running";
    const auto audioIt = currentStreams_.find(AVMEDIA_TYPE_AUDIO);
    if (audioIt == currentStreams_.end())
    {
        return;
    }
    const int audioStreamIndex = audioIt->second->getStream()->index;
    while (!quit_)
    {
        waitForSeekIfNeeded();
        if (quit_)
        {
            return;
        }

        auto queuedPktOpt = audioPacketQueue_.dequeue();
        if (!queuedPktOpt)
        {
            if (quit_)
                return;
            continue;
        }

        QueuedPacket queuedPkt = std::move(queuedPktOpt.value());

        if (isSeekStale(queuedPkt.seekSerial, seekSerial_.load(), isSeek_))
        {
            continue;
        }

        if (queuedPkt.eof)
        {
            qDebug() << "Flushing audio decoder";
            for (;;)
            {
                auto frame = demuxer_->decodePacket(audioStreamIndex, nullptr);
                if (!frame)
                {
                    break;  // flush finished, exist
                }
                if (isSeekStale(queuedPkt.seekSerial, seekSerial_.load(), isSeek_))
                {
                    continue;
                }
                audioFrameQueue_.enqueue(QueuedFrame(std::move(frame), queuedPkt.seekSerial));
            }
            if (isSeekStale(queuedPkt.seekSerial, seekSerial_.load(), isSeek_))
            {
                continue;
            }
            audioFrameQueue_.enqueue(QueuedFrame::makeEof(queuedPkt.seekSerial));  // send EOF signal
            qDebug() << "Audio flush complete, exiting";
            return;  // exist after flushing
        }

        auto frame = demuxer_->decodePacket(audioStreamIndex, std::move(queuedPkt.packet));
        if (isSeekStale(queuedPkt.seekSerial, seekSerial_.load(), isSeek_))
        {
            continue;
        }

        if (!frame)
        {
            qDebug() << "audio frame incorrect";
            continue;
        }
        audioFrameQueue_.enqueue(QueuedFrame(std::move(frame), queuedPkt.seekSerial));
    }
}

void AVPlayer::doPlayAudio()
{
    qDebug() << "doPlayAudio running";
    while (!quit_)
    {
        waitForSeekIfNeeded();
        if (quit_)
        {
            return;
        }

        auto queuedFrameOpt = audioFrameQueue_.dequeue();
        if (!queuedFrameOpt) {
            if (quit_) return;
            continue;
        }

        QueuedFrame queuedFrame = std::move(queuedFrameOpt.value());

        if (isSeekStale(queuedFrame.seekSerial, seekSerial_.load(), isSeek_))
        {
            continue;
        }

        if (queuedFrame.eof) {
            qDebug() << "Audio playback finished (EOF)";
            return;
        }
        if (audioPlayer_)
        {
            bool played = false;
            {
                std::lock_guard<std::mutex> seekLock(seekMutex_);
                if (isSeekStale(queuedFrame.seekSerial, seekSerial_.load(), isSeek_))
                {
                    continue;
                }
                played = audioPlayer_->playAudio(queuedFrame.frame.get());
            }
            if (played)
            {
                audioStart_ = true;
            }
        }
    }
}

void AVPlayer::doVideoDecode()
{
    qDebug() << "doVideoDecode running";
    const auto videoIt = currentStreams_.find(AVMEDIA_TYPE_VIDEO);
    if (videoIt == currentStreams_.end())
    {
        return;
    }
    const int videoStreamIndex = videoIt->second->getStream()->index;
    while (!quit_)
    {
        waitForSeekIfNeeded();
        if (quit_)
        {
            return;
        }

        auto queuedPktOpt = videoPacketQueue_.dequeue();
        if (!queuedPktOpt)
        {
            if (quit_) return;
            continue;
        }

        QueuedPacket queuedPkt = std::move(queuedPktOpt.value());

        if (isSeekStale(queuedPkt.seekSerial, seekSerial_.load(), isSeek_))
        {
            continue;
        }

        if (queuedPkt.eof)
        {
            qDebug() << "Flushing video decoder";
            for (;;)
            {
                auto frame = demuxer_->decodePacket(videoStreamIndex, nullptr);
                if (!frame)
                {
                    break;
                }
                if (isSeekStale(queuedPkt.seekSerial, seekSerial_.load(), isSeek_))
                {
                    continue;
                }
                videoFrameQueue_.enqueue(QueuedFrame(std::move(frame), queuedPkt.seekSerial));
            }
            if (isSeekStale(queuedPkt.seekSerial, seekSerial_.load(), isSeek_))
            {
                continue;
            }
            videoFrameQueue_.enqueue(QueuedFrame::makeEof(queuedPkt.seekSerial));  // send EOF signal
            qDebug() << "Video flush complete, exiting";
            return;
        }

        auto frame = demuxer_->decodePacket(videoStreamIndex, std::move(queuedPkt.packet));
        if (isSeekStale(queuedPkt.seekSerial, seekSerial_.load(), isSeek_))
        {
            continue;
        }

        if (!frame)
        {
            qDebug() << "video frame decode failed, continue";
            continue;
        }

        // Keep ONNX and FFmpeg filter mutually exclusive to avoid double processing.
        if (onnxProcessor_)
        {
            frame = onnxProcessor_->process(std::move(frame));
            if (!frame)
            {
                qDebug() << "ONNX processing returned empty frame";
                continue;
            }

            videoFrameQueue_.enqueue(QueuedFrame(std::move(frame), queuedPkt.seekSerial));
            continue;
        }
        else if (videoFilter_)
        {
            auto filteredFrame = videoFilter_->doFilt(std::move(frame));
            if (filteredFrame)
            {
                videoFrameQueue_.enqueue(QueuedFrame(std::move(filteredFrame), queuedPkt.seekSerial));
            }
            else
            {
                qDebug() << "filtered frame unavailable, skip enqueue";
            }
        }
        else
        {
            videoFrameQueue_.enqueue(QueuedFrame(std::move(frame), queuedPkt.seekSerial));
        }
    }
}

void AVPlayer::doPlayVideo()
{
    qDebug() << "doPlayVideo running";
    const auto videoIter = currentStreams_.find(AVMEDIA_TYPE_VIDEO);
    if (videoIter != currentStreams_.end())
    {
        const AVRational avgRate = videoIter->second->getStream()->avg_frame_rate;
        const double fps = av_q2d(avgRate);
        if (fps > 0.0)
        {
            syncTimer_.setFrameRate(1.0/fps);
        }
    }
    while (!quit_)
    {
        waitForSeekIfNeeded();
        if (quit_)
        {
            return;
        }

        if ((audioPlayer_ && !audioPlayer_->isAudioClockValid()) || !audioStart_)
        {
            QThread::msleep(5);
            continue;
        }

        auto queuedFrameOpt = videoFrameQueue_.dequeue();
        if (!queuedFrameOpt) {
            if (quit_) return;
            continue;
        }

        QueuedFrame queuedFrame = std::move(queuedFrameOpt.value());

        if (isSeekStale(queuedFrame.seekSerial, seekSerial_.load(), isSeek_))
        {
            continue;
        }

        if (queuedFrame.eof) {
            qDebug() << "Video playback finished (EOF)";
            return;
        }
        if (audioPlayer_ && audioStart_)
        {
            int64_t pts = queuedFrame.frame.get()->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) {
                pts = queuedFrame.frame.get()->pts;
            }
            if (pts == AV_NOPTS_VALUE) {
                pts = 0;
            }
            if (videoIter->second->getStream()->start_time != AV_NOPTS_VALUE) {
                pts -= videoIter->second->getStream()->start_time;
            }
            const double videoPtsSec = pts * av_q2d(videoIter->second->getStream()->time_base);
            currentVideoPtsSec_.store(videoPtsSec);
            for (;;)
            {
                if (shouldAbortForSeekOrQuit(queuedFrame.seekSerial, seekSerial_.load(), isSeek_, quit_))
                {
                    break;
                }
                const double audioTimeSec = audioPlayer_->getAudioTime();
                const bool ready = syncTimer_.wait(true, videoPtsSec, 1, audioTimeSec);
                if (ready) {
                    break;
                }
                if (quit_) {
                    return;
                }
            }

            if (shouldAbortForSeekOrQuit(queuedFrame.seekSerial, seekSerial_.load(), isSeek_, quit_))
            {
                continue;
            }

            emit videoframeReady(av_frame_clone(queuedFrame.frame.get()));
        }
    }
}

void AVPlayer::doPlaySubtitle()
{
    /*
    qDebug() << "doPlaySubtitle running";
    while (!quit_)
    {
    if (!startPlay_)
    {
    return;
    }
    auto pkt = subtitlePacketQueue_.dequeue();
    if (pkt)
    {
    auto frame = demuxer_->decodePacket(std::move(pkt.value()));
    if (!frame)
    {
    return;
    }
    }
    }
    */
}

