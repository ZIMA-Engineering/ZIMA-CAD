#include <zima/command_host/host.hpp>
#include "../cli/runner.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <sstream>

using namespace zima;
namespace fs=std::filesystem;
using commands::Json;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
QString qpath(const fs::path& path){const auto text=path.generic_u8string();return QString::fromUtf8(reinterpret_cast<const char*>(text.data()),static_cast<qsizetype>(text.size()));}
fs::path path(const QString& text){return fs::u8path(text.toStdString());}
void write(const fs::path& path,const QByteArray& content){
    QFile file(qpath(path));require(file.open(QIODevice::WriteOnly),"Cannot write fixture");
    require(file.write(content)==content.size(),"Incomplete fixture write");
}
struct Run {
    int exit_code;QByteArray output,diagnostics;
    std::vector<Json> results() const{
        std::vector<Json> records;
        for(const auto& line:output.split('\n'))if(!line.isEmpty()){
            const auto value=Json::parse(line.toStdString());
            require(value.at("protocol")=="zima-cad.commands/1","Non-protocol text appeared on stdout");records.push_back(value);
        }
        return records;
    }
};
Run launch(const QString& executable,const fs::path& cwd,const QStringList& arguments,const QByteArray& input={}){
    QProcess process;process.setWorkingDirectory(qpath(cwd));
    auto environment=QProcessEnvironment::systemEnvironment();
    environment.insert("QT_QPA_PLATFORM","this-platform-does-not-exist");process.setProcessEnvironment(environment);
    process.start(executable,arguments);require(process.waitForStarted(10000),"CLI failed to start");
    if(!input.isEmpty()){require(process.write(input)==input.size(),"stdin write failed");process.waitForBytesWritten(10000);}
    process.closeWriteChannel();
    if(!process.waitForFinished(30000)){process.kill();process.waitForFinished();throw std::runtime_error("Owned CLI test process timed out");}
    require(process.exitStatus()==QProcess::NormalExit,"CLI process crashed");
    return {process.exitCode(),process.readAllStandardOutput(),process.readAllStandardError()};
}
QString command(const Json& json){return QString::fromStdString(json.dump());}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        require(argc==2,"Pass the CLI executable path");const auto executable=app.arguments().at(1);
        const auto repository=fs::current_path();
        QTemporaryDir temporary;require(temporary.isValid(),"No temporary test directory");
        const auto root=path(temporary.path());const auto project=root/fs::path(u8"projekt žluťoučký");fs::create_directory(project);
        auto result=launch(executable,root,{"--help"});require(result.exit_code==0&&result.output.contains("--stdin")&&result.diagnostics.isEmpty(),"CLI help failed");
        result=launch(executable,root,{"--command","documents"});require(result.exit_code==0&&result.results().size()==1&&result.results().front().at("data").empty(),"Empty workspace/default config discovery failed");
        for(const auto& arguments:std::vector<QStringList>{{},{"--unknown"},{"--command"},{"--command","new part forbidden","--stdin"},
            {"--help","--command","new part forbidden"},{"--working-directory",qpath(root/"missing"),"--command","help"},
            {"--config",qpath(root/"missing.ini"),"--command","help"},{"--script",qpath(root/"missing.txt")}}){
            result=launch(executable,root,arguments);
            require(result.exit_code==2&&result.output.isEmpty()&&!result.diagnostics.isEmpty(),"Invalid startup executed commands or returned success");
        }
        // Use QSettings-produced scalar config to compare actual GUI serialization
        // (quoted punctuation, spaces, UTF-8 and escaped Windows backslashes).
        const auto base=root/"base.ini";
        {
            QSettings config(qpath(base),QSettings::IniFormat);
            auto template_path=qpath(repository/"config/templates");
#ifdef _WIN32
            template_path.replace('/','\\');
#endif
            config.setValue("Paths/Templates",template_path);
            config.setValue("Paths/Localization",qpath(repository/"config/localization"));
            config.setValue("Application/Language","en");config.setValue("Units/Length","cm");config.sync();
        }
        const auto common=QStringList{"--working-directory",qpath(project),"--config",qpath(base)};
        result=launch(executable,root,common+QStringList{"--command","new part \"díl z příkazů\"","--command","save","--command","tree"});
        require(result.exit_code==0&&result.results().size()==3,"Repeated UTF-8 command arguments failed");
        const auto part_path=project/fs::path(u8"díl z příkazů.prtz");
        auto part=document::PartDocument::load(part_path);
        require(part.name=="díl z příkazů"&&part.document_units.at("Length")=="cm"&&part.body_history.bodies().size()==1,"CLI lost config units, template body or Unicode name");
        result=launch(executable,root,common+QStringList{"--command","save"});
        require(result.exit_code==1&&result.results().at(0).at("message")=="No document is open.","Configured English translation not used");
        // Local template path belongs to the project's config, not to base.ini.
        const auto local_templates=project/fs::path(u8"šablony; s mezerou");fs::create_directory(local_templates);
        fs::copy_file(repository/"config/templates/start_part.prtz",local_templates/"custom.prtz");
        const auto catalogue_directory=project/"catalogue";fs::create_directory(catalogue_directory);
        write(catalogue_directory/"en.ini",QByteArray("[QtTranslations]\nQObject|Těleso 1 = Body from context\nQMainWindow|Není otevřený dokument. = Context-specific empty document\n"));
        {
            QSettings local(qpath(project/"config.ini"),QSettings::IniFormat);
            local.setValue("Paths/Templates",qpath(local_templates.filename()));
            local.setValue("Paths/Localization","catalogue");
            local.setValue("Templates/Part","custom.prtz");local.setValue("Units/Length","m");
            local.setValue("Paths/Materials",QByteArray("unused GUI-only setting"));local.sync();
        }
        result=launch(executable,root,common+QStringList{"--command","new part local","--command","save"});
        require(result.exit_code==0&&document::PartDocument::load(project/"local.prtz").document_units.at("Length")=="m","Project config layer or relative template path ignored");
        require(document::PartDocument::load(project/"local.prtz").body_history.bodies().front().name=="Body from context","Body name did not use the GUI factory's translation context");
        result=launch(executable,root,common+QStringList{"--command","save"});
        require(result.exit_code==1&&result.results().front().at("message")=="Context-specific empty document","Command translation context differs from GUI");
        fs::remove(project/"config.ini");
        // Scripts use launch-directory paths; document paths follow working_directory.
        const auto script=root/fs::path(u8"dávka příkazů.txt");
        write(script,QByteArray::fromStdString("\xEF\xBB\xBF# batch\r\n\r\nnew assembly group\r\nsave\r\n{\"command\":\"new\",\"arguments\":{\"type\":\"drawing\",\"name\":\"list\"}}\r\nsave"));
        result=launch(executable,root,common+QStringList{"--script",qpath(script.filename())});
        require(result.exit_code==0&&result.results().size()==4,"BOM/CRLF/JSON script failed");
        require(assembly::AssemblyDocument::load(project/"group.asmz").name=="group"&&drawing::DrawingDocument::load(project/"list.drwz").name=="list","Native assembly/drawing output failed");
        const auto open_part=command({{"command","open"},{"arguments",{{"path","díl z příkazů.prtz"}}}});
        result=launch(executable,root,common+QStringList{"--stdin"},(open_part+"\ncontext\n").toUtf8());
        const auto records=result.results();require(result.exit_code==0&&records.size()==2&&records.back().at("data").at("active_document")==part.document_id&&records.back().at("data").at("selection").is_null(),"stdin context failed or invented a View selection");
        // An input process emits a result before EOF (usable for interactive pipes).
        QProcess streaming;streaming.setWorkingDirectory(qpath(root));streaming.start(executable,common+QStringList{"--stdin"});
        require(streaming.waitForStarted(10000),"Streaming process failed to start");streaming.write("documents\n");streaming.waitForBytesWritten();
        require(streaming.waitForReadyRead(10000),"Results buffered until EOF");const auto early=streaming.readAllStandardOutput();
        require(early.endsWith('\n')&&Json::parse(early.toStdString()).at("ok")==true&&streaming.state()==QProcess::Running,"Streaming result did not arrive independently of process exit");
        streaming.closeWriteChannel();require(streaming.waitForFinished(10000)&&streaming.exitCode()==0,"Streaming EOF failed");
        result=launch(executable,root,common+QStringList{"--stdin"},"new part unsaved\nunknown\nsave\n");
        require(result.exit_code==1&&result.results().size()==2&&!fs::exists(project/"unsaved.prtz"),"Failed batch continued or implicitly saved");
        result=launch(executable,root,common+QStringList{"--stdin","--keep-going"},"unknown\nnew part after_error\nsave\n");
        require(result.exit_code==1&&result.results().size()==3&&fs::exists(project/"after_error.prtz"),"keep-going lost error exit status or skipped valid commands");
        QByteArray oversized(70000,'x');oversized+="\ndocuments\n";
        result=launch(executable,root,common+QStringList{"--stdin","--keep-going"},oversized);
        require(result.exit_code==1&&result.results().size()==2&&result.results().front().at("code")=="request_too_large"&&result.results().back().at("ok")==true,"Overlong line was unbounded or split into commands");
        QByteArray invalid("new part ");invalid+=char(0xff);invalid+="\ndocuments\n";
        result=launch(executable,root,common+QStringList{"--stdin","--keep-going"},invalid);
        require(result.exit_code==1&&result.results().front().at("code")=="invalid_utf8"&&result.results().back().at("data").empty(),"Invalid UTF-8 changed the model before failure");
        result=launch(executable,root,common+QStringList{"--command","new part occupied"});require(result.exit_code==0&&!fs::exists(project/"occupied.prtz"),"Exit silently saved an unsaved document");
        fs::create_directory(project/"blocked.prtz");
        result=launch(executable,root,common+QStringList{"--stdin"},"new part blocked\nsave\n");require(result.exit_code==1,"Directory collision reported success");
        result=launch(executable,root,common+QStringList{"--stdin"},"open missing.prtz\nnew part later\nsave\n");
        if(result.exit_code!=1||result.results().size()!=1||fs::exists(project/"later.prtz")){
            std::cerr<<"Missing Open exit="<<result.exit_code<<" stdout="<<result.output.toStdString()<<" stderr="<<result.diagnostics.toStdString()<<'\n';
            throw std::runtime_error("Failed Open continued the batch");
        }
        // A disconnected result consumer must stop execution before a later Save.
        std::vector<std::string> output_failure;
        for(const auto& arg:common)output_failure.push_back(arg.toStdString());
        for(const auto* arg:{"--command","new part disconnected","--command","save"})output_failure.emplace_back(arg);
        std::istringstream no_input;std::ostringstream error_output;
        auto* previous_errors=std::cerr.rdbuf(error_output.rdbuf());
        const auto output_exit=cli::run(output_failure,path(executable),no_input,[](const std::string&){throw std::runtime_error("disconnected consumer");});
        std::cerr.rdbuf(previous_errors);
        require(output_exit==2&&!fs::exists(project/"disconnected.prtz")&&error_output.str().find("cli_error")!=std::string::npos,"Output failure did not stop before a later Save");
        // Open persisted geometry, explicitly recalculate and save it using CLI.
        auto model=document::PartDocument::create_default();auto box=document::PartDocument::create_box_container();box.box={10,20,30};model.history={box};
        kernel::OcctKernel kernel;const auto calculated=workspace::calculate_part(kernel,model);model.save(project/"box.prtz",calculated);
        result=launch(executable,root,common+QStringList{"--stdin"},"open box.prtz\nregenerate\nsave\n");
        std::vector<kernel::BodyResult> reloaded;const auto after=document::PartDocument::load(project/"box.prtz",&reloaded);
        require(result.exit_code==0&&result.results().size()==3&&after.history==model.history&&!reloaded.empty()&&std::abs(reloaded.back().volume-6000)<1e-6,"CLI regeneration/save changed geometry");
        const auto& old_refs=calculated.back().mesh.original_references.triangle_references;
        const auto& new_refs=reloaded.back().mesh.original_references.triangle_references;
        require(!old_refs.empty()&&old_refs.size()==new_refs.size(),"CLI save lost source reference geometry");
        for(std::size_t i=0;i<old_refs.size();++i)require(old_refs[i].owner_id==new_refs[i].owner_id&&old_refs[i].semantic_key==new_refs[i].semantic_key&&old_refs[i].instance_path==new_refs[i].instance_path,"CLI changed stable face identity");
        std::cout<<"CLI processes: native files, Unicode/config, scripts/stdin, errors, streaming and explicit geometry calculation passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
