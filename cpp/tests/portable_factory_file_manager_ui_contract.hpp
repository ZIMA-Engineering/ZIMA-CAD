#pragma once
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

inline void verify_portable_factory_file_manager_ui(QApplication& application) {
    QTemporaryDir temporary;check(temporary.isValid(),"Portable File Manager fixture unavailable");
    const QString root=temporary.path()+QString::fromUtf8("/Updated CAD Š");
#ifdef _WIN32
    const QString platform="windows",executable="/zima-cad-cpp.exe";
#else
    const QString platform="linux",executable="/bin/zima-cad-cpp";
#endif
    const auto runtime=root+"/"+platform+"/2026102003";
    const auto tables=runtime+"/config/lib/03-ENGINEERING-TABLES";
    const auto write_file=[](const QString& path,const QByteArray& contents){
        QDir().mkpath(QFileInfo(path).absolutePath());QFile file(path);
        check(file.open(QIODevice::WriteOnly)&&file.write(contents)==contents.size(),"Portable GUI fixture write failed");
    };
    write_file(root+"/launcher.ini","[launcher]\n");write_file(runtime+"/version.json","{}\n");
    write_file(runtime+executable,"fixture");write_file(runtime+"/config/config.ini","[Application]\nLanguage=en\n");
    write_file(tables+"/05-FASTENERS/0000-index/metadata.ini","[Directory]\nLabel\\en=Fasteners\n");
    QJsonArray previous{QJsonObject{{"id","formats"},{"name","formats"},{"path","formats"},{"factory",true},{"enabled",false}}};
    {QSettings shared(root+"/config/config.ini",QSettings::IniFormat);shared.setValue("FileManager/Sources",QString::fromUtf8(QJsonDocument(previous).toJson(QJsonDocument::Compact)));shared.sync();}
    QDir().mkpath(root+"/Projects");
    const auto settings=zima::app::ApplicationSettings::load(root+"/Projects",runtime+executable);
    zima::app::FileManagerTab tab(settings,root+"/Projects");tab.resize(1200,900);tab.show();application.processEvents();
    auto* sources=tab.findChild<QListWidget*>("fileManagerSources");check(sources,"Portable File Manager sources missing");
    const auto definition=std::ranges::find(settings.file_manager.sources,QStringLiteral("engineering_tables"),&zima::app::FileManagerSource::id);
    check(definition!=settings.file_manager.sources.end(),"New engineering source was omitted from settings");
    const auto found=sources->findItems(definition->name,Qt::MatchExactly);check(found.size()==1,"New engineering source was omitted or duplicated");
    const auto position=sources->visualItemRect(found.front()).center();check(!sources->visualItemRect(found.front()).isEmpty(),"New engineering source is not visible");
    for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
        QMouseEvent event(type,QPointF(position),QPointF(sources->viewport()->mapToGlobal(position)),Qt::LeftButton,
            type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(sources->viewport(),&event);application.processEvents();
    }
    check(tab.current_directory()==tables,"Clicking updated engineering source did not open its current version directory");
    auto* files=tab.findChild<QTreeWidget*>("fileManagerFiles");check(files&&files->topLevelItemCount()==1,"Updated library contents are missing from File Manager");
    check(files->topLevelItem(0)->text(0)=="Fasteners","Updated library metadata is not displayed");
    check(found.front()->toolTip()==QDir::toNativeSeparators(tables),"Updated source is marked unavailable");
    check(!QFileInfo::exists(root+"/config/lib"),"GUI test accidentally supplied the old incorrect shared directory");
    std::cout<<"Actual File Manager click opened a newly installed factory library with older shared settings\n";
}
