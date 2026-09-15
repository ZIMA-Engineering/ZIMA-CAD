// Adapted from the ZIMA-CAD-Parts updater (ZIMA-Engineering).
#include "updatecore.h"
#include "version.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <openssl/evp.h>

namespace CadUpdate {
QString platform()
{
#ifdef Q_OS_WIN
    return "windows-x64";
#else
    return "linux-x86_64";
#endif
}
QString platformDirectory()
{
#ifdef Q_OS_WIN
    return "windows";
#else
    return "linux";
#endif
}
bool validVersion(const QString &version)
{
    return QRegularExpression("^[0-9]{10}$").match(version).hasMatch()
        && version.right(2) != "00" && QDate::fromString(version.left(8), "yyyyMMdd").isValid();
}
bool safeRelativePath(const QString &path)
{
    if (path.isEmpty() || path.size() > 1000 || path.contains('\\') || path.startsWith('/')) return false;
    for (const auto &part : path.split('/')) {
        if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') || part.endsWith(' ')) return false;
        if (QRegularExpression("[\\x00-\\x1f:<>\\\"|?*]").match(part).hasMatch()) return false;
        if (QRegularExpression("^(con|prn|aux|nul|com[1-9¹²³]|lpt[1-9¹²³])(?:\\..*)?$", QRegularExpression::CaseInsensitiveOption).match(part).hasMatch()) return false;
    }
    return true;
}
QByteArray canonical(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}
QString child(const QString &root, const QString &relative)
{
    if (!safeRelativePath(relative)) throw QString("Unsafe installation path");
    QString path = QDir(root).absolutePath();
    for (const auto &part : relative.split('/')) {
        path += '/' + part;
        const QFileInfo info(path);
        if (info.isSymLink() || info.isJunction()) throw QString("Linked installation path: ") + path;
    }
    return path;
}
QJsonObject readJson(const QString &path, qint64 limit)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit) throw QString("Cannot read update metadata: ") + path;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) throw QString("Invalid update metadata: ") + path;
    return doc.object();
}
void writeJson(const QString &path, const QJsonObject &object)
{
    QSaveFile file(path);
    const auto bytes = canonical(object);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw QString("Cannot save update metadata: ") + path;
}
QString hashFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw QString("Cannot read file: ") + path;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) throw QString("Cannot hash file: ") + path;
    return QString::fromLatin1(hash.result().toHex());
}
QString installationRoot(const QString &runtime)
{
    QDir dir(runtime.isEmpty() ? QCoreApplication::applicationDirPath() : runtime);
#ifndef Q_OS_WIN
    if (dir.dirName() == "bin") dir.cdUp();
#endif
    if (!validVersion(dir.dirName())) return {};
    dir.cdUp();
    if (dir.dirName() != platformDirectory()) return {};
    dir.cdUp();
    try { assertManaged(dir.absolutePath()); } catch (...) { return {}; }
    return dir.canonicalPath();
}
void assertManaged(const QString &root)
{
    if (root.isEmpty() || !QDir(root).isAbsolute() || QFileInfo(root).isSymLink() || QFileInfo(root).isJunction()) throw QString("Not a managed installation");
    const auto marker = readJson(root + "/installation.json");
    if (marker["product"] != "ZIMA-CAD" || marker["protocol"].toInt() != Protocol
        || marker["id"].toString().isEmpty() || QFileInfo::exists(root + "/.git"))
        throw QString("Not a managed distribution; use the packaged launcher");
    for (const auto &name : {QString(".updates"), QString("windows"), QString("linux"), QString("source")}) {
        const QFileInfo path(root + '/' + name);
        if (path.isSymLink() || path.isJunction()) throw QString("Linked installation directories are not supported");
    }
}
QJsonObject verifySignedObject(const QByteArray &payload, const QByteArray &signature)
{
    if (payload.size() > 32 * 1024 * 1024 || signature.size() > 4096) throw QString("Signed update metadata is too large");
    QJsonParseError error;
    const auto sig = QJsonDocument::fromJson(signature, &error);
    if (error.error != QJsonParseError::NoError || !sig.isObject() || canonical(sig.object()) != signature)
        throw QString("Invalid signature envelope");
    QFile keys(":/update/trusted-keys.json");
#ifdef ZIMA_UPDATE_TESTING
    if (qEnvironmentVariableIsSet("ZIMA_UPDATE_TEST_KEYS")) keys.setFileName(qEnvironmentVariable("ZIMA_UPDATE_TEST_KEYS"));
#endif
    if (!keys.open(QIODevice::ReadOnly)) throw QString("No trusted update key");
    const auto trusted = QJsonDocument::fromJson(keys.readAll()).object();
    const auto hexKey = trusted[sig["keyId"].toString()].toString().toLatin1();
    const auto hexSignature = sig["signature"].toString().toLatin1();
    static const QRegularExpression keyPattern("^[0-9a-f]{64}$"), sigPattern("^[0-9a-f]{128}$");
    if (!keyPattern.match(QString::fromLatin1(hexKey)).hasMatch() || !sigPattern.match(QString::fromLatin1(hexSignature)).hasMatch())
        throw QString("Update signature uses an unknown key or is malformed");
    const auto publicKey = QByteArray::fromHex(hexKey), signedBytes = QByteArray::fromHex(hexSignature);
    EVP_PKEY *key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char *>(publicKey.constData()), size_t(publicKey.size()));
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    const bool verified = key && ctx && EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1
        && EVP_DigestVerify(ctx, reinterpret_cast<const unsigned char *>(signedBytes.constData()), size_t(signedBytes.size()),
            reinterpret_cast<const unsigned char *>(payload.constData()), size_t(payload.size())) == 1;
    EVP_MD_CTX_free(ctx); EVP_PKEY_free(key);
    if (!verified) throw QString("Invalid publisher signature");
    const auto document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() || canonical(document.object()) != payload)
        throw QString("Manifest must use canonical JSON without duplicate keys");
    return document.object();
}
QJsonObject verifyManifest(const QByteArray &payload, const QByteArray &signature)
{
    if (payload.size() > 256 * 1024) throw QString("Update manifest is too large");
    const auto object = verifySignedObject(payload, signature);
    static const QRegularExpression keyPattern("^[0-9a-f]{64}$");
    const auto version = object["version"].toString();
    const auto archive = object["archive"].toObject();
    if (object["schemaVersion"].toInt() != 1 || object["product"] != "ZIMA-CAD"
        || !validVersion(version) || object["tag"] != "ZIMA-CAD-" + version
        || !QRegularExpression("^[0-9a-f]{40}$").match(object["commit"].toString()).hasMatch()
        || object["minimumUpdaterVersion"].toInt() < 1 || object["launcherProtocol"].toInt() < 1
        || archive["name"] != "ZIMA-CAD-" + version + ".zip"
        || !keyPattern.match(archive["sha256"].toString()).hasMatch()
        || !keyPattern.match(object["checksumsSha256"].toString()).hasMatch()
        || object["source"].toObject()["path"] != "source/" + version
        || !keyPattern.match(object["source"].toObject()["treeSha256"].toString()).hasMatch())
        throw QString("Invalid update manifest contract");
    for (const auto &keyName : {"size", "unpackedSize", "fileCount"}) {
        const double number = archive[keyName].toDouble(-1);
        const qint64 limit = QString(keyName) == "fileCount" ? 50000
            : QString(keyName) == "size" ? qint64(2) * 1024 * 1024 * 1024 - 1 : qint64(8) * 1024 * 1024 * 1024;
        if (number <= 0 || number > limit || double(qint64(number)) != number) throw QString("Unsupported archive limits");
    }
    if (!object["platforms"].isObject() || object["platforms"].toObject().isEmpty()) throw QString("Missing update platforms");
    const auto platforms = object["platforms"].toObject();
    for (auto it = platforms.begin(); it != platforms.end(); ++it) {
        const bool windows = it.key() == "windows-x64";
        if ((!windows && it.key() != "linux-x86_64") || !it.value().isObject()) throw QString("Unknown update platform");
        const auto target = it.value().toObject();
        if (target["runtime"] != (windows ? "windows/" : "linux/") + version
            || target["entry"] != (windows ? "zima-cad-cpp.exe" : "bin/zima-cad-cpp"))
            throw QString("Invalid runtime paths");
    }
    if (object["channel"] != "stable") {
#ifdef ZIMA_UPDATE_TESTING
        if (object["channel"] != "development")
#endif
        throw QString("This is not a stable publisher release");
    }
    return object;
}
}
