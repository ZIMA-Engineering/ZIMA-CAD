// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AIPROVIDER_H
#define AIPROVIDER_H
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

// Adapted from ZIMA-CAD-Parts. CAD operations belong to the host, never to a provider.
class AiProvider : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual bool ready() const = 0;
    virtual bool connected() const = 0;
    virtual bool busy() const = 0;
    virtual QString status() const = 0;
    virtual QJsonArray models() const = 0;
    virtual void connectAccount(const QString &executable) = 0;
    virtual void login() = 0;
    virtual void logout() = 0;
    virtual void ask(const QString &text, const QJsonObject &context, const QString &model) = 0;
    virtual void cancel() = 0;
    virtual void newConversation() = 0;
    virtual void toolResult(const QString &callId, const QJsonObject &result, bool success) = 0;
signals:
    void changed();
    void loginUrl(const QUrl &url);
    void message(const QString &text);
    void answer(const QString &text);
    void toolRequested(const QString &callId, const QString &name, const QJsonObject &arguments);
    void failed(const QString &message);
    void cancelled();
};
AiProvider *cadAiProvider();
QString findCodexExecutable();
#endif
