#pragma once

#include <vector>

#include <QByteArray>
#include <QSqlDatabase>
#include <QString>

#include "FaceRecognitionRunner.h"

class FaceDatabase
{
public:
    FaceDatabase();
    ~FaceDatabase();

    FaceDatabase(const FaceDatabase&) = delete;
    FaceDatabase& operator=(const FaceDatabase&) = delete;

    bool open(const QString& databasePath);
    void close();
    bool isOpen() const;
    bool ensureSchema();
    int upsertPerson(const QString& personName, const QString& note = QString());
    bool hasFaceSample(const QString& imagePath);
    bool addFaceSample(
        int personId,
        const QString& imagePath,
        const std::vector<float>& embedding,
        float qualityScore = 0.0f,
        const QByteArray& imageBytes = QByteArray());
    std::vector<FaceIdentityRecord> loadAllFaceIdentities();
    QString lastError() const;

private:
    void setLastError(const QString& message);
    bool execStatement(const QString& sql);
    QByteArray packEmbedding(const std::vector<float>& embedding) const;
    bool unpackEmbedding(const QByteArray& bytes, int embeddingDim, std::vector<float>& embedding) const;
    QByteArray loadImageBytesIfNeeded(const QString& imagePath, const QByteArray& imageBytes) const;

    QString connectionName_;
    QString databasePath_;
    QSqlDatabase database_;
    QString lastError_;
};