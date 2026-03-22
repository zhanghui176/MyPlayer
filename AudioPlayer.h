#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include <QObject>
#include <QAudioOutput>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QAudioSink>
#include <QThread>
#include <mutex>
#include "CodecWrapper.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

class AudioPlayer : public QObject
{
    Q_OBJECT
public:
    AudioPlayer(std::shared_ptr<CodecWrapper> codecWrapper);
    ~AudioPlayer();
    void init();
    bool playAudio(AVFrame* frame);
    int createAudioDevice();
    double getAudioTime();
    bool isAudioClockValid();
    void resetForSeek();
signals:
    void AudioFrameReady(AVFrame* frame);

private:
    void initOnAudioThread();
    void cleanupOnAudioThread();
    bool playAudioOnAudioThread(AVFrame* frame);
    double getAudioTimeOnAudioThread();
    bool isAudioClockValidOnAudioThread();
    void resetForSeekOnAudioThread();
    double getBufferedSecUnsafe();

private:
    std::shared_ptr<CodecWrapper> audioCodecWrapper_;
    AVCodecContext* audioCodecContext_ = nullptr;
    AVStream* audioStream_ = nullptr;
    QIODevice* audioDevice_ = nullptr;
    QAudioSink* audioSink_ = nullptr;
    SwrContext *swr_ctx_ = NULL;
    QAudioFormat format_;
    QThread audioThread_;
    QObject* audioThreadContext_ = nullptr;
    std::mutex audioDeviceMtx_;
    std::mutex timeStampMutex_;
    std::mutex audioClockMutex_;
    double audioClockBasePtsSce_ = 0.0;
    bool audioClockValid = false;
    double audioTimeStamp_ = 0.0;
    int bytesPerSecond_ = 0;
    bool quit_;

};

#endif // AUDIOPLAYER_H
