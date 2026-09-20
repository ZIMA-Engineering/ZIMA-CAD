#pragma once

// Observer/commands used only by the multi-process startup contract. Ordinary
// desktop launches never enable the probe or write test reports.
#include "assembly_workspace_window.hpp"
#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QSaveFile>
#include <QTabBar>
#include <QTimer>
#include <QApplication>

namespace zima::app {

inline void install_instance_verification(AssemblyWorkspaceWindow& window,
        const QString& directory) {
    const auto report_directory = qEnvironmentVariable("ZIMA_VERIFY_INSTANCE_DIRECTORY");
    if (report_directory.isEmpty()) return;
    const auto prefix = QDir(report_directory).filePath(
        QString::number(QCoreApplication::applicationPid()));
    auto* timer = new QTimer(&window);
    QObject::connect(timer, &QTimer::timeout, &window, [&window, prefix, report_directory] {
        if (QFile::remove(prefix + ".quit")) { window.close(); return; }
        if (QFile::remove(prefix + ".close-document"))
            window.findChild<QAction*>("closeDocumentAction")->trigger();
        QFile request(prefix+".command");
        if(request.open(QIODevice::ReadOnly)) {
            const auto data=QJsonDocument::fromJson(request.readAll()).object();request.close();request.remove();
            const auto result=window.execute_console_command(QString::fromUtf8(QJsonDocument(data["request"].toObject()).toJson(QJsonDocument::Compact)));
            window.setProperty("instanceProbeRequest",data["id"].toInt());
            window.setProperty("instanceProbeSuccess",result.ok);
        }
        QJsonArray documents;
        if (const auto* tabs = window.findChild<QTabBar*>("documentTabs"))
            for (int index = 0; index < tabs->count(); ++index)
                documents.push_back(tabs->tabText(index));
        auto* menu = window.findChild<QMenu*>("windowMenu");
        QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
        QJsonObject report{{"pid", QCoreApplication::applicationPid()},
            {"newWindowAvailable", window.findChild<QAction*>("newWindowAction") != nullptr},
            {"instance", window.property("applicationInstance").toInt()},
            {"title", window.windowTitle()}, {"documents", documents},
            {"directory",window.property("instanceWorkingDirectory").toString()},
            {"request",window.property("instanceProbeRequest").toInt()},
            {"success",window.property("instanceProbeSuccess").toBool()}};
        QSaveFile file(prefix + ".json");
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(report).toJson());
            file.commit();
        }
    });
    timer->start(50);
    QTimer::singleShot(45000, &window, &QWidget::close);
}

} // namespace zima::app
