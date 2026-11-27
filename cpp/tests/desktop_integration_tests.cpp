#include "../app/desktop_integration.hpp"
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImage>
#include <QIcon>
#include <QSettings>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QTranslator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QDir>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#endif

using namespace zima::app::desktop;
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
#ifdef Q_OS_WIN
struct LinkValues { QString icon,arguments,directory,id; };
LinkValues link_values(const QString& path,const LinkValues* replacement=nullptr) {
    const auto status=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    check(SUCCEEDED(status)||status==RPC_E_CHANGED_MODE,"COM initialization failed");
    const auto close_com=qScopeGuard([&]{if(SUCCEEDED(status))CoUninitialize();});
    IShellLinkW* link{};check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))),"Link creation failed");
    const auto close_link=qScopeGuard([&]{link->Release();});
    IPersistFile* file{};check(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file))),"Link persistence failed");
    const auto close_file=qScopeGuard([&]{file->Release();});
    const auto native=QDir::toNativeSeparators(path).toStdWString();
    check(SUCCEEDED(file->Load(native.c_str(),replacement?STGM_READWRITE:STGM_READ)),"Link load failed");
    IPropertyStore* properties{};check(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&properties))),"Link properties failed");
    const auto close_properties=qScopeGuard([&]{properties->Release();});
    if(replacement) {
        check(SUCCEEDED(link->SetIconLocation(replacement->icon.toStdWString().c_str(),0))&&
            SUCCEEDED(link->SetArguments(replacement->arguments.toStdWString().c_str()))&&
            SUCCEEDED(link->SetWorkingDirectory(replacement->directory.toStdWString().c_str())),"Link fixture failed");
        PROPVARIANT id;PropVariantInit(&id);
        check(SUCCEEDED(InitPropVariantFromString(replacement->id.toStdWString().c_str(),&id)),"Link identity fixture failed");
        const auto clear=qScopeGuard([&]{PropVariantClear(&id);});
        check(SUCCEEDED(properties->SetValue(PKEY_AppUserModel_ID,id))&&SUCCEEDED(properties->Commit())&&
            SUCCEEDED(file->Save(native.c_str(),TRUE)),"Link fixture save failed");
    }
    wchar_t icon[32768]{},arguments[32768]{},directory[32768]{};int index{};
    check(SUCCEEDED(link->GetIconLocation(icon,32768,&index))&&SUCCEEDED(link->GetArguments(arguments,32768))&&
        SUCCEEDED(link->GetWorkingDirectory(directory,32768)),"Link read failed");
    PROPVARIANT id;PropVariantInit(&id);const auto clear=qScopeGuard([&]{PropVariantClear(&id);});
    check(SUCCEEDED(properties->GetValue(PKEY_AppUserModel_ID,&id)),"Link identity read failed");
    return {QString::fromWCharArray(icon),QString::fromWCharArray(arguments),QString::fromWCharArray(directory),
        id.vt==VT_LPWSTR?QString::fromWCharArray(id.pwszVal):QString{}};
}
#endif
class Catalog final:public QTranslator {
public:
    QJsonObject messages;
    QString translate(const char*,const char* source,const char*,int) const override {return messages.value(QString::fromUtf8(source)).toString();}
    bool isEmpty() const override {return messages.isEmpty();}
};
int main(int argc,char** argv) {
    QApplication app(argc,argv);QTemporaryDir dir;
    const bool native=app.arguments().contains("--native-registry");
    const auto test_registry="HKEY_CURRENT_USER\\Software\\ZIMA-CAD-Tests\\"+installation_id(dir.path());
    try {
        check(dir.isValid(),"Temporary directory failed");
        Context c;c.launcher=QCoreApplication::applicationFilePath();
        const auto copied=dir.path()+QString::fromUtf8("/P\xc5\x99\xc3\xadli\xc5\xa1 ")+"CAD.exe";
        check(QFile::copy(c.launcher,copied),"Unicode launcher fixture failed");c.launcher=copied;
        c.id=installation_id(dir.path());c.data=dir.path()+"/data";c.programs=dir.path()+"/programs";
        c.registry=dir.path()+"/registry.ini";
        c.registry_format=QSettings::IniFormat;
        if(native) { c.registry=test_registry;c.registry_format=QSettings::NativeFormat; }
        check(installation_id(c.launcher)==installation_id(QFileInfo(c.launcher).absolutePath()+"/./"+QFileInfo(c.launcher).fileName()),"Unstable installation identity");
        check(installation_id(c.launcher)!=installation_id(c.launcher+"other"),"Installations share identity");
        const auto entry=desktop_entry(c);check(entry.contains("%f")&&entry.contains("application/x-zima-prtz;")&&entry.contains("application/x-zima-symz;"),"Missing desktop file handler");
        check(desktop_exec("/a b/100%/\"$`x").contains("100%%"),"Desktop percent field was not escaped");
        QXmlStreamReader xml(mime_package());int types=0;
        while(!xml.atEnd()){xml.readNext();if(xml.isStartElement()&&xml.name()=="mime-type")++types;}
        check(!xml.hasError()&&types==7&&entry.contains("application/x-zima-toolz;"),"Invalid MIME XML");
        QWidget parent;parent.resize(900,600);parent.show();
        c.installed=true;offer_first_launch(&parent,c);app.processEvents();
        auto* offer=parent.findChild<QDialog*>("desktopIntegrationOffer");
        check(offer&&offer->windowFlags().testFlag(Qt::SubWindow),"First-launch offer is not an internal dialog");
        offer->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);app.processEvents();
        check(!registered(c),"Cancel changed registration");
        offer_first_launch(&parent,c);app.processEvents();
        check(!parent.findChild<QDialog*>("desktopIntegrationOffer"),"Declined offer was repeated");
        parent.hide();
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QFile catalog(QFileInfo(__FILE__).absolutePath()+"/../../config/localization/"+language+".qt.json");
            check(catalog.open(QIODevice::ReadOnly),"Cannot read translation catalog");
            Catalog translator;translator.messages=QJsonDocument::fromJson(catalog.readAll()).object();app.installTranslator(&translator);
            std::unique_ptr<QWidget> page(settings_page(nullptr));page->resize(580,350);page->show();app.processEvents();
            const auto* action=page->findChild<QComboBox*>("desktopIntegrationAction");
            check(action&&action->itemText(1)==translator.messages.value("Register or repair").toString(),"Untranslated registration action");
            check(action->itemText(2)==translator.messages.value("Remove this registration").toString(),"Untranslated removal action");
            for(auto* label:page->findChildren<QLabel*>())check(label->geometry().bottom()<=page->height(),"Registration label exceeds page");
            if(app.arguments().contains("--capture"))page->grab().save(QFileInfo(QCoreApplication::applicationFilePath()).absolutePath()+"/desktop-integration-"+language+".png");
            // Selecting an operation must not execute it before OK.
            page->findChild<QComboBox*>("desktopIntegrationAction")->setCurrentIndex(1);
            page.reset();app.removeTranslator(&translator);
        }
