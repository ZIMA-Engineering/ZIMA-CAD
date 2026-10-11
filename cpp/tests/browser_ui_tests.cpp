#include "../browser/browser.hpp"
#include "../browser/credentials.hpp"
#include "../app/resource_icon.hpp"
#include "../app/application_settings.hpp"
#include <qtkeychain/keychain.h>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QScopeGuard>
#include <QJsonObject>
#include <QMainWindow>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPalette>
#include <QSettings>
#include <QPushButton>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUuid>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QOpenGLWidget>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Predicate> void wait(Predicate predicate, const char* message) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < 15000) {
        QApplication::processEvents(); QThread::msleep(5);
    }
    check(predicate(), message);
}
QByteArray pixels(const QIcon& icon) {
    const auto image=icon.pixmap(24,24).toImage().convertToFormat(QImage::Format_ARGB32);
    return {reinterpret_cast<const char*>(image.constBits()),image.sizeInBytes()};
}
}

int main(int argc,char** argv) {
    QSurfaceFormat format;format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3,3);format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);format.setSamples(4);QSurfaceFormat::setDefaultFormat(format);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    // The alternate mode is a diagnostic negative control for the former
    // mixed-API startup, never a product configuration.
    bool default_api=false;for(int i=1;i<argc;++i)if(QByteArray(argv[i])=="--default-graphics-api")default_api=true;
    if(!default_api)QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QApplication application(argc,argv);
    try {
        QTemporaryDir directory;check(directory.isValid(),"No browser test directory");
        QString downloads=directory.filePath("first");QDir().mkpath(downloads);
        QMainWindow owner;owner.resize(1200,800);
        auto* cad_context=new QOpenGLWidget(&owner);cad_context->resize(100,100);owner.show();cad_context->show();
        wait([&]{return cad_context->isValid();},"CAD OpenGL context did not initialize");cad_context->hide();
        QTcpServer server;check(server.listen(QHostAddress::LocalHost),"Cannot start HTTP fixture");
        QObject::connect(&server,&QTcpServer::newConnection,&server,[&] {
            while(auto* socket=server.nextPendingConnection()) {
                QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                QObject::connect(socket,&QTcpSocket::readyRead,socket,[socket] {
                    auto bytes=socket->property("request").toByteArray()+socket->readAll();
                    socket->setProperty("request",bytes);
                    if(!bytes.contains("\r\n\r\n"))return;
                    const auto path=bytes.split(' ').value(1);
                    QByteArray body="<html><head><title>Browser fixture</title></head><body style='margin:0;background:#1d9a53'>Fixture<div style='position:fixed;left:50%;top:0;right:0;bottom:0;background:#a32acf'></div></body></html>";
                    QByteArray extra="Content-Type: text/html\r\n";
                    if(path=="/second")body="<html><head><title>Second fixture</title></head><body>Second</body></html>";
                    if(path=="/login")body="<html><head><title>Login fixture</title></head><body><form action='/login'><input name='username' autocomplete='username'><input name='password' type='password' autocomplete='current-password'></form></body></html>";
                    if(path=="/download") {
                        body="zima-browser-download";
                        extra="Content-Type: application/octet-stream\r\nContent-Disposition: attachment; filename=fixture.txt\r\n";
                    }
                    socket->write("HTTP/1.1 200 OK\r\n"+extra+"Content-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
                    socket->disconnectFromHost();
                });
            }
        });
        const auto base=QString("http://127.0.0.1:%1").arg(server.serverPort());
        zima::browser::Runtime runtime(directory.filePath("www"),[&]{return downloads;},&owner);
        const auto pages_cleanup=qScopeGuard([&]{delete owner.takeCentralWidget();});
        zima::browser::Preferences preferences;preferences.home=base+"/home";runtime.configure(preferences);
        QString error;check(runtime.bookmarks.reload(&error),"Cannot initialize GUI bookmarks");
        auto* tab=new zima::browser::Tab(runtime,&owner);owner.setCentralWidget(tab);owner.show();
        auto* toolbar=tab->findChild<QToolBar*>("browserNavigation");
        check(toolbar && toolbar->actions().front()->objectName()=="browserHome","Home is not the first navigation action");
        tab->navigate(QUrl(base+"/home"));
        wait([&]{return tab->title()=="Browser fixture";},"HTTP page did not load");
        auto* rendered_view=tab->findChild<QWebEngineView*>("browserView");
        wait([&]{
            const auto image=rendered_view->grab().toImage();const auto scale=image.devicePixelRatio();
            return !image.isNull()&&image.pixelColor(int(rendered_view->width()*.25*scale),int(rendered_view->height()*.5*scale))==QColor("#1d9a53")
                &&image.pixelColor(int(rendered_view->width()*.75*scale),int(rendered_view->height()*.5*scale))==QColor("#a32acf");
        },"Browser HTTP content did not render beside the CAD OpenGL context");
        auto* tree=tab->findChild<QTreeWidget*>("browserBookmarks");
        check(tree && tree->topLevelItemCount()==1,"Bookmark Tree missing");
        const auto bookmark=runtime.bookmarks.add("Fixture",tab->url(),{},false,&error);
        check(!bookmark.isEmpty()&&tree->topLevelItemCount()==2,"GUI bookmark did not update");
        check(tab->findChild<QAction*>("browserPin")->isChecked(),"Saved-page pin is not checked");
        bool bookmark_dialog_seen=false;
        QTimer::singleShot(0,&owner,[&]{
            if(auto* dialog=owner.findChild<QDialog*>("browserBookmarkDialog")) {
                bookmark_dialog_seen=true;dialog->findChild<QLineEdit*>("browserBookmarkName")->setText("Cancelled edit");dialog->reject();
            }
        });
        tab->findChild<QAction*>("browserPin")->trigger();
        wait([&]{return bookmark_dialog_seen;},"Bookmark dialog did not open");
        check(bookmark_dialog_seen&&runtime.bookmarks.items().last().toObject().value("name")=="Fixture","Bookmark Cancel changed persisted data");
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QTimer::singleShot(0,&owner,[&]{
            if(auto* dialog=owner.findChild<QDialog*>("browserBookmarkDialog")) {
                dialog->findChild<QLineEdit*>("browserBookmarkName")->setText("Edited fixture");
                auto* view=tab->findChild<QWebEngineView*>("browserView");
                QMouseEvent confirm(QEvent::MouseButtonDblClick,QPointF(20,20),QPointF(view->mapToGlobal(QPoint(20,20))),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
                QApplication::sendEvent(view,&confirm);
            }
        });
        tab->findChild<QAction*>("browserPin")->trigger();
        wait([&]{return runtime.bookmarks.items().last().toObject().value("name")=="Edited fixture";},"Bookmark MMB confirmation over web view did not save");
        check(runtime.bookmarks.items().last().toObject().value("name")=="Edited fixture","Bookmark MMB confirmation over web view did not save");
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        tab->navigate(QUrl(base+"/second"));
        wait([&]{return tab->title()=="Second fixture";},"Second page did not load");
        wait([&]{return tab->findChild<QAction*>("browserBack")->isEnabled();},"Back did not become available");
        tab->findChild<QAction*>("browserBack")->trigger();
        wait([&]{return tab->url().path()=="/home";},"Back failed");
        wait([&]{return tab->findChild<QAction*>("browserForward")->isEnabled();},"Forward did not become available");
        tab->findChild<QAction*>("browserForward")->trigger();
        wait([&]{return tab->url().path()=="/second";},"Forward failed");
        bool home_loaded=false;
        const auto home_connection=QObject::connect(tab->page(),&QWebEnginePage::loadFinished,&application,[&](bool ok){home_loaded=ok;});
        tab->findChild<QAction*>("browserHome")->trigger();
        wait([&]{return tab->url().path()=="/home";},"Home failed");
        wait([&]{return home_loaded;},"Home did not finish loading");
        QObject::disconnect(home_connection);
        downloads=directory.filePath("second");QDir().mkpath(downloads);
        tab->page()->runJavaScript("var a=document.createElement('a');a.href='/download';document.body.appendChild(a);a.click();");
        wait([&]{return QFile::exists(downloads+"/fixture.txt");},"Download ignored changed Working Directory");
        QFile download(downloads+"/fixture.txt");check(download.open(QIODevice::ReadOnly),"Cannot read downloaded file");
        wait([&]{return download.size()==21;},"Download did not finish");
        check(download.readAll()=="zima-browser-download","Download contents changed");
        check(!QFile::exists(directory.filePath("first/fixture.txt")),"Download used previous Working Directory");
        std::unique_ptr<zima::browser::Tab> additional_tab;
        runtime.new_page=[&]() -> QWebEnginePage* {additional_tab=std::make_unique<zima::browser::Tab>(runtime);return additional_tab->page();};
        tab->page()->runJavaScript("window.open('/second','_blank');");
        wait([&]{return additional_tab&&additional_tab->title()=="Second fixture";},"Website popup did not create another web tab");
        check(additional_tab->page()->profile()==tab->page()->profile(),"Web tabs do not share the browser profile");
        additional_tab.reset();runtime.new_page={};
        auto light=application.palette();light.setColor(QPalette::WindowText,Qt::black);light.setColor(QPalette::Window,Qt::white);
        application.setPalette(light);application.processEvents();
        const auto icon=zima::app::resource_icon("web-home");const auto light_pixels=pixels(icon);
        auto dark=light;dark.setColor(QPalette::WindowText,Qt::white);dark.setColor(QPalette::Window,Qt::black);
        application.setPalette(dark);application.processEvents();
        check(pixels(icon)!=light_pixels,"Existing browser icon ignored live dark theme");
        application.setPalette(light);application.processEvents();
        check(pixels(icon)==light_pixels,"Browser icon failed Light/Dark/Light round-trip");
        QVariantMap request{{"origin",base},{"actionOrigin",base},{"actionPath","/login"},{"requestId","fixture"}};
        check(zima::browser::Credentials::valid_request(QUrl(base+"/home"),request),"Same-origin localhost credential request rejected");
        request["actionOrigin"]="https://example.org";
        check(!zima::browser::Credentials::valid_request(QUrl(base+"/home"),request),"Cross-origin credential request accepted");
        request["origin"]=request["actionOrigin"]="http://example.org";
        check(!zima::browser::Credentials::valid_request(QUrl("http://example.org/login"),request),"Plain HTTP credential request accepted");
        check(zima::browser::Credentials::origin(QUrl("https://EXAMPLE.org:443/a"))=="https://example.org","Origin normalization failed");
        const auto account_id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto key="browser/credential/"+account_id;
        QKeychain::WritePasswordJob write("ZIMA-CAD");write.setAutoDelete(false);write.setInsecureFallback(false);
        write.setKey(key);write.setTextData("fixture-secret");bool finished=false;
        QObject::connect(&write,&QKeychain::Job::finished,&application,[&]{finished=true;});write.start();
        wait([&]{return finished;},"System credential write timed out");check(write.error()==QKeychain::NoError,"System credential store unavailable");
        QKeychain::ReadPasswordJob read("ZIMA-CAD");read.setAutoDelete(false);read.setInsecureFallback(false);read.setKey(key);finished=false;
        QObject::connect(&read,&QKeychain::Job::finished,&application,[&]{finished=true;});read.start();
        wait([&]{return finished;},"System credential read timed out");const bool secret_matches=read.error()==QKeychain::NoError&&read.textData()=="fixture-secret";
        QSettings metadata(directory.filePath("www/credentials.ini"),QSettings::IniFormat);
        metadata.beginGroup("Accounts/"+account_id);
        metadata.setValue("type","form");metadata.setValue("origin",base);
        metadata.setValue("actionOrigin",base);metadata.setValue("actionPath","/login");metadata.setValue("username","fixture-user");
        metadata.endGroup();metadata.sync();
        request={{"origin",base},{"actionOrigin",base},{"actionPath","/login"},{"requestId","fixture"},{"username","fixture-user"}};
        bool autofilled=false;QVariantMap autofill;
        runtime.credentials()->autofill(tab->page(),request,[&](QVariantMap result){autofill=result;autofilled=true;});
        wait([&]{return autofilled;},"Credential autofill timed out");
        const bool autofill_matches=autofill.value("username")=="fixture-user"&&autofill.value("password")=="fixture-secret";
        tab->navigate(QUrl(base+"/login"));
        wait([&]{return tab->title()=="Login fixture";},"Login form did not load");
        bool form_filled=false,query_pending=false;
        wait([&]{
            if(!query_pending&&!form_filled){query_pending=true;
                tab->page()->runJavaScript("document.querySelector('[name=password]')?.value || ''",[&](const QVariant& value){form_filled=value.toString()=="fixture-secret";query_pending=false;});
            }
            return form_filled;
        },"Isolated-world password bridge did not fill the login form");
        QKeychain::DeletePasswordJob remove("ZIMA-CAD");remove.setAutoDelete(false);remove.setInsecureFallback(false);remove.setKey(key);finished=false;
        QObject::connect(&remove,&QKeychain::Job::finished,&application,[&]{finished=true;});remove.start();
        wait([&]{return finished;},"System credential deletion timed out");
        metadata.remove("Accounts/"+account_id);metadata.sync();
        check(remove.error()==QKeychain::NoError&&secret_matches&&autofill_matches,"System credential and autofill round-trip failed");
        runtime.show_passwords();application.processEvents();
        auto* manager=owner.findChild<QDialog*>("browserPasswordsDialog");
        check(manager && (manager->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"Password manager is not an internal Properties window");
        manager->reject();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        const auto catalogs=QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).absoluteFilePath("../../config/localization");
        for(const auto& language:QStringList{"cs","en","de","fr","ru"}) {
            QSettings configuration(directory.filePath("config.ini"),QSettings::IniFormat);
            configuration.setValue("Application/Language",language);configuration.setValue("Paths/Localization",catalogs);
            configuration.setValue("Browser/HomePage",QString{});configuration.sync();
            const auto settings=zima::app::ApplicationSettings::load(directory.path());
            check(settings.browser.home.isEmpty(),"Blank browser home did not survive settings load");
            zima::app::apply_application_translations(application,settings);
            zima::browser::Tab localized(runtime);
            check(localized.findChild<QAction*>("browserHome")->text()==settings.qt_translations.value("Home"),"Browser navigation language mismatch");
            check(localized.findChild<QAction*>("browserPin")->text()==settings.qt_translations.value("Save bookmark"),"Bookmark pin language mismatch");
            metadata.setValue("NeverSaveOrigins",QStringList{base});metadata.sync();
            runtime.show_passwords();application.processEvents();
            auto* localized_manager=owner.findChild<QDialog*>("browserPasswordsDialog");
            check(localized_manager->windowTitle()==settings.qt_translations.value("Saved passwords"),"Password manager language mismatch");
            localized_manager->findChild<QTableWidget*>("browserPasswordAccounts")->selectRow(0);
            auto* remove_choice=localized_manager->findChild<QPushButton*>("browserPasswordRemove");
            check(remove_choice->text()==settings.qt_translations.value("Allow password saving for this website"),"Never-save removal action has wrong language or meaning");
            remove_choice->click();metadata.sync();
            check(metadata.value("NeverSaveOrigins").toStringList().isEmpty(),"Never-save choice was not removed");
            localized_manager->reject();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        }
        delete owner.takeCentralWidget();
        std::cout<<"Browser navigation, bookmarks, downloads, themes, credential origin and system storage contracts passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
