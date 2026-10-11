#pragma once
#include "preferences.hpp"
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <functional>

class QWebEnginePage;
class QWebEngineProfile;
class QWidget;

namespace zima::browser {
class Credentials final : public QObject {
    Q_OBJECT
public:
    Credentials(QString directory, const Preferences& preferences, QWidget* owner);
    void attach(QWebEnginePage* page);
    void install_script(QWebEngineProfile* profile);
    void show_manager();
    static QString origin(const QUrl& url);
    static bool valid_request(const QUrl& page, const QVariantMap& request);
    void autofill(QPointer<QWebEnginePage> page, QVariantMap request,
        std::function<void(QVariantMap)> reply);
    void submitted(QPointer<QWebEnginePage> page, QVariantMap request);
private:
    QString directory_;
    const Preferences& preferences_;
    QPointer<QWidget> owner_;
    void warn(const QString& error);
};
class CredentialsBridge final : public QObject {
    Q_OBJECT
public:
    CredentialsBridge(Credentials& credentials,QWebEnginePage* page);
public slots:
    void requestAutofill(const QVariantMap& request);
    void formSubmitted(const QVariantMap& request);
signals:
    void autofillReady(const QVariantMap& response);
private:
    Credentials& credentials_;
    QPointer<QWebEnginePage> page_;
};
}
