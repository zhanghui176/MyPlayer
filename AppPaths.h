#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QString>

enum class ModelKind
{
    None,
    ImageToImage,
    FaceDetection,
};
class AppPaths
{
public:
    static QString getRuntimeDir()
    {
        return QCoreApplication::applicationDirPath();
    }

    static QString getModelPathByKind(ModelKind kind)
    {
        switch (kind)
        {
        case ModelKind::FaceDetection:
            return getFaceDetectionModelPath();
        case ModelKind::ImageToImage:
            // TODO: add specific model path for image-to-image models if needed
        default:
            return QString();
        }
    }

    static QString getModelsDir()
    {
        return QDir(getRuntimeDir()).filePath(QStringLiteral("models"));
    }

    static QString getFaceSampleDir()
    {
        return QDir(getRuntimeDir()).filePath(QStringLiteral("faceSample"));
    }

    static QString getFaceDatabasePath()
    {
        return QDir(getRuntimeDir()).filePath(QStringLiteral("faceDatabase.sqlite"));
    }

    static QString getFaceDetectionModelPath()
    {
        return QDir(getModelsDir()).filePath(QStringLiteral("yunet_n_640_640.onnx"));
    }

    static QString getFaceRecognitionModelPath()
    {
        return QDir(getModelsDir()).filePath(QStringLiteral("face_recognition_sface_2021dec.onnx"));
    }
};
