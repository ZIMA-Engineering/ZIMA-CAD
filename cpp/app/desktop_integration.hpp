#pragma once
#include <QString>
#include <QMap>
#include <QByteArray>
#include <QSettings>
class QWidget;
namespace zima::app::desktop {
struct Context {
    QString launcher;
    QString id;
    QString data;
    QString programs;
    QString registry = "HKEY_CURRENT_USER\\Software";
    QSettings::Format registry_format = QSettings::NativeFormat;
    bool installed = false;
};
Context current_context();
QString installation_id(QString path);
QString desktop_exec(QString path);
QByteArray mime_package();
QByteArray desktop_entry(const Context& context);
bool registered(const Context& context);
void install(const Context& context);
void remove(const Context& context);
void offer_first_launch(QWidget* parent);
void offer_first_launch(QWidget* parent, const Context& context);
QWidget* settings_page(QWidget* parent);
QWidget* settings_page(QWidget* parent,const Context& context);
bool submit_settings(QWidget* page);
// Returns -1 when no desktop-integration argument was supplied.
int command_line();
}
