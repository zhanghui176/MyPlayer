#pragma once

#include <string>
#include <vector>

#include <opencv2/core.hpp>

class FaceBoxDrawer
{
public:
    explicit FaceBoxDrawer(float scoreThreshold = 0.5f);

    void setScoreThreshold(float threshold);
    float scoreThreshold() const;

    // faces format follows OpenCV FaceDetectorYN: [x, y, w, h, l0x, l0y, ... l4x, l4y, score]
    void draw(cv::Mat& rgbFrame, const cv::Mat& faces) const;
    void draw(cv::Mat& rgbFrame, const cv::Mat& faces, const std::vector<std::string>& labels) const;

private:
    static cv::Rect clampRectToFrame(int x, int y, int w, int h, int frameWidth, int frameHeight);

private:
    float scoreThreshold_;
};
