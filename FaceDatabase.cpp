#include "FaceDatabase.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include <cstring>

namespace {

constexpr const char* kFaceDatabaseTraceTitle = "[MYPLAYER_FACE_DB]";

}  // namespace

FaceDatabase::FaceDatabase() = default;

FaceDatabase::~FaceDatabase()
{
    close();
}

bool FaceDatabase::open(const QString& databasePath)
{
    close();
    lastError_.clear();

    if (databasePath.trimmed().isEmpty())
    {
        setLastError(QStringLiteral("Database path is empty."));
        return false;
    }

    const QFileInfo fileInfo(databasePath);
    const QString parentDir = fileInfo.absolutePath();
    if (!parentDir.isEmpty() && !QDir().mkpath(parentDir))
    {
        setLastError(QStringLiteral("Failed to create database directory: %1").arg(parentDir));
        return false;
    }

    connectionName_ = QStringLiteral("MyPlayerFaceDb_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(databasePath);

    if (!database_.open())
    {
        setLastError(database_.lastError().text());
        close();
        return false;
    }

    databasePath_ = databasePath;
    return ensureSchema();
}

void FaceDatabase::close()
{
    if (!database_.isValid())
    {
        connectionName_.clear();
        databasePath_.clear();
        return;
    }

    const QString connectionName = connectionName_;
    if (database_.isOpen())
    {
        database_.close();
    }
    database_ = QSqlDatabase();

    if (!connectionName.isEmpty())
    {
        QSqlDatabase::removeDatabase(connectionName);
    }

    connectionName_.clear();
    databasePath_.clear();
}

bool FaceDatabase::isOpen() const
{
    return database_.isValid() && database_.isOpen();
}

bool FaceDatabase::ensureSchema()
{
    if (!isOpen())
    {
        setLastError(QStringLiteral("Database is not open."));
        return false;
    }

    return execStatement(QStringLiteral("PRAGMA foreign_keys = ON"))
        && execStatement(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS persons ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL UNIQUE, "
            "note TEXT, "
            "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ")"))
        && execStatement(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS face_samples ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "person_id INTEGER NOT NULL, "
            "image_path TEXT, "
            "image_bytes BLOB, "
            "embedding BLOB NOT NULL, "
            "embedding_dim INTEGER NOT NULL, "
            "quality_score REAL NOT NULL DEFAULT 0.0, "
            "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, "
            "FOREIGN KEY(person_id) REFERENCES persons(id) ON DELETE CASCADE"
            ")"))
        && execStatement(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_face_samples_person_id ON face_samples(person_id)"))
        && execStatement(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_persons_name ON persons(name)"));
}

int FaceDatabase::upsertPerson(const QString& personName, const QString& note)
{
    if (!isOpen())
    {
        setLastError(QStringLiteral("Database is not open."));
        return -1;
    }

    const QString normalizedName = personName.trimmed();
    if (normalizedName.isEmpty())
    {
        setLastError(QStringLiteral("Person name is empty."));
        return -1;
    }

    QSqlQuery selectQuery(database_);
    selectQuery.prepare(QStringLiteral("SELECT id FROM persons WHERE name = :name"));
    selectQuery.bindValue(QStringLiteral(":name"), normalizedName);
    if (!selectQuery.exec())
    {
        setLastError(selectQuery.lastError().text());
        return -1;
    }

    if (selectQuery.next())
    {
        const int personId = selectQuery.value(0).toInt();
        QSqlQuery updateQuery(database_);
        updateQuery.prepare(QStringLiteral("UPDATE persons SET note = :note WHERE id = :id"));
        updateQuery.bindValue(QStringLiteral(":note"), note);
        updateQuery.bindValue(QStringLiteral(":id"), personId);
        if (!updateQuery.exec())
        {
            setLastError(updateQuery.lastError().text());
            return -1;
        }
        return personId;
    }

    QSqlQuery insertQuery(database_);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO persons(name, note) VALUES(:name, :note)"));
    insertQuery.bindValue(QStringLiteral(":name"), normalizedName);
    insertQuery.bindValue(QStringLiteral(":note"), note);
    if (!insertQuery.exec())
    {
        setLastError(insertQuery.lastError().text());
        return -1;
    }

    return insertQuery.lastInsertId().toInt();
}

bool FaceDatabase::hasFaceSample(const QString& imagePath)
{
    if (!isOpen())
    {
        setLastError(QStringLiteral("Database is not open."));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM face_samples WHERE image_path = :image_path LIMIT 1"));
    query.bindValue(QStringLiteral(":image_path"), imagePath);

    if (!query.exec())
    {
        setLastError(query.lastError().text());
        return false;
    }

    lastError_.clear();
    return query.next();
}

bool FaceDatabase::addFaceSample(
    int personId,
    const QString& imagePath,
    const std::vector<float>& embedding,
    float qualityScore,
    const QByteArray& imageBytes)
{
    if (!isOpen())
    {
        setLastError(QStringLiteral("Database is not open."));
        return false;
    }

    if (personId <= 0)
    {
        setLastError(QStringLiteral("Person id must be greater than zero."));
        return false;
    }

    if (embedding.empty())
    {
        setLastError(QStringLiteral("Embedding is empty."));
        return false;
    }

    const QByteArray packedEmbedding = packEmbedding(embedding);
    const QByteArray sampleImageBytes = loadImageBytesIfNeeded(imagePath, imageBytes);

    QSqlQuery insertQuery(database_);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO face_samples(person_id, image_path, image_bytes, embedding, embedding_dim, quality_score) "
        "VALUES(:person_id, :image_path, :image_bytes, :embedding, :embedding_dim, :quality_score)"));
    insertQuery.bindValue(QStringLiteral(":person_id"), personId);
    insertQuery.bindValue(QStringLiteral(":image_path"), imagePath);
    insertQuery.bindValue(QStringLiteral(":image_bytes"), sampleImageBytes);
    insertQuery.bindValue(QStringLiteral(":embedding"), packedEmbedding);
    insertQuery.bindValue(QStringLiteral(":embedding_dim"), static_cast<int>(embedding.size()));
    insertQuery.bindValue(QStringLiteral(":quality_score"), qualityScore);

    if (!insertQuery.exec())
    {
        setLastError(insertQuery.lastError().text());
        return false;
    }

    lastError_.clear();
    qDebug() << kFaceDatabaseTraceTitle << "[INSERT] stored face sample for personId =" << personId;
    return true;
}

std::vector<FaceIdentityRecord> FaceDatabase::loadAllFaceIdentities()
{
    std::vector<FaceIdentityRecord> identities;

    if (!isOpen())
    {
        setLastError(QStringLiteral("Database is not open."));
        return identities;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT fs.id, p.id, p.name, fs.image_path, fs.embedding, fs.embedding_dim, fs.quality_score "
        "FROM face_samples fs "
        "INNER JOIN persons p ON p.id = fs.person_id "
        "ORDER BY p.name ASC, fs.id ASC"));

    if (!query.exec())
    {
        setLastError(query.lastError().text());
        return identities;
    }

    while (query.next())
    {
        FaceIdentityRecord identity;
        identity.sampleId = query.value(0).toInt();
        identity.personId = query.value(1).toInt();
        identity.personName = query.value(2).toString().toStdString();
        identity.imagePath = query.value(3).toString().toStdString();
        identity.qualityScore = query.value(6).toFloat();

        if (!unpackEmbedding(query.value(4).toByteArray(), query.value(5).toInt(), identity.embedding))
        {
            qDebug() << kFaceDatabaseTraceTitle
                     << "[LOAD] skip invalid embedding for sampleId =" << identity.sampleId;
            continue;
        }

        identities.push_back(std::move(identity));
    }

    lastError_.clear();
    qDebug() << kFaceDatabaseTraceTitle << "[LOAD] loaded face identities =" << identities.size();
    return identities;
}

