// Adapted from the ZIMA-CAD-Parts updater (ZIMA-Engineering).
#include "updatecore.h"
#include "version.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <algorithm>

namespace CadUpdate {
namespace {
bool allowed(const QUrl &url)
{
#ifdef ZIMA_UPDATE_TESTING
    if (url.scheme() == "http" && url.host() == "127.0.0.1") return true;
#endif
    return url.scheme() == "https" && url.userInfo().isEmpty() && (url.port() == -1 || url.port() == 443)
        && QStringList{"api.github.com", "github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com"}.contains(url.host());
}
QByteArray get(QUrl url, qint64 limit, QFile *output = nullptr, const Progress &progress = {}, bool cache = false)
{
    QNetworkAccessManager manager;
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/updates";
#ifdef ZIMA_UPDATE_TESTING
    if (!qEnvironmentVariable("ZIMA_UPDATE_TEST_STATE").isEmpty()) cacheDir = qEnvironmentVariable("ZIMA_UPDATE_TEST_STATE") + "/cache";
#endif
    QDir().mkpath(cacheDir);
    const auto cachePath = cacheDir + '/' + QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha256).toHex()) + ".json";
    QJsonObject cached;
    if (cache && QFileInfo::exists(cachePath)) { try { cached = readJson(cachePath, 12 * 1024 * 1024); } catch (...) {} }
    for (int redirects = 0; redirects <= 5; ++redirects) {
        if (!allowed(url)) throw QString("Unapproved update download URL");
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "ZIMA-CAD/" VERSION);
        request.setRawHeader("Accept", output ? "application/octet-stream" : "application/vnd.github+json");
        request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
        if (cache && !cached["etag"].toString().isEmpty()) request.setRawHeader("If-None-Match", cached["etag"].toString().toUtf8());
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(15000);
        QNetworkReply *reply = manager.get(request);
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        QByteArray bytes;
        QString failure;
        qint64 received = 0;
        QObject::connect(&deadline, &QTimer::timeout, reply, [&] { failure = "Update request timed out"; reply->abort(); });
        const auto read = [&] {
            const auto chunk = reply->readAll();
            received += chunk.size();
            if (received > limit) { failure = "Update response exceeds its size limit"; reply->abort(); return; }
            const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (output && http == 200) {
                if (output->write(chunk) != chunk.size()) { failure = "Cannot write downloaded update"; reply->abort(); }
                if (progress) progress({{"phase", "downloading"}, {"received", received}, {"total", limit}});
            } else bytes += chunk;
        };
        QObject::connect(reply, &QNetworkReply::readyRead, &loop, read);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        deadline.start(output ? 60 * 60 * 1000 : 30000);
        loop.exec(); read(); deadline.stop();
        const auto http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        const auto error = reply->error();
        const auto etag = reply->rawHeader("ETag");
        const auto retry = reply->rawHeader("Retry-After");
        const auto reset = reply->rawHeader("X-RateLimit-Reset");
        const auto description = reply->errorString();
        delete reply;
        if (!failure.isEmpty()) throw failure;
        if (http == 304 && cache && cached["body"].isString()) {
            const auto body = QByteArray::fromBase64(cached["body"].toString().toLatin1());
            if (body.size() > limit) throw QString("Cached update response is too large");
            return body;
        }
        if (http >= 300 && http < 400 && !redirect.isEmpty()) { url = url.resolved(redirect); continue; }
        if (http == 403 || http == 429) {
            qint64 next = QDateTime::currentSecsSinceEpoch() + 60;
            if (!retry.isEmpty()) next = qMax(next, QDateTime::currentSecsSinceEpoch() + retry.toLongLong());
            if (!reset.isEmpty()) next = qMax(next, reset.toLongLong());
            QSettings settings; settings.setValue("Updates/RetryAfter", next);
            throw QString("GitHub rate limit; retry after ") + QDateTime::fromSecsSinceEpoch(next).toString(Qt::ISODate);
        }
        if (http != 200 || error != QNetworkReply::NoError) throw QString("Update service request failed (%1): %2").arg(http).arg(description);
        if (cache) {
            try { writeJson(cachePath, {{"etag", QString::fromUtf8(etag)}, {"body", QString::fromLatin1(bytes.toBase64())}}); }
            catch (const QString &) {} // A read-only cache must not break a check.
        }
        return bytes;
    }
    throw QString("Too many update download redirects");
}
QUrl apiBase()
{
#ifdef ZIMA_UPDATE_TESTING
    const auto test = qEnvironmentVariable("ZIMA_UPDATE_TEST_API");
    if (!test.isEmpty()) return QUrl(test);
#endif
    return QUrl("https://api.github.com/repos/ZIMA-Engineering/ZIMA-CAD/releases");
}
QString asset(const QJsonObject &release, const QString &name)
{
    QString result;
    for (const auto &value : release["assets"].toArray()) {
        const auto item = value.toObject();
        if (item["name"] == name && item["state"] == "uploaded") {
            if (!result.isEmpty()) throw QString("Duplicate release asset");
            result = item["browser_download_url"].toString();
        }
    }
    if (result.isEmpty()) throw QString("Release asset missing: ") + name;
    return result;
}
}
QJsonObject check(const QString &root, const Progress &progress)
{
    QSettings settings;
    if (settings.value("Updates/RetryAfter", 0).toLongLong() > QDateTime::currentSecsSinceEpoch())
        throw QString("Update check deferred by GitHub rate limit");
    QString installed = QStringLiteral(VERSION);
    if (!root.isEmpty()) {
        assertManaged(root);
        QSettings launcher(root + "/launcher.ini", QSettings::IniFormat);
        if (launcher.value("launcher/" + platformDirectory() + "_custom", false).toBool()) throw QString("Custom builds are not updated automatically");
        installed = launcher.value("launcher/" + platformDirectory()).toString();
        if (!validVersion(installed)) throw QString("No official build selected");
    }
    QList<QJsonObject> releases;
    for (int page = 1; ; ++page) {
        if (page > 20) throw QString("Release scan is incomplete: page limit reached");
        QUrl url = apiBase();
        url.setQuery(QString("per_page=100&page=%1").arg(page));
        const auto bytes = get(url, 8 * 1024 * 1024, nullptr, {}, true);
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(bytes, &parse);
        if (!document.isArray() || parse.error != QJsonParseError::NoError) throw QString("Invalid GitHub release response");
        const auto entries = document.array();
        for (const auto &entry : entries) {
            const auto release = entry.toObject();
            const auto tag = release["tag_name"].toString();
            if (release["draft"].toBool(true) || release["prerelease"].toBool(true) || !tag.startsWith("ZIMA-CAD-")) continue;
            const auto version = tag.mid(9);
            if (validVersion(version) && version > installed) releases.append(release);
        }
        if (entries.size() < 100) break;
    }
    std::sort(releases.begin(), releases.end(), [](const auto &a, const auto &b) { return a["tag_name"].toString() > b["tag_name"].toString(); });
    QString invalid;
    for (const auto &release : releases) {
        try {
            const auto payload = get(QUrl(asset(release, "update-manifest.json")), 256 * 1024);
            const auto signature = get(QUrl(asset(release, "update-manifest.sig")), 4096);
            const auto manifest = verifyManifest(payload, signature);
            if (manifest["tag"] != release["tag_name"]) throw QString("Release tag does not match signed manifest");
            if (!manifest["platforms"].toObject().contains(platform())) continue;
            const auto target = manifest["platforms"].toObject()[platform()].toObject();
            if (QSysInfo::currentCpuArchitecture() != "x86_64")
                throw QString("This update requires x86_64");
            const auto high = settings.value("Updates/HighestVerified/" + platform()).toString();
            const auto version = manifest["version"].toString();
            if (version < high) throw QString("Update catalog is older than a previously verified release");
            settings.setValue("Updates/HighestVerified/" + platform(), version);
            const bool compatible = manifest["minimumUpdaterVersion"].toInt() <= Protocol && manifest["launcherProtocol"].toInt() <= Protocol;
            const bool trusted = !root.isEmpty() && trustedInstallation(root);
            QJsonObject offer{{"status", "available"}, {"installedVersion", installed}, {"availableVersion", version},
                {"manifest", manifest}, {"signature", QJsonDocument::fromJson(signature).object()},
                {"downloadUrl", asset(release, manifest["archive"].toObject()["name"].toString())},
                {"notes", release["body"].toString().left(16000)}, {"installable", compatible && trusted},
                {"reason", !trusted ? "Use a signed distribution package to install updates" : compatible ? "" : "A newer root launcher is required"}};
            settings.setValue("Updates/LastCheck", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
            if (progress) progress({{"phase", "checked"}});
            return offer;
        } catch (const QString &error) { invalid = error; }
    }
    if (!invalid.isEmpty()) throw QString("No verified compatible release: ") + invalid;
    if (settings.value("Updates/HighestVerified/" + platform()).toString() > installed)
        throw QString("A previously verified release is missing from the catalog");
    settings.setValue("Updates/LastCheck", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return {{"status", "current"}, {"installedVersion", installed}};
}
QJsonObject download(const QString &root, const QJsonObject &offer, const Progress &progress)
{
    assertManaged(root);
    const auto payload = canonical(offer["manifest"].toObject()), signature = canonical(offer["signature"].toObject());
    const auto manifest = verifyManifest(payload, signature);
    if (!manifest["platforms"].toObject().contains(platform()) || manifest["minimumUpdaterVersion"].toInt() > Protocol || manifest["launcherProtocol"].toInt() > Protocol)
        throw QString("Update is incompatible");
    const auto archive = manifest["archive"].toObject();
    const auto version = manifest["version"].toString();
    QStorageInfo disk(root);
    const qint64 required = archive["size"].toInteger() + 2 * archive["unpackedSize"].toInteger() + 64 * 1024 * 1024;
    if (disk.bytesAvailable() < required) throw QString("Not enough disk space for update and rollback");
    const auto directory = child(root, ".updates/downloads");
    if (!QDir().mkpath(directory)) throw QString("Cannot create update staging directory");
    QLockFile lock(child(root, ".updates/download.lock")); lock.setStaleLockTime(0);
    if (!lock.tryLock()) throw QString("Another download is running");
    const auto path = child(root, ".updates/downloads/" + version + ".zip.part");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) throw QString("Cannot create download file");
    try {
        get(QUrl(offer["downloadUrl"].toString()), archive["size"].toInteger(), &file, progress);
        file.close();
        const auto result = prepare(root, path, payload, signature, progress);
        QFile::remove(path);
        return result;
    } catch (...) { file.close(); QFile::remove(path); throw; }
}
}
