

#ifndef AVPLAYER_H
#define AVPLAYER_H

#include <string>
#include <atomic>
#include <cstdint>
#include <utility>
#include <mutex>
#include <condition_variable>
#include "AVDemuxer.h"
#include "AVQueue.h"
#include "AudioPlayer.h"
#include <QtConcurrent/qtconcurrentrun.h>
#include "SyncTimer.h"
#include "FrameFilter.h"
#include "OnnxFrameProcessor.h"
#include "AppPaths.h"

struct QueuedPacket
{
    AVPacketPtr packet;
    uint64_t seekSerial = 0;
    bool eof = false;

    QueuedPacket() = default;
    QueuedPacket(AVPacketPtr pkt, uint64_t serial)
        : packet(std::move(pkt)), seekSerial(serial), eof(false)
    {
    }

    static QueuedPacket makeEof(uint64_t serial)
    {
        QueuedPacket item;
        item.seekSerial = serial;
        item.eof = true;
        return item;
    }
};

struct QueuedFrame
{
    AVFramePtr frame;
    uint64_t seekSerial = 0;
    bool eof = false;

    QueuedFrame() = default;
    QueuedFrame(AVFramePtr frm, uint64_t serial)
        : frame(std::move(frm)), seekSerial(serial), eof(false)
    {
    }

    static QueuedFrame makeEof(uint64_t serial)
    {
        QueuedFrame item;
        item.seekSerial = serial;
        item.eof = true;
        return item;
    }
};

class AVPlayer :public QObject
{
    Q_OBJECT
public:
    AVPlayer();
    ~AVPlayer();
    void setUrl(std::string url);
    void doload(ModelKind modelKind = ModelKind::FaceDetection);
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
    void setVideoOnnxModel(const std::string& modelPath);
    const std::string& getVideoOnnxModel() const;
    void setFaceDetectionInputMode(OnnxFrameProcessor::FaceInputMode mode);
    OnnxFrameProcessor::FaceInputMode faceDetectionInputMode() const;
    void setFaceDetectionFixedInputSize(int width, int height);
    std::pair<int, int> faceDetectionFixedInputSize() const;

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
    AVQueue<QueuedPacket> videoPacketQueue_;
    AVQueue<QueuedPacket> audioPacketQueue_;
    AVQueue<QueuedPacket> subtitlePacketQueue_;
    AVQueue<QueuedFrame> videoFrameQueue_;
    AVQueue<QueuedFrame> audioFrameQueue_;
    AVQueue<QueuedFrame> subtitleFrameQueue_;
    std::unique_ptr<AudioPlayer> audioPlayer_;
    std::atomic<bool> audioStart_ = false;
    double audioTimestamp_ = 0;
    std::map<int, std::shared_ptr<CodecWrapper>> currentStreams_;
    SyncTimer syncTimer_;
    std::shared_ptr<FrameFilter> videoFilter_;
    std::unique_ptr<OnnxFrameProcessor> onnxProcessor_;
    std::string onnxModelPath_;
    OnnxFrameProcessor::FaceInputMode faceInputMode_ = OnnxFrameProcessor::FaceInputMode::DynamicInputSize;
    int faceFixedInputWidth_ = 640;
    int faceFixedInputHeight_ = 640;
    std::atomic<bool> isSeek_ = false;
    // Increments on each successful seek. Workers drop packets/frames from older generations.
    std::atomic<uint64_t> seekSerial_ = 0;
    std::mutex seekMutex_;
    std::mutex seekStateMutex_;
    std::condition_variable seekCv_;
    std::atomic<double> currentVideoPtsSec_ = 0.0;
    bool useModel_ = true;
};

#endif // AVPLAYER_H