QString FaceDatabase::lastError() const
{
    return lastError_;
}

void FaceDatabase::setLastError(const QString& message)
{
    lastError_ = message;
    if (!message.isEmpty())
    {
        qDebug() << kFaceDatabaseTraceTitle << "[ERROR]" << message;
    }
}

bool FaceDatabase::execStatement(const QString& sql)
{
    QSqlQuery query(database_);
    if (!query.exec(sql))
    {
        setLastError(query.lastError().text());
        return false;
    }
    return true;
}

QByteArray FaceDatabase::packEmbedding(const std::vector<float>& embedding) const
{
    return QByteArray(
        reinterpret_cast<const char*>(embedding.data()),
        static_cast<int>(embedding.size() * sizeof(float)));
}

bool FaceDatabase::unpackEmbedding(const QByteArray& bytes, int embeddingDim, std::vector<float>& embedding) const
{
    embedding.clear();

    if (embeddingDim <= 0)
    {
        return false;
    }

    const int expectedBytes = embeddingDim * static_cast<int>(sizeof(float));
    if (bytes.size() != expectedBytes)
    {
        return false;
    }

    embedding.resize(static_cast<size_t>(embeddingDim));
    std::memcpy(embedding.data(), bytes.constData(), static_cast<size_t>(expectedBytes));
    return true;
}

QByteArray FaceDatabase::loadImageBytesIfNeeded(const QString& imagePath, const QByteArray& imageBytes) const
{
    if (!imageBytes.isEmpty())
    {
        return imageBytes;
    }

    if (imagePath.trimmed().isEmpty())
    {
        return QByteArray();
    }

    QFile imageFile(imagePath);
    if (!imageFile.open(QIODevice::ReadOnly))
    {
        qDebug() << kFaceDatabaseTraceTitle << "[IMAGE] unable to read image file:" << imagePath;
        return QByteArray();
    }

    return imageFile.readAll();
}