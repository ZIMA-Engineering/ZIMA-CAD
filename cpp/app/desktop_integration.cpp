#include "desktop_integration.hpp"
#include "../common/installation.hpp"
#include <zima/ui/properties_subwindow.hpp>
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QSvgRenderer>
#include <QPainter>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <iostream>
#include <stdexcept>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#endif

namespace zima::app::desktop {
namespace {
struct Type { const char* extension; const char* name; const char* icon; };
constexpr Type types[]={{"prtz","Part","part"},{"asmz","Assembly","assembly"},
    {"drwz","Drawing","drawing"},{"frmz","Format","drawing-format"},{"tblz","TitleBlock","title-block"},
    {"symz","Symbol","symbol"}};
QString mime(const Type& t) { return "application/x-zima-"+QString(t.extension); }
QString progid(const Context& c,const Type& t) { return "ZIMA.CAD."+c.id+'.'+t.name; }
QString asset_root(const Context& c) { return c.data+"/zima-cad/desktop/"+c.id; }
QString shortcut(const Context& c) { return c.programs+"/ZIMA-CAD-"+c.id+".lnk"; }
QString capabilities(const Context& c) { return "ZIMA-CAD/Desktop/"+c.id; }
QString registration_name(const Context& c) { return "ZIMA-CAD "+c.id; }
QString desktop_value(QString value) {
    value.replace('\\',"\\\\");value.replace('\n',"\\n");value.replace('\r',"\\r");value.replace('\t',"\\t");return value;
}
[[noreturn]] void fail(const char* detail="operation failed") { throw std::runtime_error(std::string("Desktop integration: ")+detail); }
QByteArray read(const QString& path) { QFile f(path); if(!f.open(QIODevice::ReadOnly))return {};return f.readAll(); }
void write(const QString& path,const QByteArray& data) {
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))fail("create directory");
    QSaveFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size()||!f.commit())fail("write asset");
}
QString owner(const Context& c) { return QDir::cleanPath(QFileInfo(c.launcher).absoluteFilePath()); }
bool owns(const Context& c) { return read(asset_root(c)+"/owner")==owner(c).toUtf8(); }
void erase(const QString& path) { if(QFileInfo::exists(path)&&!QFile::remove(path))fail(); }
QByteArray icon_data(const QString& resource) {
#ifdef Q_OS_WIN
    QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);
    QSvgRenderer renderer(resource);if(!renderer.isValid())fail("load icon");
    QImage image(256,256,QImage::Format_ARGB32);image.fill(Qt::transparent);
    {QPainter painter(&image);renderer.render(&painter);}
    if(!image.save(&buffer,"PNG"))fail("encode icon");
    QByteArray ico;QDataStream out(&ico,QIODevice::WriteOnly);out.setByteOrder(QDataStream::LittleEndian);
    out<<quint16(0)<<quint16(1)<<quint16(1)<<quint8(0)<<quint8(0)<<quint8(0)<<quint8(0)
       <<quint16(1)<<quint16(32)<<quint32(png.size())<<quint32(22);
    ico.append(png);return ico;
#else
    const auto bytes=read(resource);if(bytes.isEmpty())fail();return bytes;
