#include "ImageToImageOnnxRunner.h"

#include <QDebug>

#include <opencv2/imgproc.hpp>

ImageToImageOnnxRunner::ImageToImageOnnxRunner() = default;

bool ImageToImageOnnxRunner::loadModel(const std::string& modelPath)
{
    reset();

#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
    if (modelPath.empty())
    {
        return false;
    }

    try
    {
        net_ = cv::dnn::readNetFromONNX(modelPath);
        if (net_.empty())
        {
            qDebug() << "ImageToImageOnnxRunner: failed to load ONNX model:" << modelPath.c_str();
            ready_ = false;
            return false;
        }

        net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);

        inputName_.clear();

        const auto outputNames = net_.getUnconnectedOutLayersNames();
        outputName_ = outputNames.empty() ? std::string() : outputNames.front();

        ready_ = true;
        return true;
    }
    catch (const cv::Exception& ex)
    {
        qDebug() << "ImageToImageOnnxRunner load exception:" << ex.what();
        ready_ = false;
        return false;
    }
#else
    Q_UNUSED(modelPath);
    qDebug() << "ONNX support is disabled. Build with OpenCV DNN to enable image-to-image inference.";
    return false;
#endif
}

bool ImageToImageOnnxRunner::runOnRgbFrame(const cv::Mat& inputRgbFrame, cv::Mat& outputRgbFrame)
{
#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
    if (!ready_ || inputRgbFrame.empty())
    {
        return false;
    }

    cv::Mat contiguousInput = inputRgbFrame.isContinuous() ? inputRgbFrame : inputRgbFrame.clone();

    cv::Mat networkOutput;
    try
    {
        cv::Mat blob = cv::dnn::blobFromImage(
            contiguousInput,
            1.0 / 255.0,
            cv::Size(inputRgbFrame.cols, inputRgbFrame.rows),
            cv::Scalar(),
            false,
            false,
            CV_32F);

        if (!inputName_.empty())
        {
            net_.setInput(blob, inputName_);
        }
        else
        {
            net_.setInput(blob);
        }

        if (!outputName_.empty())
        {
            networkOutput = net_.forward(outputName_);
        }
        else
        {
            networkOutput = net_.forward();
        }
    }
    catch (const cv::Exception& ex)
    {
        qDebug() << "ImageToImageOnnxRunner forward exception:" << ex.what();
        return false;
    }

    cv::Mat rgbFloat = tensorToRgbFloat(networkOutput);
    if (rgbFloat.empty())
    {
        qDebug() << "ImageToImageOnnxRunner: unsupported output shape. Expected 3-channel image tensor.";
        return false;
    }

    cv::Mat rgbU8 = normalizeToU8(rgbFloat);
    if (rgbU8.empty())
    {
        return false;
    }

    if (rgbU8.cols != inputRgbFrame.cols || rgbU8.rows != inputRgbFrame.rows)
    {
        cv::resize(rgbU8, rgbU8, cv::Size(inputRgbFrame.cols, inputRgbFrame.rows), 0.0, 0.0, cv::INTER_LINEAR);
    }

    outputRgbFrame = rgbU8;
    return true;
#else
    Q_UNUSED(inputRgbFrame);
    Q_UNUSED(outputRgbFrame);
    return false;
#endif
}

bool ImageToImageOnnxRunner::isReady() const
{
    return ready_;
}

void ImageToImageOnnxRunner::reset()
{
    ready_ = false;
#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
    net_ = cv::dnn::Net();
    inputName_.clear();
    outputName_.clear();
#endif
}

#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
cv::Mat ImageToImageOnnxRunner::tensorToRgbFloat(const cv::Mat& outputTensor)
{
    if (outputTensor.empty())
    {
        return cv::Mat();
    }

    cv::Mat tensor;
    if (outputTensor.type() != CV_32F)
    {
        outputTensor.convertTo(tensor, CV_32F);
    }
    else
    {
        tensor = outputTensor;
    }

    if (tensor.dims == 4)
    {
        if (tensor.size[0] != 1)
        {
            return cv::Mat();
        }

        if (tensor.size[1] == 3)
        {
            const int h = tensor.size[2];
            const int w = tensor.size[3];
            const int planeSize = h * w;
            const float* data = tensor.ptr<float>();

            cv::Mat rgb(h, w, CV_32FC3);
            for (int y = 0; y < h; ++y)
            {
                float* row = rgb.ptr<float>(y);
                for (int x = 0; x < w; ++x)
                {
                    const int index = y * w + x;
                    row[3 * x + 0] = data[index];
                    row[3 * x + 1] = data[planeSize + index];
                    row[3 * x + 2] = data[2 * planeSize + index];
                }
            }
            return rgb;
        }

        if (tensor.size[3] == 3)
        {
            const int h = tensor.size[1];
            const int w = tensor.size[2];
            cv::Mat rgb(h, w, CV_32FC3, const_cast<float*>(tensor.ptr<float>()));
            return rgb.clone();
        }
    }

    if (tensor.dims == 3)
    {
        if (tensor.size[0] == 3)
        {
            const int h = tensor.size[1];
            const int w = tensor.size[2];
            const int planeSize = h * w;
            const float* data = tensor.ptr<float>();

            cv::Mat rgb(h, w, CV_32FC3);
            for (int y = 0; y < h; ++y)
            {
                float* row = rgb.ptr<float>(y);
                for (int x = 0; x < w; ++x)
                {
                    const int index = y * w + x;
                    row[3 * x + 0] = data[index];
                    row[3 * x + 1] = data[planeSize + index];
                    row[3 * x + 2] = data[2 * planeSize + index];
                }
            }
            return rgb;
        }

        if (tensor.size[2] == 3)
        {
            const int h = tensor.size[0];
            const int w = tensor.size[1];
            cv::Mat rgb(h, w, CV_32FC3, const_cast<float*>(tensor.ptr<float>()));
            return rgb.clone();
        }
    }

    return cv::Mat();
}

cv::Mat ImageToImageOnnxRunner::normalizeToU8(const cv::Mat& rgbFloat)
{
    if (rgbFloat.empty())
    {
        return cv::Mat();
    }

    cv::Mat floatRgb;
    if (rgbFloat.type() != CV_32FC3)
    {
        rgbFloat.convertTo(floatRgb, CV_32FC3);
    }
    else
    {
        floatRgb = rgbFloat;
    }

    cv::Mat flat = floatRgb.reshape(1);
    double minVal = 0.0;
    double maxVal = 0.0;
    cv::minMaxLoc(flat, &minVal, &maxVal);

    cv::Mat normalized;
    if (maxVal <= 1.5 && minVal >= -1.5)
    {
        if (minVal < 0.0)
        {
            normalized = (floatRgb + 1.0f) * 0.5f;
        }
        else
        {
            normalized = floatRgb;
        }

        cv::min(normalized, 1.0, normalized);
        cv::max(normalized, 0.0, normalized);
        normalized.convertTo(normalized, CV_8UC3, 255.0);
        return normalized;
    }

    cv::min(floatRgb, 255.0, normalized);
    cv::max(normalized, 0.0, normalized);
    normalized.convertTo(normalized, CV_8UC3);
    return normalized;
}
#endif
