// Adapted from the ZIMA-CAD-Parts updater (ZIMA-Engineering).
#include "updateservice.h"
#include "installationclient.h"
#include "version.h"
#include <QApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSettings>
#include <QFile>
#include <QUuid>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QTimer>
#include <QDateTime>

UpdateService *UpdateService::get()
{
    static auto service = new UpdateService(qApp);
    return service;
}
UpdateService::UpdateService(QObject *parent) : QObject(parent)
{
    m_progressTimer.start();
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (m_process.state() != QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(1000); }
    });
#ifdef Q_OS_WIN
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    QFile previousResult(zimaInstallationRoot() + "/.updates/" +
#ifdef Q_OS_WIN
        "windows-x64"
#else
        "linux-x86_64"
#endif
        + "-result.json");
    if (!zimaInstallationRoot().isEmpty() && previousResult.open(QIODevice::ReadOnly) && previousResult.size() < 65536) {
        const auto result = QJsonDocument::fromJson(previousResult.readAll()).object();
        if (result["status"] == "error") m_previousInstallationError = result["error"].toString();
    }
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_buffer += m_process.readAllStandardOutput();
        if (m_buffer.size() > 4 * 1024 * 1024) { m_process.kill(); m_error = tr("Update response is too large."); return; }
        while (m_buffer.contains('\n')) {
            const auto index = m_buffer.indexOf('\n');
            const auto object = QJsonDocument::fromJson(m_buffer.left(index)).object();
            m_buffer.remove(0, index + 1);
            if (object["event"] == "progress" && object.contains("received")) {
                m_received = object["received"].toInteger(); m_total = object["total"].toInteger();
                if (m_progressTimer.elapsed() >= 100) { m_progressTimer.restart(); emit changed(); }
            }
            if (object["event"] == "progress" && m_phase != object["phase"].toString()) {
                m_phase = object["phase"].toString(); emit changed();
            }
            if (object["event"] == "result") { m_result = object; emit changed(); }
        }
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        m_busy = false;
        m_error = tr("The update component could not be started."); emit changed();
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus exit) {
        m_busy = false;
        if (m_cancelled) { m_error = tr("Aktualizace byla zrušena. Nainstalovaná verze se nezměnila."); }
        else if (code || exit != QProcess::NormalExit || m_result.isEmpty()) {
            m_error = m_result["error"].toString();
            if (m_error.isEmpty()) m_error = tr("The update operation failed.");
            if (m_command == "check") m_offer = {};
        } else if (m_command == "check") {
            m_offer = m_result["status"] == "available" ? m_result : QJsonObject();
            m_phase = m_result["status"].toString();
            m_lastCheck = QDateTime::currentDateTime().toString(Qt::ISODate);
        } else if (m_command == "download" && m_result["status"] == "prepared") {
            m_preparedVersion = m_result["version"].toString();
            m_phase = "prepared";
        }
        emit changed();
    });
}
void UpdateService::run(const QString &command, const QStringList &arguments)
{
    if (m_busy) return;
    m_received = 0; m_total = 0;
    m_buffer.clear(); m_result = {}; m_error.clear(); m_cancelled = false;
    m_command = command; m_phase = command == "check" ? "checking" : "downloading"; m_busy = true;
    auto args = QStringList{command} + arguments;
    const auto root = zimaInstallationRoot();
    if (!root.isEmpty()) args << "--root" << root;
    m_process.start(zimaUpdateExecutable(), args);
    emit changed();
}
void UpdateService::check() { if (!m_busy) { m_offer = {}; m_preparedVersion.clear(); run("check"); } }
void UpdateService::install()
{
    if (m_busy || !m_offer["installable"].toBool()) return;
    m_previousInstallationError.clear();
    m_preparedVersion.clear();
    run("download", {"--target", m_offer["availableVersion"].toString()});
}
void UpdateService::restartPrepared()
{
    if (!m_busy && !m_preparedVersion.isEmpty()) handoff(false);
}
void UpdateService::handoff(bool rollback)
{
    const auto blocker = restartBlocker();
    if (!blocker.isEmpty()) { m_error = blocker; emit changed(); return; }
    const auto root = zimaInstallationRoot();
    if (root.isEmpty()) { m_error = tr("Use the distribution launcher to install updates."); return; }
    QStringList args{rollback ? "rollback" : "install", "--root", root, "--apply"};
    if (!rollback) args << "--target" << m_preparedVersion;
    m_error.clear(); m_previousInstallationError.clear();
    const auto operation = QUuid::createUuid().toString(QUuid::WithoutBraces);
    args << "--operation" << operation;
    QProcess installer;
    installer.setProgram(zimaUpdateExecutable()); installer.setArguments(args);
#ifdef Q_OS_WIN
    installer.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    if (!installer.startDetached()) { m_error = tr("The installer could not be started."); emit changed(); return; }
    m_phase = "waiting"; m_busy = true;
    auto poll = new QTimer(this);
    connect(poll, &QTimer::timeout, this, [this, root, operation, poll] {
        QFile file(root + "/.updates/" +
#ifdef Q_OS_WIN
            "windows-x64"
#else
            "linux-x86_64"
#endif
            + "-result.json");
        if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return;
        const auto result = QJsonDocument::fromJson(file.readAll()).object();
        if (result["operation"] != operation) return;
        m_busy = false; m_error = result["error"].toString(); m_phase = result["status"].toString();
        poll->deleteLater(); emit changed();
    });
    poll->start(1000);
    emit changed();
    QTimer::singleShot(0, qApp, [] { QApplication::closeAllWindows(); });
}
void UpdateService::rollback() { if (!m_busy) handoff(true); }
void UpdateService::cancel()
{
    m_cancelled = true;
    if (m_process.state() != QProcess::NotRunning) m_process.kill();
}
void UpdateService::scheduleStartupCheck()
{
    QTimer::singleShot(1500, this, [this] { if (automatic()) check(); });
}
void UpdateService::configure(const QString& preferences, std::function<QString()> guard)
{
    m_preferences = preferences;
    m_restartGuard = std::move(guard);
}
bool UpdateService::automatic() const
{
    return !m_preferences.isEmpty() && QSettings(m_preferences, QSettings::IniFormat).value("Updates/CheckAtStartup", true).toBool();
}
bool UpdateService::saveAutomatic(bool enabled, QString* error)
{
    if (m_preferences.isEmpty()) return false;
    if (enabled == automatic()) return true;
    QSettings settings(m_preferences, QSettings::IniFormat);
    settings.setAtomicSyncRequired(true);
    settings.setValue("Updates/CheckAtStartup", enabled);
    settings.sync();
    if (settings.status() == QSettings::NoError) return true;
    if (error) *error = tr("Nelze uložit nastavení aktualizací.");
    return false;
}
QString UpdateService::restartBlocker() const
{
    return m_restartGuard ? m_restartGuard() : tr("Restart není v tomto okně dostupný.");
}
void UpdateService::acknowledgeStartup()
{
    const auto name = qEnvironmentVariable("ZIMA_UPDATE_SOCKET");
    const auto token = qEnvironmentVariable("ZIMA_UPDATE_TOKEN");
    if (name.isEmpty() || token.isEmpty()) return;
    qunsetenv("ZIMA_UPDATE_SOCKET"); qunsetenv("ZIMA_UPDATE_TOKEN");
    auto socket = new QLocalSocket(this);
    QTimer::singleShot(5000, socket, &QObject::deleteLater);
    connect(socket, &QLocalSocket::connected, this, [socket, token] {
        socket->write(QJsonDocument(QJsonObject{{"token", token}, {"version", VERSION},
            {"pid", QCoreApplication::applicationPid()}}).toJson(QJsonDocument::Compact) + '\n');
        socket->flush();
    });
    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    socket->connectToServer(name);
}