#ifdef Q_OS_WIN
        QSettings registry(c.registry,c.registry_format);
        registry.setValue("Classes/.prtz/.","Other.CAD");
        registry.setValue("UserChoice/ProgId","Other.CAD");registry.sync();
        check(!registered(c),"Portable mode registered itself");
        check(!refresh_registered_icons(c),"Icon refresh registered an unregistered installation");
        install(c);check(registered(c),"Registration incomplete");
        const auto check_application_icon=[](const QImage& image) {
            std::size_t white{},cyan{};
            for(int y=0;y<image.height();++y)
                for(int x=0;x<image.width();++x) {
                    const auto color=image.pixelColor(x,y);
                    if(color.alpha()<100)continue;
                    const auto spread=std::max({color.red(),color.green(),
                        color.blue()})-std::min({color.red(),color.green(),
                        color.blue()});
                    if(spread<20&&color.lightness()>200)++white;
                    if(color.red()<80&&color.green()>120&&
                            color.blue()>180)++cyan;
                }
            const auto minimum=static_cast<std::size_t>(
                image.width()*image.height()/50);
            check(white>minimum&&cyan>minimum,
                "Windows application icon lost its white ZC monogram or azure accent");
        };
        auto application_icon_path=registry.value(
            "ZIMA-CAD/Desktop/"+c.id+
            "/Capabilities/ApplicationIcon").toString();
        if(application_icon_path.startsWith('"')&&
                application_icon_path.endsWith('"'))
            application_icon_path=application_icon_path.mid(
                1,application_icon_path.size()-2);
        check(QFileInfo(application_icon_path).fileName().startsWith("app-")&&
                QFileInfo(application_icon_path).suffix()=="ico",
            "Windows application icon is not content-addressed");
        QFile application_icon_file(application_icon_path);
        check(application_icon_file.open(QIODevice::ReadOnly),
            "Registered application icon missing");
        const auto application_icon_bytes=application_icon_file.readAll();
        application_icon_file.close();
        QFile embedded_icon(":/zima/branding/windows-app-icon.ico");
        check(embedded_icon.open(QIODevice::ReadOnly)&&embedded_icon.readAll()==application_icon_bytes,
            "Registration diverged from the executable artwork");
        const QIcon registered_icon(application_icon_path);
        for(const int size:{16,24,32,48,64,128,256}) {
            const auto image=registered_icon.pixmap(size,size).toImage();
            check(!image.isNull(),"Registered application icon cannot be decoded");
            check_application_icon(image);
            int first=image.height(),last=-1;
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
                if(image.pixelColor(x,y).alpha()>100){first=std::min(first,y);last=std::max(last,y);}
            check(last-first+1>image.height()*0.68&&last-first+1<=image.height()*0.85,
                "Registered application icon has incorrect vertical proportions");
        }
        const QIcon executable_icon(QFileInfo(__FILE__).absolutePath()+
            "/../app/windows/app-icon.ico");
        check(!executable_icon.isNull(),"Embedded Windows icon is missing");
        check_application_icon(executable_icon.pixmap(48,48).toImage());
        check(registry.value("Classes/.prtz/.").toString()=="Other.CAD","Default overwritten");
        check(registry.value("UserChoice/ProgId").toString()=="Other.CAD","UserChoice overwritten");
        const auto application_shortcut=c.programs+"/ZIMA-CAD-"+c.id+".lnk";
        check(QFile::remove(application_shortcut)&&
                QFile::link(c.launcher,application_shortcut),
            "Cannot prepare an obsolete application shortcut");
        check(!registered(c),"Obsolete shortcut identity was not detected");
        install(c);check(registered(c),
            "Register or repair did not rewrite the application shortcut");
        const auto pins=dir.path()+"/taskbar";
        check(QDir().mkpath(pins),"Pin fixture directory failed");c.shortcut_directories={pins};
        const auto pinned=pins+"/User named ZC.lnk",foreign=pins+"/Other identity.lnk";
        check(QFile::copy(application_shortcut,pinned)&&QFile::copy(application_shortcut,foreign),"Pin fixtures failed");
        const LinkValues obsolete{dir.path()+"/old.ico","--working-directory \"custom project\"",dir.path(),"ZIMA.CAD."+c.id};
        link_values(pinned,&obsolete);
        auto other_identity=obsolete;other_identity.id="Other.Application";link_values(foreign,&other_identity);
        registry.setValue("ZIMA-CAD/Desktop/"+c.id+"/Capabilities/ApplicationIcon",obsolete.icon);registry.sync();
        check(refresh_registered_icons(c)&&registered(c),"Automatic icon refresh failed");
        const auto refreshed=link_values(pinned);
        check(QDir::fromNativeSeparators(refreshed.icon)==QDir::fromNativeSeparators(application_icon_path)&&
            refreshed.arguments==obsolete.arguments&&QDir::fromNativeSeparators(refreshed.directory)==obsolete.directory&&
            refreshed.id==obsolete.id,"Icon refresh changed shortcut launch settings or identity");
        check(QDir::fromNativeSeparators(link_values(foreign).icon)==other_identity.icon,"Icon refresh modified a foreign application shortcut");
        const auto icon_time=QFileInfo(application_icon_path).lastModified();
        const auto pin_time=QFileInfo(pinned).lastModified();
        check(!refresh_registered_icons(c)&&QFileInfo(application_icon_path).lastModified()==icon_time&&
            QFileInfo(pinned).lastModified()==pin_time,"Unchanged startup rewrote shell assets");
        link_values(pinned,&obsolete);install(c);
        check(QDir::fromNativeSeparators(link_values(pinned).icon)==QDir::fromNativeSeparators(application_icon_path),"Manual repair retained stale pinned icon");
        for(const auto* ext:{"prtz","asmz","drwz","frmz","tblz","symz"}) {
            QFile icon(c.data+"/zima-cad/desktop/"+c.id+'/'+ext+".ico");
            check(icon.open(QIODevice::ReadOnly),"Document icon missing");
            const auto bytes=icon.readAll();
            check(bytes.left(6)==QByteArray::fromHex("000001000100") &&
                QImage::fromData(bytes.mid(22),"PNG").size()==QSize(256,256),"Document icon cannot be decoded");
            const auto icon_path=icon.fileName().toStdWString();
            const auto shell_icon=static_cast<HICON>(LoadImageW(nullptr,icon_path.c_str(),IMAGE_ICON,32,32,LR_LOADFROMFILE));
            check(shell_icon!=nullptr,"Windows cannot load the document icon");DestroyIcon(shell_icon);
        }
        const QString command="Classes/ZIMA.CAD."+c.id+".Part/shell/open/command/.";
        const QString symbol="Classes/ZIMA.CAD."+c.id+".Symbol";
        check(registry.value("ZIMA-CAD/Desktop/"+c.id+"/Capabilities/FileAssociations/.symz").toString()=="ZIMA.CAD."+c.id+".Symbol","Symbol capability missing");
        registry.remove(symbol+"/DefaultIcon");registry.sync();
        check(!registered(c),"Missing symbol icon not detected");install(c);check(registered(c),"Symbol icon repair failed");
        registry.setValue(command,"broken");registry.sync();
        check(!registered(c),"Broken command not detected");install(c);check(registered(c),"Repair failed");
        Context other=c;other.id=installation_id(dir.path()+"/second");install(other);
        remove(c);check(!registered(c)&&registered(other),"Removal affected another installation");
        check(registry.value("Classes/.prtz/.").toString()=="Other.CAD","Removal changed defaults");
        remove(other);remove(c);
        check(!refresh_registered_icons(c)&&!registered(c),"Startup recreated removed registration");
        install(c);
        registry.setValue("Classes/ZIMA.CAD."+c.id+".Part/Owner","another installation");registry.sync();
        bool rejected=false;try {install(c);}catch(const std::exception&){rejected=true;}
        check(rejected,"Repair took ownership of a foreign handler");
        remove(c);
        check(registry.value("Classes/ZIMA.CAD."+c.id+".Part/Owner").toString()=="another installation","Removal erased a foreign handler");
        registry.remove("");registry.sync();
#endif
        std::cout<<"Desktop integration ownership, repair, removal, icons and default preservation passed\n";
        return 0;
    } catch(const std::exception& e) {
#ifdef Q_OS_WIN
        if(native) {QSettings cleanup(test_registry,QSettings::NativeFormat);cleanup.remove("");cleanup.sync();}
#endif
        std::cerr<<e.what()<<'\n';return 1;
    }
}
