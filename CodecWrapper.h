#ifndef CODECWRAPPER_H
#define CODECWRAPPER_H

#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
}

class CodecWrapper
{
public:
    CodecWrapper(AVStream *stream, const std::string inputVideoCodec, AVMediaType type, bool supportHWdevice = false);
    ~CodecWrapper();
    bool isSupportHardware();
    AVStream *getStream();
    AVCodecContext* getAvctx();

private:
    int createHWDevice();
    std::string inputVideoCodec_ = "";
    AVCodecContext *avctx_ = nullptr;
    const AVCodec* decoder_;
    AVMediaType mediaType_;
    bool supportHWDevice_;
    AVBufferRef *hwDev_ = nullptr;
    AVStream *stream_ = nullptr;
};

#endif // CODECWRAPPER_H
