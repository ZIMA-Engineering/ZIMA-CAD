#include "../app/desktop_integration.hpp"
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImage>
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
#include <iostream>
#include <memory>
#include <stdexcept>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace zima::app::desktop;
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
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
        const auto entry=desktop_entry(c);check(entry.contains("%f")&&entry.contains("application/x-zima-prtz;"),"Missing desktop file handler");
        check(desktop_exec("/a b/100%/\"$`x").contains("100%%"),"Desktop percent field was not escaped");
        QXmlStreamReader xml(mime_package());int types=0;
        while(!xml.atEnd()){xml.readNext();if(xml.isStartElement()&&xml.name()=="mime-type")++types;}
        check(!xml.hasError()&&types==5,"Invalid MIME XML");
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
        install(c);check(registered(c),"Registration incomplete");
        check(registry.value("Classes/.prtz/.").toString()=="Other.CAD","Default overwritten");
        check(registry.value("UserChoice/ProgId").toString()=="Other.CAD","UserChoice overwritten");
        for(const auto* ext:{"prtz","asmz","drwz","frmz","tblz"}) {
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
        registry.setValue(command,"broken");registry.sync();
        check(!registered(c),"Broken command not detected");install(c);check(registered(c),"Repair failed");
        Context other=c;other.id=installation_id(dir.path()+"/second");install(other);
        remove(c);check(!registered(c)&&registered(other),"Removal affected another installation");
        check(registry.value("Classes/.prtz/.").toString()=="Other.CAD","Removal changed defaults");
        remove(other);remove(c);
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
