#include "OnnxFrameProcessor.h"
#include "AppPaths.h"
#include "FaceBoxDrawer.h"
#include "FaceDatabase.h"
#include "FaceDetectionRunner.h"
#include "FaceRecognitionRunner.h"
#include "ImageToImageOnnxRunner.h"

#include <QDebug>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

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

    std::string modelPath_;
    bool ready_ = false;
    OnnxFrameProcessor::FaceInputMode faceInputMode_ = OnnxFrameProcessor::FaceInputMode::DynamicInputSize;
    int faceFixedInputWidth_ = 640;
    int faceFixedInputHeight_ = 640;

#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
    std::mutex mutex_;
    ModelKind modelKind_ = ModelKind::None;
    SwsContext* srcToRgbCtx_ = nullptr;
    SwsContext* rgbToYuvCtx_ = nullptr;
    std::uint64_t faceFrameCounter_ = 0;
    int lastLoggedFaceDetectInterval_ = 0;

    FaceDetectionRunner faceDetectionRunner_;
    FaceRecognitionRunner faceRecognitionRunner_;
    FaceBoxDrawer faceBoxDrawer_;
    ImageToImageOnnxRunner imageToImageRunner_;
    std::vector<FaceIdentityRecord> faceIdentityCache_;
    cv::Mat lastFaceDetections_;
    std::vector<std::string> lastFaceLabels_;
    cv::Size lastFaceFrameSize_ = cv::Size(0, 0);

    ~Impl()
    {
        if (srcToRgbCtx_)
        {
            sws_freeContext(srcToRgbCtx_);
            srcToRgbCtx_ = nullptr;
        }
        if (rgbToYuvCtx_)
        {
            sws_freeContext(rgbToYuvCtx_);
            rgbToYuvCtx_ = nullptr;
        }
    }
#endif
};

#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
namespace {

constexpr const char* kOnnxFaceTraceTitle = "[MYPLAYER_ONNX_FACE]";
constexpr const char* kUnknownFaceLabel = "Unknown";

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

FaceDetectionRunner::InputResizeMode toRunnerResizeMode(OnnxFrameProcessor::FaceInputMode mode)
{
    switch (mode)
    {
    case OnnxFrameProcessor::FaceInputMode::FixedSize:
        return FaceDetectionRunner::InputResizeMode::FixedSize;
    case OnnxFrameProcessor::FaceInputMode::DynamicInputSize:
    default:
        return FaceDetectionRunner::InputResizeMode::DynamicInputSize;
    }
}

int chooseFaceDetectInterval(int width, int height)
{
    const std::int64_t pixelCount = static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height);
    if (pixelCount >= static_cast<std::int64_t>(1920 * 1080))
    {
        return 4;
    }
    if (pixelCount >= static_cast<std::int64_t>(1280 * 720))
    {
        return 3;
    }
    if (pixelCount >= static_cast<std::int64_t>(640 * 360))
    {
        return 2;
    }
    return 1;
}

void clearFaceOverlayCache(cv::Mat& lastFaceDetections, std::vector<std::string>& lastFaceLabels, cv::Size& lastFaceFrameSize)
{
    lastFaceDetections.release();
    lastFaceLabels.clear();
    lastFaceFrameSize = cv::Size(0, 0);
}

std::vector<FaceIdentityRecord> loadFaceIdentityCache()
{
    std::vector<FaceIdentityRecord> identities;

    FaceDatabase database;
    const QString databasePath = AppPaths::getFaceDatabasePath();
    if (!database.open(databasePath))
    {
        qDebug() << kOnnxFaceTraceTitle << "[DB] unable to open face database:" << databasePath
                 << database.lastError();
        return identities;
    }

    identities = database.loadAllFaceIdentities();
    if (!database.lastError().isEmpty())
    {
        qDebug() << kOnnxFaceTraceTitle << "[DB] unable to load face identities:" << database.lastError();
        identities.clear();
        return identities;
    }

    qDebug() << kOnnxFaceTraceTitle << "[DB] loaded face identities =" << identities.size();
    return identities;
}

