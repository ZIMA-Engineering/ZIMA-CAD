// Short-lived fake GUI for updater lifecycle tests; never accesses user data.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QLockFile>
#include <QTimer>
#include <QUuid>
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QDir dir(app.applicationDirPath());
#ifndef Q_OS_WIN
    if (dir.dirName() == "bin") dir.cdUp();
#endif
    const auto version = dir.dirName();
    const auto runtime = dir.absolutePath();
    dir.cdUp(); dir.cdUp();
    QDir().mkpath(dir.filePath(".updates/instances"));
    QLockFile lock(dir.filePath(".updates/instances/" + version + '-' + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".lock"));
    lock.setStaleLockTime(0); if (!lock.tryLock()) return 4;
    QFile started(dir.filePath("fixture-started"));
    if (started.open(QIODevice::Append)) started.write(version.toUtf8() + '\n');
    if (QFile::exists(runtime + "/fail-start")) return 5;
    QLocalSocket socket;
    QObject::connect(&socket, &QLocalSocket::connected, [&] {
        socket.write(QJsonDocument(QJsonObject{{"token", qEnvironmentVariable("ZIMA_UPDATE_TOKEN")},
            {"version", version}, {"pid", app.applicationPid()}}).toJson(QJsonDocument::Compact) + '\n');
        socket.flush();
    });
    socket.connectToServer(qEnvironmentVariable("ZIMA_UPDATE_SOCKET"));
    QTimer::singleShot(app.arguments().contains("--hold") ? 5000 : 750, &app, &QCoreApplication::quit);
    return app.exec();
}
