#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include <QObject>
#include <QAudioOutput>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QAudioSink>
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
    void playAudio(AVFrame* frame);
    int createAudioDevice();
    double getAudioTime();
    bool isAudioClockValid();
signals:
    void AudioFrameReady(AVFrame* frame);

private:
    std::shared_ptr<CodecWrapper> audioCodecWrapper_;
    AVCodecContext* audioCodecContext_;
    AVStream* audioStream_;
    QIODevice* audioDevice_;
    QAudioSink* audioSink_;
    SwrContext *swr_ctx_ = NULL;
    QAudioFormat format_;
    std::mutex audioDeviceMtx_;
    std::mutex timeStampMutex_;
    std::mutex audioClockMutex_;
    double audioClockBasePtsSce_ = 0.0;
    bool audioClockValid = false;
    double audioTimeStamp_;
    int bytesPerSecond_ = 0;
    bool quit_;

};

#endif // AUDIOPLAYER_H
