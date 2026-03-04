#include "AudioPlayer.h"
#include <QDebug>
#include <QThread>

AudioPlayer::AudioPlayer(std::shared_ptr<CodecWrapper> codecWrapper)
    : audioCodecWrapper_(codecWrapper)
    , quit_(false)
{
    audioStream_ = audioCodecWrapper_->getStream();
    audioCodecContext_ = codecWrapper->getAvctx();
    qDebug() << "AudioPlay create";
    init();
}


AudioPlayer::~AudioPlayer()
{
    quit_ = true;
    if (audioSink_) {
        audioSink_->stop();
        delete audioSink_;
        audioSink_ = nullptr;
    }
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
    }
    qDebug() << "AudioPlayer destructed.";
}

void AudioPlayer::init()
{
    {
        std::lock_guard<std::mutex> locker(audioClockMutex_);
        audioClockBasePtsSce_ = 0.0;
        audioClockValid = false;
    }
    // 创建音频重采样器
    AVChannelLayout out_ch_layout;
    av_channel_layout_default(&out_ch_layout, 2); // 立体声

    AVChannelLayout in_ch_layout;
    av_channel_layout_copy(&in_ch_layout, &audioCodecContext_->ch_layout); // 复制输入布局

    int ret = swr_alloc_set_opts2(
        &swr_ctx_,                   // SwrContext 指针
        &out_ch_layout,              // 输出声道布局
        AV_SAMPLE_FMT_S16,           // 输出采样格式
        44100,                       // 输出采样率
        &in_ch_layout,               // 输入声道布局
        audioCodecContext_->sample_fmt,       // 输入采样格式
        audioCodecContext_->sample_rate,      // 输入采样率
        0,                           // 日志偏移
        NULL                         // 日志上下文
        );
    swr_init(swr_ctx_);

    format_.setSampleRate(44100);
    format_.setChannelCount(2);
    format_.setSampleFormat(QAudioFormat::Int16); // 16位采样格式

    // 获取默认音频设备
    QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();

    // 检查格式是否支持
    if (!outputDevice.isFormatSupported(format_)) {
        qDebug() << "format is invalid";
        format_ = outputDevice.preferredFormat();
    }

    bytesPerSecond_ = format_.sampleRate() * format_.channelCount() * qMax(1, format_.bytesPerSample());

    // 创建音频输出 (Qt6 使用 QAudioSink)
    audioSink_ = new QAudioSink(outputDevice, format_);

    // 启动音频设备
    audioDevice_ = audioSink_->start();
    if (!audioDevice_) {
        qDebug() << "无法启动音频设备";
        delete audioSink_;
        return;
    }

    if (ret < 0 || !swr_ctx_) {
        qDebug() << "无法创建音频重采样器";
        return;
    }
    qDebug() << "audio initialize finished.";
}

double AudioPlayer::getBufferedSecUnsafe()
{
    // calcute how many bytes still in device and not play yet.
    if (!audioSink_ || bytesPerSecond_ <= 0)
    {
        return 0.0;
    }
    int freeBytes = 0;
    int bufferSize = 0;
    {
        std::lock_guard<std::mutex> audioLk(audioDeviceMtx_);
        freeBytes = audioSink_->bytesFree();
        bufferSize = audioSink_->bufferSize();
    }
    const int usedBytes = qMax(0, bufferSize - freeBytes);
    return static_cast<double>(usedBytes) / static_cast<double>(bytesPerSecond_);
}

double AudioPlayer::getAudioTime()
{
    std::lock_guard<std::mutex> locker(audioClockMutex_);
    if (!audioClockValid || !audioSink_)
    {
        return 0.0;
    }
    // the duration which was sent into audio thread
    double processdSec = static_cast<double>(audioSink_->processedUSecs())/1000000.0;
    const double bufferedSec = getBufferedSecUnsafe();
    return audioClockBasePtsSce_ + processdSec - bufferedSec;
}

