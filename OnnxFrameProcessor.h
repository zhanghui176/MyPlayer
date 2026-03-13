#pragma once

#include <memory>
#include <string>
#include "AVDemuxer.h"

class OnnxFrameProcessor
{
public:
    enum class FaceInputMode
    {
        DynamicInputSize,
        FixedSize,
    };

    OnnxFrameProcessor();
    ~OnnxFrameProcessor();

    OnnxFrameProcessor(const OnnxFrameProcessor&) = delete;
    OnnxFrameProcessor& operator=(const OnnxFrameProcessor&) = delete;

    bool loadModel(const std::string& modelPath);
    AVFramePtr process(AVFramePtr inputFrame);
    bool isReady() const;
    const std::string& modelPath() const;
    void setFaceDetectionInputMode(FaceInputMode mode);
    FaceInputMode faceDetectionInputMode() const;
    void setFaceDetectionFixedInputSize(int width, int height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
