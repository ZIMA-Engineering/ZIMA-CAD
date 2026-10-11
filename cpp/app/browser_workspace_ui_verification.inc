int verify_browser_workspace(QApplication& application) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    class WindowObserver final : public QObject {
    public:
        int hides{},surfaces{},ids{};
        bool eventFilter(QObject*,QEvent* event) override {
            if(event->type()==QEvent::Hide)++hides;
            if(event->type()==QEvent::PlatformSurface)++surfaces;
            if(event->type()==QEvent::WinIdChange)++ids;
            return false;
        }
    };
    try {
        QTemporaryDir directory;
        QTcpServer server;check(server.listen(QHostAddress::LocalHost),"Cannot start browser rendering HTTP fixture");
        QObject::connect(&server,&QTcpServer::newConnection,&server,[&] {
            while(auto* socket=server.nextPendingConnection()) {
                QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                QObject::connect(socket,&QTcpSocket::readyRead,socket,[socket] {
                    const auto request=socket->property("request").toByteArray()+socket->readAll();
                    socket->setProperty("request",request);if(!request.contains("\r\n\r\n"))return;
                    const QByteArray body="<html><head><title>Rendered browser fixture</title></head><body style='margin:0;background:#1d9a53'><div style='position:fixed;left:50%;top:0;right:0;bottom:0;background:#a32acf'></div></body></html>";
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();
                });
            }
        });
        QSettings settings(directory.filePath("config.ini"),QSettings::IniFormat);
        settings.setValue("Paths/WorkingDirectory",directory.path());
        settings.setValue("Browser/HomePage","");settings.sync();
        {
            zima::app::AssemblyWorkspaceWindow empty(directory.path(),directory.path());empty.resize(1366,768);empty.show();flush();
            const auto native=empty.winId();WindowObserver observer;
            empty.installEventFilter(&observer);empty.windowHandle()->installEventFilter(&observer);
            check(empty.execute_console_command("browser.open").ok,"Cannot open the first browser without a CAD document");flush();
            auto* empty_browser=empty.findChild<QWebEngineView*>("browserView");check(empty_browser,"Empty application's browser view missing");
            empty_browser->setUrl(QUrl(QString("http://127.0.0.1:%1/render").arg(server.serverPort())));
            QElapsedTimer loading;loading.start();
            while(empty_browser->title()!="Rendered browser fixture"&&loading.elapsed()<15000){flush();QThread::msleep(20);}
            check(empty_browser->title()=="Rendered browser fixture","Empty application's HTTP browser did not load");flush();
            check(empty.winId()==native&&observer.hides==0&&observer.surfaces==0&&observer.ids==0,"First browser recreated or hid the empty application's native window");
        }
        zima::app::AssemblyWorkspaceWindow window(directory.path(),directory.path());window.resize(1366,768);window.show();flush();
        check(window.execute_console_command("new part browser-context").ok,"Cannot create CAD context");
        check(window.execute_console_command("save").ok,"Cannot save CAD context");flush();
        auto* tabs=window.findChild<QTabBar*>("documentTabs");
        const int original=tabs->count();const auto document=tabs->tabData(tabs->currentIndex());
        auto* globe=window.findChild<QAction*>("newBrowserTabAction");check(globe,"Browser action missing");
        const auto native=window.winId();WindowObserver observer;
        window.installEventFilter(&observer);window.windowHandle()->installEventFilter(&observer);
        globe->trigger();flush();check(tabs->count()==original+1&&tabs->tabData(tabs->currentIndex()).toString().startsWith("web:"),"Browser is not an ordinary active tab");
        auto* browser=window.findChild<QWebEngineView*>("browserView");check(browser,"Browser view missing");
        browser->setUrl(QUrl(QString("http://127.0.0.1:%1/render").arg(server.serverPort())));
        const auto verify_rendered=[&] {
            if(application.platformName()=="offscreen") {std::cout<<"Browser native composition check skipped on offscreen platform\n";return;}
            QElapsedTimer rendering;rendering.start();bool rendered=false;
            while(!rendered&&rendering.elapsed()<15000) {
                flush();QThread::msleep(20);
                const auto screenshot=window.screen()->grabWindow(window.winId());
                const auto image=screenshot.toImage();const auto ratio=screenshot.devicePixelRatio();
                const auto sample=[&](double x) {
                    const auto position=browser->mapTo(&window,QPoint(int(browser->width()*x),browser->height()/2));
                    return image.pixelColor(int(position.x()*ratio),int(position.y()*ratio));
                };
                if(!image.isNull()&&browser->title()=="Rendered browser fixture")rendered=sample(.25)==QColor("#1d9a53")&&sample(.75)==QColor("#a32acf");
            }
            if(!rendered) {
                window.screen()->grabWindow(window.winId()).save("build/browser-render-failure.png");
                browser->grab().save("build/browser-widget-failure.png");
            }
            check(rendered,"Browser page loaded but its colored pixels were not composed in the CAD window");
        };
        verify_rendered();
        const auto original_palette=application.palette();
        auto light=original_palette;light.setColor(QPalette::Window,Qt::white);light.setColor(QPalette::WindowText,Qt::black);
        auto dark=light;dark.setColor(QPalette::Window,QColor("#262b30"));dark.setColor(QPalette::WindowText,Qt::white);
        for(const auto& palette:{light,dark,light}) {
            application.setPalette(palette);flush();verify_rendered();
            tabs->setCurrentIndex(0);flush();check(!browser->isVisible(),"CAD switch retained the browser surface");
            tabs->setCurrentIndex(1);flush();verify_rendered();
        }
        application.setPalette(original_palette);
        window.resize(1200,720);flush();verify_rendered();window.resize(1366,768);flush();verify_rendered();
        check(window.execute_console_command("browser.open").ok,"Desktop browser.open failed");flush();
        check(tabs->count()==original+2,"Multiple browser tabs not retained");
        window.findChild<QAction*>("globalSettingsAction")->trigger();flush();
        auto* dialog=window.findChild<QDialog*>("globalSettingsDialog");check(dialog,"Global settings missing");
        auto* home=dialog->findChild<QLineEdit*>("browserHomePage");check(home&&home->text().isEmpty(),"Blank home not preserved");
        home->setText("https://example.com/");dialog->reject();flush();
        check(zima::app::ApplicationSettings::load(directory.path()).browser.home.isEmpty(),"Cancel committed browser preferences");
        tabs->tabCloseRequested(tabs->currentIndex());flush();check(tabs->count()==original+1,"Closing browser changed CAD tabs");
        tabs->tabCloseRequested(tabs->currentIndex());flush();
        check(tabs->count()==original&&tabs->tabData(tabs->currentIndex())==document,"Last browser close did not restore CAD context");
        check(window.winId()==native&&observer.hides==0&&observer.surfaces==0&&observer.ids==0,"Browser tabs recreated or hid the CAD application's native window");
        const auto original_settings=zima::app::ApplicationSettings::load();
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            settings.setValue("Application/Language",language);
            settings.setValue("Paths/Localization",QDir::current().absoluteFilePath("config/localization"));settings.sync();
            const auto localized=zima::app::ApplicationSettings::load(directory.path());
            zima::app::apply_application_translations(application,localized);
            zima::app::GlobalSettingsDialog options(localized,&window);options.show();flush();
            auto* sections=options.findChild<QTabWidget*>("globalSettingsSections");check(sections,"Global settings sections missing");
            check(sections->tabBar()->width()>=sections->tabBar()->sizeHint().width(),"Global settings clipped the translated tab strip");
            std::cout<<"Global settings "<<language<<": window="<<options.width()<<", tabs="<<sections->tabBar()->width()<<", required="<<sections->tabBar()->sizeHint().width()<<'\n';
            check(options.width()<=window.width(),"Global settings exceeded the application width");options.reject();flush();
        }
        zima::app::apply_application_translations(application,original_settings);
        std::cout<<"Browser native pixels, stable windows, themes, tabs and five-language settings width passed\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
