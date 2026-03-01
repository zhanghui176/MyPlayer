

#ifndef AVPLAYER_H
#define AVPLAYER_H

#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "AVDemuxer.h"
#include "AVQueue.h"
#include "AudioPlayer.h"
#include <QtConcurrent/qtconcurrentrun.h>
#include "SyncTimer.h"
#include "FrameFilter.h"

class AVPlayer :public QObject
{
    Q_OBJECT
public:
    AVPlayer();
    ~AVPlayer();
    void setUrl(std::string url);
    void doload();
    void doPlayVideo();
    void doPlayAudio();
    void doPlaySubtitle();
    void doDemux();
    void doVideoDecode();
    void doAudioDecode();
    void stop();
    void doSeek(double pos);
    double getCurrentTimeSec();
    double getDurationSec();

signals:
    void videoframeReady(AVFrame* frame);
    void audioframeReady(AVFrame* frame);
private:
    void waitForSeekIfNeeded();
private:
    std::string url_;
    std::atomic<bool> quit_ = false;
    QThreadPool threadPool_;
    std::shared_ptr<AVDemuxer> demuxer_;
    QFuture<void> videoDecodeFuture_;
    QFuture<void> videoPlayFuture_;
    QFuture<void> audioDecodeFuture_;
    QFuture<void> audioPlayFuture_;
    QFuture<void> subtitlePlayFuture_;
    QFuture<void> demuxFuture_;
    AVQueue<AVPacketPtr> videoPacketQueue_;
    AVQueue<AVPacketPtr> audioPacketQueue_;
    AVQueue<AVPacketPtr> subtitlePacketQueue_;
    AVQueue<AVFramePtr> videoFrameQueue_;
    AVQueue<AVFramePtr> audioFrameQueue_;
    AVQueue<AVFramePtr> subtitleFrameQueue_;
    std::unique_ptr<AudioPlayer> audioPlayer_;
    bool audioStart_ = false;
    double audioTimestamp_ = 0;
    std::map<int, std::shared_ptr<CodecWrapper>> currentStreams_;
    SyncTimer syncTimer_;
    std::shared_ptr<FrameFilter> videoFilter_;
    std::atomic<bool> isSeek_ = false;
    std::mutex seekMutex_;
    std::mutex seekStateMutex_;
    std::condition_variable seekCv_;
    std::atomic<double> currentVideoPtsSec_ = 0.0;


};

#endif // AVPLAYER_H

