#ifndef AVDEMUXER_H
#define AVDEMUXER_H

#include <string>
#include <map>
#include <memory>
#include "CodecWrapper.h"
#include "QAVPacket.h"
#include <mutex>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

//通过RAII来管理dict
struct DictionaryStruct{
    AVDictionary* dict = nullptr;
    DictionaryStruct() = default;
    ~DictionaryStruct()
    {
        if(dict)
        {
            av_dict_free(&dict);
        }
    }
};

struct AVPacketDeleter
{
    void operator()(AVPacket* p)
    {
        av_packet_free(&p);
    }
};
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;

struct AVFrameDeleter
{
    void operator()(AVFrame* p)
    {
        av_frame_free(&p);
    }
};
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

class AVDemuxer
{
public:
    AVDemuxer();
    ~AVDemuxer();
    int doLoad(std::string url);
    void abort(bool abortRequest);
    void collectStreamInfo(AVFormatContext* formatCtx);
    void unLoad();
    bool getAbortRequest();
    bool isEof() const;
    const std::map<int, std::shared_ptr<CodecWrapper>>& getAvailableStream() const;
    const std::map<int, std::shared_ptr<CodecWrapper>>& getCurrentStream() const;
    static AVStream getVideoStream();
    static AVStream getAudioStream();
    AVFormatContext* getFormatCtx();
    AVPacketPtr readPacket();
    AVFramePtr decodePacket(int streamIndex, AVPacketPtr packet);
    void clearEofFlag();

private:
    bool abortRequest_ = false;
    bool eof_ = false;
    std::map<std::string, std::string> inputOptions_;
    AVFormatContext* formatCtx_ = nullptr;
    std::string inputFormat_;
    std::map<int, std::shared_ptr<CodecWrapper>> availableStream_;
    std::map<int, std::shared_ptr<CodecWrapper>> currentStream_;
    std::mutex mutex_;
};


#endif // AVDEMUXER_H
