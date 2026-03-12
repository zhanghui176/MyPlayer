#include "OnnxFrameProcessor.h"
#include "FaceDetectionRunner.h"
#include "ImageToImageOnnxRunner.h"

#include <QDebug>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
#include <opencv2/core.hpp>
#endif

struct OnnxFrameProcessor::Impl
{
    enum class ModelKind
    {
        None,
        ImageToImage,
        FaceDetection,
    };

    std::string modelPath;
    bool ready = false;

#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
    std::mutex mutex;
    ModelKind modelKind = ModelKind::None;
    SwsContext* srcToRgbCtx = nullptr;
    SwsContext* rgbToYuvCtx = nullptr;

    FaceDetectionRunner faceDetectionRunner;
    ImageToImageOnnxRunner imageToImageRunner;

    ~Impl()
    {
        if (srcToRgbCtx)
        {
            sws_freeContext(srcToRgbCtx);
            srcToRgbCtx = nullptr;
        }
        if (rgbToYuvCtx)
        {
            sws_freeContext(rgbToYuvCtx);
            rgbToYuvCtx = nullptr;
        }
    }
#endif
};

#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
namespace {

std::string toLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool looksLikeFaceDetectionModel(const std::string& modelPath)
{
    const std::string lowerPath = toLowerCopy(modelPath);
    return lowerPath.find("yunet") != std::string::npos
        || lowerPath.find("face_detection") != std::string::npos
        || lowerPath.find("retinaface") != std::string::npos
        || lowerPath.find("scrfd") != std::string::npos;
}

AVFramePtr allocFrame(int width, int height, AVPixelFormat format)
{
    AVFramePtr frame(av_frame_alloc());
    if (!frame)
    {
        return nullptr;
    }

    frame->width = width;
    frame->height = height;
    frame->format = format;

    if (av_frame_get_buffer(frame.get(), 32) < 0)
    {
        return nullptr;
    }

    if (av_frame_make_writable(frame.get()) < 0)
    {
        return nullptr;
    }

    return frame;
}

AVFramePtr convertSourceToRgb(SwsContext*& srcToRgbCtx, const AVFrame* inputFrame)
{
    if (!inputFrame)
    {
        return nullptr;
    }

    const int srcWidth = inputFrame->width;
    const int srcHeight = inputFrame->height;
    const AVPixelFormat srcFormat = static_cast<AVPixelFormat>(inputFrame->format);

    AVFramePtr rgbFrame = allocFrame(srcWidth, srcHeight, AV_PIX_FMT_RGB24);
    if (!rgbFrame)
    {
        return nullptr;
    }

    srcToRgbCtx = sws_getCachedContext(
        srcToRgbCtx,
        srcWidth,
        srcHeight,
        srcFormat,
        srcWidth,
        srcHeight,
        AV_PIX_FMT_RGB24,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr);

    if (!srcToRgbCtx)
    {
        return nullptr;
    }

    sws_scale(
        srcToRgbCtx,
        inputFrame->data,
        inputFrame->linesize,
        0,
        srcHeight,
        rgbFrame->data,
        rgbFrame->linesize);

    return rgbFrame;
}

AVFramePtr convertRgbToYuv420p(SwsContext*& rgbToYuvCtx, const AVFrame* rgbFrame)
{
    if (!rgbFrame)
    {
        return nullptr;
    }

    const int width = rgbFrame->width;
    const int height = rgbFrame->height;

    AVFramePtr yuvFrame = allocFrame(width, height, AV_PIX_FMT_YUV420P);
    if (!yuvFrame)
    {
        return nullptr;
    }

    rgbToYuvCtx = sws_getCachedContext(
        rgbToYuvCtx,
        width,
        height,
        AV_PIX_FMT_RGB24,
        width,
        height,
        AV_PIX_FMT_YUV420P,
        SWS_BICUBIC,
        nullptr,
        nullptr,
        nullptr);

    if (!rgbToYuvCtx)
    {
        return nullptr;
    }

    sws_scale(
        rgbToYuvCtx,
        rgbFrame->data,
        rgbFrame->linesize,
        0,
        height,
        yuvFrame->data,
        yuvFrame->linesize);

    return yuvFrame;
}

bool copyRgbMatToFrame(const cv::Mat& rgb, AVFrame* frame)
{
    if (!frame || rgb.empty() || rgb.type() != CV_8UC3)
    {
        return false;
    }

    for (int y = 0; y < rgb.rows; ++y)
    {
        std::memcpy(frame->data[0] + y * frame->linesize[0], rgb.ptr(y), static_cast<size_t>(rgb.cols * 3));
    }
    return true;
}

AVFramePtr processFaceDetectionFrame(
    AVFramePtr inputFrame,
    SwsContext*& srcToRgbCtx,
    SwsContext*& rgbToYuvCtx,
    FaceDetectionRunner& faceDetectionRunner
)
{
    AVFramePtr rgbFrame = convertSourceToRgb(srcToRgbCtx, inputFrame.get());
    if (!rgbFrame)
    {
        qDebug() << "Failed to convert frame to RGB for face detection";
        return inputFrame;
    }

    cv::Mat rgbMat(rgbFrame->height, rgbFrame->width, CV_8UC3, rgbFrame->data[0], rgbFrame->linesize[0]);
    faceDetectionRunner.runOnRgbFrame(rgbMat);

    AVFramePtr outputFrame = convertRgbToYuv420p(rgbToYuvCtx, rgbFrame.get());
    if (!outputFrame)
    {
        qDebug() << "Failed to convert face-overlay RGB frame back to YUV";
        return inputFrame;
    }

    av_frame_copy_props(outputFrame.get(), inputFrame.get());
    return outputFrame;
}

AVFramePtr processImageToImageFrame(
    AVFramePtr inputFrame,
    SwsContext*& srcToRgbCtx,
    SwsContext*& rgbToYuvCtx,
    ImageToImageOnnxRunner& imageToImageRunner)
{
    AVFramePtr rgbInputFrame = convertSourceToRgb(srcToRgbCtx, inputFrame.get());
    if (!rgbInputFrame)
    {
        qDebug() << "Failed to convert frame to RGB for ONNX processing";
        return inputFrame;
    }

    const int srcWidth = inputFrame->width;
    const int srcHeight = inputFrame->height;

    cv::Mat rgbInput(srcHeight, srcWidth, CV_8UC3, rgbInputFrame->data[0], rgbInputFrame->linesize[0]);
    cv::Mat rgbOutput;
    if (!imageToImageRunner.runOnRgbFrame(rgbInput, rgbOutput))
    {
        qDebug() << "Image-to-image ONNX runner failed on current frame";
        return inputFrame;
    }

    AVFramePtr rgbOutputFrame = allocFrame(srcWidth, srcHeight, AV_PIX_FMT_RGB24);
    if (!rgbOutputFrame || !copyRgbMatToFrame(rgbOutput, rgbOutputFrame.get()))
    {
        qDebug() << "Failed to build RGB output frame from ONNX result";
        return inputFrame;
    }

    AVFramePtr yuvOutputFrame = convertRgbToYuv420p(rgbToYuvCtx, rgbOutputFrame.get());
    if (!yuvOutputFrame)
    {
        qDebug() << "Failed to convert ONNX RGB output frame back to YUV";
        return inputFrame;
    }

    av_frame_copy_props(yuvOutputFrame.get(), inputFrame.get());
    return yuvOutputFrame;
}

}  // namespace
#endif

