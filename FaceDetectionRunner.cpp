#include "FaceDetectionRunner.h"

#include <QDebug>

#include <algorithm>

#include <opencv2/imgproc.hpp>
#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
#include <opencv2/dnn.hpp>
#endif

namespace {

constexpr const char* kFaceTraceTitle = "[MYPLAYER_FACE_PIPELINE]";
constexpr float kFixedResizeTriggerRatio = 1.1f;

cv::Size sanitizeSize(const cv::Size& size)
{
    return cv::Size(std::max(1, size.width), std::max(1, size.height));
}

const char* resizeModeToString(FaceDetectionRunner::InputResizeMode mode)
{
    switch (mode)
    {
    case FaceDetectionRunner::InputResizeMode::DynamicInputSize:
        return "DynamicInputSize";
    case FaceDetectionRunner::InputResizeMode::FixedSize:
        return "FixedSize";
    default:
        return "Unknown";
    }
}

void scaleFaceCoords(cv::Mat& faces, float scaleX, float scaleY)
{
    if (faces.empty())
    {
        return;
    }

    static const int kXColumns[] = {0, 2, 4, 6, 8, 10, 12};
    static const int kYColumns[] = {1, 3, 5, 7, 9, 11, 13};

    for (int row = 0; row < faces.rows; ++row)
    {
        for (int col : kXColumns)
        {
            if (col < faces.cols)
            {
                faces.at<float>(row, col) *= scaleX;
            }
        }
        for (int col : kYColumns)
        {
            if (col < faces.cols)
            {
                faces.at<float>(row, col) *= scaleY;
            }
        }
    }
}

}  // namespace

FaceDetectionRunner::FaceDetectionRunner() = default;

void FaceDetectionRunner::setInputResizeMode(InputResizeMode mode)
{
    inputResizeMode_ = mode;
    clearLastDetections();
    qDebug() << kFaceTraceTitle << "[CONFIG] setInputResizeMode =" << resizeModeToString(mode);
}

FaceDetectionRunner::InputResizeMode FaceDetectionRunner::inputResizeMode() const
{
    return inputResizeMode_;
}

void FaceDetectionRunner::setFixedInputSize(int width, int height)
{
    fixedInputSize_ = sanitizeSize(cv::Size(width, height));
    clearLastDetections();
    qDebug() << kFaceTraceTitle << "[CONFIG] setFixedInputSize ="
             << fixedInputSize_.width << "x" << fixedInputSize_.height;
}

cv::Size FaceDetectionRunner::fixedInputSize() const
{
    return fixedInputSize_;
}

void FaceDetectionRunner::drawLastDetections(cv::Mat& rgbFrame) const
{
#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
    if (rgbFrame.empty())
    {
        return;
    }

    const cv::Size frameSize(rgbFrame.cols, rgbFrame.rows);
    if (frameSize != lastFacesFrameSize_)
    {
        return;
    }

    boxDrawer_.draw(rgbFrame, lastFaces_);
#else
    Q_UNUSED(rgbFrame);
#endif
}

void FaceDetectionRunner::clearLastDetections()
{
#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
    lastFaces_.release();
    lastFacesFrameSize_ = cv::Size(0, 0);
#endif
}

bool FaceDetectionRunner::loadModel(const std::string& modelPath)
{
    reset();

#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
    if (modelPath.empty())
    {
        return false;
    }

    try
    {
        detector_ = cv::FaceDetectorYN::create(
            modelPath,
            "",
            cv::Size(320, 320),
            0.6f,
            0.3f,
            5000,
            cv::dnn::DNN_BACKEND_OPENCV,
            cv::dnn::DNN_TARGET_CPU);

        ready_ = !detector_.empty();
        if (!ready_)
        {
            qDebug() << "Failed to initialize FaceDetectorYN with model:" << modelPath.c_str();
        }
        else
        {
            qDebug() << kFaceTraceTitle << "[MODEL] FaceDetectorYN loaded:" << modelPath.c_str();
        }
        return ready_;
    }
    catch (const cv::Exception& ex)
    {
        qDebug() << "FaceDetectionRunner load exception:" << ex.what();
        ready_ = false;
        return false;
    }
#else
    Q_UNUSED(modelPath);
    qDebug() << "Face detection support is disabled. Build with OpenCV objdetect to enable.";
    return false;
#endif
}

