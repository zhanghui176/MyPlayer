#pragma once

#include <string>

#include <opencv2/core.hpp>

#include "FaceBoxDrawer.h"

#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
#include <opencv2/objdetect.hpp>
#endif

class FaceDetectionRunner
{
public:
    enum class InputResizeMode
    {
        DynamicInputSize,
        FixedSize,
    };

    FaceDetectionRunner();

    bool loadModel(const std::string& modelPath);
    bool runOnRgbFrame(cv::Mat& rgbFrame);
    bool isReady() const;
    void reset();

    void setInputResizeMode(InputResizeMode mode);
    InputResizeMode inputResizeMode() const;
    void setFixedInputSize(int width, int height);
    cv::Size fixedInputSize() const;
    void drawLastDetections(cv::Mat& rgbFrame) const;
    void clearLastDetections();

private:
    bool ready_ = false;
    InputResizeMode inputResizeMode_ = InputResizeMode::DynamicInputSize;
    cv::Size fixedInputSize_ = cv::Size(640, 640);

#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
    cv::Ptr<cv::FaceDetectorYN> detector_;
    cv::Size inputSize_ = cv::Size(0, 0);
    FaceBoxDrawer boxDrawer_;
    cv::Mat lastFaces_;
    cv::Size lastFacesFrameSize_ = cv::Size(0, 0);
#endif
};
