#pragma once

#include <string>
#include <vector>

#include <opencv2/core.hpp>

#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
#include <opencv2/objdetect.hpp>
#endif

struct FaceIdentityRecord
{
	int sampleId = -1;
	int personId = -1;
	std::string personName;
	std::string imagePath;
	std::vector<float> embedding;
	float qualityScore = 0.0f;
};

struct FaceMatchResult
{
	bool matched = false;
	int sampleId = -1;
	int personId = -1;
	std::string personName;
	std::string imagePath;
	float score = 0.0f;
};

class FaceRecognitionRunner
{
public:
	FaceRecognitionRunner();

	bool loadModel(const std::string& modelPath);
	bool extractEmbedding(const cv::Mat& rgbFrame, const cv::Mat& detectedFaceRow, std::vector<float>& embedding);
	FaceMatchResult match(
		const std::vector<float>& queryEmbedding,
		const std::vector<FaceIdentityRecord>& candidates,
		float minSimilarity = 0.363f) const;
	bool isReady() const;
	void reset();

	static float defaultCosineThreshold();
	static float cosineSimilarity(const std::vector<float>& lhs, const std::vector<float>& rhs);

private:
	bool ready_ = false;

#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
	cv::Ptr<cv::FaceRecognizerSF> recognizer_;
#endif
};