#endif
}
QString icon_suffix() {
#ifdef Q_OS_WIN
    return ".ico";
#else
    return ".svg";
#endif
}
QMap<QString,QByteArray> assets(const Context& c) {
    QMap<QString,QByteArray> result;
    result.insert(asset_root(c)+"/app"+icon_suffix(),icon_data(":/zima/branding/app-icon.svg"));
    for(const auto& t:types)result.insert(asset_root(c)+'/'+t.extension+icon_suffix(),icon_data(":/zima/icons/"+QString(t.icon)+".svg"));
#ifndef Q_OS_WIN
    result.insert(c.data+"/applications/zima-cad-"+c.id+".desktop",desktop_entry(c));
    result.insert(c.data+"/mime/packages/zima-cad-"+c.id+".xml",mime_package());
    for(const auto& t:types)result.insert(c.data+"/icons/hicolor/scalable/mimetypes/application-x-zima-"+QString(t.extension)+".svg",icon_data(":/zima/icons/"+QString(t.icon)+".svg"));
#endif
    return result;
}
#ifdef Q_OS_WIN
QMap<QString,QString> registry_values(const Context& c) {
    const auto base=capabilities(c);QMap<QString,QString> result;
    result[base+"/Owner"]=owner(c);
    result[base+"/Capabilities/ApplicationName"]=registration_name(c);
    result[base+"/Capabilities/ApplicationDescription"]="ZIMA-CAD";
    result[base+"/Capabilities/ApplicationIcon"]='"'+QDir::toNativeSeparators(asset_root(c)+"/app.ico")+'"';
    result["RegisteredApplications/"+registration_name(c)]="Software\\"+QString(base+"/Capabilities").replace('/','\\');
    for(const auto& t:types) {
        const auto id=progid(c,t),key="Classes/"+id;
        result[key+"/."]="ZIMA-CAD ."+QString(t.extension);
        result[key+"/Owner"]=owner(c);
        result[key+"/DefaultIcon/."]='"'+QDir::toNativeSeparators(asset_root(c)+'/'+t.extension+".ico")+'"';
        result[key+"/shell/open/command/."]='"'+QDir::toNativeSeparators(c.launcher)+"\" \"%1\"";
        result["Classes/."+QString(t.extension)+"/OpenWithProgids/"+id]="";
        result[base+"/Capabilities/FileAssociations/."+t.extension]=id;
    }
    return result;
}
#endif
void refresh(const Context& c) {
#ifdef Q_OS_WIN
    Q_UNUSED(c);SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
#else
    for(const auto& command:QList<QStringList>{{"update-mime-database",c.data+"/mime"},{"update-desktop-database",c.data+"/applications"}}) {
        QProcess p;p.start(command.front(),command.mid(1));
        if(!p.waitForStarted(3000)||!p.waitForFinished(15000)||p.exitStatus()!=QProcess::NormalExit||p.exitCode()!=0)fail();
    }
#endif
}
}
QString installation_id(QString path) {
    path=QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    path=path.toCaseFolded();
#endif
    return QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(),QCryptographicHash::Sha256).toHex().left(16));
}
Context current_context() {
    Context c;c.launcher=QCoreApplication::applicationFilePath();
    const auto installation=distribution::locate_installation(std::filesystem::u8path(c.launcher.toStdString()));
    if(installation) {
        c.installed=true;
#ifdef Q_OS_WIN
        c.launcher=QString::fromStdWString((installation->root/"ZIMA-CAD.exe").wstring());
#else
        c.launcher=QString::fromStdString((installation->root/"ZIMA-CAD.sh").string());
#endif
    }
    c.id=installation_id(c.launcher);
    c.data=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#ifdef Q_OS_WIN
    PWSTR path=nullptr;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs,0,nullptr,&path))) { c.programs=QString::fromWCharArray(path);CoTaskMemFree(path); }
