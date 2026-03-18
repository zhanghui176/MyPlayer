#include "FaceRecognitionRunner.h"

#include <QDebug>

#include <cmath>
#include <cstring>

#include <opencv2/imgproc.hpp>
#if defined(MYPLAYER_ENABLE_ONNX) && MYPLAYER_ENABLE_ONNX
#include <opencv2/dnn.hpp>
#endif

namespace {

constexpr const char* kFaceRecognitionTraceTitle = "[MYPLAYER_FACE_RECOGNITION]";
constexpr float kDefaultCosineThreshold = 0.363f;

bool matToFloatVector(const cv::Mat& featureMat, std::vector<float>& values)
{
    if (featureMat.empty())
    {
        return false;
    }

    cv::Mat flattened = featureMat.reshape(1, 1);
    cv::Mat floatFeature;
    flattened.convertTo(floatFeature, CV_32F);

    values.resize(static_cast<size_t>(floatFeature.total()));
    if (values.empty())
    {
        return false;
    }

    std::memcpy(values.data(), floatFeature.ptr<float>(), values.size() * sizeof(float));
    return true;
}

}  // namespace

FaceRecognitionRunner::FaceRecognitionRunner() = default;

bool FaceRecognitionRunner::loadModel(const std::string& modelPath)
{
    reset();

#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
    if (modelPath.empty())
    {
        return false;
    }

    try
    {
        recognizer_ = cv::FaceRecognizerSF::create(
            modelPath,
            "",
            cv::dnn::DNN_BACKEND_OPENCV,
            cv::dnn::DNN_TARGET_CPU);

        ready_ = !recognizer_.empty();
        if (ready_)
        {
            qDebug() << kFaceRecognitionTraceTitle << "[MODEL] FaceRecognizerSF loaded:" << modelPath.c_str();
        }
        else
        {
            qDebug() << kFaceRecognitionTraceTitle << "[MODEL] FaceRecognizerSF init failed:" << modelPath.c_str();
        }
        return ready_;
    }
    catch (const cv::Exception& ex)
    {
        qDebug() << kFaceRecognitionTraceTitle << "[MODEL] load exception:" << ex.what();
        ready_ = false;
        return false;
    }
#else
    Q_UNUSED(modelPath);
    qDebug() << kFaceRecognitionTraceTitle
             << "[MODEL] Face recognition support is disabled. Build with OpenCV objdetect to enable.";
    return false;
#endif
}

bool FaceRecognitionRunner::extractEmbedding(
    const cv::Mat& rgbFrame,
    const cv::Mat& detectedFaceRow,
    std::vector<float>& embedding)
{
    embedding.clear();

#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
    if (!ready_ || recognizer_.empty() || rgbFrame.empty() || detectedFaceRow.empty())
    {
        return false;
    }

    try
    {
        cv::Mat faceRow = detectedFaceRow;
        if (faceRow.rows != 1)
        {
            faceRow = faceRow.reshape(1, 1);
        }

        cv::Mat faceRowFloat;
        faceRow.convertTo(faceRowFloat, CV_32F);

        cv::Mat bgrFrame;
        cv::cvtColor(rgbFrame, bgrFrame, cv::COLOR_RGB2BGR);

        cv::Mat alignedFace;
        recognizer_->alignCrop(bgrFrame, faceRowFloat, alignedFace);
        if (alignedFace.empty())
        {
            return false;
        }

        cv::Mat feature;
        recognizer_->feature(alignedFace, feature);
        return matToFloatVector(feature, embedding);
    }
    catch (const cv::Exception& ex)
    {
        qDebug() << kFaceRecognitionTraceTitle << "[EMBED] extract exception:" << ex.what();
        return false;
    }
#else
    Q_UNUSED(rgbFrame);
    Q_UNUSED(detectedFaceRow);
    return false;
#endif
}

FaceMatchResult FaceRecognitionRunner::match(
    const std::vector<float>& queryEmbedding,
    const std::vector<FaceIdentityRecord>& candidates,
    float minSimilarity) const
{
    FaceMatchResult result;
    float bestScore = -1.0f;

    if (queryEmbedding.empty())
    {
        return result;
    }

    for (const FaceIdentityRecord& candidate : candidates)
    {
        const float score = cosineSimilarity(queryEmbedding, candidate.embedding);
        if (score < 0.0f)
        {
            continue;
        }

        if (score > bestScore)
        {
            bestScore = score;
            result.sampleId = candidate.sampleId;
            result.personId = candidate.personId;
            result.personName = candidate.personName;
            result.imagePath = candidate.imagePath;
            result.score = score;
        }
    }

    result.matched = bestScore >= minSimilarity;
    if (!result.matched)
    {
        result.sampleId = -1;
        result.personId = -1;
        result.personName.clear();
        result.imagePath.clear();
    }
    return result;
}

bool FaceRecognitionRunner::isReady() const
{
    return ready_;
}

void FaceRecognitionRunner::reset()
{
    ready_ = false;

#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
    recognizer_.release();
#endif
}

float FaceRecognitionRunner::defaultCosineThreshold()
{
    return kDefaultCosineThreshold;
}

float FaceRecognitionRunner::cosineSimilarity(const std::vector<float>& lhs, const std::vector<float>& rhs)
{
    if (lhs.empty() || rhs.empty() || lhs.size() != rhs.size())
    {
        return -1.0f;
    }

    double dot = 0.0;
    double lhsNorm = 0.0;
    double rhsNorm = 0.0;

    for (size_t index = 0; index < lhs.size(); ++index)
    {
        const double lhsValue = lhs[index];
        const double rhsValue = rhs[index];
        dot += lhsValue * rhsValue;
        lhsNorm += lhsValue * lhsValue;
        rhsNorm += rhsValue * rhsValue;
    }

    if (lhsNorm <= 0.0 || rhsNorm <= 0.0)
    {
        return -1.0f;
    }

    return static_cast<float>(dot / (std::sqrt(lhsNorm) * std::sqrt(rhsNorm)));
}