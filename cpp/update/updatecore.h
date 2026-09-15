// Adapted from the ZIMA-CAD-Parts updater (ZIMA-Engineering).
#ifndef ZIMA_UPDATECORE_H
#define ZIMA_UPDATECORE_H
#include <QJsonObject>
#include <QByteArray>
#include <QString>
#include <functional>

namespace CadUpdate {
constexpr int Protocol = 1;
using Progress = std::function<void(const QJsonObject &)>;
QString platform();
QString platformDirectory();
bool validVersion(const QString &version);
bool safeRelativePath(const QString &path);
QString child(const QString &root, const QString &relative);
QByteArray canonical(const QJsonObject &object);
QJsonObject readJson(const QString &path, qint64 limit = 4 * 1024 * 1024);
void writeJson(const QString &path, const QJsonObject &object);
QString hashFile(const QString &path);
QString installationRoot(const QString &runtime = QString());
QJsonObject verifySignedObject(const QByteArray &payload, const QByteArray &signature);
QJsonObject verifyManifest(const QByteArray &payload, const QByteArray &signature);
QJsonObject check(const QString &root, const Progress &progress = {});
QJsonObject download(const QString &root, const QJsonObject &offer, const Progress &progress = {});
bool trustedInstallation(const QString &root);
QJsonObject status(const QString &root);
QJsonObject activate(const QString &root, const QString &version, const Progress &progress = {});
QJsonObject rollback(const QString &root, const Progress &progress = {});
int launch(const QString &root, bool recoverOnly = false);
QJsonObject prepare(const QString &root, const QString &archive, const QByteArray &manifest,
                    const QByteArray &signature, const Progress &progress = {});
void assertManaged(const QString &root);
}
#endif