#endif
    return c;
}
QString desktop_exec(QString path) {
    path.replace('%',"%%");path.replace('\\',"\\\\\\\\");path.replace('"',"\\\\\"");
    path.replace('`',"\\\\`");path.replace('$',"\\\\$");
    path.replace('\n',"\\n");path.replace('\r',"\\r");
    return '"'+path+"\" %f";
}
QByteArray mime_package() {
    QByteArray xml="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n";
    for(const auto& t:types)xml+=("<mime-type type=\""+mime(t)+"\"><comment>ZIMA-CAD ."+t.extension+"</comment><glob pattern=\"*."+t.extension+"\"/></mime-type>\n").toUtf8();
    return xml+"</mime-info>\n";
}
QByteArray desktop_entry(const Context& c) {
    QString value="[Desktop Entry]\nType=Application\nName=ZIMA-CAD\nTerminal=false\nCategories=Graphics;Engineering;\nExec="+desktop_exec(c.launcher)+"\nIcon="+desktop_value(asset_root(c)+"/app.svg")+"\nMimeType=";
    for(const auto& t:types)value+=mime(t)+';';
    return (value+"\nX-ZIMA-Owner="+c.id+'\n').toUtf8();
}
bool registered(const Context& c) {
    if(!owns(c))return false;
    const auto files=assets(c);
    for(auto it=files.cbegin();it!=files.cend();++it)if(read(it.key())!=it.value())return false;
#ifdef Q_OS_WIN
    QSettings registry(c.registry,c.registry_format);
    const auto values=registry_values(c);
    for(auto it=values.cbegin();it!=values.cend();++it)if(!registry.contains(it.key())||registry.value(it.key()).toString()!=it.value())return false;
    if(QDir::cleanPath(QFileInfo(shortcut(c)).symLinkTarget()).compare(owner(c),Qt::CaseInsensitive)!=0)return false;
#endif
    return true;
}
void install(const Context& c) {
    if(!QFileInfo::exists(c.launcher)||c.data.isEmpty())fail();
    const auto marker=asset_root(c)+"/owner";
    if(QFileInfo::exists(marker)&&!owns(c))fail();
#ifdef Q_OS_WIN
    if(c.programs.isEmpty())fail();
    QSettings registry(c.registry,c.registry_format);
    const auto registered_owner=registry.value(capabilities(c)+"/Owner").toString();
    if(!registered_owner.isEmpty()&&registered_owner!=owner(c))fail("registration belongs to another installation");
    for(const auto& t:types) {
        const auto existing=registry.value("Classes/"+progid(c,t)+"/Owner").toString();
        if(!existing.isEmpty()&&existing!=owner(c))fail("handler belongs to another installation");
    }
    if(QFileInfo::exists(shortcut(c))&&QFileInfo(shortcut(c)).symLinkTarget().compare(owner(c),Qt::CaseInsensitive)!=0)
        fail("shortcut belongs to another installation");
#endif
    write(marker,owner(c).toUtf8());
    const auto files=assets(c);
    for(auto it=files.cbegin();it!=files.cend();++it)write(it.key(),it.value());
#ifdef Q_OS_WIN
    const auto values=registry_values(c);
    for(auto it=values.cbegin();it!=values.cend();++it)registry.setValue(it.key(),it.value());
    registry.sync();if(registry.status()!=QSettings::NoError)fail("write registry");
    if(!QFileInfo::exists(shortcut(c))) {
        if(!QDir().mkpath(c.programs)||!QFile::link(c.launcher,shortcut(c)))fail("create shortcut");
    }
#endif
    refresh(c);
    if(!registered(c))fail("verify registration");
}
void remove(const Context& c) {
    if(!owns(c))return;
#ifdef Q_OS_WIN
    QSettings registry(c.registry,c.registry_format);
    for(const auto& t:types) {
        const auto id=progid(c,t),key="Classes/"+id;
        if(registry.value(key+"/Owner").toString()!=owner(c))continue;
        registry.remove(key);
        registry.remove("Classes/."+QString(t.extension)+"/OpenWithProgids/"+id);
    }
    if(registry.value(capabilities(c)+"/Owner").toString()==owner(c)) {
        registry.remove(capabilities(c));
        if(registry.value("RegisteredApplications/"+registration_name(c)).toString()==registry_values(c).value("RegisteredApplications/"+registration_name(c)))
            registry.remove("RegisteredApplications/"+registration_name(c));
    }
    registry.sync();if(registry.status()!=QSettings::NoError)fail();
    if(QFileInfo(shortcut(c)).symLinkTarget().compare(owner(c),Qt::CaseInsensitive)==0)erase(shortcut(c));
#endif
    const auto files=assets(c);
    for(auto it=files.cbegin();it!=files.cend();++it) {
#ifndef Q_OS_WIN
        // MIME icons are shared by installations; retain these small derived assets.
        if(it.key().contains("/icons/hicolor/"))continue;
#endif
        if(read(it.key())==it.value())erase(it.key());
    }
    erase(asset_root(c)+"/owner");refresh(c);
}
namespace {
class Page final:public QWidget {
public:
    Context context;QComboBox* action;QLabel* status;
    explicit Page(QWidget* parent,Context value=current_context()):QWidget(parent),context(std::move(value)) {
        setObjectName("desktopIntegrationPage");auto* layout=new QVBoxLayout(this);
        status=new QLabel(this);status->setWordWrap(true);layout->addWidget(status);
        auto* target=new QLabel(QDir::toNativeSeparators(context.launcher),this);target->setWordWrap(true);layout->addWidget(target);
        action=new QComboBox(this);action->setObjectName("desktopIntegrationAction");
        action->addItem(tr("Keep current registration"));action->addItem(tr("Register or repair"));action->addItem(tr("Remove this registration"));layout->addWidget(action);
        auto* note=new QLabel(tr("Register .prtz, .asmz, .drwz, .frmz, .tblz and .symz with their icons for this user. Existing default applications are preserved."),this);note->setWordWrap(true);layout->addWidget(note);
#ifdef Q_OS_WIN
        auto* defaults=new QPushButton(tr("Default applications"),this);layout->addWidget(defaults);
        connect(defaults,&QPushButton::clicked,this,[]{QDesktopServices::openUrl(QUrl("ms-settings:defaultapps"));});
#endif
        layout->addStretch();update_status();
    }
    void update_status() { status->setText(registered(context)?tr("Desktop integration is registered."):tr("Desktop integration is not registered or needs repair.")); }
    bool submit() {
        try {
            if(action->currentIndex()==1)install(context);
            if(action->currentIndex()==2)remove(context);
            action->setCurrentIndex(0);update_status();return true;
        } catch(...) { status->setText(tr("Desktop integration could not be updated. Check write permissions and try again."));return false; }
    }
};
class Offer final:public ui::PropertiesSubWindow {
    Page* page;
public:
    explicit Offer(QWidget* parent,const Context& context):PropertiesSubWindow(tr("Desktop integration"),parent) {
        setObjectName("desktopIntegrationOffer");
        setAttribute(Qt::WA_DeleteOnClose);page=new Page(this,context);page->action->setCurrentIndex(1);
        content_layout()->addWidget(page);set_initial_size({620,330});set_centered_on_show();
    }
    bool submit() override{return page->submit();}
};
}
QWidget* settings_page(QWidget* parent) {return new Page(parent);}
QWidget* settings_page(QWidget* parent,const Context& context) {return new Page(parent,context);}
bool submit_settings(QWidget* page) {return static_cast<Page*>(page)->submit();}
void offer_first_launch(QWidget* parent) {
    offer_first_launch(parent,current_context());
}
void offer_first_launch(QWidget* parent,const Context& c) {
    if(!c.installed||registered(c))return;
    QSettings preferences(asset_root(c)+"/offer.ini",QSettings::IniFormat);
    if(preferences.value("offered",false).toBool())return;
    preferences.setValue("offered",true);preferences.sync();
    if(preferences.status()!=QSettings::NoError)return;
    QTimer::singleShot(0,parent,[parent,c]{(new Offer(parent,c))->show();});
}
int command_line() {
    for(const auto& arg:QCoreApplication::arguments())if(arg.startsWith("--desktop-integration=")) {
        const auto action=arg.mid(QString("--desktop-integration=").size());
        try {
            const auto c=current_context();
            if(action=="register"||action=="repair")install(c);
            else if(action=="remove")remove(c);
            else if(action=="status")return registered(c)?0:1;
            else return 2;
            return 0;
        } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 2;}
    }
    return -1;
}
}
