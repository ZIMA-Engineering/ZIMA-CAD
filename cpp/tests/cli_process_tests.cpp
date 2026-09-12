#include <zima/command_host/host.hpp>
#include <zima/document/file_path.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/interchange/step_model.hpp>
#include <zima/interchange/dxf.hpp>
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
        result=launch(executable,root,common+QStringList{"--stdin"},"new part commanded-box\nbox.create 10 20 30\nsave\n");
        require(result.exit_code==0 && result.results().size()==3,"CLI could not calculate a new Box");
        const auto box_id=result.results()[1].at("data").at("container").get<std::string>();
        const auto new_box=document::PartDocument::load(project/"commanded-box.prtz",&reloaded);
        require(new_box.find_container(box_id) && std::abs(reloaded.back().volume-6000)<1e-6,"CLI Box creation did not persist real geometry");
        result=launch(executable,root,common+QStringList{"--stdin"},QByteArray::fromStdString("open commanded-box.prtz\nreference.list "+box_id+" face\n"));
        require(result.exit_code==0 && result.results().back().at("data").at("total")==6,"CLI could not list original faces");
        const auto original_face=result.results().back().at("data").at("items")[0];
        result=launch(executable,root,common+QStringList{"--command","open commanded-box.prtz","--command",command({{"command","reference.get"},{"arguments",original_face}})});
        require(result.exit_code==0 && result.results().back().at("data").at("surface").at("kind")=="plane" &&
            result.results().back().at("data").at("triangle_count")==2,"CLI lost persisted analytic face details");
        const auto batch="open commanded-box.prtz\nbox.set "+box_id+" 15\nbox.get "+box_id+"\nundo\nbox.get "+box_id+"\nredo\nsave\n";
        result=launch(executable,root,common+QStringList{"--stdin"},QByteArray::fromStdString(batch));
        const auto edits=result.results();
        require(result.exit_code==0 && edits.size()==7 && edits[2].at("data").at("length_mm")==15 && edits[4].at("data").at("length_mm")==10,"CLI Box patch/query/Undo/Redo failed");
        const auto resized=document::PartDocument::load(project/"commanded-box.prtz",&reloaded);
        require(resized.find_container(box_id)->feature_id==new_box.find_container(box_id)->feature_id && std::abs(reloaded.back().volume-9000)<1e-6,"CLI Box resize replaced identity or saved wrong geometry");
        for(const auto* primitive:{"cylinder.create 3 6","sphere.create 3","cone.create 4 1 6","pyramid.create 10 8 6","wedge.create 10 8 6 2"}) {
            const auto text=std::string(primitive);const auto kind=text.substr(0,text.find('.'));
            const auto script="new part cli-"+kind+"\n"+text+"\nsave\n";
            result=launch(executable,root,common+QStringList{"--stdin"},QByteArray::fromStdString(script));
            require(result.exit_code==0 && result.results().size()==3,"CLI primitive creation failed");
            const auto loaded=document::PartDocument::load(project/("cli-"+kind+".prtz"),&reloaded);
            require(loaded.history.size()==1 && !reloaded.empty() && reloaded.back().volume>0,"CLI primitive did not persist calculated geometry");
        }

        result=launch(executable,root,common+QStringList{"--stdin"},"open commanded-box.prtz\nsave_as independent.prtz\nclose\nopen independent.prtz\ndocuments\nclose\n");
        require(result.exit_code==0 && result.results().size()==6 && result.results().back().at("data").empty(),"CLI copy/open/close lifecycle failed");
        const auto independent=document::PartDocument::load(project/"independent.prtz");
        require(independent.document_id!=new_box.document_id,"CLI copy reused original identity");
        result=launch(executable,root,common+QStringList{"--stdin","--keep-going"},"new drawing unsaved\nclose\n{\"command\":\"close\",\"arguments\":{\"discard\":true}}\ndocuments\n");
        const auto closed=result.results();
        require(result.exit_code==1 && closed.size()==4 && closed[1].at("code")=="unsaved_changes" && closed.back().at("data").empty(),"CLI failed to protect or explicitly discard an unsaved drawing");

        result=launch(executable,root,common+QStringList{"--stdin"},"new part commanded-bodies\nbox.create 10 10 10\nbody.create Tool\nbox.create 4 4 4\nsave\n");
        require(result.exit_code==0 && result.results().size()==5,"CLI could not create two independent Bodies");
        const auto first_body=result.results()[1].at("data").at("body").get<std::string>();
        const auto tool_body=result.results()[2].at("data").at("body").get<std::string>();
        const auto boolean_script="open commanded-bodies.prtz\nbody.boolean.create subtract "+first_body+" "+tool_body+"\nsave\n";
        result=launch(executable,root,common+QStringList{"--stdin"},QByteArray::fromStdString(boolean_script));
        require(result.exit_code==0 && result.results().size()==3,"CLI could not calculate a Body Boolean");
        const auto bodies=document::PartDocument::load(project/"commanded-bodies.prtz",&reloaded);
        require(bodies.body_history.booleans().size()==1 && bodies.body_history.booleans().front().target_id==first_body &&
            bodies.body_history.booleans().front().tool_id==tool_body && std::abs(reloaded.back().volume-936)<1e-6,"CLI Boolean saved wrong ownership or volume");
        const auto boolean_id=bodies.body_history.booleans().front().id;
        const auto history_script="open commanded-bodies.prtz\nhistory.delete "+boolean_id+"\nhistory.move "+tool_body+" "+first_body+"\nhistory.list\nsave\n";
        result=launch(executable,root,common+QStringList{"--stdin"},QByteArray::fromStdString(history_script));
        const auto history_result=document::PartDocument::load(project/"commanded-bodies.prtz",&reloaded);
        require(result.exit_code==0 && result.results().size()==5 && history_result.body_history.booleans().empty() &&
            history_result.body_history.order().front()==tool_body && !reloaded.empty(),"CLI history did not persist deleted Boolean/reordered Bodies");
        const auto dxf_source=project/fs::path(u8"obrys český.dxf");
        write(dxf_source,"0\nSECTION\n2\nENTITIES\n0\nLINE\n10\n0\n20\n0\n11\n20\n21\n10\n0\nENDSEC\n0\nEOF\n");
        const auto dxf_import=command({{"command","import.dxf"},{"arguments",{{"path",document::path_to_utf8(dxf_source)}}}});
        result=launch(executable,root,common+QStringList{"--command","new part cli-dxf","--command",dxf_import,"--command","save"});
        require(result.exit_code==0 && result.results()[1].at("data").at("imported_entities")==1,"CLI DXF import failed");
        const auto dxf_native=document::PartDocument::load(project/"cli-dxf.prtz");
        require(dxf_native.sketches.back().segments.size()==1 && dxf_native.sketches.back().name=="obrys český","CLI DXF lost native geometry or UTF-8 metadata");
        const auto exported_dxf=project/fs::path(u8"exportovaný obrys.dxf");
        const auto dxf_export=command({{"command","export.dxf"},{"arguments",{{"path",document::path_to_utf8(exported_dxf)},{"sketch",dxf_native.sketches.back().id}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-dxf.prtz","--command",dxf_export});
        require(result.exit_code==0 && result.results()[1].at("data").at("model_changed")==false,"CLI DXF export failed");
        auto dxf_roundtrip=sketcher::Sketch::create_default();require(interchange::import_dxf(exported_dxf,dxf_roundtrip).imported_entities==1,"CLI DXF output is not readable");
        result=launch(executable,root,common+QStringList{"--command","open cli-dxf.prtz","--command",dxf_export});
        require(result.exit_code==1 && result.results()[1].at("code")=="file_exists","CLI exported over an existing file without permission");
        kernel::OcctKernel import_kernel;auto import_source=document::PartDocument::create_default();
        auto source_box=document::PartDocument::create_box_container();source_box.box.length=10;source_box.box.width=20;source_box.box.height=30;import_source.history.push_back(source_box);
        const auto source_bodies=import_kernel.evaluate_history(import_source.kernel_operations());
        const auto step_source=project/fs::path(u8"kvádr český.step");
        import_kernel.export_step(interchange::step_product(import_source,source_bodies),document::path_to_utf8(step_source));
        const auto step_import=command({{"command","import.step"},{"arguments",{{"path",document::path_to_utf8(step_source)},{"mesh_deflection_mm",2.0}}}});
        result=launch(executable,root,common+QStringList{"--command","new part cli-step","--command",step_import,"--command","save"});
        require(result.exit_code==0 && result.results()[1].at("data").at("bodies").size()==1,"CLI STEP import failed or mixed OCCT diagnostics into protocol");
        std::vector<kernel::BodyResult> step_bodies;const auto step_native=document::PartDocument::load(project/"cli-step.prtz",&step_bodies);
        require(!step_bodies.empty() && std::abs(step_bodies.back().volume-6000)<1e-5 && step_native.history.back().imported_step.mesh_deflection==2.0,"CLI STEP lost volume or selected precision");
        const auto step_output=project/fs::path(u8"exportovaný kvádr.step"),stl_output=project/fs::path(u8"exportovaný kvádr.stl");
        const auto step_export=command({{"command","export.step"},{"arguments",{{"path",document::path_to_utf8(step_output)}}}});
        const auto stl_export=command({{"command","export.stl"},{"arguments",{{"path",document::path_to_utf8(stl_output)}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-step.prtz","--command",step_export,"--command",stl_export});
        if(result.exit_code!=0)std::cerr << result.output.toStdString() << result.diagnostics.toStdString();
        require(result.exit_code==0 && result.results().size()==3 && fs::file_size(stl_output)>84,"CLI STEP/STL export failed or polluted the protocol");
        const auto exported_part=interchange::import_step_part(document::PartDocument::create_default(),{},step_output);
        require(std::abs(exported_part.calculated.back().volume-6000)<1e-5,"CLI STEP export changed the solid");
        const auto iges_source=project/fs::path(u8"krychle česká.igs");fs::copy_file(repository/"cpp/tests/fixtures/import/cube-10mm.igs",iges_source);
        const auto iges_import=command({{"command","import.iges"},{"arguments",{{"path",document::path_to_utf8(iges_source)}}}});
        result=launch(executable,root,common+QStringList{"--command","new part cli-iges","--command",iges_import,"--command","save"});
        require(result.exit_code==0 && result.results()[1].at("data").at("bodies").size()==1,"CLI IGES import failed");
        std::vector<kernel::BodyResult> iges_bodies;static_cast<void>(document::PartDocument::load(project/"cli-iges.prtz",&iges_bodies));
        require(!iges_bodies.empty() && std::abs(iges_bodies.back().volume-1000)<1e-4,"CLI IGES changed native scale");
        const Json metadata_entries=Json::array({{{"key","NUMBER"},{"values",{{"","CLI-001"}}}},{{"key","NAME"},{"values",{{"cs","Český díl"},{"en","Part"}}}}});
        const auto metadata_set=command({{"command","document.parameters.set"},{"arguments",{{"parameters",metadata_entries}}}});
        const auto settings_set=command({{"command","document.settings.set"},{"arguments",{{"units",{{"Length","m"}}},{"precision",{{"mesh_deflection",2},{"decimal_places",6}}}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-step.prtz","--command",metadata_set,"--command",settings_set,"--command","save"});
        require(result.exit_code==0 && result.results().size()==4,"CLI document metadata update failed");
        std::vector<kernel::BodyResult> metadata_bodies;const auto metadata_part=document::PartDocument::load(project/"cli-step.prtz",&metadata_bodies);
        require(metadata_part.user_parameter_values.at("NAME").at("cs")=="Český díl" && !metadata_part.user_parameters.contains("NAME") && metadata_part.document_units.at("Length")=="m" && metadata_part.document_precision.at("decimal_places")=="6" && std::abs(metadata_bodies.back().volume-6000)<1e-5,"CLI metadata update changed geometry or lost localization");
        result=launch(executable,root,common+QStringList{"--command","open cli-step.prtz","--command","document.parameters.get","--command","document.settings.get"});
        require(result.exit_code==0 && result.results()[1].at("data").at("parameters")[1].at("values")==metadata_entries[1].at("values") && result.results()[2].at("data").at("units").at("Length")=="m","CLI metadata readback failed");
        const auto engineering_material=command({{"command","document.material.set"},{"arguments",{{"properties",Json::array({{{"key","MATERIAL_NAME"},{"value","Hliník"}},{{"key","MASS_DENSITY"},{"value","2700"},{"unit","kg/m^3"}}})}}}});
        const auto engineering_relations=command({{"command","document.relations.set"},{"arguments",{{"relations",Json::array({{{"target","grams"},{"expression","model.mass * 1000"}}})}}}});
        const auto engineering_family=command({{"command","document.family.set"},{"arguments",{{"table",{{"columns",{"NUMBER"}},{"instances",Json::array({{{"name","Varianta A"},{"values",{{"NUMBER","ZE-100"}}}}})}}}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-step.prtz","--command",engineering_material,"--command",engineering_relations,"--command",engineering_family,"--command","save"});
        if(result.exit_code!=0)std::cerr<<result.output.toStdString()<<result.diagnostics.toStdString();
        require(result.exit_code==0 && result.results().size()==5,"CLI engineering metadata update failed");
        const auto engineering_native=document::PartDocument::load(project/"cli-step.prtz");
        require(engineering_native.physical_parameters.at("MATERIAL_NAME")=="Hliník" && engineering_native.user_parameters.at("grams")=="16.200000" && engineering_native.family_table.find("ZE-100")!=std::string::npos,"CLI engineering metadata failed native persistence or mass calculation");
        result=launch(executable,root,common+QStringList{"--command","open cli-step.prtz","--command","document.material.get","--command","document.relations.get","--command","document.family.get"});
        require(result.exit_code==0 && result.results()[2].at("data").at("parameters").at("grams")=="16.200000" && result.results()[3].at("data").at("table").at("instances")[0].at("name")=="Varianta A","CLI engineering metadata readback failed");
        const auto material_source=project/fs::path(u8"Ocel česká.matz");fs::copy_file(repository/"config/materials/01_oceli/konstrukcni/S235JR.matz",material_source);
        const auto material_load=command({{"command","document.material.load"},{"arguments",{{"path",document::path_to_utf8(material_source)}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-step.prtz","--command",material_load,"--command","save"});
        require(result.exit_code==0 && result.results().size()==3,"CLI material library assignment failed");fs::remove(material_source);
        result=launch(executable,root,common+QStringList{"--command","open cli-step.prtz","--command","document.material.get","--command","document.relations.get"});
        require(result.exit_code==0 && result.results()[2].at("data").at("parameters").at("grams")=="47.100000","Assigned material retained a file dependency or lost mass");
        const auto component_insert=command({{"command","component.insert"},{"arguments",{{"source",step_native.document_id},{"name","Opakovaný díl"}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-step.prtz","--command","new assembly cli-components","--command",component_insert,"--command",component_insert,"--command","undo","--command","redo","--command","save"});
        if(result.exit_code!=0)std::cerr<<result.output.toStdString()<<result.diagnostics.toStdString();
        require(result.exit_code==0 && result.results().size()==7,"CLI component insertion or history failed");
        const auto component_native=assembly::AssemblyDocument::load(project/"cli-components.asmz");
        require(component_native.components.size()==2 && component_native.components[0].occurrence_id!=component_native.components[1].occurrence_id && component_native.components[0].source_document_id==step_native.document_id,"CLI component persistence lost repeated identities");
        const auto component_get=command({{"command","component.get"},{"arguments",{{"instance_path",assembly::InstancePath{}.child(component_native.components[0].occurrence_id).encoded()}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-components.asmz","--command","component.list","--command",component_get});
        require(result.exit_code==0 && result.results()[1].at("data").at("total")==2 && std::abs(result.results()[2].at("data").at("cached_volume_mm3").get<double>()-6000)<1e-7,"CLI component queries did not read native snapshots");
        const auto component_open=command({{"command","component.open"},{"arguments",{{"instance_path",assembly::InstancePath{}.child(component_native.components[0].occurrence_id).encoded()}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-components.asmz","--command",component_open,"--command","context"});
        require(result.exit_code==0 && result.results()[1].at("data").at("document")==step_native.document_id && result.results()[1].at("data").at("opened")==true && result.results()[2].at("data").at("active_document")==step_native.document_id,"CLI could not open the exact native source occurrence");
        const auto sheet_create=command({{"command","drawing.sheet.create"},{"arguments",{{"name","Český list"},{"format","A3"},{"scale",2}}}});
        result=launch(executable,root,common+QStringList{"--command","new drawing cli-sheets","--command",sheet_create,"--command","undo","--command","redo","--command","save"});
        require(result.exit_code==0 && result.results().size()==5,"CLI drawing sheet creation and history failed");
        const auto sheet_id=result.results()[1].at("data").at("sheet").get<std::string>();
        const auto sheet_edit=command({{"command","drawing.sheet.set"},{"arguments",{{"sheet",sheet_id},{"scale",.5},{"locale","en"}}}});
        const auto sheet_frame=command({{"command","drawing.frame.load"},{"arguments",{{"sheet",sheet_id},{"path",document::path_to_utf8(repository/"config/formats/ZE-A3.frmz")}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-sheets.drwz","--command",sheet_edit,"--command",sheet_frame,"--command","save"});
        require(result.exit_code==0,"CLI drawing settings or native frame load failed");
        const auto saved_sheets=drawing::DrawingDocument::load(project/"cli-sheets.drwz");
        require(saved_sheets.sheets.size()==2 && saved_sheets.find_sheet(sheet_id)->name=="Český list" && saved_sheets.find_sheet(sheet_id)->default_scale==.5 && !saved_sheets.find_sheet(sheet_id)->frame_lines.empty(),"CLI sheet or embedded frame did not persist");
        result=launch(executable,root,common+QStringList{"--command","open cli-sheets.drwz","--command","drawing.sheet.list"});
        require(result.exit_code==0 && result.results()[1].at("data").at("items")[1].at("locale")=="en","CLI drawing sheet readback failed");
        std::vector<kernel::BodyResult> drawing_source_cache;const auto drawing_source=document::PartDocument::load(project/"cli-step.prtz",&drawing_source_cache);
        auto view_doc=drawing::DrawingDocument::create_default();view_doc.source_document_id=drawing_source.document_id;view_doc.source_path=project/"cli-step.prtz";
        const auto native_view=drawing::DrawingDocument::create_view(drawing_source.document_id,project/"cli-step.prtz",drawing_source_cache.back().mesh);view_doc.sheets.front().views.push_back(native_view);view_doc.save(project/"cli-views.drwz");
        const auto view_get=command({{"command","drawing.view.get"},{"arguments",{{"view",native_view.id}}}});
        const auto view_references=command({{"command","drawing.view.references"},{"arguments",{{"view",native_view.id},{"limit",3}}}});
        const auto view_delete=command({{"command","drawing.view.delete"},{"arguments",{{"view",native_view.id}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-views.drwz","--command",view_references,"--command","regenerate","--command",view_get,"--command",view_delete,"--command","undo","--command","save"});
        if(result.exit_code!=0)std::cerr<<result.output.toStdString()<<result.diagnostics.toStdString();
        require(result.exit_code==0&&result.results().size()==7&&result.results()[1].at("data").at("items").size()==3&&result.results()[3].at("data").at("model_annotations").get<int>()>0,"CLI drawing view regeneration, original references or history failed");
        require(drawing::DrawingDocument::load(project/"cli-views.drwz").sheets.front().views.front().id==native_view.id,"CLI view Undo and save lost its stable identity");
        const auto create_view=command({{"command","drawing.view.create"},{"arguments",{{"sheet",view_doc.sheets.front().id},{"source",drawing_source.document_id},{"name","Pohled český"},{"orientation","top"},{"scale",2}}}});
        const auto create_projection=command({{"command","drawing.view.create"},{"arguments",{{"sheet",view_doc.sheets.front().id},{"parent_view",native_view.id},{"projection_direction","right"},{"distance_mm",30}}}});
        const auto edit_view=command({{"command","drawing.view.set"},{"arguments",{{"view",native_view.id},{"x_mm",120},{"show_caption",true}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-step.prtz","--command","open cli-views.drwz","--command",create_view,"--command",create_projection,"--command",edit_view,"--command","save"});
        if(result.exit_code!=0)std::cerr<<result.output.toStdString()<<result.diagnostics.toStdString();
        require(result.exit_code==0&&result.results().size()==6,"Standalone CLI view creation or properties failed");
        const auto edited_views=drawing::DrawingDocument::load(project/"cli-views.drwz");
        require(edited_views.sheets.front().views.size()==3&&edited_views.sheets.front().views[1].name=="Pohled český"&&edited_views.sheets.front().views[1].scale==2&&edited_views.sheets.front().views[2].x==90,"CLI view parameters, hierarchy or UTF-8 persistence failed");
        result=launch(executable,root,common+QStringList{"--command","open cli-views.drwz","--command",command({{"command","drawing.annotation.list"},{"arguments",{{"view",native_view.id},{"mode","show"}}}})});
        require(result.exit_code==0&&!result.results()[1].at("data").at("items").empty(),"CLI did not offer stored annotations");
        const auto annotation_ref=result.results()[1].at("data").at("items")[0].at("reference");
        const auto show_annotation=command({{"command","drawing.annotation.show_erase"},{"arguments",{{"views",Json::array({Json{{"view",native_view.id},{"selected",Json::array({annotation_ref})}}})}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-views.drwz","--command",show_annotation,"--command","undo","--command","redo","--command","save"});
        require(result.exit_code==0,"Standalone CLI Show/Erase or Undo/Redo failed");
        const auto shown_document=drawing::DrawingDocument::load(project/"cli-views.drwz");
        std::size_t shown_count=0;
        for(const auto& item:shown_document.find_view(native_view.id)->model_annotations)if(item.visible){
            ++shown_count;require(item.source.semantic_id==annotation_ref.at("key").get<std::string>()&&item.source.instance_path==annotation_ref.at("instance_path").get<std::string>(),"CLI showed a different occurrence");
        }
        require(shown_count==1,"CLI Show/Erase native persistence lost exact visibility");
        auto measured_doc=drawing::DrawingDocument::create_default();auto measured_view=native_view;
        const auto& measured_curves=measured_view.measurement_geometry->curves;
        require(!measured_curves.empty()&&measured_curves.front().line,"CLI dimension fixture needs a straight original edge");
        auto measured_dimension=drawing::make_drawing_dimension(measured_view.id);
        measured_dimension.attachments={{drawing::DimensionAttachmentKind::CurvePoint,measured_curves.front().source,{},0},{drawing::DimensionAttachmentKind::CurvePoint,measured_curves.front().source,{},1}};
        drawing::refresh_drawing_dimension(measured_view,measured_dimension);
        measured_doc.sheets.front().views={measured_view};measured_doc.sheets.front().dimensions={measured_dimension};measured_doc.save(project/"cli-measured.drwz");
        const auto measured_get=command({{"command","drawing.dimension.get"},{"arguments",{{"dimension",measured_dimension.id}}}});
        const auto measured_delete=command({{"command","drawing.dimension.delete"},{"arguments",{{"dimension",measured_dimension.id}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-measured.drwz","--command",measured_get,"--command",measured_delete,"--command","drawing.dimension.list","--command","undo","--command","save"});
        require(result.exit_code==0&&result.results()[1].at("data").at("attachments").size()==2&&result.results()[3].at("data").at("total")==0,"CLI measured dimension query/delete failed");
        require(drawing::DrawingDocument::load(project/"cli-measured.drwz").sheets.front().dimensions.front()==measured_dimension,"CLI measured dimension Undo lost persisted references");
        const auto assembly_step=command({{"command","import.step"},{"arguments",{{"path",document::path_to_utf8(step_source)},{"output_directory","sestava nativní"},{"mesh_deflection_mm",2.0}}}});
        result=launch(executable,root,common+QStringList{"--command","new assembly cli-import-owner","--command",assembly_step,"--command","save"});
        require(result.exit_code==0 && result.results().size()==3 && result.results()[1].at("data").at("parts").size()==1,"CLI Assembly STEP import failed");
        auto assembly_imported=assembly::AssemblyDocument::load(project/"cli-import-owner.asmz");
        require(assembly_imported.components.size()==1 && std::abs(assembly_imported.components.front().calculated_source->volume-6000)<1e-5,"CLI Assembly import lost source geometry");
        result=launch(executable,root,common+QStringList{"--command","open cli-import-owner.asmz","--command",iges_import,"--command",dxf_import,"--command","save"});
        require(result.exit_code==0 && result.results().size()==4,"CLI Assembly IGES/DXF import failed");
        assembly_imported=assembly::AssemblyDocument::load(project/"cli-import-owner.asmz");
        require(assembly_imported.components.size()==3 && std::abs(assembly_imported.components[1].calculated_source->volume-1000)<1e-4,"CLI Assembly import did not persist independent components");
        const auto imported_profile=document::PartDocument::load(assembly_imported.components[2].source_path);
        require(imported_profile.sketches.back().segments.size()==1,"CLI Assembly DXF lost its source sketch");
        result=launch(executable,root,common+QStringList{"--stdin"},"new part cli-sketch\nsketch.create Spline XY\nsave\n");
        require(result.exit_code==0 && result.results().size()==3,"CLI could not create an owned Sketch");
        const auto sketch_id=result.results()[1].at("data").at("sketch").get<std::string>();
        const auto spline_request=command({{"command","sketch.bspline.create"},{"arguments",{{"sketch",sketch_id},{"points",{{0,0},{3,8},{7,-3},{10,0}}},{"degree",3}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-sketch.prtz","--command",spline_request,"--command","save"});
        require(result.exit_code==0,"CLI failed to persist B-spline");
        const auto sketch_document=document::PartDocument::load(project/"cli-sketch.prtz");
        require(sketch_document.sketches.back().id==sketch_id && sketch_document.sketches.back().bsplines.size()==1 &&
            sketch_document.sketches.back().bsplines.front().degree==3,"CLI B-spline lost exact degree or owning Sketch");
        const auto spline_id=sketch_document.sketches.back().bsplines.front().id;
        const auto spline_patch=command({{"command","sketch.bspline.set"},{"arguments",{{"sketch",sketch_id},{"geometry",spline_id},{"points",{{0,0},{3,10},{7,2},{10,0}}}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-sketch.prtz","--command",spline_patch,"--command","save"});
        require(result.exit_code==0,"CLI spline property edit failed");
        const auto spline_edited=document::PartDocument::load(project/"cli-sketch.prtz");
        const auto& edited_curve=spline_edited.sketches.back().bsplines.front();
        require(edited_curve.id==spline_id && edited_curve.control_point_ids==sketch_document.sketches.back().bsplines.front().control_point_ids &&
            spline_edited.sketches.back().find_point(edited_curve.control_point_ids[1])->y==10,"CLI spline edit changed IDs or lost pole coordinates");
        const auto offset_request=command({{"command","sketch.offset.create"},{"arguments",{{"sketch",sketch_id},{"source",spline_id},{"distance_mm",.1}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-sketch.prtz","--command",offset_request,"--command","save"});
        require(result.exit_code==0,"CLI failed to calculate/persist a native spline offset");
        const auto offset_document=document::PartDocument::load(project/"cli-sketch.prtz");
        require(offset_document.sketches.back().offsets.size()==1 && offset_document.sketches.back().offsets.front().source_id==spline_id &&
            std::abs(offset_document.sketches.back().offsets.front().distance-.1)<1e-12,"CLI offset lost source identity or distance");
        result=launch(executable,root,common+QStringList{"--stdin"},"new part cli-relations\nsketch.create Relations XY\nsave\n");
        require(result.exit_code==0,"CLI relation fixture creation failed");const auto relation_sketch=result.results()[1].at("data").at("sketch").get<std::string>();
        const auto line_request=command({{"command","sketch.segment.create"},{"arguments",{{"sketch",relation_sketch},{"first",{0,0}},{"second",{10,4}}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-relations.prtz","--command",line_request,"--command","save"});
        require(result.exit_code==0,"CLI relation fixture segment failed");const auto relation_line=result.results()[1].at("data").at("geometry").get<std::string>();
        const auto relation_request=command({{"command","sketch.constraint.create"},{"arguments",{{"sketch",relation_sketch},{"kind","horizontal"},{"geometry",{relation_line}}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-relations.prtz","--command",relation_request,"--command","save"});
        require(result.exit_code==0,"CLI relation command failed");const auto relations=document::PartDocument::load(project/"cli-relations.prtz");const auto& rs=relations.sketches.back();
        require(rs.constraints.size()==1 && rs.constraints.front().kind==sketcher::ConstraintKind::Horizontal &&
            std::abs(rs.find_point(rs.segments[0].first_point_id)->y-rs.find_point(rs.segments[0].second_point_id)->y)<1e-7,"CLI did not save the solved horizontal relation");
        const auto dimension_request=command({{"command","sketch.dimension.create"},{"arguments",{{"sketch",relation_sketch},{"kind","distance"},{"geometry",{relation_line}},{"value",25},{"locked",true},{"layout",{{"text_along",2}}}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-relations.prtz","--command",dimension_request,"--command","save"});
        require(result.exit_code==0,"CLI dimension command failed");const auto dimension_document=document::PartDocument::load(project/"cli-relations.prtz");const auto& ds=dimension_document.sketches.back();
        require(ds.dimensions.size()==1 && ds.dimensions.front().locked && std::abs(ds.dimensions.front().value-25)<1e-7 &&
            std::abs(std::hypot(ds.find_point(ds.segments[0].first_point_id)->x-ds.find_point(ds.segments[0].second_point_id)->x,
                               ds.find_point(ds.segments[0].first_point_id)->y-ds.find_point(ds.segments[0].second_point_id)->y)-25)<1e-6 &&
            dimension_document.dimension_layouts.back().layout.text_along==2,"CLI dimension did not persist solved length, lock and label together");
        result=launch(executable,root,common+QStringList{"--stdin"},"new part cli-projection\nbox.create 10 10 10\nsketch.create Projection XY\nsave\n");
        require(result.exit_code==0,"CLI projection fixture failed");const auto projection_sketch=result.results()[2].at("data").at("sketch").get<std::string>();
        std::vector<kernel::BodyResult> projection_cache;const auto projection_document=document::PartDocument::load(project/"cli-projection.prtz",&projection_cache);
        const auto& projection_edges=projection_cache.back().mesh.original_references.edges;
        const auto projection_edge=std::ranges::find_if(projection_edges,[](const auto& e){return e.points.size()>=2 && std::hypot(e.points.front().x-e.points.back().x,e.points.front().y-e.points.back().y)>1;});
        require(projection_edge!=projection_edges.end(),"CLI fixture has no projectable original edge");
        const auto projection_request=command({{"command","sketch.reference.create"},{"arguments",{{"sketch",projection_sketch},{"kind","edge"},{"owner",projection_edge->reference.owner_id},{"key",projection_edge->reference.semantic_key},{"profile",true}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-projection.prtz","--command",projection_request,"--command","save"});
        require(result.exit_code==0,"CLI projection command failed");const auto projection_id=result.results()[1].at("data").at("reference").get<std::string>();
        const auto projected=document::PartDocument::load(project/"cli-projection.prtz");require(projected.sketches.back().external_references.size()==1 && projected.sketches.back().segments.size()==1,"CLI projection did not persist reference and native line");
        result=launch(executable,root,common+QStringList{"--command","open cli-projection.prtz","--command",QString::fromStdString("sketch.reference.delete "+projection_sketch+" "+projection_id),"--command","save"});
        require(result.exit_code==0,"CLI reference detach failed");const auto detached=document::PartDocument::load(project/"cli-projection.prtz");
        require(detached.sketches.back().external_references.empty() && detached.sketches.back().segments==projected.sketches.back().segments,"CLI detach destroyed native projection");
        const auto text_request=command({{"command","sketch.text.create"},{"arguments",{{"sketch",projection_sketch},{"value","Řez Ø10"},{"position",{20,30}},{"height_mm",3},{"modeling_geometry",false}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-projection.prtz","--command",text_request,"--command","save"});
        require(result.exit_code==0,"Qt-free CLI text generation failed");const auto text_document=document::PartDocument::load(project/"cli-projection.prtz");
        require(text_document.sketches.back().texts.size()==1 && text_document.sketches.back().texts.front().value=="Řez Ø10" && !text_document.sketches.back().texts.front().contours.empty() && !text_document.sketches.back().texts.front().modeling_geometry,"CLI text lost Unicode, glyph outlines or annotation mode");
        std::cout<<"CLI processes: native files, Unicode/config, scripts/stdin, errors, streaming and explicit geometry calculation passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
