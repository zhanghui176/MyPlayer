#include "FaceSampleImporter.h"

#include "AppPaths.h"

#include "FaceDatabase.h"
#include "FaceDetectionRunner.h"
#include "FaceRecognitionRunner.h"

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStringList>

#include <limits>
#include <vector>

#include <opencv2/core.hpp>

namespace {

constexpr const char* kFaceImportTraceTitle = "[MYPLAYER_FACE_IMPORT]";

QString normalizeStoredImagePath(const QDir& sampleRoot, const QString& absoluteImagePath)
{
    QString relativePath = sampleRoot.relativeFilePath(absoluteImagePath);
    relativePath = QDir::cleanPath(relativePath);
    return relativePath.replace('\\', '/');
}

QString resolvePersonName(const QDir& sampleRoot, const QFileInfo& imageFileInfo)
{
    QString relativeParentPath = sampleRoot.relativeFilePath(imageFileInfo.absolutePath());
    relativeParentPath = QDir::cleanPath(relativeParentPath);
    if (!relativeParentPath.isEmpty() && relativeParentPath != QStringLiteral("."))
    {
        return relativeParentPath;
    }
    return imageFileInfo.completeBaseName();
}

bool loadRgbImage(const QString& absoluteImagePath, cv::Mat& rgbImage, QByteArray& imageBytes)
{
    rgbImage.release();
    imageBytes.clear();

    QFile imageFile(absoluteImagePath);
    if (!imageFile.open(QIODevice::ReadOnly))
    {
        return false;
    }

    imageBytes = imageFile.readAll();
    const QImage image = QImage::fromData(imageBytes);
    if (image.isNull())
    {
        return false;
    }

    const QImage rgb888Image = image.convertToFormat(QImage::Format_RGB888);
    const cv::Mat wrappedImage(
        rgb888Image.height(),
        rgb888Image.width(),
        CV_8UC3,
        const_cast<uchar*>(rgb888Image.constBits()),
        rgb888Image.bytesPerLine());

    rgbImage = wrappedImage.clone();
    return !rgbImage.empty();
}

int chooseBestFaceRow(const cv::Mat& faces)
{
    if (faces.rows <= 1)
    {
        return 0;
    }

    int bestRow = 0;
    float bestScore = std::numeric_limits<float>::lowest();
    for (int row = 0; row < faces.rows; ++row)
    {
        float score = 0.0f;
        if (faces.cols > 14)
        {
            score = faces.at<float>(row, 14);
        }
        else if (faces.cols > 3)
        {
            score = faces.at<float>(row, 2) * faces.at<float>(row, 3);
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestRow = row;
        }
    }

    return bestRow;
}

float readQualityScore(const cv::Mat& faces, int row)
{
    if (row < 0 || row >= faces.rows || faces.cols <= 14)
    {
        return 0.0f;
    }
    return faces.at<float>(row, 14);
}

}  // namespace

