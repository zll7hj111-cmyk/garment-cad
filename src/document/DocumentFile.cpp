#include "DocumentFile.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QFileInfo>
#include <QSaveFile>
#include <QFile>

#include <miniz.h>

#include "document/DocumentSerializer.h"
#include "document/FormatMigration.h"
#include "parametric/ParamDocument.h"

namespace cad::doc {

namespace {

constexpr const char* kAppVersion = "0.1.0";

/// Build the manifest.json content.
QByteArray buildManifest()
{
    QJsonObject obj;
    obj["format"] = "gcad";
    obj["version"] = cad::doc::kFormatVersion;
    obj["appVersion"] = kAppVersion;
    obj["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    obj["modifiedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return QJsonDocument(obj).toJson(QJsonDocument::Indented);
}

/// Add a file entry to a ZIP archive. Returns false on failure.
bool zipAddFile(mz_zip_archive& zip, const char* name, const QByteArray& data)
{
    return mz_zip_writer_add_mem(&zip, name, data.constData(),
                                 static_cast<size_t>(data.size()),
                                 MZ_BEST_COMPRESSION) != 0;
}

/// Read a file entry from a ZIP archive. Returns empty on failure.
QByteArray zipReadFile(mz_zip_archive& zip, const char* name)
{
    int idx = mz_zip_reader_locate_file(&zip, name, nullptr, 0);
    if (idx < 0) return {};

    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(idx), &stat))
        return {};

    QByteArray buf(static_cast<int>(stat.m_uncomp_size), '\0');
    if (!mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(idx),
                                      buf.data(),
                                      static_cast<size_t>(buf.size()), 0))
        return {};
    return buf;
}

} // anonymous namespace

bool DocumentFile::save(const QString& path, const cad::param::ParamDocument& doc,
                        QString* error)
{
    // Serialize document to JSON
    const QJsonObject root = cad::param::DocumentSerializer::serialize(doc);
    const QJsonObject docObj = root["document"].toObject();
    const QJsonObject varObj = root["variables"].toObject();

    const QByteArray manifestData = buildManifest();
    const QByteArray documentData = QJsonDocument(docObj).toJson(QJsonDocument::Indented);
    const QByteArray variablesData = QJsonDocument(varObj).toJson(QJsonDocument::Indented);

    // Write to a temp file first, then rename (crash-safe)
    const QString tmpPath = path + ".tmp";

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_writer_init_file(&zip, tmpPath.toUtf8().constData(), 0)) {
        if (error) *error = QStringLiteral("无法创建临时 ZIP 文件: %1").arg(tmpPath);
        return false;
    }

    bool ok = true;
    ok = ok && zipAddFile(zip, "manifest.json", manifestData);
    ok = ok && zipAddFile(zip, "document.json", documentData);
    ok = ok && zipAddFile(zip, "variables.json", variablesData);

    if (!ok || !mz_zip_writer_finalize_archive(&zip)) {
        mz_zip_writer_end(&zip);
        QFile::remove(tmpPath);
        if (error) *error = QStringLiteral("写入 ZIP 文件失败");
        return false;
    }
    mz_zip_writer_end(&zip);

    // Crash-safe replace: rename the old file ASIDE first, then rename the
    // temp in. A crash between the two renames leaves path.bak (recoverable) —
    // the previous remove-then-rename lost BOTH the original and the temp.
    const QString bakPath = path + QStringLiteral(".bak");
    QFile::remove(bakPath);
    if (QFile::exists(path) && !QFile::rename(path, bakPath)) {
        QFile::remove(tmpPath);
        if (error) *error = QStringLiteral("无法保存文件: %1").arg(path);
        return false;
    }
    if (!QFile::rename(tmpPath, path)) {
        // Roll the backup back so the user keeps their previous file.
        QFile::rename(bakPath, path);
        QFile::remove(tmpPath);
        if (error) *error = QStringLiteral("无法保存文件: %1").arg(path);
        return false;
    }
    QFile::remove(bakPath);

    return true;
}

bool DocumentFile::load(const QString& path, cad::param::ParamDocument& doc,
                        QString* error, QStringList* warnings)
{
    if (!QFile::exists(path)) {
        if (error) *error = QStringLiteral("文件不存在: %1").arg(path);
        return false;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, path.toUtf8().constData(), 0)) {
        if (error) *error = QStringLiteral("无法打开 ZIP 文件: %1").arg(path);
        return false;
    }

    // Read manifest (validate format)
    const QByteArray manifestData = zipReadFile(zip, "manifest.json");
    if (manifestData.isEmpty()) {
        mz_zip_reader_end(&zip);
        if (error) *error = QStringLiteral("无效的 .gcad 文件：缺少 manifest.json");
        return false;
    }
    const QJsonObject manifest = QJsonDocument::fromJson(manifestData).object();
    if (manifest["format"].toString() != "gcad") {
        mz_zip_reader_end(&zip);
        if (error) *error = QStringLiteral("无效的文件格式（非 gcad）");
        return false;
    }
    // P2-1: the declared version is walked FORWARD to kFormatVersion by the
    // registered migration chain before the serializer ever sees the bytes.
    // A missing "version" field reads as 0 = "predates versioning" and takes
    // the v0 -> v1 step (layer indices -> stable ids).
    const int version = manifest["version"].toInt();

    // Read document.json
    const QByteArray documentData = zipReadFile(zip, "document.json");
    if (documentData.isEmpty()) {
        mz_zip_reader_end(&zip);
        if (error) *error = QStringLiteral("无效的 .gcad 文件：缺少 document.json");
        return false;
    }

    // Read variables.json
    const QByteArray variablesData = zipReadFile(zip, "variables.json");

    mz_zip_reader_end(&zip);

    // Parse and deserialize
    QJsonParseError parseErr;
    const QJsonObject docObj = QJsonDocument::fromJson(documentData, &parseErr).object();
    if (parseErr.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("document.json 解析失败: %1").arg(parseErr.errorString());
        return false;
    }

    QJsonObject varObj;
    if (!variablesData.isEmpty()) {
        varObj = QJsonDocument::fromJson(variablesData, &parseErr).object();
        if (parseErr.error != QJsonParseError::NoError) {
            if (error) *error = QStringLiteral("variables.json 解析失败: %1").arg(parseErr.errorString());
            return false;
        }
    }

    QJsonObject root{{"document", docObj}, {"variables", varObj}};
    QString migrationError;
    if (!cad::doc::FormatMigration::migrate(version, root, warnings, &migrationError)) {
        if (error) *error = migrationError;
        return false;
    }
    cad::param::DocumentSerializer::deserialize(doc, root, warnings);

    return true;
}

} // namespace cad::doc
