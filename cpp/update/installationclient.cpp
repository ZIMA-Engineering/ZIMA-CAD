// Adapted from the ZIMA-CAD-Parts updater (ZIMA-Engineering).
#include "installationclient.h"
#include "version.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QUuid>
#include <memory>

QString zimaUpdateExecutable()
{
    return QCoreApplication::applicationDirPath() + "/zima-cad-update"
#ifdef Q_OS_WIN
        ".exe"
#endif
        ;
}
QString zimaInstallationRoot(const QString& executable)
{
    QDir path(executable.isEmpty() ? QCoreApplication::applicationDirPath() : QFileInfo(executable).absolutePath());
#ifndef Q_OS_WIN
    if (path.dirName() == "bin") path.cdUp();
#endif
    if (!QRegularExpression("^[0-9]{10}$").match(path.dirName()).hasMatch()) return {};
    path.cdUp();
    if (QFileInfo(path.absolutePath()).isSymLink() || QFileInfo(path.absolutePath()).isJunction()) return {};
#ifdef Q_OS_WIN
    if (path.dirName() != "windows") return {};
#else
    if (path.dirName() != "linux") return {};
#endif
    path.cdUp();
    QFile file(path.filePath("installation.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) return {};
    const auto marker = QJsonDocument::fromJson(file.readAll()).object();
    if (marker["product"] != "ZIMA-CAD" || marker["protocol"].toInt() != 1 || QFileInfo::exists(path.filePath(".git"))) return {};
    return path.canonicalPath();
}
bool registerZimaInstance(QString *error, const QString& executable)
{
    static std::unique_ptr<QLockFile> instance;
    const auto root = zimaInstallationRoot(executable);
    if (root.isEmpty()) return true;
    const auto directory = root + "/.updates/instances";
    for (const auto &name : {root + "/.updates", directory}) {
        const QFileInfo info(name);
        if (info.isSymLink() || info.isJunction()) { *error = "Linked installation state"; return false; }
    }
    if (!QDir().mkpath(directory)) { *error = "Cannot register application instance"; return false; }
    QLockFile gate(root + "/.updates/install.lock"); gate.setStaleLockTime(0);
    const bool candidate = !qEnvironmentVariable("ZIMA_UPDATE_SOCKET").isEmpty()
        && !qEnvironmentVariable("ZIMA_UPDATE_TOKEN").isEmpty();
    if (!candidate && !gate.tryLock(5000)) { *error = "An update is being activated. Try again shortly."; return false; }
    instance = std::make_unique<QLockFile>(directory + '/' + VERSION + '-' + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".lock");
    instance->setStaleLockTime(0);
    if (!instance->tryLock()) { *error = "Cannot lock application registration"; return false; }
    return true;
}
