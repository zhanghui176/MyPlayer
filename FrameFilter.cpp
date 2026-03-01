#include "FrameFilter.h"
#include <QDebug>

FrameFilter::FrameFilter(AVCodecContext *dec_ctx, std::string filter_desc)
    : filterGraph_(nullptr)
    , buffersrcCtx_(nullptr)
    , buffersinkCtx_(nullptr)
    , dec_ctx_(dec_ctx)
    , filter_desc_(filter_desc)
{
    auto ret = initFilter(filter_desc.c_str());
    if (ret)
    {
        qDebug() << "Failed to initialize filter with description: " << filter_desc << ", error code: " << ret;
    } else {
        qDebug() << "Filter initialized successfully with description: " << filter_desc;
    }
}

FrameFilter::~FrameFilter()
{
    if (filterGraph_) {
        avfilter_graph_free(&filterGraph_);
        filterGraph_ = nullptr;
    }
}

int FrameFilter::setFilterDescription(const std::string& filter_desc)
{
    if (filter_desc.empty()) {
        qDebug() << "Filter description cannot be empty.";
        return -1;
    }
    filter_desc_ = filter_desc;
    return initFilter(filter_desc.c_str());
}

int FrameFilter::initFilter(std::string filter_desc)
{
    if (!dec_ctx_ || filter_desc.empty()) {
        qDebug() << "Decoder context is null or filter description is empty.";
        return -1;
    }
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    filterGraph_ = avfilter_graph_alloc();
    if (!filterGraph_) {
        qDebug() << "Unable to create filter graph.";
        return -1;
    }

    AVRational sar = dec_ctx_->sample_aspect_ratio.num ? dec_ctx_->sample_aspect_ratio : AVRational{1, 1};
    AVRational timeBase = {0, 1};
    if (dec_ctx_->pkt_timebase.num > 0 && dec_ctx_->pkt_timebase.den > 0)
    {
        timeBase = dec_ctx_->pkt_timebase;
    }
    else if (dec_ctx_->time_base.num > 0 && dec_ctx_->time_base.den > 0)
    {
        timeBase = dec_ctx_->time_base;
    }
    else
    {
        timeBase = AVRational{1, 1000};
    }
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             dec_ctx_->width, dec_ctx_->height, dec_ctx_->pix_fmt,
             timeBase.num, timeBase.den,
             sar.num, sar.den);

    ret = avfilter_graph_create_filter(&buffersrcCtx_, buffersrc, "in", args, nullptr, filterGraph_);
    if (ret < 0) {
        qDebug() << "Cannot create buffer source";
        return ret;
    }

    ret = avfilter_graph_create_filter(&buffersinkCtx_, buffersink, "out", nullptr, nullptr, filterGraph_);
    if (ret < 0) {
        qDebug() << "Cannot create buffer sink";
        return ret;
    }

    // Force YUV420P output so the result matches the OpenGL YUV420P path.
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
    if (av_opt_set_int_list(buffersinkCtx_, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN) < 0) {
        qDebug() << "Failed to set output pixel format.";
        return -1;
    }

    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    if (!outputs || !inputs) {
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        qDebug() << "Unable to allocate filter in/out.";
        return -1;
    }

    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrcCtx_;
    outputs->pad_idx    = 0;
    outputs->next       = nullptr;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersinkCtx_;
    inputs->pad_idx    = 0;
    inputs->next       = nullptr;

    if ((ret = avfilter_graph_parse_ptr(filterGraph_, filter_desc.c_str(), &inputs, &outputs, nullptr)) < 0) {
        qDebug() << "Error while parsing filter graph";
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        return ret;
    }

    if ((ret = avfilter_graph_config(filterGraph_, nullptr)) < 0) {
        qDebug() << "Error while configuring filter graph";
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        return ret;
    }

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return 0;
}

AVFramePtr FrameFilter::doFilt(AVFramePtr inFrame)
{
    AVFramePtr outputFrame(av_frame_alloc());
    if (!outputFrame)
    {
        qDebug() << "failed to alloc outputFrame";
        return nullptr;
    }

    if (!buffersrcCtx_ || !buffersinkCtx_)
    {
        qDebug() << "buffersinkCtx_ or buffersrcCtx_ is invalid";
        return nullptr;
    }

    int ret = av_buffersrc_add_frame_flags(buffersrcCtx_, inFrame.get(), AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0) {
        qDebug() << "Error while feeding the filter graph";
        return nullptr;
    }

    ret = av_buffersink_get_frame(buffersinkCtx_, outputFrame.get());
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return nullptr;
    }
    if (ret < 0) {
        qDebug() << "Error while getting frame from filter graph";
        return nullptr;
    }

    return std::move(outputFrame);
}




