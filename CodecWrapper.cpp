#include "CodecWrapper.h"
#include <QDebug>

static bool isSoftwarePixelFormat(AVPixelFormat format)
{
    switch (format)
    {
        case AV_PIX_FMT_VAAPI:
        case AV_PIX_FMT_VDPAU:
        case AV_PIX_FMT_MEDIACODEC:
        case AV_PIX_FMT_VIDEOTOOLBOX:
        case AV_PIX_FMT_D3D11VA_VLD:
        case AV_PIX_FMT_D3D11:
        case AV_PIX_FMT_CUDA:
        case AV_PIX_FMT_DXVA2_VLD:
        case AV_PIX_FMT_DRM_PRIME:
        case AV_PIX_FMT_MMAL:
        case AV_PIX_FMT_QSV:
            return false;
        default:
            return true;
    }
}


static AVPixelFormat my_get_format(AVCodecContext *ctx, const AVPixelFormat *format)
{
    auto d = reinterpret_cast<CodecWrapper*>(ctx->opaque);
    std::list<AVHWDeviceType> supported;
    for (int i=0;;i++)
    {
        const AVCodecHWConfig *hwconfig = avcodec_get_hw_config(ctx->codec, i);
        if (!hwconfig)
        {
            qDebug() << "avcodec_get_hw_config error";
            break;
        }
        else if (hwconfig->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
        {
            supported.push_back(hwconfig->device_type);
        }
    }

    if (supported.empty())
    {
         qDebug() << "Didn't support any hardware codedc";
    }
    else;
    {
        for (auto const& iter : supported)
        {
            qDebug() << "support hardware codec : " << av_hwdevice_get_type_name(iter);
        }
    }

    std::list<AVPixelFormat> softwareFormats;
    std::list<AVPixelFormat> hardwareFormats;

    for (int i = 0; format[i] != AV_PIX_FMT_NONE;i++)
    {
        if(isSoftwarePixelFormat(format[i]))
        {
            softwareFormats.push_back(format[i]);
            qDebug() << av_pix_fmt_desc_get(format[i])->name
                     << " : Available software pixel format : " << format[i];
        }
        else
        {
            hardwareFormats.push_back(format[i]);
            qDebug() << av_pix_fmt_desc_get(format[i])->name
                     << " : Available hardware pixel format : " << format[i];
        }
    }

    AVPixelFormat selectFormat = !softwareFormats.empty() ? softwareFormats.front() : AV_PIX_FMT_NONE;

    if(d->isSupportHardware())
    {
        for (auto &iter: hardwareFormats)
        {
            if (iter == AV_PIX_FMT_D3D11)
            {
                selectFormat = iter;
            }
        }
    }

    qDebug() << av_pix_fmt_desc_get(selectFormat)->name
             << " : select pixel format : " << selectFormat;
    return selectFormat;
}

CodecWrapper::CodecWrapper(AVStream *stream, const std::string inputVideoCodec, AVMediaType type, bool supportHWdevice)
    : mediaType_(type)
    , stream_(stream)
    , supportHWDevice_(supportHWdevice)
{
    if (inputVideoCodec.empty())
    {
        decoder_ = avcodec_find_decoder(stream->codecpar->codec_id);
    }
    else
    {
        qDebug() << "Loading : -vcodec " << inputVideoCodec;
        decoder_ = avcodec_find_decoder_by_name(inputVideoCodec.c_str());
    }
    if (!decoder_)
    {
        qWarning() << "avcodec_find_decoder failed";
    }
    avctx_ = avcodec_alloc_context3(nullptr);
    if (!avctx_)
    {
        qWarning() << "avcodec_alloc_context3 failed";
    }

    auto ret = avcodec_parameters_to_context(avctx_, stream->codecpar);
    if (ret < 0)
    {
        qWarning() << "avcodec_parameters_to_context failed";
    }

    if (AVMEDIA_TYPE_VIDEO == mediaType_ && supportHWDevice_)
    {
        if (!createHWDevice())
        {
            qWarning() << "HWdevice create failed.";
        }
    }

    avctx_->opaque = this;
    avctx_->get_format = my_get_format;

    ret = avcodec_open2(avctx_, decoder_, nullptr);

    if (ret < 0)
    {
        qDebug() << "avcodec_open2 failed";
        av_buffer_unref(&hwDev_);
        avcodec_free_context(&avctx_);
    }
}

AVStream *CodecWrapper::getStream()
{
    return stream_;
}

bool CodecWrapper::isSupportHardware()
{
    return supportHWDevice_;
}

AVCodecContext* CodecWrapper::getAvctx()
{
    return avctx_;
}

int CodecWrapper::createHWDevice()
{
    if (av_hwdevice_ctx_create(&hwDev_, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0) >= 0)
    {
        qDebug() << "av_hwdevice_ctx_create success, type is " <<  AV_HWDEVICE_TYPE_D3D11VA;
        avctx_->hw_device_ctx = hwDev_;
        avctx_->pix_fmt = AV_PIX_FMT_D3D11;
        return 1;
    }
    return 0;
}

CodecWrapper::~CodecWrapper()
{
    av_buffer_unref(&hwDev_);
    avcodec_free_context(&avctx_);
}