FaceSampleImporter::Result FaceSampleImporter::importSamples()
{
    Result result;
    const QString sampleDirectory = AppPaths::getFaceSampleDir();
    const QString databasePath = AppPaths::getFaceDatabasePath();
    const QString detectionModelPath = AppPaths::getFaceDetectionModelPath();
    const QString recognitionModelPath = AppPaths::getFaceRecognitionModelPath();

#if defined(MYPLAYER_ENABLE_OPENCV_FACE) && MYPLAYER_ENABLE_OPENCV_FACE
    const QFileInfo sampleDirectoryInfo(sampleDirectory);
    if (!sampleDirectoryInfo.exists() || !sampleDirectoryInfo.isDir())
    {
        qDebug() << kFaceImportTraceTitle << "[SKIP] sample directory not found:" << sampleDirectory;
        return result;
    }

    if (detectionModelPath.trimmed().isEmpty() || recognitionModelPath.trimmed().isEmpty())
    {
        qDebug() << kFaceImportTraceTitle << "[SKIP] missing face model path";
        return result;
    }

    FaceDatabase database;
    if (!database.open(databasePath))
    {
        qDebug() << kFaceImportTraceTitle << "[ERROR] unable to open database:" << database.lastError();
        return result;
    }

    FaceDetectionRunner detectionRunner;
    detectionRunner.setInputResizeMode(FaceDetectionRunner::InputResizeMode::FixedSize);
    detectionRunner.setFixedInputSize(640, 640);
    if (!detectionRunner.loadModel(detectionModelPath.toStdString()))
    {
        qDebug() << kFaceImportTraceTitle << "[ERROR] unable to load detection model:" << detectionModelPath;
        return result;
    }

    FaceRecognitionRunner recognitionRunner;
    if (!recognitionRunner.loadModel(recognitionModelPath.toStdString()))
    {
        qDebug() << kFaceImportTraceTitle << "[ERROR] unable to load recognition model:" << recognitionModelPath;
        return result;
    }

    const QDir sampleRoot(sampleDirectoryInfo.absoluteFilePath());
    const QStringList imageFilters = {
        QStringLiteral("*.jpg"),
        QStringLiteral("*.jpeg"),
        QStringLiteral("*.png"),
        QStringLiteral("*.bmp"),
        QStringLiteral("*.webp")
    };
    QDirIterator iterator(
        sampleRoot.absolutePath(),
        imageFilters,
        QDir::Files | QDir::Readable,
        QDirIterator::Subdirectories);

    while (iterator.hasNext())
    {
        const QString absoluteImagePath = iterator.next();
        const QFileInfo imageFileInfo(absoluteImagePath);
        const QString storedImagePath = normalizeStoredImagePath(sampleRoot, absoluteImagePath);

        if (database.hasFaceSample(storedImagePath))
        {
            ++result.skippedCount;
            qDebug() << kFaceImportTraceTitle << "[SKIP] sample already imported:" << storedImagePath;
            continue;
        }
        if (!database.lastError().isEmpty())
        {
            ++result.failedCount;
            qDebug() << kFaceImportTraceTitle << "[ERROR] failed to query sample existence:"
                     << storedImagePath << database.lastError();
            continue;
        }

        QByteArray imageBytes;
        cv::Mat rgbImage;
        if (!loadRgbImage(absoluteImagePath, rgbImage, imageBytes))
        {
            ++result.failedCount;
            qDebug() << kFaceImportTraceTitle << "[ERROR] unable to decode image:" << absoluteImagePath;
            continue;
        }

        cv::Mat faces;
        if (!detectionRunner.detect(rgbImage, faces) || faces.empty())
        {
            ++result.failedCount;
            qDebug() << kFaceImportTraceTitle << "[ERROR] no face detected in sample:" << storedImagePath;
            continue;
        }

        const int bestFaceRow = chooseBestFaceRow(faces);
        std::vector<float> embedding;
        if (!recognitionRunner.extractEmbedding(rgbImage, faces.row(bestFaceRow).clone(), embedding))
        {
            ++result.failedCount;
            qDebug() << kFaceImportTraceTitle << "[ERROR] unable to extract embedding:" << storedImagePath;
            continue;
        }

        const QString personName = resolvePersonName(sampleRoot, imageFileInfo);
        const int personId = database.upsertPerson(personName);
        if (personId <= 0)
        {
            ++result.failedCount;
            qDebug() << kFaceImportTraceTitle << "[ERROR] unable to upsert person:"
                     << personName << database.lastError();
            continue;
        }

        if (!database.addFaceSample(
                personId,
                storedImagePath,
                embedding,
                readQualityScore(faces, bestFaceRow),
                imageBytes))
        {
            ++result.failedCount;
            qDebug() << kFaceImportTraceTitle << "[ERROR] unable to store sample:"
                     << storedImagePath << database.lastError();
            continue;
        }

        ++result.importedCount;
        qDebug() << kFaceImportTraceTitle << "[IMPORT] stored sample:" << storedImagePath
                 << "person =" << personName;
    }

    qDebug() << kFaceImportTraceTitle << "[SUMMARY] imported =" << result.importedCount
             << "skipped =" << result.skippedCount
             << "failed =" << result.failedCount
             << "database =" << databasePath;
#else
    Q_UNUSED(sampleDirectory);
    Q_UNUSED(databasePath);
    Q_UNUSED(detectionModelPath);
    Q_UNUSED(recognitionModelPath);
    qDebug() << kFaceImportTraceTitle
             << "[SKIP] face import requires OpenCV face support at build time.";
#endif

    return result;
}
