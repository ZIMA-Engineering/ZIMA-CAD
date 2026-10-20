#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <vector>

inline void verify_portable_factory_sources() {
    QTemporaryDir temporary;require(temporary.isValid(),"Factory source fixture unavailable");
    const auto root=temporary.path()+QString::fromUtf8("/Portable knihovna Š");
#ifdef _WIN32
    const QString platform="windows", executable="/zima-cad-cpp.exe";
#else
    const QString platform="linux", executable="/bin/zima-cad-cpp";
#endif
    const auto older=root+"/"+platform+"/2026100101";
    const auto newer=root+"/"+platform+"/2026102003";
    write(root+"/launcher.ini","[launcher]\n");
    QJsonArray defaults;
    for(const auto& item:std::vector<std::pair<QString,QString>>{{"formats","formats"},{"materials","materials"},
        {"symbols","symbols"},{"templates","templates"},{"sheet_metal","lib/01-SHEETMETAL"},
        {"native","lib/02-NATIVE"},{"engineering_tables","lib/03-ENGINEERING-TABLES"}}) {
        defaults.push_back(QJsonObject{{"id",item.first},{"name",item.first},{"path",item.second},
            {"factory",true},{"enabled",true},{"readOnly",true}});
        write(newer+"/config/"+item.second+"/factory-marker.txt","new factory");
    }
    const auto prepare=[&](const QString& runtime){
        write(runtime+executable,"fixture");write(runtime+"/version.json","{}\n");
        write(runtime+"/config/config.ini","[Application]\nLanguage=en\n");
        QSettings values(runtime+"/config/config.ini",QSettings::IniFormat);
        values.setValue("FileManager/Sources",QString::fromUtf8(QJsonDocument(defaults).toJson(QJsonDocument::Compact)));values.sync();
    };
    prepare(newer);write(older+"/config/config.ini","old factory");
    QDir().mkpath(root+"/Projects");
    require(!QFileInfo::exists(older+"/config/lib/03-ENGINEERING-TABLES"),"Old-version missing-directory reproduction is invalid");
    const auto source=[](const zima::app::ApplicationSettings& settings,const QString& id)->zima::app::FileManagerSource {
        for(const auto& item:settings.file_manager.sources)if(item.id==id)return item;
        throw std::runtime_error("Missing required factory source: "+id.toStdString());
    };
    const auto fresh=zima::app::ApplicationSettings::load(root+"/Projects",newer+executable);
    for(const auto& item:defaults) {
        const auto row=item.toObject();const auto actual=source(fresh,row["id"].toString());
        require(QDir::cleanPath(actual.resolved_path)==newer+"/config/"+row["path"].toString(),"Fresh factory source resolved outside the active version");
        require(QFileInfo::exists(actual.resolved_path+"/factory-marker.txt"),"Fresh factory library is not reachable");
    }
    QJsonArray old_sources{QJsonObject{{"id","formats"},{"name","formats"},{"path","formats"},
        {"factory",true},{"enabled",false},{"readOnly",false}},
        QJsonObject{{"id","custom-relative"},{"name","Workshop"},{"path","workshop"},{"factory",false},{"enabled",true},{"readOnly",false}},
        QJsonObject{{"id","custom-absolute"},{"name","Supplier"},{"path",root+"/supplier"},{"factory",false},{"enabled",false},{"readOnly",true}}};
    old_sources.push_back(QJsonObject{{"id","materials"},{"name","materials"},{"path","../"+platform+"/2026100101/config/materials"},{"factory",true}});
    old_sources.push_back(QJsonObject{{"id","symbols"},{"name","symbols"},{"path","my-symbols"},{"factory",true}});
    write(root+"/config/workshop/user.part","custom");write(root+"/supplier/user.part","custom");
    {
        QSettings shared(root+"/config/config.ini",QSettings::IniFormat);
        shared.setValue("FileManager/Sources",QString::fromUtf8(QJsonDocument(old_sources).toJson(QJsonDocument::Compact)));
        shared.setValue("Paths/Templates","my-templates");shared.setValue("UserData/Keep","unchanged");shared.sync();
    }
    const auto before=read(root+"/config/config.ini"),factory_before=read(newer+"/config/config.ini");
    const auto upgraded=zima::app::ApplicationSettings::load(root+"/Projects",newer+executable);
    const auto tables=source(upgraded,"engineering_tables");
    require(tables.resolved_path==newer+"/config/lib/03-ENGINEERING-TABLES"&&QFileInfo::exists(tables.resolved_path),"New factory library missing after older configuration upgrade");
    require(upgraded.file_manager.sources.size()==9,"New defaults duplicate or omit custom sources");
    const auto formats=source(upgraded,"formats");require(!formats.enabled&&!formats.read_only,"Upgrade reset factory source preferences");
    require(source(upgraded,"custom-relative").resolved_path==root+"/config/workshop","Upgrade moved a relative custom source");
    require(source(upgraded,"custom-absolute").resolved_path==root+"/supplier"&&!source(upgraded,"custom-absolute").enabled,"Upgrade moved or enabled an absolute custom source");
    require(source(upgraded,"templates").resolved_path==root+"/config/my-templates","Factory source ignored explicit template path");
    require(source(upgraded,"materials").resolved_path==newer+"/config/materials","Materialized older factory path did not follow the active version");
    require(source(upgraded,"symbols").resolved_path==root+"/config/my-symbols","Explicit factory source override was replaced");
    require(read(root+"/config/config.ini")==before&&read(newer+"/config/config.ini")==factory_before,"Loading rewrote user or factory settings");
    auto saved=upgraded;QString error;require(saved.save(&error),"Factory source preferences could not be saved");
    const auto next=root+"/"+platform+"/2026102101";prepare(next);
    write(next+"/config/lib/03-ENGINEERING-TABLES/factory-marker.txt","next factory");
    const auto switched=zima::app::ApplicationSettings::load(root+"/Projects",next+executable);
    require(source(switched,"engineering_tables").resolved_path==next+"/config/lib/03-ENGINEERING-TABLES","Saved settings pinned an old version library");
    require(!source(switched,"formats").enabled,"Version switch reset a disabled factory source");
    require(QSettings(root+"/config/config.ini",QSettings::IniFormat).value("UserData/Keep")=="unchanged","Settings save lost custom data");
    std::cout<<"Portable factory sources: missing old directories, defaults, saved preferences, custom libraries and version switching passed\n";
}
