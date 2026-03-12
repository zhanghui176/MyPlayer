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
    FaceDetectionRunner();

    bool loadModel(const std::string& modelPath);
    bool runOnRgbFrame(cv::Mat& rgbFrame);
    bool isReady() const;
    void reset();

private:
    bool ready_ = false;

#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
    cv::Ptr<cv::FaceDetectorYN> detector_;
    cv::Size inputSize_ = cv::Size(0, 0);
    FaceBoxDrawer boxDrawer_;
#endif
};