void AudioPlayer::resetForSeek()
{
    {
        std::lock_guard<std::mutex> locker(audioClockMutex_);
        audioClockBasePtsSce_ = 0.0;
        audioClockValid = false;
    }

    std::lock_guard<std::mutex> audioLk(audioDeviceMtx_);
    if (!audioSink_)
    {
        audioDevice_ = nullptr;
        return;
    }

    if (swr_ctx_)
    {
        swr_close(swr_ctx_);
        swr_init(swr_ctx_);
    }

    audioSink_->stop();
    audioSink_->reset();
    audioDevice_ = audioSink_->start();
}

bool AudioPlayer::isAudioClockValid()
{
    std::lock_guard<std::mutex> locker(audioClockMutex_);
    return audioClockValid && audioSink_;
}
bool AudioPlayer::playAudio(AVFrame* frame)
{
    if (!frame || !audioSink_ || !audioDevice_ || !swr_ctx_)
    {
        return false;
    }

    int64_t pts = frame->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE) {
        pts = frame->pts;
    }
    if (pts == AV_NOPTS_VALUE) {
        pts = 0;
    }
    if (audioStream_ && audioStream_->start_time != AV_NOPTS_VALUE) {
        pts -= audioStream_->start_time;
    }

    const double framePtsSec = pts * av_q2d(audioStream_->time_base);
    qDebug() << "playAudio running, sample is " << frame->nb_samples << ", pts is " << framePtsSec;
    {
        std::lock_guard<std::mutex> locker(timeStampMutex_);
        audioTimeStamp_ = framePtsSec;
    }

    int out_samples = swr_get_out_samples(swr_ctx_, frame->nb_samples);
    if (out_samples <= 0) {
        qDebug() << "out_samples error";
        return false;
    }

    // 分配输出缓冲区
    uint8_t *output_buffer = nullptr;
    int buffer_size = av_samples_alloc(&output_buffer, nullptr,
                                       format_.channelCount(),
                                       out_samples,
                                       AV_SAMPLE_FMT_S16, 0);
    if (buffer_size < 0) {
        qDebug()  << "buffer_size error";
        return false;
    }

    bool wroteToDevice = false;

    int samples_converted = swr_convert(
        swr_ctx_,
        &output_buffer,
        out_samples,
        (const uint8_t **)frame->data,
        frame->nb_samples
        );


    if (samples_converted > 0) {
        int data_size = samples_converted * format_.channelCount() * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

        // std::cout << "samples_converted is " << samples_converted << ", data_size : " << data_size;
        int free_bytes = 0;
        int bufferSize = 0;

        {
            std::lock_guard<std::mutex> audioLk(audioDeviceMtx_);
            free_bytes = audioSink_->bytesFree();
            bufferSize = audioSink_->bufferSize();
        }
        int used = bufferSize - free_bytes;

        // 当前缓冲时长（ms）
        int msBuffered = (bytesPerSecond_ > 0)
                             ? static_cast<int>((double)used / bytesPerSecond_ * 1000.0)
                             : 0;

        // 避免缓冲超过 200ms 堆积 → 等待一小会儿
        if (msBuffered > 200) {
            int dynamicWait = qMin(msBuffered - 200, 50); // 最多等 50ms
            // qDebug() << "dynamicWait for " << dynamicWait;
            QThread::msleep(dynamicWait);
            free_bytes = audioSink_->bytesFree();
        }

        // 写入前二次验证缓冲区
        qint64 bytesWritten = 0;
        if (free_bytes >= data_size) {
            {
                std::lock_guard<std::mutex> audioLk(audioDeviceMtx_);
                bytesWritten = audioDevice_->write(
                    reinterpret_cast<const char*>(output_buffer),
                    data_size
                    );
            }
            qDebug() << "audio in:" << bytesWritten << "/" << data_size;

            if (bytesWritten > 0)
            {
                wroteToDevice = true;
                std::lock_guard<std::mutex> locker(audioClockMutex_);
                if (!audioClockValid && audioSink_)
                {
                    const double processdSec = static_cast<double>(audioSink_->processedUSecs())/1000000.0;
                    const double bufferedSec = getBufferedSecUnsafe();
                    audioClockBasePtsSce_ = framePtsSec - processdSec + bufferedSec;
                    audioClockValid = true;
                }
            }
        } else {
            qDebug() << "drop audio frame";
        }
    }

    if (output_buffer)
    {
        av_freep(&output_buffer);
    }

    return wroteToDevice;
}
