#pragma once

#include <memory>
#include <string>
#include "AVDemuxer.h"

class OnnxFrameProcessor
{
public:
    OnnxFrameProcessor();
    ~OnnxFrameProcessor();

    OnnxFrameProcessor(const OnnxFrameProcessor&) = delete;
    OnnxFrameProcessor& operator=(const OnnxFrameProcessor&) = delete;

    bool loadModel(const std::string& modelPath);
    AVFramePtr process(AVFramePtr inputFrame);
    bool isReady() const;
    const std::string& modelPath() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
