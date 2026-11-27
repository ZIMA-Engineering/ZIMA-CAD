#pragma once

// Opt-in native UI evidence for documentation. Never active during normal use.
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QToolButton>
#include <memory>

namespace zima::app::documentation {
inline void install_command_capture(QWidget& window, const QString& language) {
    const auto destination=qEnvironmentVariable("ZIMA_CAPTURE_GUIDE_DIALOGS");
    if(destination.isEmpty()||!QDir::isAbsolutePath(destination)||
        !QCoreApplication::arguments().contains("--verify-startup"))return;
    auto* timer=new QTimer(&window);
    timer->setInterval(100);
    auto captured=std::make_shared<QMap<QString,bool>>();
    QObject::connect(timer,&QTimer::timeout,&window,[&window,language,destination,captured] {
        // Some verification suites temporarily translate a dialog in place.
        // A control belongs to this fresh workspace's original language only.
        if(qApp->property("zimaLanguage").toString()!=language)return;
        const QDir output(QDir(destination).filePath("commands/"+language));
        if(!QDir().mkpath(output.absolutePath()))return;
        for(auto* button:window.findChildren<QToolButton*>("applicationCommandButton")) {
            auto* action=button->defaultAction();
            if(!action)continue;
            const auto id=action->objectName();
            static const QRegularExpression safe("^[A-Za-z][A-Za-z0-9]+$");
            if(!safe.match(id).hasMatch()||button->width()<10||button->height()<10)continue;
            if(captured->contains(id)&&(captured->value(id)||!action->isEnabled()))continue;
            QFile previous(output.filePath(id+".json"));
            if(!action->isEnabled()&&previous.open(QIODevice::ReadOnly)) {
                const auto evidence=QJsonDocument::fromJson(previous.readAll()).object();
                if(evidence.value("enabled").toBool()&&evidence.value("text").toString()==action->text()&&
                    QFile::exists(output.filePath(id+".png")))continue;
            }
            const auto image=button->grab();
            if(image.isNull()||!image.save(output.filePath(id+".png")))continue;
            QSaveFile metadata(output.filePath(id+".json"));
            const auto data=QJsonDocument(QJsonObject{{"language",language},{"action",id},
                {"text",action->text()},{"enabled",action->isEnabled()},
                {"image",id+".png"},{"width",image.width()},{"height",image.height()},
                {"source",QStringLiteral("native applicationCommandButton QWidget::grab")}}).toJson();
            if(metadata.open(QIODevice::WriteOnly)&&metadata.write(data)==data.size()&&metadata.commit())
                captured->insert(id,action->isEnabled());
        }
    });
    timer->start();
}
}