std::vector<std::string> buildFaceLabels(
    const cv::Mat& rgbFrame,
    const cv::Mat& faces,
    FaceRecognitionRunner& faceRecognitionRunner,
    const std::vector<FaceIdentityRecord>& faceIdentityCache)
{
    std::vector<std::string> labels(static_cast<size_t>(faces.rows), kUnknownFaceLabel);
    if (faces.empty())
    {
        return labels;
    }

    if (!faceRecognitionRunner.isReady() || faceIdentityCache.empty())
    {
        return labels;
    }

    for (int row = 0; row < faces.rows; ++row)
    {
        std::vector<float> embedding;
        if (!faceRecognitionRunner.extractEmbedding(rgbFrame, faces.row(row).clone(), embedding))
        {
            continue;
        }

        const FaceMatchResult match = faceRecognitionRunner.match(embedding, faceIdentityCache);
        if (match.matched && !match.personName.empty())
        {
            labels[static_cast<size_t>(row)] = match.personName;
        }
    }

    return labels;
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
    FaceDetectionRunner& faceDetectionRunner,
    FaceRecognitionRunner& faceRecognitionRunner,
    FaceBoxDrawer& faceBoxDrawer,
    const std::vector<FaceIdentityRecord>& faceIdentityCache,
    cv::Mat& lastFaceDetections,
    std::vector<std::string>& lastFaceLabels,
    cv::Size& lastFaceFrameSize,
    std::uint64_t& faceFrameCounter,
    int& lastLoggedFaceDetectInterval
)
{
    AVFramePtr rgbFrame = convertSourceToRgb(srcToRgbCtx, inputFrame.get());
    if (!rgbFrame)
    {
        qDebug() << "Failed to convert frame to RGB for face detection";
        return inputFrame;
    }

    cv::Mat rgbMat(rgbFrame->height, rgbFrame->width, CV_8UC3, rgbFrame->data[0], rgbFrame->linesize[0]);
    const int detectInterval = chooseFaceDetectInterval(rgbFrame->width, rgbFrame->height);
    if (lastLoggedFaceDetectInterval != detectInterval)
    {
        qDebug() << kOnnxFaceTraceTitle << "[PERF] face detection interval =" << detectInterval
                 << "frame =" << rgbFrame->width << "x" << rgbFrame->height;
        lastLoggedFaceDetectInterval = detectInterval;
    }

    const bool shouldRunDetection = (faceFrameCounter % static_cast<std::uint64_t>(detectInterval)) == 0;
    ++faceFrameCounter;

    if (shouldRunDetection)
    {
        cv::Mat faces;
        if (!faceDetectionRunner.detect(rgbMat, faces) || faces.empty())
        {
            clearFaceOverlayCache(lastFaceDetections, lastFaceLabels, lastFaceFrameSize);
        }
        else
        {
            lastFaceDetections = faces.clone();
            lastFaceLabels = buildFaceLabels(
                rgbMat,
                lastFaceDetections,
                faceRecognitionRunner,
                faceIdentityCache);
            lastFaceFrameSize = cv::Size(rgbMat.cols, rgbMat.rows);
        }
    }

    if (lastFaceFrameSize != cv::Size(rgbMat.cols, rgbMat.rows))
    {
        clearFaceOverlayCache(lastFaceDetections, lastFaceLabels, lastFaceFrameSize);
    }
    else if (!lastFaceDetections.empty())
    {
        faceBoxDrawer.draw(rgbMat, lastFaceDetections, lastFaceLabels);
    }

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
    impl_->modelPath_ = modelPath;

#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
    if (modelPath.empty())
    {
        impl_->ready_ = false;
        impl_->modelKind_ = Impl::ModelKind::None;
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->ready_ = false;
    impl_->modelKind_ = Impl::ModelKind::None;
    impl_->faceFrameCounter_ = 0;
    impl_->lastLoggedFaceDetectInterval_ = 0;
    impl_->faceDetectionRunner_.reset();
    impl_->faceRecognitionRunner_.reset();
    impl_->imageToImageRunner_.reset();
    impl_->faceIdentityCache_.clear();
    clearFaceOverlayCache(impl_->lastFaceDetections_, impl_->lastFaceLabels_, impl_->lastFaceFrameSize_);

    if (looksLikeFaceDetectionModel(modelPath))
    {
        if (impl_->faceDetectionRunner_.loadModel(modelPath))
        {
            const std::string recognitionModelPath = AppPaths::getFaceRecognitionModelPath().toStdString();
            if (impl_->faceRecognitionRunner_.loadModel(recognitionModelPath))
            {
                impl_->faceIdentityCache_ = loadFaceIdentityCache();
                qDebug() << kOnnxFaceTraceTitle << "[RECOGNITION] enabled, gallery size ="
                         << impl_->faceIdentityCache_.size();
            }
            else
            {
                qDebug() << kOnnxFaceTraceTitle << "[RECOGNITION] disabled, unable to load model:"
                         << recognitionModelPath.c_str();
            }

            impl_->ready_ = true;
            impl_->modelKind_ = Impl::ModelKind::FaceDetection;
            qDebug() << "ONNX face detection model loaded:" << modelPath.c_str();
            return true;
        }
        qDebug() << "Face detector init failed, fallback to generic ONNX path:" << modelPath.c_str();
    }

    if (impl_->imageToImageRunner_.loadModel(modelPath))
    {
        impl_->ready_ = true;
        impl_->modelKind_ = Impl::ModelKind::ImageToImage;
        qDebug() << "ONNX image model loaded:" << modelPath.c_str();
        return true;
    }

    qDebug() << "Failed to load ONNX model:" << modelPath.c_str();
    impl_->ready_ = false;
    impl_->modelKind_ = Impl::ModelKind::None;
    return false;
#else
    Q_UNUSED(modelPath);
    qDebug() << "ONNX support is disabled. Build with OpenCV DNN to enable model inference.";
    impl_->ready_ = false;
    return false;
#endif
}

AVFramePtr OnnxFrameProcessor::process(AVFramePtr inputFrame)
{
#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
    if (!inputFrame || !impl_->ready_)
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

        std::lock_guard<std::mutex> lock(impl_->mutex_);

        if (impl_->modelKind_ == Impl::ModelKind::FaceDetection)
    {
        return processFaceDetectionFrame(
            std::move(inputFrame),
            impl_->srcToRgbCtx_,
            impl_->rgbToYuvCtx_,
            impl_->faceDetectionRunner_,
            impl_->faceRecognitionRunner_,
            impl_->faceBoxDrawer_,
            impl_->faceIdentityCache_,
            impl_->lastFaceDetections_,
            impl_->lastFaceLabels_,
            impl_->lastFaceFrameSize_,
            impl_->faceFrameCounter_,
            impl_->lastLoggedFaceDetectInterval_);
    }

        if (impl_->modelKind_ == Impl::ModelKind::ImageToImage)
    {
        return processImageToImageFrame(
            std::move(inputFrame),
            impl_->srcToRgbCtx_,
            impl_->rgbToYuvCtx_,
            impl_->imageToImageRunner_);
    }

    return inputFrame;
#else
    return inputFrame;
#endif
}

bool OnnxFrameProcessor::isReady() const
{
    return impl_->ready_;
}

const std::string& OnnxFrameProcessor::modelPath() const
{
    return impl_->modelPath_;
}

void OnnxFrameProcessor::setFaceDetectionInputMode(FaceInputMode mode)
{
    impl_->faceInputMode_ = mode;

#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->faceFrameCounter_ = 0;
    impl_->lastLoggedFaceDetectInterval_ = 0;
    clearFaceOverlayCache(impl_->lastFaceDetections_, impl_->lastFaceLabels_, impl_->lastFaceFrameSize_);
    impl_->faceDetectionRunner_.setInputResizeMode(toRunnerResizeMode(mode));
#endif
}

OnnxFrameProcessor::FaceInputMode OnnxFrameProcessor::faceDetectionInputMode() const
{
    return impl_->faceInputMode_;
}

void OnnxFrameProcessor::setFaceDetectionFixedInputSize(int width, int height)
{
    impl_->faceFixedInputWidth_ = std::max(1, width);
    impl_->faceFixedInputHeight_ = std::max(1, height);

#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->faceFrameCounter_ = 0;
    impl_->lastLoggedFaceDetectInterval_ = 0;
    clearFaceOverlayCache(impl_->lastFaceDetections_, impl_->lastFaceLabels_, impl_->lastFaceFrameSize_);
    impl_->faceDetectionRunner_.setFixedInputSize(impl_->faceFixedInputWidth_, impl_->faceFixedInputHeight_);
#endif
}