OnnxFrameProcessor::OnnxFrameProcessor()
    : impl_(std::make_unique<Impl>())
{
}

OnnxFrameProcessor::~OnnxFrameProcessor() = default;

bool OnnxFrameProcessor::loadModel(const std::string& modelPath)
{
    impl_->modelPath = modelPath;

#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
    if (modelPath.empty())
    {
        impl_->ready = false;
        impl_->modelKind = Impl::ModelKind::None;
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ready = false;
    impl_->modelKind = Impl::ModelKind::None;
    impl_->faceDetectionRunner.reset();
    impl_->imageToImageRunner.reset();

    if (looksLikeFaceDetectionModel(modelPath))
    {
        if (impl_->faceDetectionRunner.loadModel(modelPath))
        {
            impl_->ready = true;
            impl_->modelKind = Impl::ModelKind::FaceDetection;
            qDebug() << "ONNX face detection model loaded:" << modelPath.c_str();
            return true;
        }
        qDebug() << "Face detector init failed, fallback to generic ONNX path:" << modelPath.c_str();
    }

    if (impl_->imageToImageRunner.loadModel(modelPath))
    {
        impl_->ready = true;
        impl_->modelKind = Impl::ModelKind::ImageToImage;
        qDebug() << "ONNX image model loaded:" << modelPath.c_str();
        return true;
    }

    qDebug() << "Failed to load ONNX model:" << modelPath.c_str();
    impl_->ready = false;
    impl_->modelKind = Impl::ModelKind::None;
    return false;
#else
    Q_UNUSED(modelPath);
    qDebug() << "ONNX support is disabled. Build with OpenCV DNN to enable model inference.";
    impl_->ready = false;
    return false;
#endif
}

AVFramePtr OnnxFrameProcessor::process(AVFramePtr inputFrame)
{
#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
    if (!inputFrame || !impl_->ready)
    {
        return inputFrame;
    }

    const int srcWidth = inputFrame->width;
    const int srcHeight = inputFrame->height;
    const AVPixelFormat srcFormat = static_cast<AVPixelFormat>(inputFrame->format);

    if (srcWidth <= 0 || srcHeight <= 0 || srcFormat == AV_PIX_FMT_NONE)
    {
        return inputFrame;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);

    if (impl_->modelKind == Impl::ModelKind::FaceDetection)
    {
        return processFaceDetectionFrame(
            std::move(inputFrame),
            impl_->srcToRgbCtx,
            impl_->rgbToYuvCtx,
            impl_->faceDetectionRunner);
    }

    if (impl_->modelKind == Impl::ModelKind::ImageToImage)
    {
        return processImageToImageFrame(
            std::move(inputFrame),
            impl_->srcToRgbCtx,
            impl_->rgbToYuvCtx,
            impl_->imageToImageRunner);
    }

    return inputFrame;
#else
    return inputFrame;
#endif
}

bool OnnxFrameProcessor::isReady() const
{
    return impl_->ready;
}

const std::string& OnnxFrameProcessor::modelPath() const
{
    return impl_->modelPath;
}
