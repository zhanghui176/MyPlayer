#pragma once

#include <string>

#include <opencv2/core.hpp>

#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
#include <opencv2/dnn.hpp>
#endif

class ImageToImageOnnxRunner
{
public:
    ImageToImageOnnxRunner();

    bool loadModel(const std::string& modelPath);
    bool runOnRgbFrame(const cv::Mat& inputRgbFrame, cv::Mat& outputRgbFrame);

    bool isReady() const;
    void reset();

private:
#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
    static cv::Mat tensorToRgbFloat(const cv::Mat& outputTensor);
    static cv::Mat normalizeToU8(const cv::Mat& rgbFloat);

    cv::dnn::Net net_;
    std::string inputName_;
    std::string outputName_;
#endif

    bool ready_ = false;
};
