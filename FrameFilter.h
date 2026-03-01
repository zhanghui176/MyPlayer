#pragma once

#include <string>
#include "AVDemuxer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}

class FrameFilter
{
public:
    FrameFilter(AVCodecContext *dec_ctx, std::string filter_desc);
    ~FrameFilter();
    int initFilter(std::string filter_desc);
    AVFramePtr doFilt(AVFramePtr inFrame);
    int setFilterDescription(const std::string& filter_desc);

    AVFilterGraph* getFilterGraph() const { return filterGraph_; }
    AVFilterContext* getBuffersrcCtx() const { return buffersrcCtx_; }
    AVFilterContext* getBuffersinkCtx() const { return buffersinkCtx_; }

private:
    AVFilterGraph *filterGraph_;
    AVFilterContext *buffersrcCtx_;
    AVFilterContext *buffersinkCtx_;
    AVCodecContext *dec_ctx_;
    std::string filter_desc_;
};

