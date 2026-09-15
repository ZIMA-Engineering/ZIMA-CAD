// Adapted from the ZIMA-CAD-Parts updater (ZIMA-Engineering).
#ifndef ZIMA_UPDATESERVICE_H
#define ZIMA_UPDATESERVICE_H
#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QElapsedTimer>
#include <functional>

class UpdateService : public QObject
{
    Q_OBJECT
public:
    static UpdateService *get();
    void check();
    void install();
    void restartPrepared();
    void rollback();
    void cancel();
    qint64 receivedBytes() const { return m_received; }
    qint64 totalBytes() const { return m_total; }
    bool busy() const { return m_busy; }
    QJsonObject offer() const { return m_offer; }
    QJsonObject result() const { return m_result; }
    QString phase() const { return m_phase; }
    QString error() const { return m_error.isEmpty() ? m_previousInstallationError : m_error; }
    QString preparedVersion() const { return m_preparedVersion; }
    QString lastCheck() const { return m_lastCheck; }
    void configure(const QString& preferences, std::function<QString()> restartGuard);
    bool automatic() const;
    bool saveAutomatic(bool enabled, QString* error);
    QString restartBlocker() const;
    void scheduleStartupCheck();
    void acknowledgeStartup();
signals:
    void changed();
    void showSettingsRequested();
private:
    explicit UpdateService(QObject *parent);
    void run(const QString &command, const QStringList &arguments = {});
    void handoff(bool rollback);
    QProcess m_process;
    QElapsedTimer m_progressTimer;
    qint64 m_received = 0, m_total = 0;
    QByteArray m_buffer;
    QString m_command, m_phase, m_error, m_previousInstallationError;
    QString m_preferences, m_preparedVersion, m_lastCheck;
    std::function<QString()> m_restartGuard;
    QJsonObject m_offer, m_result;
    bool m_busy = false, m_cancelled = false;
};
#endif