bool FaceDetectionRunner::detect(const cv::Mat& rgbFrame, cv::Mat& faces)
{
    faces.release();

#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
    if (!ready_ || rgbFrame.empty() || detector_.empty())
    {
        return false;
    }

    const bool useFixedSize = inputResizeMode_ == InputResizeMode::FixedSize;
    const cv::Size frameSize(rgbFrame.cols, rgbFrame.rows);
    const cv::Size requestedFixedSize = sanitizeSize(fixedInputSize_);
    const bool widthLargeEnough = static_cast<float>(frameSize.width)
                                  >= static_cast<float>(requestedFixedSize.width) * kFixedResizeTriggerRatio;
    const bool heightLargeEnough = static_cast<float>(frameSize.height)
                                   >= static_cast<float>(requestedFixedSize.height) * kFixedResizeTriggerRatio;
    const bool shouldResizeForDetect = useFixedSize
                                       && widthLargeEnough
                                       && heightLargeEnough;
    const cv::Size detectSize = shouldResizeForDetect ? requestedFixedSize : frameSize;

    if (useFixedSize && !shouldResizeForDetect)
    {
        static cv::Size lastBypassSize(0, 0);
        if (lastBypassSize != frameSize)
        {
            qDebug() << kFaceTraceTitle << "[INPUT] bypass fixed resize for low-res frame"
                     << "frame =" << frameSize.width << "x" << frameSize.height
                     << "requestedFixed =" << requestedFixedSize.width << "x" << requestedFixedSize.height
                     << "triggerRatio =" << kFixedResizeTriggerRatio;
            lastBypassSize = frameSize;
        }
    }

    if (inputSize_ != detectSize)
    {
        detector_->setInputSize(detectSize);
        inputSize_ = detectSize;
        qDebug() << kFaceTraceTitle << "[INPUT] setInputSize ="
                 << detectSize.width << "x" << detectSize.height
                 << "mode =" << resizeModeToString(inputResizeMode_)
                 << "frame =" << rgbFrame.cols << "x" << rgbFrame.rows;
    }

    cv::Mat detectRgb;
    if (useFixedSize && (detectSize.width != rgbFrame.cols || detectSize.height != rgbFrame.rows))
    {
        cv::resize(rgbFrame, detectRgb, detectSize, 0.0, 0.0, cv::INTER_LINEAR);
    }
    else
    {
        detectRgb = rgbFrame;
    }

    cv::Mat bgrFrame;
    cv::cvtColor(detectRgb, bgrFrame, cv::COLOR_RGB2BGR);

    cv::Mat detectedFaces;
    const int result = detector_->detect(bgrFrame, detectedFaces);
    if (result < 0)
    {
        qDebug() << "FaceDetectionRunner detect failed on current frame";
        return false;
    }

    detectedFaces.convertTo(faces, CV_32F);

    if (useFixedSize && detectSize.width > 0 && detectSize.height > 0
        && (detectSize.width != rgbFrame.cols || detectSize.height != rgbFrame.rows))
    {
        const float scaleX = static_cast<float>(rgbFrame.cols) / static_cast<float>(detectSize.width);
        const float scaleY = static_cast<float>(rgbFrame.rows) / static_cast<float>(detectSize.height);

        static cv::Size lastLoggedSourceSize(0, 0);
        static cv::Size lastLoggedDetectSize(0, 0);
        const cv::Size sourceSize(rgbFrame.cols, rgbFrame.rows);
        if (lastLoggedSourceSize != sourceSize || lastLoggedDetectSize != detectSize)
        {
            qDebug() << kFaceTraceTitle << "[SCALE] map detection coords"
                     << "source =" << sourceSize.width << "x" << sourceSize.height
                     << "detect =" << detectSize.width << "x" << detectSize.height
                     << "scaleX =" << scaleX << "scaleY =" << scaleY;
            lastLoggedSourceSize = sourceSize;
            lastLoggedDetectSize = detectSize;
        }

        scaleFaceCoords(faces, scaleX, scaleY);
    }

    return true;
#else
    Q_UNUSED(rgbFrame);
    return false;
#endif
}

bool FaceDetectionRunner::runOnRgbFrame(cv::Mat& rgbFrame)
{
#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
    cv::Mat faces;
    if (!detect(rgbFrame, faces))
    {
        clearLastDetections();
        return false;
    }

    lastFaces_ = faces.clone();
    lastFacesFrameSize_ = cv::Size(rgbFrame.cols, rgbFrame.rows);
    boxDrawer_.draw(rgbFrame, lastFaces_);
    return true;
#else
    Q_UNUSED(rgbFrame);
    return false;
#endif
}

bool FaceDetectionRunner::isReady() const
{
    return ready_;
}

void FaceDetectionRunner::reset()
{
    ready_ = false;
    clearLastDetections();
#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
    detector_.release();
    inputSize_ = cv::Size(0, 0);
#endif
}
