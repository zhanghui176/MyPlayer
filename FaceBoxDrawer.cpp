#include "FaceBoxDrawer.h"

#include <algorithm>
#include <cstdio>

#include <opencv2/imgproc.hpp>

FaceBoxDrawer::FaceBoxDrawer(float scoreThreshold)
    : scoreThreshold_(scoreThreshold)
{
}

void FaceBoxDrawer::setScoreThreshold(float threshold)
{
    scoreThreshold_ = threshold;
}

float FaceBoxDrawer::scoreThreshold() const
{
    return scoreThreshold_;
}

cv::Rect FaceBoxDrawer::clampRectToFrame(int x, int y, int w, int h, int frameWidth, int frameHeight)
{
    if (frameWidth <= 0 || frameHeight <= 0)
    {
        return cv::Rect();
    }

    x = std::max(0, std::min(x, frameWidth - 1));
    y = std::max(0, std::min(y, frameHeight - 1));
    w = std::max(0, std::min(w, frameWidth - x));
    h = std::max(0, std::min(h, frameHeight - y));
    return cv::Rect(x, y, w, h);
}

void FaceBoxDrawer::draw(cv::Mat& rgbFrame, const cv::Mat& faces) const
{
    if (rgbFrame.empty() || faces.empty() || faces.cols < 15)
    {
        return;
    }

    for (int i = 0; i < faces.rows; ++i)
    {
        const float score = faces.at<float>(i, 14);
        if (score < scoreThreshold_)
        {
            continue;
        }

        const int x = static_cast<int>(faces.at<float>(i, 0));
        const int y = static_cast<int>(faces.at<float>(i, 1));
        const int w = static_cast<int>(faces.at<float>(i, 2));
        const int h = static_cast<int>(faces.at<float>(i, 3));
        const cv::Rect faceRect = clampRectToFrame(x, y, w, h, rgbFrame.cols, rgbFrame.rows);
        if (faceRect.width <= 0 || faceRect.height <= 0)
        {
            continue;
        }

        cv::rectangle(rgbFrame, faceRect, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);

        for (int lm = 0; lm < 5; ++lm)
        {
            const int lx = static_cast<int>(faces.at<float>(i, 4 + lm * 2));
            const int ly = static_cast<int>(faces.at<float>(i, 5 + lm * 2));
            if (lx >= 0 && ly >= 0 && lx < rgbFrame.cols && ly < rgbFrame.rows)
            {
                cv::circle(rgbFrame, cv::Point(lx, ly), 2, cv::Scalar(255, 0, 0), -1, cv::LINE_AA);
            }
        }

        char scoreText[16] = {0};
        std::snprintf(scoreText, sizeof(scoreText), "%.2f", score);
        const int textY = std::max(0, faceRect.y - 8);
        cv::putText(
            rgbFrame,
            scoreText,
            cv::Point(faceRect.x, textY),
            cv::FONT_HERSHEY_SIMPLEX,
            0.45,
            cv::Scalar(0, 255, 0),
            1,
            cv::LINE_AA);
    }
}
