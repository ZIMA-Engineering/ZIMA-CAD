#include "ai_fixture.hpp"
#include "codexprovider.h"
#include "command_console.hpp"
#include "ai_settings_page.hpp"
#include <zima/command_host/host.hpp>
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTemporaryDir>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QKeyEvent>
#include <QSettings>
#include <QScopeGuard>
#include <iostream>
#include <stdexcept>
using namespace zima;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void waitFor(const std::function<bool()>& predicate){QElapsedTimer timer;timer.start();while(!predicate() && timer.elapsed()<10000){QCoreApplication::processEvents();QThread::msleep(5);}check(predicate(),"AI protocol fixture timed out");}
QJsonObject request(const char* command,QJsonObject args={}){return {{"command",command},{"arguments",args},{"reason","Test requested operation"}};}
void verifyExecutableRecovery(const QString& directory) {
    const auto current=QCoreApplication::applicationFilePath();
    check(findCodexExecutable(current)==current,"Existing selected executable was replaced");
    const auto custom=directory+"/custom-codex-missing.exe";
    check(findCodexExecutable(custom)==custom,"Missing custom selection was silently replaced");
#ifdef Q_OS_WIN
    const auto local=directory+"/local";
    const auto bins=local+"/OpenAI/Codex/bin/";
    check(QDir().mkpath(bins+"current"),"Executable fixture directory failed");
    const auto installed=bins+"current/codex.exe";
    QFile executable(installed);check(executable.open(QIODevice::WriteOnly),"Executable fixture failed");executable.close();
    const auto old_path=qgetenv("PATH"),old_local=qgetenv("LOCALAPPDATA");
    const auto restore=qScopeGuard([&]{qputenv("PATH",old_path);qputenv("LOCALAPPDATA",old_local);});
    qputenv("PATH",{});qputenv("LOCALAPPDATA",local.toUtf8());
    check(findCodexExecutable()==installed,"Desktop executable discovery failed without PATH");
    const auto stale=bins+"retired/codex.exe";
    check(findCodexExecutable(stale)==installed,"Retired desktop executable did not recover");
    check(findCodexExecutable(installed)==installed,"Existing managed selection was replaced");
    check(findCodexExecutable(bins+"retired/other.exe")==bins+"retired/other.exe","Non-Codex filename was replaced");
    check(findCodexExecutable(bins+"retired/nested/codex.exe")==bins+"retired/nested/codex.exe","Unmanaged nested location was replaced");
    const auto config=directory+"/recovery-config.ini";
    check(CadAi::savePreferences({stale,"saved-model"},config,nullptr),"Recovery preferences failed");
    FakeCadAi provider;
    {
        app::AiSettingsPage page(config,nullptr,&provider);
        check(page.values().executable==installed&&page.values().model=="saved-model","Settings did not offer recovered executable or preserve model");
        check(provider.connections==0,"Executable discovery started Codex automatically");
        page.findChild<QPushButton*>("aiConnect")->click();
        check(provider.connections==1&&provider.executable==installed,"Connect did not use recovered executable");
    }
    check(CadAi::preferences(config).executable==stale,"Opening/closing Settings saved recovered preference before OK");
#endif
}
void verifyProvider(const QString& directory) {
    CodexProvider provider(nullptr,directory+"/profile");int answers=0,failures=0,tools=0,logins=0;QString thread;
    QObject::connect(&provider,&AiProvider::answer,[&](const QString& text){++answers;thread=text;});
    QObject::connect(&provider,&AiProvider::failed,[&]{++failures;});
    QObject::connect(&provider,&AiProvider::loginUrl,[&](const QUrl& url){check(url.host()=="auth.openai.com","Wrong sign-in URL");++logins;});
    QObject::connect(&provider,&AiProvider::toolRequested,[&](const QString& id,const QString& name,const QJsonObject&){check(name=="cad_context","Wrong dynamic tool");++tools;provider.toolResult(id,{{"ok",true}},true);});
    check(!provider.connected() && !provider.busy(),"Provider connected automatically");
    const auto connect=[&]{provider.connectAccount(QCoreApplication::applicationFilePath());waitFor([&]{return provider.ready()&&!provider.models().isEmpty();});};
    connect();
    QJsonObject context{{"currentDirectory",directory},{"cad",QJsonObject{{"active_document","part"},{"displayed_document","part"}}}};
    const auto ask=[&](const char* text){provider.ask(text,context,"fixture");};
    ask("Read context");waitFor([&]{return answers==1;});const auto first=thread;
    ask("Same tab");waitFor([&]{return answers==2;});check(thread==first,"Same document lost conversation");
    context["cad"]=QJsonObject{{"active_document","assembly"},{"displayed_document","assembly"}};
    ask("Different tab, same folder");waitFor([&]{return answers==3;});check(thread!=first,"New tab reused another document's conversation");const auto second=thread;
    context["cad"]=QJsonObject{{"active_document","part"},{"displayed_document","assembly"},{"active_occurrence","assembly/part-2"}};
    ask("Active occurrence");waitFor([&]{return answers==4;});check(thread!=second && tools==4,"Occurrence context failed");
    ask("NATIVE_TOOL");waitFor([&]{return failures==1;});check(!provider.connected(),"Unsupported tool did not close provider");
    connect();ask("INVALID_JSON");waitFor([&]{return failures==2;});
    connect();ask("WAIT_FOREVER");provider.cancel();check(!provider.busy()&&!provider.connected(),"Stop left a running request");
    connect();provider.logout();waitFor([&]{return !provider.busy()&&!provider.ready();});provider.login();waitFor([&]{return logins==1&&provider.ready();});
    provider.cancel();
}
void verifyHostAndConsole(const QString& directory) {
    workspace::Workspace live;kernel::OcctKernel kernel;std::filesystem::path working=directory.toStdString();
    command_host::Interaction interaction;command_host::Options options;options.interaction=[&]{return interaction;};
    options.settings=[]{return command_host::Settings{{std::filesystem::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body 1"},{}};};
    command_host::Host host(live,kernel,working,options);
    int mutations=0;
    const auto execute=[&](const QString& text){auto result=host.execute_text(text.toStdString());if(host.change())++mutations;return result;};
    const auto run=[&](const QString& text){auto result=execute(text);if(!result.ok)throw std::runtime_error(result.code+": "+result.message);return result;};
    const auto snapshot=[&]{
        const auto cad=QJsonDocument::fromJson(QByteArray::fromStdString(run("context").data.dump())).object();
        return QJsonObject{{"cad",cad},{"documents",QJsonDocument::fromJson(QByteArray::fromStdString(run("documents").data.dump())).array()},
            {"currentDirectory",cad["working_directory"]},{"language","en"}};
    };
    run("new part AI-part");const auto part=live.active_document_id();
    CadAi::CommandSession session(execute,snapshot);
    check(session.begin()["activeDocument"].toObject()["type"]=="part","Missing active Part metadata");
    check(CadAi::instructions().contains("INPUTS")&&CadAi::instructions().contains("MEANS"),"Engineering reasoning resource missing");
    auto page=session.call("cad_help",{{"search",""},{"offset",0}});check(page.success&&page.data["commands"].toArray().size()==12,"Catalog pagination failed");
    auto catalog=session.call("cad_help",{{"search","twisted_sheet.create"},{"offset",0}});check(catalog.success&&!catalog.data["commands"].toArray().isEmpty(),"Real command catalog is unavailable");
    QJsonObject boxArgs;for(const auto& v:catalog.data["commands"].toArray().first().toObject()["arguments"].toArray()){
        const auto arg=v.toObject();if(arg["required"].toBool())boxArgs[arg["name"].toString()]=arg["type"]=="string"?QJsonValue("10"):QJsonValue(10);
    }
    const auto box=request("twisted_sheet.create",boxArgs);const int before=mutations;
    check(session.call("cad_command",box).approval&&mutations==before,"Mutation ran before approval");
    check(session.call("cad_context",{}).data["code"]=="approval_pending","Concurrent tool bypassed review");
    check(session.decide(false).data["code"]=="denied"&&mutations==before,"Deny modified CAD");
    check(session.call("cad_command",box).approval,"Review missing");
    const auto approved=session.decide(true);
    if(!approved.success)std::cerr<<QJsonDocument(approved.data).toJson().constData()<<QJsonDocument(boxArgs).toJson().constData();
    check(approved.success&&mutations==before+1,"Approved command did not reach real CAD host");
    check(!session.decide(true).success&&mutations==before+1,"Approval executed twice");
    check(session.call("cad_context",{}).success,"Approved changes invalidated subsequent queries");
    check(session.call("cad_command",request("undo")).approval&&session.decide(true).success,"AI changes are not undoable");
    check(session.call("cad_command",request("fabricated.command")).data["code"]=="unknown_command","Unknown command accepted");
    session.call("cad_command",box);run("twisted_sheet.create 20 30 45 1 0");check(session.decide(true).data["code"]=="context_changed","Manual geometry changes did not invalidate review");
    session.begin();session.call("cad_command",box);interaction.selection={{"owner_id","other"}};
    check(session.decide(true).data["code"]=="context_changed","Selection change did not invalidate review");interaction.selection=nullptr;
    session.begin();session.call("cad_command",box);interaction.active_occurrence="root/part";
    check(session.decide(true).data["code"]=="context_changed","Occurrence change did not invalidate review");interaction.active_occurrence.clear();
    session.begin();session.call("cad_command",box);interaction.hover={{"owner_id","hover"}};
    check(session.decide(true).success,"Hover invalidated an otherwise unchanged review");
    for(const auto* type:{"assembly","drawing"}){
        run("activate "+QString::fromStdString(part));session.begin();session.call("cad_command",box);
        run(QString("new %1 AI-%1").arg(type));
        const auto count=mutations;check(session.decide(true).data["code"]=="context_changed"&&mutations==count,"Switching tabs redirected AI mutation");
        check(session.begin()["activeDocument"].toObject()["type"]==type,"New request did not follow active tab");
    }
    run("activate "+QString::fromStdString(part));
    session.begin();
    check(session.call("cad_command",request("new",{{"type","part"},{"name","AI-new-target"}})).approval,"New document must be reviewed");
    check(session.decide(true).success,"Approved new document failed");
    check(session.call("cad_command",box).data["activeDocument"].toObject()["name"]=="AI-new-target","Subsequent review named the previous target");
    session.cancel();run("activate "+QString::fromStdString(part));
    FakeCadAi provider;int settingsShown=0;
    app::CommandConsole::AiOptions ai{snapshot,[&]{++settingsShown;},[]{return CadAi::Preferences{};},&provider};
    app::CommandConsole console(execute,ai);console.resize(850,600);console.show();QCoreApplication::processEvents();
    auto* input=console.findChild<QLineEdit*>("commandConsoleInput");auto* allow=console.findChild<QPushButton*>("consoleAiAllow");
    const auto enter=[&](const QString& value){input->setText(value);QKeyEvent event(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(input,&event);QCoreApplication::processEvents();};
    enter("codex");enter("Create a test box");check(provider.asks==1&&provider.context["activeDocument"].toObject()["id"].toString().toStdString()==part,"Console lost tab context");
    const auto unchanged=mutations;emit provider.toolRequested("one","cad_command",box);
    check(allow->isVisible()&&mutations==unchanged,"Review was not inline or ran immediately");
    enter("draft while AI works");check(input->text()=="draft while AI works"&&provider.asks==1,"Busy draft was cleared or sent");
    allow->click();check(provider.result["ok"].toBool()&&mutations==unchanged+1,"Inline approval failed");
    provider.done();check(input->text()=="draft while AI works"&&provider.asks==1,"Draft was automatically sent after completion");
    enter("Another box");emit provider.toolRequested("two","cad_command",box);
    check(console.grab().save(directory+"/ai-console-review.png"),"AI screenshot failed");
    console.findChild<QPushButton*>("consoleAiStop")->click();allow->click();check(mutations==unchanged+1&&!allow->isVisible(),"Stop left an executable stale review");
    enter("/exit");enter("documents");check(!console.findChild<QPlainTextEdit*>("commandConsoleOutput")->toPlainText().isEmpty(),"Manual console broken");
    const auto config=directory+"/config.ini";QSettings settings(config,QSettings::IniFormat);settings.setValue("Unrelated/Value","preserve");settings.sync();
    QString error;check(CadAi::savePreferences({"saved-executable","saved-model"},config,&error),"AI preferences could not be saved");
    app::AiSettingsPage pageUi(config,&console,&provider);check(provider.connections==0,"Opening Settings connected automatically");
    check(pageUi.values().model=="saved-model","Model list replaced saved preference");
    pageUi.findChild<QLineEdit*>("aiExecutable")->setText("pending-executable");
    check(CadAi::preferences(config).executable=="saved-executable","Editing Settings saved before OK");
    check(CadAi::savePreferences(pageUi.values(),config,&error),"Preferences commit failed");
    check(QSettings(config,QSettings::IniFormat).value("Unrelated/Value")=="preserve","AI settings damaged unrelated configuration");
}
}
int main(int argc,char** argv) {
    if(argc>1&&QString::fromLocal8Bit(argv[1])=="app-server"){QCoreApplication app(argc,argv);return runCadAiFixture();}
    if(argc>=3&&QString::fromLocal8Bit(argv[1])=="--probe-codex") {
        QApplication app(argc,argv);app.setApplicationName("ZIMA-CAD");
        const bool live=QStringList(app.arguments()).contains("--live");
        const auto profile=argc>=4&&QString::fromLocal8Bit(argv[3])!="--live"?QString::fromLocal8Bit(argv[3]):QString{};
        CodexProvider provider(nullptr,profile);
        QString failure;
        QObject::connect(&provider,&AiProvider::failed,[&](const QString& text){failure=text;});
        provider.connectAccount(QString::fromLocal8Bit(argv[2]));
        QElapsedTimer timer;timer.start();
        while(provider.busy()&&timer.elapsed()<35000){QCoreApplication::processEvents();QThread::msleep(5);}
        if(provider.ready()) {
            timer.restart();
            while(provider.models().isEmpty()&&failure.isEmpty()&&timer.elapsed()<5000){QCoreApplication::processEvents();QThread::msleep(5);}
        }
        std::cout<<QJsonDocument(QJsonObject{{"connected",provider.connected()},{"signedIn",provider.ready()},
            {"modelCount",provider.models().size()},{"status",provider.status()},{"failure",failure},
            {"timedOut",provider.busy()}}).toJson(QJsonDocument::Compact).constData()<<'\n';
        if(!failure.isEmpty()||provider.busy())return 1;
        if(!live)return 0;
        if(!provider.ready()){std::cerr<<"CAD account is signed out; live inference was not attempted.\n";return 2;}
        QTemporaryDir temporary;
        workspace::Workspace workspace;kernel::OcctKernel kernel;std::filesystem::path working=temporary.path().toStdString();
        command_host::Options options;
        options.settings=[]{return command_host::Settings{{std::filesystem::absolute("config/templates"),"START_PART.prtz","START_ASSEMBLY.asmz","Body 1"},{}};};
        command_host::Host host(workspace,kernel,working,options);
        const auto execute=[&](const QString& text){return host.execute_text(text.toStdString());};
        const auto created=execute("new part AI-Integration-Diagnostic");
        if(!created.ok){std::cerr<<created.code<<'\n';return 1;}
        const auto snapshot=[&] {
            const auto cad=QJsonDocument::fromJson(QByteArray::fromStdString(execute("context").data.dump())).object();
            return QJsonObject{{"cad",cad},{"documents",QJsonDocument::fromJson(QByteArray::fromStdString(execute("documents").data.dump())).array()},
                {"currentDirectory",cad["working_directory"]},{"language","cs"}};
        };
        CadAi::CommandSession session(execute,snapshot);
        int contexts=0,catalogs=0,denied=0;bool answered=false;
        QObject::connect(&provider,&AiProvider::toolRequested,[&](const QString& id,const QString& name,const QJsonObject& args) {
            auto reply=session.call(name,args);
            if(reply.approval){++denied;reply=session.decide(false);}
            if(name=="cad_context"&&reply.success)++contexts;
            if(name=="cad_help"&&reply.success)++catalogs;
            provider.toolResult(id,reply.data,reply.success);
        });
        QObject::connect(&provider,&AiProvider::answer,[&](const QString& answer){answered=true;std::cout<<"AI diagnostic answer: "<<answer.toStdString()<<'\n';});
        const auto before=workspace.open_part(workspace.active_document_id())->session.document().serialized();
        provider.ask("Read-only integration diagnostic: call cad_context once and cad_help with search documents and offset 0. Then reply briefly in Czech naming the active diagnostic Part and whether the command catalog worked. Do not propose any model or file changes.",session.begin(),{});
        timer.restart();
        while(provider.busy()&&failure.isEmpty()&&timer.elapsed()<180000){QCoreApplication::processEvents();QThread::msleep(5);}
        const bool unchanged=before==workspace.open_part(workspace.active_document_id())->session.document().serialized();
        std::cout<<QJsonDocument(QJsonObject{{"answered",answered},{"contextCalls",contexts},{"catalogCalls",catalogs},
            {"deniedChanges",denied},{"documentUnchanged",unchanged},{"failure",failure},{"timedOut",provider.busy()}}).toJson(QJsonDocument::Compact).constData()<<'\n';
        return answered&&contexts>0&&catalogs>0&&unchanged&&failure.isEmpty()&&!provider.busy()?0:1;
    }
    QApplication app(argc,argv);QTemporaryDir temporary;
    try{
        check(temporary.isValid(),"Temporary directory failed");
        verifyExecutableRecovery(temporary.path());verifyProvider(temporary.path());verifyHostAndConsole(temporary.path());
        if(qEnvironmentVariableIsSet("ZIMA_AI_SCREENSHOT"))QFile::copy(temporary.filePath("ai-console-review.png"),qEnvironmentVariable("ZIMA_AI_SCREENSHOT"));
        std::cout<<"AI protocol, context binding, command approval, real CAD history, console and settings contracts passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
