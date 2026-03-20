#include "FaceBoxDrawer.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QString>

#include <algorithm>
#include <cstdio>

#include <opencv2/imgproc.hpp>

namespace {

cv::Point computeLabelOrigin(const cv::Rect& faceRect, const cv::Size& frameSize, const cv::Size& textSize, int baseline)
{
    int x = faceRect.x + faceRect.width + 6;
    int y = faceRect.y + faceRect.height;

    if (x + textSize.width + 4 >= frameSize.width)
    {
        x = std::max(0, frameSize.width - textSize.width - 4);
    }

    const int minBaselineY = textSize.height + 4;
    const int maxBaselineY = std::max(minBaselineY, frameSize.height - baseline - 2);
    y = std::max(minBaselineY, std::min(y, maxBaselineY));
    return cv::Point(x, y);
}

}  // namespace

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
    draw(rgbFrame, faces, std::vector<std::string>());
}

void FaceBoxDrawer::draw(cv::Mat& rgbFrame, const cv::Mat& faces, const std::vector<std::string>& labels) const
{
    if (rgbFrame.empty() || faces.empty() || faces.cols < 15)
    {
        return;
    }

    QImage rgbImage(
        rgbFrame.data,
        rgbFrame.cols,
        rgbFrame.rows,
        static_cast<int>(rgbFrame.step),
        QImage::Format_RGB888);
    QPainter painter(&rgbImage);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    QFont labelFont = painter.font();
    labelFont.setPointSize(10);
    painter.setFont(labelFont);
    const QFontMetrics labelMetrics(labelFont);

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

        if (static_cast<size_t>(i) < labels.size() && !labels[static_cast<size_t>(i)].empty())
        {
            const std::string& label = labels[static_cast<size_t>(i)];
            const QString labelText = QString::fromUtf8(label.c_str());
            const int baseline = labelMetrics.descent();
            const cv::Size textSize(labelMetrics.horizontalAdvance(labelText), labelMetrics.height());
            const cv::Point textOrigin = computeLabelOrigin(faceRect, rgbFrame.size(), textSize, baseline);
            const cv::Rect labelBackground(
                std::max(0, textOrigin.x - 3),
                std::max(0, textOrigin.y - textSize.height - 3),
                std::min(textSize.width + 6, rgbFrame.cols - std::max(0, textOrigin.x - 3)),
                std::min(textSize.height + baseline + 6, rgbFrame.rows - std::max(0, textOrigin.y - textSize.height - 3)));

            if (labelBackground.width > 0 && labelBackground.height > 0)
            {
                painter.fillRect(
                    QRect(labelBackground.x, labelBackground.y, labelBackground.width, labelBackground.height),
                    QColor(0, 255, 0));
            }

            painter.setPen(QColor(0, 0, 0));
            painter.drawText(QPoint(textOrigin.x, textOrigin.y), labelText);
        }
    }
}
