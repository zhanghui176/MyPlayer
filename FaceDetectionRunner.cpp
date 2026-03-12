#include "FaceDetectionRunner.h"

#include <QDebug>

#include <opencv2/imgproc.hpp>
#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
#include <opencv2/dnn.hpp>
#endif

FaceDetectionRunner::FaceDetectionRunner() = default;

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

bool FaceDetectionRunner::runOnRgbFrame(cv::Mat& rgbFrame)
{
#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
    if (!ready_ || rgbFrame.empty() || detector_.empty())
    {
        return false;
    }

    const cv::Size currentSize(rgbFrame.cols, rgbFrame.rows);
    if (inputSize_ != currentSize)
    {
        detector_->setInputSize(currentSize);
        inputSize_ = currentSize;
    }

    cv::Mat bgrFrame;
    cv::cvtColor(rgbFrame, bgrFrame, cv::COLOR_RGB2BGR);

    cv::Mat faces;
    const int result = detector_->detect(bgrFrame, faces);
    if (result < 0)
    {
        qDebug() << "FaceDetectionRunner detect failed on current frame";
        return false;
    }

    boxDrawer_.draw(rgbFrame, faces);
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
#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
    detector_.release();
    inputSize_ = cv::Size(0, 0);
#endif
}
