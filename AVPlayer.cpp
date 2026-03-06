#include "AVPlayer.h"
#include <QDebug>
#include <cmath>

AVPlayer::AVPlayer()
    : demuxer_(std::make_shared<AVDemuxer>())
    , videoFilter_(nullptr)
{
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
        videoFilter_ = std::make_shared<FrameFilter>(currentStreams_.find(AVMEDIA_TYPE_VIDEO)->second->getAvctx(), "hue=s=0");
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

        AVPacketPtr pkt;
        {
            std::lock_guard<std::mutex> lock(seekMutex_);
            pkt = demuxer_->readPacket();
        }

        if (pkt == nullptr)
        {
            if (demuxer_->isEof())
            {
                if (videoIt != currentStreams_.end())
                {
                    videoPacketQueue_.enqueue(AVPacketPtr());
                }
                if (audioIt != currentStreams_.end())
                {
                    audioPacketQueue_.enqueue(AVPacketPtr());
                }
                if (subtitleIt != currentStreams_.end())
                {
                    subtitlePacketQueue_.enqueue(AVPacketPtr());
                }
                qDebug() << "return here";
            }
            else
            {
                qDebug() << "doDemux readPacket abnormal";
            }
            return;
        }

        if (videoIt != currentStreams_.end() && pkt->stream_index == videoIt->second->getStream()->index)
        {
            videoPacketQueue_.enqueue(std::move(pkt));
        }
        else if (audioIt != currentStreams_.end() && pkt->stream_index == audioIt->second->getStream()->index)
        {
            audioPacketQueue_.enqueue(std::move(pkt));
        }
        else if (subtitleIt != currentStreams_.end() && pkt->stream_index == subtitleIt->second->getStream()->index)
        {
            subtitlePacketQueue_.enqueue(std::move(pkt));
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

        auto pkt = audioPacketQueue_.dequeue();
        if (!pkt)
        {
            if (quit_)
                return;
            continue;
        }

        if (isSeek_)
        {
            continue;
        }

        if (!pkt.value())
        {
            qDebug() << "Flushing audio decoder";
            for (;;)
            {
                auto frame = demuxer_->decodePacket(audioStreamIndex, nullptr);
                if (!frame)
                {
                    break;  // flush finished, exist
                }
                audioFrameQueue_.enqueue(std::move(frame));
            }
            audioFrameQueue_.enqueue(AVFramePtr());  // send EOF signal
            qDebug() << "Audio flush complete, exiting";
            return;  // exist after flushing
        }

        auto frame = demuxer_->decodePacket(audioStreamIndex, std::move(pkt.value()));
        if (isSeek_)
        {
            continue;
        }

        if (!frame)
        {
            qDebug() << "audio frame incorrect";
            continue;
        }
        qDebug() << "decode successfully, audioPacketQueue size = " << audioPacketQueue_.size();
        audioFrameQueue_.enqueue(std::move(frame));
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

        auto frame = audioFrameQueue_.dequeue();
        if (!frame) {
            if (quit_) return;
            continue;
        }

        if (isSeek_)
        {
            continue;
        }

        if (!frame.value()) {
            qDebug() << "Audio playback finished (EOF)";
            return;
        }
        if (audioPlayer_)
        {
            bool played = false;
            {
                std::lock_guard<std::mutex> seekLock(seekMutex_);
                if (isSeek_)
                {
                    continue;
                }
                played = audioPlayer_->playAudio(frame.value().get());
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

        auto pkt = videoPacketQueue_.dequeue();
        if (!pkt)
        {
            if (quit_) return;
            continue;
        }

        if (isSeek_)
        {
            continue;
        }

        if (!pkt.value())
        {
            qDebug() << "Flushing video decoder";
            for (;;)
            {
                auto frame = demuxer_->decodePacket(videoStreamIndex, nullptr);
                if (!frame)
                {
                    break;
                }
                videoFrameQueue_.enqueue(std::move(frame));
            }
            videoFrameQueue_.enqueue(AVFramePtr());  // send EOF signal
            qDebug() << "Video flush complete, exiting";
            return;
        }

        auto frame = demuxer_->decodePacket(videoStreamIndex, std::move(pkt.value()));
        if (isSeek_)
        {
            continue;
        }

        if (!frame)
        {
            qDebug() << "video frame decode failed, continue";
            continue;
        }
        if (videoFilter_)
        {
            auto filteredFrame = videoFilter_->doFilt(std::move(frame));
            if (filteredFrame)
            {
                videoFrameQueue_.enqueue(std::move(filteredFrame));
                qDebug() << "enqueue video frame after doFilt";
            }
            else
            {
                qDebug() << "filtered frame unavailable, skip enqueue";
            }
        }
        else
        {
            videoFrameQueue_.enqueue(std::move(frame));
            qDebug() << "enqueue video frame without doFilt";
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

        auto frame = videoFrameQueue_.dequeue();
        if (!frame) {
            if (quit_) return;
            continue;
        }

        if (isSeek_)
        {
            continue;
        }

        if (!frame.value()) {
            qDebug() << "Video playback finished (EOF)";
            return;
        }
        qDebug() << "frame enqueue height: " << frame.value().get()->height  << ",width is "
                 << frame.value().get()->width << ",audioStart_ is " << audioStart_;
        if (audioPlayer_ && audioStart_)
        {
            int64_t pts = frame.value().get()->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) {
                pts = frame.value().get()->pts;
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
                if (isSeek_ || quit_)
                {
                    break;
                }
                const double audioTimeSec = audioPlayer_->getAudioTime();
                const bool ready = syncTimer_.wait(true, videoPtsSec, 1, audioTimeSec);
                if (ready) {
                    qDebug() << "Sync result is " << ready << ", video time stamp is " << videoPtsSec;
                    break;
                }
                if (quit_) {
                    return;
                }
            }

            if (isSeek_ || quit_)
            {
                continue;
            }

            emit videoframeReady(av_frame_clone(frame.value().get()));
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

