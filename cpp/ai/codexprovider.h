// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CODEXPROVIDER_H
#define CODEXPROVIDER_H
#include "aiprovider.h"
#include <QProcess>
#include <QTimer>
#include <QHash>

class CodexProvider : public AiProvider
{
    Q_OBJECT
public:
    explicit CodexProvider(QObject *parent = nullptr, const QString &profile = {});
    ~CodexProvider() override;
    bool ready() const override { return m_ready; }
    bool connected() const override { return m_initialized; }
    bool busy() const override { return m_busy; }
    QString status() const override { return m_statusSource ? tr(m_statusSource) : m_status; }
    QJsonArray models() const override { return m_models; }
    void connectAccount(const QString &executable) override;
    void login() override;
    void logout() override;
    void ask(const QString &text, const QJsonObject &context, const QString &model) override;
    void cancel() override;
    void newConversation() override;
    void toolResult(const QString &callId, const QJsonObject &result, bool success) override;
    static QString profileDirectory();
private:
    void send(const QString &method, const QJsonObject &params = {});
    void read();
    void receive(const QJsonObject &message);
    void stop();
    void fail(const QString &message);
    void account(const QJsonObject &result);
    void startTurn();
    QProcess *m_process = nullptr;
    QTimer m_timeout;
    QByteArray m_buffer;
    QHash<int, QString> m_requests;
    QHash<QString, QJsonValue> m_toolRequests;
    int m_nextId = 1;
    bool m_ready = false, m_busy = false, m_initialized = false;
    const char *m_statusSource = nullptr;
    QString m_status, m_executable, m_thread, m_turn, m_response, m_loginId;
    QString m_prompt, m_contextKey;
    QString m_profile;
    int m_turnCount = 0, m_toolCount = 0;
    QJsonArray m_models;
};
#endif
