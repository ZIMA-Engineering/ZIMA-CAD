#include <zima/drawing/drawing_template.hpp>
#include <zima/sketcher/text_geometry.hpp>
#include "derived_copy_query_test_support.hpp"
#include "drill_point_test_support.hpp"
#include <zima/kernel/drill_point_identity.hpp>
#include "sweep_test_support.hpp"
#include "owned_reference_test_support.hpp"
#include "construction_query_test_support.hpp"
#include "dxf_export_test_support.hpp"
#include "dxf_spline_import_test_support.hpp"
#include "stl_export_test_support.hpp"
#include <zima/command_host/host.hpp>
#include <zima/document/file_path.hpp>
#include <zima/document/measurement_record.hpp>
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
        result=launch(executable,root,{"--command","thread.catalog metric M10"});
        require(result.exit_code==0&&result.results().size()==1&&result.results().front().at("data").at("items")[0].at("internal_root_diameter_mm")==8.376,
            "Standalone CLI could not read its bundled thread catalog outside the repository");
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
        result=launch(executable,root,common+QStringList{"--command","new part headless-view","--command","export.view no-view.png"});
        require(result.exit_code!=0&&result.results().back().at("code")=="view_unavailable"&&!fs::exists(project/"no-view.png"),"CLI fabricated a model View or wrote a false capture");
        for(const bool title:{false,true}) {
            const std::string kind=title?"title_block":"drawing_format",name=title?"CLI razítko":"CLI rámeček",suffix=title?".tblz":".frmz";
            const auto operations=Json::array({{{"command","sketch.segment.create"},{"arguments",{{"first",{0,0}},{"second",{-20,0}}}}},
                {{"command","sketch.text.create"},{"arguments",{{"value","Žluťoučký &name"},{"position",{-5,3}},{"height_mm",2.5}}}}});
            result=launch(executable,root,common+QStringList{"--command",command({{"command","template.new"},{"arguments",{{"kind",kind},{"name",name}}}}),
                "--command",command({{"command","template.sketch.edit"},{"arguments",{{"operations",operations}}}}),
                "--command","undo","--command","redo","--command","template.save","--command","template.get"});
            require(result.exit_code==0&&result.results()[1].at("data").at("body_calculated")==false&&result.results().back().at("data").at("texts")==1,"CLI template batch or history failed");
            const auto file=project/fs::u8path(name+suffix);const auto load=[](auto& text){sketcher::rebuild_text_contours(text,true);};
            const auto saved=drawing::load_template_sketch(file,load);require(saved.drawing_template->kind==kind&&saved.segments.size()==1&&saved.texts.front().value=="Žluťoučký &name"&&!saved.texts.front().modeling_geometry,"CLI template lost native data");
            result=launch(executable,root,common+QStringList{"--command",command({{"command","template.open"},{"arguments",{{"path",name+suffix}}}}),
                "--command",command({{"command","template.save"},{"arguments",{{"path",name+" copy"+suffix},{"copy",true}}}}),"--command","close"});
            require(result.exit_code==0&&drawing::load_template_sketch(project/fs::u8path(name+" copy"+suffix),load).id==saved.id,"CLI template copy or reopen changed its identity");
        }
        {
            const auto logo=project/"CLI logo.svg";{std::ofstream out(logo);out<<R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 4 2"><rect width="4" height="2" fill="red"/></svg>)";}
            result=launch(executable,root,common+QStringList{"--command",command({{"command","template.new"},{"arguments",{{"kind","title_block"},{"name","CLI objects"}}}}),
                "--command",command({{"command","template.image.create"},{"arguments",{{"path","CLI logo.svg"},{"x_mm",10},{"y_mm",20},{"width_mm",20}}}}),
                "--command",command({{"command","template.region.create"},{"arguments",{{"x_mm",0},{"y_mm",0},{"width_mm",50},{"height_mm",8}}}}),"--command","template.save"});
            require(result.exit_code==0,"CLI template objects could not be created");
            const auto image=result.results()[1].at("data").at("image").get<std::string>(),region=result.results()[2].at("data").at("region").get<std::string>();
            result=launch(executable,root,common+QStringList{"--command",command({{"command","template.open"},{"arguments",{{"path","CLI objects.tblz"}}}}),
                "--command",command({{"command","template.image.set"},{"arguments",{{"image",image},{"height_mm",15}}}}),
                "--command",command({{"command","template.region.set"},{"arguments",{{"region",region},{"step_mm",9.123456789},{"direction","right"},{"value_locks",{"step"}}}}}),"--command","template.save",
                "--command",command({{"command","template.image.remove"},{"arguments",{{"image",image}}}}),"--command","undo","--command","template.image.list"});
            require(result.exit_code==0&&result.results().back().at("data").at("items").front().at("image")==image,"CLI object update or removal Undo failed");
            fs::remove(logo);const auto saved=drawing::load_template_sketch(project/"CLI objects.tblz",[](auto& text){sketcher::rebuild_text_contours(text,true);});
            require(saved.drawing_template->images.front().id==image&&saved.drawing_template->images.front().width==30&&saved.drawing_template->images.front().height==15&&
                saved.drawing_template->repeat_regions.front().id==region&&saved.drawing_template->repeat_regions.front().direction=="right"&&saved.drawing_template->repeat_regions.front().value_locks.contains("step"),"CLI object save lost geometry, identity or locks");
        }
        for(const bool assembly_mode:{false,true}) {
            const std::string type=assembly_mode?"assembly":"part",name="construction-reference-"+type,suffix=assembly_mode?".asmz":".prtz";
            auto arguments=common+QStringList{"--command",command({{"command","new"},{"arguments",{{"type",type},{"name",name}}}})};
            if(!assembly_mode)arguments+=QStringList{"--command","box.create 10 10 10"};
            arguments+=QStringList{"--command",command({{"command","construction.create"},{"arguments",{{"kind","plane"},{"name","Referenced plane"},{"base_plane","xy"}}}}),"--command","save"};
            result=launch(executable,root,arguments);require(result.exit_code==0,"Cannot prepare CLI construction reference fixture");
            const auto made=result.results()[assembly_mode?1:2].at("data");const auto object=made.at("construction").get<std::string>(),document=made.at("document").get<std::string>();
            result=launch(executable,root,common+QStringList{"--command",command({{"command","open"},{"arguments",{{"path",name+suffix}}}}),
                "--command",command({{"command","construction.reference.set"},{"arguments",{{"construction",object},{"index",0},{"reference",{{"owner",document+":origin"},{"key","origin:plane:xy"}}},{"offset_mm",6}}}}),
                "--command",command({{"command","placement.set"},{"arguments",{{"object",object},{"values",{{"reference_offset:0",8}}}}}}),"--command","undo","--command","redo","--command","save"});
            require(result.exit_code==0&&result.results()[1].at("data").at("body_calculated")==false,"CLI construction reference or Undo/Redo failed");
            const auto values=assembly_mode?assembly::AssemblyDocument::load(project/(name+suffix)).constructions:document::PartDocument::load(project/(name+suffix)).constructions;
            const auto found=std::ranges::find(values,object,&document::ConstructionObject::id);require(found!=values.end()&&found->origin.z==8&&found->references.front().owner_id==document+":origin"&&found->references.front().offset==8,"CLI native save lost reference owner or calculated position");
        }
        {
            result=launch(executable,root,common+QStringList{"--command","new part primitive-reference","--command","box.create 10 10 10","--command","save"});
            require(result.exit_code==0,"Cannot prepare CLI primitive reference fixture");const auto made=result.results()[1].at("data");const auto object=made.at("container").get<std::string>(),document=made.at("document").get<std::string>();
            result=launch(executable,root,common+QStringList{"--command","open primitive-reference.prtz",
                "--command",command({{"command","placement.reference.set"},{"arguments",{{"object",object},{"index",0},{"reference",{{"owner",document+":origin"},{"key","origin:plane:xy"}}},{"offset_mm",6}}}}),
                "--command",command({{"command","placement.set"},{"arguments",{{"object",object},{"values",{{"reference_offset:0",8}}}}}}),"--command","undo","--command","redo","--command","save"});
            require(result.exit_code==0&&result.results()[1].at("data").at("placement").at("z")==6,"CLI primitive reference or Undo/Redo failed");
            std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(project/"primitive-reference.prtz",&cache);const auto* found=saved.find_container(object);
            require(found&&found->placement.z==8&&found->placement.references.front().owner_id==document+":origin"&&!cache.empty()&&std::abs(cache.back().volume-1000)<1e-6,"CLI native save lost primitive source, placement or geometry");
        }
        for(bool extrusion:{true,false}) {
            const std::string prefix=extrusion?"extrusion":"revolution",name=prefix+"-reference-cli",path=name+".prtz";
            result=launch(executable,root,common+QStringList{"--command",command({{"command","new"},{"arguments",{{"type","part"},{"name",name}}}}),
                "--command",command({{"command","sketch.create"},{"arguments",{{"name","Profile"},{"plane","XY"}}}}),"--command","save"});
            require(result.exit_code==0,"Cannot create CLI profile Sketch");const auto sketch=result.results()[1].at("data").at("sketch").get<std::string>();
            auto arguments=common+QStringList{"--command",command({{"command","open"},{"arguments",{{"path",path}}}})};
            const auto line=[&](int x1,int y1,int x2,int y2){arguments+=QStringList{"--command",command({{"command","sketch.segment.create"},{"arguments",{{"sketch",sketch},{"first",{x1,y1}},{"second",{x2,y2}},{"snap_mm",0.000001}}}})};};
            line(2,1,4,1);line(4,1,4,4);line(4,4,2,4);line(2,4,2,1);if(!extrusion)line(0,0,0,5);arguments+=QStringList{"--command","save"};
            result=launch(executable,root,arguments);require(result.exit_code==0,"Cannot create CLI profile curves");
            arguments=common+QStringList{"--command",command({{"command","open"},{"arguments",{{"path",path}}}})};
            if(!extrusion)arguments+=QStringList{"--command",command({{"command","sketch.segment.centerline"},{"arguments",{{"sketch",sketch},{"segment",result.results()[5].at("data").at("geometry")},{"centerline",true}}}})};
            Json create={{"sketch",sketch}};if(extrusion)create["length_forward_mm"]=5;
            arguments+=QStringList{"--command",command({{"command",prefix+".create"},{"arguments",create}}),"--command","save"};
            result=launch(executable,root,arguments);require(result.exit_code==0,"Cannot calculate CLI profile fixture");const auto made=result.results()[extrusion?1:2].at("data");const auto object=made.at("container").get<std::string>(),document=made.at("document").get<std::string>();
            result=launch(executable,root,common+QStringList{"--command",command({{"command","open"},{"arguments",{{"path",path}}}}),
                "--command",command({{"command",prefix+".reference.set"},{"arguments",{{"container",object},{"index",0},{"reference",{{"owner",document+":origin"},{"key","origin:plane:xy"}}},{"offset_mm",8}}}}),"--command","undo","--command","redo","--command","save"});
            require(result.exit_code==0,"CLI profile reference or Undo/Redo failed");std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(project/path,&cache);const auto* found=saved.find_container(object);
            require(found&&found->placement.z==8&&found->placement.references.front().owner_id==document+":origin"&&!cache.empty()&&std::abs(cache.back().volume-(extrusion?30:36*std::acos(-1.0)))<1e-6,"CLI profile reference lost native geometry or owner");
            require(std::ranges::find(saved.sketches,sketch,&sketcher::Sketch::id)->owner_container_id==object,"CLI profile reference lost owned Sketch");
        }
        const auto shell_path=project/"shell-cli.prtz";
        auto shell_fixture=document::PartDocument::create_default();auto shell_box=document::PartDocument::create_box_container();shell_box.box={10,10,10};
        shell_fixture.insert_history_entry(document::PartHistoryKind::Feature,shell_box.id);shell_fixture.history.push_back(shell_box);
        kernel::OcctKernel shell_kernel;shell_fixture.save(shell_path,shell_kernel.evaluate_history(shell_fixture.kernel_operations()));
        result=launch(executable,root,common+QStringList{"--command",command({{"command","open"},{"arguments",{{"path",qpath(shell_path).toStdString()}}}}),
            "--command","shell.faces","--command","edge_treatment.edges",
            "--command",command({{"command","edge_treatment.route"},{"arguments",{{"seed",{{"owner",shell_box.id},{"key","edge:x_max:y_min:z_max--x_max:y_min:z_min"}}}}}}),
            "--command","shell.create","--command","save"});
        require(result.exit_code==0&&result.results()[1].at("data").at("total")==6,"CLI Shell creation/input face query failed");
        require(result.results()[2].at("data").at("total")==12&&result.results()[3].at("data").at("edges").size()==1,"Actual CLI edge/route queries failed");
        const auto lock_path=project/fs::path(u8"zámky CLI.prtz");shell_fixture.save(lock_path,shell_kernel.evaluate_history(shell_fixture.kernel_operations()));
        const auto lock_open=command({{"command","open"},{"arguments",{{"path",qpath(lock_path).toStdString()}}}});
        const auto lock_command=[&](bool locked){return command({{"command","value_lock.set"},{"arguments",{{"object",shell_box.id},{"key","length"},{"locked",locked}}}});};
        const auto lock_list=command({{"command","value_lock.list"},{"arguments",{{"object",shell_box.id}}}});
        result=launch(executable,root,common+QStringList{"--command",lock_open,"--command",lock_command(true),"--command",lock_list,"--command","save"});
        require(result.exit_code==0&&result.results()[2].at("data").at("items").size()==12&&document::PartDocument::load(lock_path).find_container(shell_box.id)->value_locks.contains("length"),
            "CLI lock list/set did not persist on a Unicode path");
        const auto lock_edit=command({{"command","box.set"},{"arguments",{{"container",shell_box.id},{"length_mm","11"}}}});
        result=launch(executable,root,common+QStringList{"--command",lock_open,"--command",lock_edit});
        require(result.exit_code==1&&result.results().back().at("code")=="value_locked","Cold CLI process bypassed persisted lock");
        result=launch(executable,root,common+QStringList{"--command",lock_open,"--command",lock_command(false),"--command",lock_edit,"--command",lock_command(true),
            "--command","undo","--command","redo","--command","save"});
        std::vector<kernel::BodyResult> lock_cache;const auto lock_saved=document::PartDocument::load(lock_path,&lock_cache);
        require(result.exit_code==0&&lock_saved.find_container(shell_box.id)->box.length==11&&lock_saved.find_container(shell_box.id)->value_locks.contains("length")&&
            std::abs(lock_cache.back().volume-1100)<1e-6,"CLI unlock/edit/relock/Undo lost values or geometry");
        const auto copy_fixture=test::copy_query_fixture();const auto copy_path=project/fs::path(u8"kopie příkazy.prtz");
        copy_fixture.document.save(copy_path,shell_kernel.evaluate_history(copy_fixture.document.kernel_operations()));
        const auto copy_open=command({{"command","open"},{"arguments",{{"path",qpath(copy_path).toStdString()}}}});
        const auto copy_sources=command({{"command","derived_copy.sources"},{"arguments",{{"object",copy_fixture.mirror}}}});
        const auto mirror_get=command({{"command","mirror.get"},{"arguments",{{"object",copy_fixture.mirror}}}});
        const auto pattern_get=command({{"command","pattern.get"},{"arguments",{{"object",copy_fixture.pattern}}}});
        result=launch(executable,root,common+QStringList{"--command",copy_open,"--command",copy_sources,"--command",mirror_get,"--command",pattern_get});
        const auto copy_records=result.results();
        require(result.exit_code==0&&copy_records.size()==4&&copy_records[1].at("data").at("items").size()==2&&
            copy_records[2].at("data").at("source")==copy_fixture.source&&copy_records[2].at("data").at("placement").at("x")==-2&&
            copy_records[3].at("data").at("pattern").at("instance_count")==12&&copy_records[3].at("data").at("name")=="Pole žluťoučké"&&
            copy_records[1].at("data").at("revision")==copy_records[3].at("data").at("revision"),
            "Actual CLI failed persisted copy source/Mirror/Pattern queries or changed revision");
        const auto create_mirror=command({{"command","mirror.create"},{"arguments",{{"source",copy_fixture.combined},{"local_plane","yz"}}}});
        const auto unlock_mirror=command({{"command","value_lock.set"},{"arguments",{{"object",copy_fixture.mirror},{"key","placement:x"},{"locked",false}}}});
        const auto edit_mirror=command({{"command","mirror.set"},{"arguments",{{"object",copy_fixture.mirror},{"source",copy_fixture.other},{"placement",{{"x",-4}}}}}});
        const auto create_pattern=command({{"command","pattern.create"},{"arguments",{{"source",copy_fixture.combined},{"mode","circular"},{"count",3}}}});
        const auto edit_pattern=command({{"command","pattern.set"},{"arguments",{{"object",copy_fixture.pattern},
            {"linear",Json::array({Json{{"axis","x"},{"spacing_mm",40}},Json{{"axis","y"}}})}}}});
        result=launch(executable,root,common+QStringList{"--command",copy_open,"--command",create_mirror,"--command",unlock_mirror,
            "--command",edit_mirror,"--command",create_pattern,"--command",edit_pattern,"--command","save"});
        const auto edited_records=result.results();
        require(result.exit_code==0&&edited_records.size()==7,"Actual CLI could not create/edit Mirror and Pattern");
        std::vector<kernel::BodyResult> copied_cache;const auto copy_saved=document::PartDocument::load(copy_path,&copied_cache);
        const auto new_mirror=edited_records[1].at("data").at("object").get<std::string>();
        const auto new_pattern=edited_records[4].at("data").at("object").get<std::string>();
        require(copy_saved.body_history.find(copy_fixture.mirror)->scope.placement.x==-4&&
            copy_saved.body_history.find(copy_fixture.mirror)->derived_copy->source_id==copy_fixture.other&&
            std::abs(copied_cache.back().body_outputs.at(copy_fixture.mirror)->volume-12)<1e-6&&
            copy_saved.body_history.find(copy_fixture.pattern)->derived_copy->pattern->linear[0].spacing==40&&
            std::abs(copied_cache.back().body_outputs.at(new_mirror)->volume-60)<1e-6&&
            std::abs(copied_cache.back().body_outputs.at(new_pattern)->volume-120)<1e-6,
            "CLI copy properties or independent Boolean-source copy volumes did not persist");
        const auto shell_created=document::PartDocument::load(shell_path).history.back();
        const auto shell_edit=command({{"command","shell.set"},{"arguments",{{"container",shell_created.id},{"thickness_mm",2},
            {"faces",Json::array({Json{{"owner",shell_box.id},{"key","z_max"}}})}}}});
        result=launch(executable,root,common+QStringList{"--command",command({{"command","open"},{"arguments",{{"path",qpath(shell_path).toStdString()}}}}),
            "--command",shell_edit,"--command","undo","--command","redo","--command","save","--command","shell.get "+QString::fromStdString(shell_created.id)});
        require(result.exit_code==0&&result.results().back().at("data").at("thickness_mm")==2,"CLI Shell properties/Undo failed");
        std::vector<kernel::BodyResult> shell_cache;const auto shell_saved=document::PartDocument::load(shell_path,&shell_cache);
        require(shell_saved.history.back().feature_id==shell_created.feature_id&&shell_saved.history.back().shell.removed_faces==std::vector<kernel::FaceReference>{{shell_box.id,"z_max",{}}}&&
            std::abs(shell_cache.back().volume-712)<1e-6,"CLI Shell lost identities or saved a wrong material volume");
        for(const bool fillet:{true,false}) {
            const std::string prefix=fillet?"fillet":"chamfer";
            const auto path=project/(prefix+"-cli.prtz");shell_fixture.save(path,shell_kernel.evaluate_history(shell_fixture.kernel_operations()));
            const auto open=command({{"command","open"},{"arguments",{{"path",qpath(path).toStdString()}}}});
            const auto routes=Json::array({Json{{"edges",Json::array({Json{{"owner",shell_box.id},{"key","edge:x_max:y_min:z_max--x_max:y_min:z_min"}}})}}});
            result=launch(executable,root,common+QStringList{"--command",open,"--command",
                command({{"command",prefix+".create"},{"arguments",{{"routes",routes},{fillet?"radius_mm":"distance_a_mm",2}}}}),"--command","save"});
            require(result.exit_code==0,"Standalone edge treatment creation failed");
            const auto created=document::PartDocument::load(path).history.back();
            const auto get=command({{"command",prefix+".get"},{"arguments",{{"container",created.id}}}});
            result=launch(executable,root,common+QStringList{"--command",open,"--command",get,"--command",
                command({{"command",prefix+".set"},{"arguments",{{"container",created.id},{fillet?"radius_mm":"distance_a_mm",3},{"name","Úprava žluťoučká"}}}}),
                "--command","undo","--command","redo","--command","save","--command",get});
            require(result.exit_code==0&&result.results().size()==7&&result.results().back().at("data").at("name")=="Úprava žluťoučká","Standalone edge treatment edit/query/Undo/Unicode failed");
            std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(path,&cache);
            const auto expected=1000-90*(fillet?1-std::acos(-1.)/4:.5);
            require(saved.history.back().feature_id==created.feature_id&&saved.history.back().edge_treatment.routes==created.edge_treatment.routes&&!cache.empty()&&std::abs(cache.back().volume-expected)<1e-5,
                "Standalone edge treatment lost input identity or saved incorrect volume");
            const auto remove=command({{"command","edge_treatment.remove"},{"arguments",{{"container",created.id},{"route",0},
                {"edge",Json{{"owner",shell_box.id},{"key","edge:x_max:y_min:z_max--x_max:y_min:z_min"}}}}}});
            result=launch(executable,root,common+QStringList{"--command",open,"--command",remove,"--command","undo","--command",get,
                "--command","redo","--command","save"});
            require(result.exit_code==0&&result.results()[1].at("data").at("removed")==true&&result.results()[3].at("data").at("feature")==created.feature_id,
                "Standalone last-route removal or Undo failed");
            std::vector<kernel::BodyResult> restored_input;const auto removed=document::PartDocument::load(path,&restored_input);
            require(!removed.find_container(created.id)&&std::abs(restored_input.back().volume-1000)<1e-5,"CLI last-route removal did not persist the real input body");
        }
        const auto drill_path=project/"drill-cli.prtz";const auto drill_fixture=test::drill_point_fixture();
        kernel::OcctKernel drill_kernel;drill_fixture.save(drill_path,drill_kernel.evaluate_history(drill_fixture.kernel_operations()));
        const auto drill_face=[&](std::size_t index){return Json{{"owner",drill_fixture.history.at(index).id},{"key","z_min"}};};
        const auto make_drill=command({{"command","drill_point.create"},{"arguments",{{"faces",Json::array({drill_face(1),drill_face(2)})}}}});
        result=launch(executable,root,common+QStringList{"--command","open drill-cli.prtz","--command",make_drill,"--command","save"});
        require(result.exit_code==0,"CLI drill-point creation failed");
        const auto drill_created=document::PartDocument::load(drill_path).history.back();
        require(drill_created.name=="Drill point","CLI drill point name was not translated");
        const auto edit_drill=command({{"command","drill_point.set"},{"arguments",{{"container",drill_created.id},{"angle_degrees",120},{"faces",Json::array({drill_face(2)})}}}});
        result=launch(executable,root,common+QStringList{"--command","open drill-cli.prtz","--command",edit_drill,"--command","undo","--command","redo",
            "--command","save","--command","drill_point.get "+QString::fromStdString(drill_created.id)});
        require(result.exit_code==0&&result.results()[5].at("data").at("faces").size()==1&&result.results()[5].at("data").at("angle_degrees")==120,
            "CLI drill-point reference edit/Undo/Redo failed");
        std::vector<kernel::BodyResult> drill_cache;const auto drill_saved=document::PartDocument::load(drill_path,&drill_cache);
        const auto drill_parent=drill_saved.history.back().drill_point.bottom_faces.front();
        require(drill_saved.history.back().feature_id==drill_created.feature_id&&std::abs(drill_cache.back().volume-test::drilled_block_volume(120,false,true))<1e-5&&
            std::ranges::any_of(drill_cache.back().mesh.triangle_references,[&](const auto& ref){return ref.owner_id==drill_created.id&&ref.semantic_key==kernel::drill_point_key("side",drill_parent);}),
            "CLI drill point changed its remaining source identity or saved an incorrect volume");

        const auto appearance_style=command({{"command","appearance.set"},{"arguments",{{"style",{{"color","#5588CC"},{"roughness",.123456789},{"metallic",.75}}}}}});
        result=launch(executable,root,common+QStringList{"--command","new part appearance-cli","--command","box.create 10 20 30",
            "--command","appearance.faces","--command",appearance_style,"--command","undo","--command","redo","--command","save","--command","appearance.get"});
        require(result.exit_code==0&&result.results()[2].at("data").at("total")==6&&result.results()[7].at("data").at("style").at("roughness")==.123456789,"CLI appearance faces/edit/Undo/Redo failed");
        std::vector<kernel::BodyResult> appearance_cache;const auto appearance_saved=document::PartDocument::load(project/"appearance-cli.prtz",&appearance_cache);
        require(appearance_saved.appearance.bodies.at(appearance_saved.body_history.active_body_id()).color=="#5588CC"&&
            std::abs(appearance_cache.back().volume-6000)<1e-6,"CLI appearance persistence changed geometry");

        const auto measure_owner=appearance_saved.history.front().id;
        const auto measure_query=command({{"command","measurement.evaluate"},{"arguments",{{"references",Json::array({
            {{"kind","object"},{"owner",measure_owner}}})}}}});
        const auto measurement_stamp=fs::last_write_time(project/"appearance-cli.prtz");
        result=launch(executable,root,common+QStringList{"--command","open appearance-cli.prtz","--command",measure_query,"--command","measurement.list"});
        require(result.exit_code==0&&result.results()[1]["data"]["values"][0]["volume"]["value"]==6000&&
            result.results()[2]["data"]["total"]==0&&fs::last_write_time(project/"appearance-cli.prtz")==measurement_stamp,
            "CLI measurement evaluation or empty list mutated the native Part");
        auto measurement_row=result.results()[1].at("data");measurement_row["id"]="cli-control";measurement_row["name"]="Control";
        measurement_row["body_id"]=appearance_saved.body_history.active_body_id();measurement_row["after_object_id"]=measure_owner;
        auto measured_part=appearance_saved;measured_part.measurements=document::parse_measurements(Json::array({measurement_row}).dump());
        measured_part.save(project/"measurement-cli.prtz",appearance_cache);
        result=launch(executable,root,common+QStringList{"--command","open measurement-cli.prtz","--command","measurement.get cli-control","--command","measurement.list"});
        require(result.exit_code==0&&result.results()[1]["data"]["saved_values"]==true&&result.results()[2]["data"]["total"]==1&&
            result.results()[1]["data"]["values"]==measurement_row["values"],"CLI could not read native saved measurements");

        const auto create_measurement=command({{"command","measurement.create"},{"arguments",{{"name","Created"},{"references",measurement_row.at("references")}}}});
        result=launch(executable,root,common+QStringList{"--command","open measurement-cli.prtz","--command",create_measurement,"--command","save"});
        require(result.exit_code==0&&result.results()[1]["data"]["changed"]==true,"CLI measurement creation failed");
        const auto measured_id=result.results()[1]["data"]["object"].get<std::string>();
        const auto change_measurement=command({{"command","measurement.set"},{"arguments",{{"object",measured_id},{"name","Changed"},
            {"references",Json::array({{{"kind","plane"},{"owner",appearance_saved.document_id+":origin"},{"key","origin:plane:xy"}},
                {{"kind","face"},{"owner",measure_owner},{"key","z_max"}}})}}}});
        const auto delete_measurement=command({{"command","measurement.delete"},{"arguments",{{"object",measured_id}}}});
        result=launch(executable,root,common+QStringList{"--command","open measurement-cli.prtz","--command",change_measurement,
            "--command",delete_measurement,"--command","undo","--command","redo","--command","undo","--command","save",
            "--command","measurement.get "+QString::fromStdString(measured_id)});
        require(result.exit_code==0&&result.results()[7]["data"]["name"]=="Changed"&&
            std::abs(result.results()[7]["data"]["distance"]["value"]["value"].get<double>()-15)<1e-8,
            "CLI measurement edit/delete/Undo/Redo lost values");
        require(document::PartDocument::load(project/"measurement-cli.prtz").measurements.size()==2,"CLI measurement history lost the existing record");

        auto section_source=appearance_saved;auto section=document::create_section();
        static_cast<void>(section.sketch.add_segment(-20,0,20,0));
        section_source.sections.push_back(section);const auto section_path=project/"section-cli.prtz";
        section_source.save(section_path,appearance_cache);
        const auto section_stamp=fs::last_write_time(section_path);
        result=launch(executable,root,common+QStringList{"--command",command({{"command","open"},{"arguments",{{"path",document::path_to_utf8(section_path)}}}}),
            "--command","section.list","--command",command({{"command","section.get"},{"arguments",{{"object",section.id}}}}),
            "--command",command({{"command","section.components"},{"arguments",{{"object",section.id}}}})});
        require(result.exit_code==0&&result.results()[1].at("data").at("total")==1&&result.results()[2].at("data").at("valid")==true&&
            result.results()[2].at("data").at("sketch")==section.sketch.id&&result.results()[3].at("data").at("total")==1,
            "CLI native Section queries lost definitions or Body options");
        require(fs::last_write_time(section_path)==section_stamp,"Section query rewrote its native document");
        result=launch(executable,root,common+QStringList{"--command",command({{"command","open"},{"arguments",{{"path",document::path_to_utf8(section_path)}}}}),
            "--command",command({{"command","section.activate"},{"arguments",{{"object",section.id}}}}),
            "--command",command({{"command","section.get"},{"arguments",{{"object",section.id}}}}),
            "--command","section.activate","--command",command({{"command","section.delete"},{"arguments",{{"object",section.id}}}}),
            "--command","section.list","--command","undo","--command","save"});
        require(result.exit_code==0&&result.results()[2].at("data").at("show_cut")==true&&result.results()[5].at("data").at("total")==0,
            "CLI Section activation/normal/removal failed");
        const auto restored_section=document::PartDocument::load(section_path);
        require(restored_section.sections.size()==1&&restored_section.sections.front().id==section.id&&!restored_section.sections.front().show_cut,
            "CLI Section Undo did not persist the same inactive definition");

        const auto create_section=command({{"command","section.create"},{"arguments",{{"path_mm",Json::array({Json::array({-20,0}),Json::array({20,0})})},{"name","CLI section"},{"show_cut",true}}}});
        result=launch(executable,root,common+QStringList{"--command","open section-cli.prtz","--command",create_section,"--command","save"});
        require(result.exit_code==0,"CLI Section creation failed");
        const auto created_section=document::PartDocument::load(section_path).sections.back();
        const auto edit_section=command({{"command","section.set"},{"arguments",{{"object",created_section.id},{"name","CLI edited"},{"reversed",true},{"placement",{{"y",2}}},
            {"components",Json::array({{{"component",appearance_saved.body_history.active_body_id()},{"hatch",{{"angle_degrees",12.3456789},{"spacing_mm",2.3456789}}}}})}}}});
        result=launch(executable,root,common+QStringList{"--command","open section-cli.prtz","--command",edit_section,"--command","undo","--command","redo","--command","save"});
        require(result.exit_code==0,"CLI Section properties/Undo/Redo failed");
        std::vector<kernel::BodyResult> section_cache;const auto edited_section=document::PartDocument::load(section_path,&section_cache).sections.back();
        require(edited_section.id==created_section.id&&edited_section.sketch.id==created_section.sketch.id&&edited_section.name=="CLI edited"&&edited_section.reversed&&
            edited_section.placement.y==2&&edited_section.components.at(appearance_saved.body_history.active_body_id()).hatch.angle==12.3456789&&
            std::abs(section_cache.back().volume-6000)<1e-6,"CLI Section properties lost identity, precision or cached geometry");

        const auto section_move=[](const std::string& point,double x) {
            Json args={{"point",point},{"position",Json::array({x,1.})}};
            return Json{{"command","sketch.point.move"},{"arguments",std::move(args)}};
        };
        const auto section_operations=Json::array({section_move(created_section.sketch.points.front().id,-20),section_move(created_section.sketch.points.back().id,20)});
        const auto edit_section_sketch=command({{"command","section.sketch.edit"},{"arguments",{{"object",created_section.id},{"operations",section_operations}}}});
        result=launch(executable,root,common+QStringList{"--command","open section-cli.prtz","--command",edit_section_sketch,"--command","undo","--command","redo","--command","save"});
        require(result.exit_code==0&&result.results()[1].at("data").at("results").size()==2,"CLI Section Sketch batch/Undo/Redo failed");
        const auto edited_trace=document::PartDocument::load(section_path).sections.back();
        require(edited_trace.sketch.id==created_section.sketch.id&&edited_trace.sketch.find_point(created_section.sketch.points.front().id)->y==1&&
            edited_trace.sketch.find_point(created_section.sketch.points.back().id)->y==1,"CLI native Section lost batched point edits");

        const auto make_hole=command({{"command","hole.create"},{"arguments",{{"diameter_mm",10},{"bore_length_mm",10},{"placement",{{"z",-20}}}}}});
        result=launch(executable,root,common+QStringList{"--command","new part native-hole-cli","--command","box.create 40 40 40",
            "--command",make_hole,"--command","save"});
        require(result.exit_code==0,"CLI native Hole creation failed");
        const auto hole_path=project/"native-hole-cli.prtz";const auto hole_created=document::PartDocument::load(hole_path).history.back();
        require(hole_created.feature_kind==document::FeatureKind::Hole&&hole_created.name=="Hole","CLI native Hole kind or translation is wrong");
        const auto hole_id=QString::fromStdString(hole_created.id);
        const auto resize_hole=command({{"command","hole.set"},{"arguments",{{"container",hole_created.id},{"diameter_mm",8},{"entrance_chamfer_mm",1}}}});
        result=launch(executable,root,common+QStringList{"--command","open native-hole-cli.prtz","--command","hole.get "+hole_id,
            "--command",resize_hole,"--command","undo","--command","redo","--command","save","--command","hole.get "+hole_id});
        require(result.exit_code==0&&result.results()[6].at("data").at("diameter_mm")==8,"CLI native Hole edit/history failed");
        std::vector<kernel::BodyResult> hole_cache;const auto hole_saved=document::PartDocument::load(hole_path,&hole_cache);
        require(hole_saved.history.back().hole.circle_id==hole_created.hole.circle_id&&
            std::abs(hole_cache.back().volume-(64000-std::acos(-1.0)*(160+4+1.0/3)))<1e-5,"CLI native Hole lost identity or saved wrong chamfer geometry");

        const auto hole_targets=Json::array({Json{{"owner",hole_saved.document_id+":origin"},{"key","origin:plane:xy"},{"label","Origin XY"}}});
        const auto target_hole=command({{"command","hole.set"},{"arguments",{{"container",hole_created.id},{"bore_end","up_to"},{"bore_targets",hole_targets}}}});
        result=launch(executable,root,common+QStringList{"--command","open native-hole-cli.prtz","--command",target_hole,"--command","undo","--command","redo","--command","save"});
        require(result.exit_code==0,"CLI native Hole original target/history failed");
        const auto targeted_hole=document::PartDocument::load(hole_path,&hole_cache);
        require(targeted_hole.history.back().hole.bore_end_targets.front().reference.owner_id==hole_saved.document_id+":origin"&&
            std::abs(hole_cache.back().volume-(64000-std::acos(-1.0)*(320+4+1.0/3)))<1e-5,"CLI native Hole saved stale target geometry");

        const auto make_opening=command({{"command","opening.create"},{"arguments",{{"type","metric"},{"designation","M10"},
            {"bore_length_mm",20},{"thread_length_mm",10},{"chamfer_enabled",false},{"drill_point_enabled",false},{"placement",{{"z",-20}}}}}});
        result=launch(executable,root,common+QStringList{"--command","new part opening-cli","--command","box.create 40 40 40",
            "--command",make_opening,"--command","save"});
        require(result.exit_code==0,"CLI opening creation failed");
        const auto opening_path=project/"opening-cli.prtz";const auto opening_created=document::PartDocument::load(opening_path).history.back();
        require(opening_created.name=="Hole","CLI opening default name was not translated");
        const auto opening_id=QString::fromStdString(opening_created.id);
        const auto resize_opening=command({{"command","opening.set"},{"arguments",{{"container",opening_created.id},{"bore_length_mm",25}}}});
        result=launch(executable,root,common+QStringList{"--command","open opening-cli.prtz","--command","opening.get "+opening_id,
            "--command",resize_opening,"--command","undo","--command","redo","--command","save","--command","opening.get "+opening_id});
        require(result.exit_code==0&&result.results()[6].at("data").at("bore_length_mm")==25,"CLI opening query/edit/Undo/Redo failed");
        std::vector<kernel::BodyResult> opening_cache;const auto opening_saved=document::PartDocument::load(opening_path,&opening_cache);
        require(opening_saved.history.back().hole.circle_id==opening_created.hole.circle_id&&
            std::abs(opening_cache.back().volume-(64000-std::acos(-1.0)*std::pow(8.376/2,2)*25))<1e-5,"CLI opening lost source identity or saved the wrong volume");

        const auto opening_target=nlohmann::json::array({{{"owner",opening_saved.document_id+":origin"},{"key","origin:plane:xy"}}});
        const auto limit_opening=command({{"command","opening.set"},{"arguments",{{"container",opening_created.id},
            {"bore_end","up_to"},{"bore_targets",opening_target},{"thread_end","up_to"},{"thread_targets",opening_target}}}});
        result=launch(executable,root,common+QStringList{"--command","open opening-cli.prtz","--command",limit_opening,
            "--command","undo","--command","redo","--command","save"});
        require(result.exit_code==0,"CLI original opening end references failed");
        opening_cache.clear();const auto limited_opening=document::PartDocument::load(opening_path,&opening_cache);
        require(limited_opening.history.back().thread.end_targets_forward.front().reference.owner_id==opening_saved.document_id+":origin"&&
            std::abs(opening_cache.back().volume-(64000-std::acos(-1.0)*std::pow(8.376/2,2)*20))<1e-5,"CLI opening target lost its identity or depth");

        for(const bool native:{false,true}) {
            const std::string kind=native?"hole":"opening",container=native?hole_created.id:opening_created.id;
            QStringList component_commands{"--command",native?"open native-hole-cli.prtz":"open opening-cli.prtz"};
            if(native)component_commands<<"--command"<<command({{"command","hole.set"},{"arguments",{{"container",container},{"type","metric"},{"thread_diameter_mm",10},{"thread_pitch_mm",1.5},{"thread_length_mm",8}}}});
            component_commands<<"--command"<<command({{"command",kind+".components"},{"arguments",{{"container",container}}}})
                <<"--command"<<command({{"command",kind+".component.remove"},{"arguments",{{"container",container},{"role","thread"}}}})
                <<"--command"<<"undo"<<"--command"<<"redo"<<"--command"<<"save"
                <<"--command"<<command({{"command",kind+".components"},{"arguments",{{"container",container}}}});
            result=launch(executable,root,common+component_commands);
            require(result.exit_code==0,"CLI component removal or history failed");
            const auto rows=result.results().back().at("data").at("items");
            require(std::ranges::none_of(rows,[](const auto& row){return row.at("role")=="thread";}),"CLI still reports removed thread");
            std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(native?hole_path:opening_path,&cache);
            const auto& feature=*saved.find_container(container);
            require((native?!feature.hole.thread_enabled:!feature.thread.enabled)&&feature.hole.circle_id==(native?hole_created.hole.circle_id:opening_created.hole.circle_id),"CLI removed original profile identity");
            require(std::abs(cache.back().volume-(native?hole_cache.back().volume:opening_cache.back().volume))<1e-5,"CLI thread removal changed bore volume");
        }

        result=launch(executable,root,common+QStringList{"--command","new part shaft-cli","--command","cylinder.create 5 30","--command","save"});
        require(result.exit_code==0,"CLI shaft fixture failed");
        const auto shaft_path=project/"shaft-cli.prtz";
        const auto shaft_owner=document::PartDocument::load(shaft_path).history.front().id;
        const auto make_shaft=command({{"command","shaft_thread.create"},{"arguments",{{"cylinder",{{"owner",shaft_owner},{"key","side"}}},
            {"start",{{"owner",shaft_owner},{"key","z_min"}}},{"designation","M10"},{"length_mm",15}}}});
        result=launch(executable,root,common+QStringList{"--command","open shaft-cli.prtz","--command",make_shaft,"--command","save"});
        require(result.exit_code==0,"CLI shaft creation failed");
        const auto shaft_created=document::PartDocument::load(shaft_path).history.back();
        require(shaft_created.name=="External thread","CLI shaft name was not translated");
        const auto shaft_id=QString::fromStdString(shaft_created.id);
        const auto resize_shaft=command({{"command","shaft_thread.set"},{"arguments",{{"container",shaft_created.id},{"length_mm",20},{"root_diameter_mm",8.05}}}});
        result=launch(executable,root,common+QStringList{"--command","open shaft-cli.prtz","--command",resize_shaft,
            "--command","undo","--command","redo","--command","save","--command","shaft_thread.get "+shaft_id});
        require(result.exit_code==0&&result.results()[5].at("data").at("length_mm")==20&&result.results()[5].at("data").at("root_diameter_mm")==8.05,
            "CLI shaft query/edit/Undo/Redo failed");
        std::vector<kernel::BodyResult> shaft_cache;const auto shaft_saved=document::PartDocument::load(shaft_path,&shaft_cache);
        require(shaft_saved.history.back().feature_id==shaft_created.feature_id&&shaft_saved.history.back().shaft_thread.cylinder==shaft_created.shaft_thread.cylinder&&
            std::abs(shaft_cache.back().volume-750*std::acos(-1.0))<1e-6,"CLI shaft changed original identity or solid volume");

        result = launch(executable, root, common + QStringList{"--command", "new part placement-cli",
            "--command", "box.create 10 20 30", "--command", "save"});
        require(result.exit_code == 0, "CLI placement fixture failed");
        const auto placement_path = project / "placement-cli.prtz";
        const auto placement_native = document::PartDocument::load(placement_path);
        const auto placement_object = placement_native.history.front().id;
        const auto placement_patch = command({{"command", "placement.set"}, {"arguments",
            {{"object", placement_object}, {"values", {{"x", 5}, {"y", 7}, {"rotation_z", 90}}}}}});
        result = launch(executable, root, common + QStringList{"--command", "open placement-cli.prtz",
            "--command", placement_patch, "--command", "undo", "--command", "redo", "--command", "save",
            "--command", "placement.get " + QString::fromStdString(placement_object)});
        require(result.exit_code == 0 && result.results()[5].at("data").at("placement").at("x") == 5, "CLI placement transaction failed");
        std::vector<kernel::BodyResult> placed_bodies;
        const auto placed_native = document::PartDocument::load(placement_path, &placed_bodies);
        require(placed_native.history.front().placement.rotation_z == 90 && std::abs(placed_bodies.back().volume - 6000) < 1e-7,
            "CLI placement lost its saved angle or changed solid volume");

        result=launch(executable,root,common+QStringList{"--command","new part cli-body-references","--command","box.create 10 10 10","--command","body.create Follower","--command","box.create 2 3 4","--command","save"});
        require(result.exit_code==0,"CLI Body reference fixture failed");
        const auto body_reference_path=project/"cli-body-references.prtz";const auto body_reference_fixture=document::PartDocument::load(body_reference_path);
        const auto body_reference_source=body_reference_fixture.body_history.bodies().front().origin().id,body_reference_target=body_reference_fixture.body_history.bodies().back().scope.id;
        const auto body_reference_command=command({{"command","body.reference.set"},{"arguments",{{"body",body_reference_target},{"index",0},{"reference",{{"owner",body_reference_source},{"key","origin:plane:xy"}}},{"offset_mm",7}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-body-references.prtz","--command",body_reference_command,"--command","undo","--command","redo","--command","save","--command","body.get "+QString::fromStdString(body_reference_target)});
        require(result.exit_code==0&&result.results()[5].at("data").at("placement").at("z")==7,"CLI Body reference or history failed");
        const auto referenced_body=document::PartDocument::load(body_reference_path).body_history.find(body_reference_target)->scope.placement;
        require(referenced_body.z==7&&referenced_body.references[0].owner_id==body_reference_source&&referenced_body.references[0].offset==7,"CLI lost native Body source or offset");

        result=launch(executable,root,common+QStringList{"--command","new part cli-cut-source","--command","box.create 10 10 10","--command","save"});
        require(result.exit_code==0,"CLI cut source creation failed");
        const auto cut_source=document::PartDocument::load(project/"cli-cut-source.prtz");
        const auto insert_cut_source=command({{"command","component.insert"},{"arguments",{{"source",cut_source.document_id}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-cut-source.prtz","--command","new assembly cli-profile-cuts","--command",insert_cut_source,"--command",insert_cut_source,"--command","sketch.create CutProfile XY","--command","save"});
        require(result.exit_code==0,"CLI cut Assembly creation failed");
        const auto cut_path=project/"cli-profile-cuts.asmz";const auto cut_fixture=assembly::AssemblyDocument::load(cut_path);
        const auto cut_sketch=cut_fixture.sketches.front().id,cut_target=cut_fixture.components.front().occurrence_id;
        QStringList cut_commands{"--command","open cli-profile-cuts.asmz"};
        for(const auto& points:std::vector<std::array<double,4>>{{-1,-1.5,1,-1.5},{1,-1.5,1,1.5},{1,1.5,-1,1.5},{-1,1.5,-1,-1.5}})
            cut_commands<<"--command"<<command({{"command","sketch.segment.create"},{"arguments",{{"sketch",cut_sketch},{"first",{points[0],points[1]}},{"second",{points[2],points[3]}},{"snap_mm",0.000001}}}});
        cut_commands<<"--command"<<command({{"command","extrusion.create"},{"arguments",{{"sketch",cut_sketch},{"length_forward_mm",4},{"targets",{cut_target}}}}})
                    <<"--command"<<"undo"<<"--command"<<"redo"<<"--command"<<"save"<<"--command"<<"assembly.cut.list";
        result=launch(executable,root,common+cut_commands);
        require(result.exit_code==0&&result.results().back().at("data").at("total")==1,"CLI Assembly cut or history failed");
        const auto cut_saved=assembly::AssemblyDocument::load(cut_path);
        require(cut_saved.cuts.front().target_occurrence_ids==std::vector<std::string>{cut_target}&&cut_saved.sketches.front().owner_container_id==cut_saved.cuts.front().definition.id,"CLI cut lost native ownership");
        require(std::abs(cut_saved.components.front().calculated_source->volume-976)<1e-6&&std::abs(cut_saved.components.back().calculated_source->volume-1000)<1e-6,"CLI cut changed wrong occurrence or volume");

        const auto cut_id=cut_saved.cuts.front().definition.id;
        auto cut_batch=commands::Json::array();
        for(const auto& point:cut_saved.sketches.front().points)if(std::abs(point.x-1)<1e-9)
            cut_batch.push_back({{"command","sketch.point.move"},{"arguments",{{"point",point.id},{"position",{2,point.y}}}}});
        require(cut_batch.size()==2,"CLI batch fixture lacks two right vertices");
        result=launch(executable,root,common+QStringList{"--command","open cli-profile-cuts.asmz",
            "--command",command({{"command","extrusion.sketch.edit"},{"arguments",{{"container",cut_id},{"operations",cut_batch}}}}),
            "--command","undo","--command","redo","--command","save"});
        require(result.exit_code==0&&result.results()[1].at("data").at("body_calculated")==true,"CLI profile batch or history failed");
        const auto batched_cut=assembly::AssemblyDocument::load(cut_path);
        require(std::abs(batched_cut.components.front().calculated_source->volume-964)<1e-6&&batched_cut.sketches.front().id==cut_sketch,"CLI batch lost calculated geometry or Sketch identity");
        result=launch(executable,root,common+QStringList{"--command","open cli-profile-cuts.asmz",
            "--command",command({{"command","assembly.cut.can_move"},{"arguments",{{"container",cut_id}}}}),
            "--command",command({{"command","assembly.cut.move"},{"arguments",{{"container",cut_id}}}}),
            "--command",command({{"command","assembly.cut.suppress"},{"arguments",{{"container",cut_id},{"suppressed",true}}}}),
            "--command",command({{"command","component.get"},{"arguments",{{"instance_path",assembly::InstancePath{}.child(cut_target).encoded()}}}}),
            "--command",command({{"command","assembly.cut.suppress"},{"arguments",{{"container",cut_id},{"suppressed",false}}}}),
            "--command",command({{"command","assembly.cut.remove"},{"arguments",{{"container",cut_id}}}}),
            "--command","undo","--command","redo","--command","save","--command","assembly.cut.list"});
        require(result.exit_code==0&&result.results()[1].at("data").at("would_change")==false&&result.results()[2].at("data").at("changed")==false&&std::abs(result.results()[4].at("data").at("cached_volume_mm3").get<double>()-1000)<1e-6&&result.results().back().at("data").at("total")==0,"CLI cut history or suppression failed");
        const auto removed_cut=assembly::AssemblyDocument::load(cut_path);
        require(removed_cut.cuts.empty()&&removed_cut.sketches.empty()&&std::abs(removed_cut.components.front().calculated_source->volume-1000)<1e-6,"CLI removal left orphan profile data or a cut body");

        const auto create_plane = command({{"command","construction.create"},{"arguments",{{"kind","plane"},{"name","CLI rovina žluťoučká"},
            {"base_plane","yz"},{"offset_mm",10},{"values",{{"x",1},{"y",2},{"z",3},{"rotation_z",90}}}}}});
        result=launch(executable,root,common+QStringList{"--command","new part construction-created","--command",create_plane,"--command","save"});
        require(result.exit_code==0,"CLI construction creation failed");
        const auto construction_created_path=project/"construction-created.prtz";
        const auto created_plane=document::PartDocument::load(construction_created_path).constructions.front();
        require(created_plane.name=="CLI rovina žluťoučká"&&std::abs(created_plane.entity_origin.y-12)<1e-7,"CLI plane has incorrect saved geometry/name");
        const auto update_plane=command({{"command","construction.set"},{"arguments",{{"construction",created_plane.id},{"offset_mm",15}}}});
        result=launch(executable,root,common+QStringList{"--command","open construction-created.prtz","--command",update_plane,
            "--command","undo","--command","redo","--command","save","--command","construction.get "+QString::fromStdString(created_plane.id)});
        require(result.exit_code==0&&result.results()[5].at("data").at("offset_mm")==15,"CLI construction set/Undo/Redo failed");
        const auto saved_plane=document::PartDocument::load(construction_created_path).constructions.front();
        require(saved_plane.entity_id==created_plane.entity_id&&std::abs(saved_plane.entity_origin.y-17)<1e-7,"CLI edit changed plane identity or lost geometry");
        result=launch(executable,root,common+QStringList{"--stdin"},
            "new assembly construction-created\nconstruction.create axis \"CLI osa\"\nsave\n");
        require(result.exit_code==0,"Text stdin construction creation failed");
        const auto saved_axis=assembly::AssemblyDocument::load(project/"construction-created.asmz").constructions.front();
        require(saved_axis.direction_axis=="y"&&saved_axis.display_size==100&&std::abs(saved_axis.direction.y-1)<1e-7,"Text CLI axis parameters lost");
        for(const auto& [file,object]:std::vector<std::pair<std::string,std::string>>{
            {"construction-created.prtz",created_plane.id},{"construction-created.asmz",saved_axis.id}}) {
            const auto remove=command({{"command","construction.delete"},{"arguments",{{"construction",object}}}});
            result=launch(executable,root,common+QStringList{"--command",QString::fromStdString("open "+file),"--command",remove,
                "--command","undo","--command","redo","--command","save","--command","construction.list"});
            require(result.exit_code==0&&result.results()[1].at("data").at("removed").get<bool>()&&
                result.results().back().at("data").at("total")==0,"CLI construction removal/Undo/Redo failed");
            require(file.ends_with("prtz")?document::PartDocument::load(project/file).constructions.empty():
                assembly::AssemblyDocument::load(project/file).constructions.empty(),"CLI construction deletion did not persist in the native document");
        }
        const auto construction_native = test::construction_query_fixture();
        construction_native.save(project / "construction-query.prtz");
        const auto& construction_curve = construction_native.constructions[3];
        const auto child_query = command({{"command", "construction.get"},
            {"arguments", {{"construction", construction_curve.curve_points[1].id}}}});
        result = launch(executable, root, common + QStringList{"--command", "open construction-query.prtz",
            "--command", "construction.list", "--command", child_query});
        require(result.exit_code == 0 && result.results()[1].at("data").at("total") == 7, "CLI construction list failed");
        test::check_construction_child(result.results()[2].at("data"), construction_curve,
            construction_native.body_history.active_body_id());
        auto construction_assembly = assembly::AssemblyDocument::create_default();
        construction_assembly.constructions = construction_native.constructions;
        construction_assembly.save(project / "construction-query.asmz");
        result = launch(executable, root, common + QStringList{"--stdin"},
            QByteArray::fromStdString("open construction-query.asmz\nconstruction.get " + construction_curve.curve_points[1].id + "\n"));
        require(result.exit_code == 0, "CLI Assembly construction query failed");
        test::check_construction_child(result.results()[1].at("data"), construction_curve, {});
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
        auto owned_dxf_document=document::PartDocument::create_default();auto owned_dxf_profile=sketcher::Sketch::create_default();
        auto owned_dxf_feature=document::PartDocument::create_sweep3d_container();
        for(double z:{0.0,20.0}){auto point=document::PartDocument::create_construction(document::ConstructionKind::Point);point.parent_construction_id=owned_dxf_feature.sweep3d.path.id;point.origin={0,0,z};owned_dxf_feature.sweep3d.path.curve_points.push_back(point);}
        owned_dxf_profile.owner_container_id=owned_dxf_feature.id;static_cast<void>(owned_dxf_profile.add_circle(5,5,1));
        owned_dxf_feature.sweep3d.profiles={{owned_dxf_profile.id+":profile",owned_dxf_feature.sweep3d.path.curve_points[0].id,owned_dxf_profile.id,owned_dxf_profile.serialized()}};
        document::BodyHistoryGraph owned_dxf_history;static_cast<void>(owned_dxf_history.create_body("Body"));owned_dxf_history.insert({document::PartHistoryKind::Feature,owned_dxf_feature.id});
        owned_dxf_document.history={owned_dxf_feature};owned_dxf_document.set_body_history(std::move(owned_dxf_history));owned_dxf_document.save(project/"cli-owned-dxf.prtz");
        const auto owned_dxf_import=command({{"command","import.dxf"},{"arguments",{{"path",document::path_to_utf8(dxf_source)},{"sketch",owned_dxf_profile.id}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-owned-dxf.prtz","--command",owned_dxf_import,"--command","undo","--command","redo","--command","save"});
        require(result.exit_code==0&&sketcher::Sketch::from_serialized(document::PartDocument::load(project/"cli-owned-dxf.prtz").history.front().sweep3d.profiles.front().sketch_serialized).segments.size()==1,"CLI embedded DXF import did not persist");
        const auto exported_dxf=project/fs::path(u8"exportovaný obrys.dxf");
        const auto dxf_export=command({{"command","export.dxf"},{"arguments",{{"path",document::path_to_utf8(exported_dxf)},{"sketch",dxf_native.sketches.back().id}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-dxf.prtz","--command",dxf_export});
        require(result.exit_code==0 && result.results()[1].at("data").at("model_changed")==false,"CLI DXF export failed");
        auto dxf_roundtrip=sketcher::Sketch::create_default();require(interchange::import_dxf(exported_dxf,dxf_roundtrip).imported_entities==1,"CLI DXF output is not readable");
        result=launch(executable,root,common+QStringList{"--command","open cli-dxf.prtz","--command",dxf_export});
        require(result.exit_code==1 && result.results()[1].at("code")=="file_exists","CLI exported over an existing file without permission");
        test::write_dxf_points(project/"only-points.dxf");
        result=launch(executable,root,common+QStringList{"--command","new part cli-points","--command","import.dxf only-points.dxf","--command","undo","--command","redo","--command","save"});
        if(result.exit_code!=0)std::cerr<<result.output.toStdString()<<result.diagnostics.toStdString();
        require(result.exit_code==0&&result.results().size()==5&&result.results()[1]["data"]["imported_entities"]==3&&result.results()[1]["data"]["warnings"].empty(),"Standalone CLI POINT import or history failed");
        const auto points_native=document::PartDocument::load(project/"cli-points.prtz");
        require(points_native.sketches.back().points.size()==3&&points_native.sketches.back().import_blocks.size()==1&&points_native.sketches.back().import_blocks[0].geometry_ids.empty(),"CLI lost point-only native block");
        const auto points_export=command({{"command","export.dxf"},{"arguments",{{"path","only-points-roundtrip.dxf"},{"sketch",points_native.sketches.back().id}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-points.prtz","--command",points_export});
        require(result.exit_code==0&&test::read_dxf_entities(project/"only-points-roundtrip.dxf").size()==3,"CLI point-only native roundtrip lost POINT entities");
        test::write_unclamped_dxf(project/"unclamped-splines.dxf");
        result=launch(executable,root,common+QStringList{"--command","new part cli-unclamped-splines","--command","import.dxf unclamped-splines.dxf","--command","undo","--command","redo","--command","save"});
        require(result.exit_code==0&&result.results()[1].at("data").at("imported_entities")==4,"Standalone CLI unclamped spline import failed");
        const auto splines_native=document::PartDocument::load(project/"cli-unclamped-splines.prtz");test::check_unclamped_dxf_sketch(splines_native.sketches.back());
        const auto splines_export=command({{"command","export.dxf"},{"arguments",{{"path","unclamped-roundtrip.dxf"},{"sketch",splines_native.sketches.back().id}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-unclamped-splines.prtz","--command",splines_export});
        require(result.exit_code==0,"CLI native spline DXF export failed");auto splines_roundtrip=sketcher::Sketch::create_default();static_cast<void>(interchange::import_dxf(project/"unclamped-roundtrip.dxf",splines_roundtrip));test::check_unclamped_dxf_sketch(splines_roundtrip);
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
        auto curve_part=document::PartDocument::create_default();curve_part.sketches.push_back(test::dxf_curve_fixture());curve_part.save(project/"cli-dxf-curves.prtz");
        const auto curve_export=command({{"command","export.dxf"},{"arguments",{{"path","exact-curves.dxf"},{"sketch",curve_part.sketches.front().id}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-dxf-curves.prtz","--command",curve_export});
        if(result.exit_code!=0)std::cerr<<result.output.toStdString()<<result.diagnostics.toStdString();
        require(result.exit_code==0&&result.results().size()==2&&result.results()[1].at("data").at("model_changed")==false,"CLI exact DXF export failed");test::check_dxf_curves(project/"exact-curves.dxf");
        result=launch(executable,root,common+QStringList{"--command","new part cli-dxf-roundtrip","--command","import.dxf exact-curves.dxf","--command","undo","--command","redo","--command","save"});
        if(result.exit_code!=0)std::cerr<<result.output.toStdString()<<result.diagnostics.toStdString();
        require(result.exit_code==0&&result.results().size()==5&&result.results()[1].at("data").at("imported_entities")==12&&result.results()[1].at("data").at("warnings").empty(),"CLI exact-curve import or history failed");
        const auto roundtrip_sketch=result.results()[1].at("data").at("sketch");
        const auto roundtrip_export=command({{"command","export.dxf"},{"arguments",{{"path","roundtrip-curves.dxf"},{"sketch",roundtrip_sketch}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-dxf-roundtrip.prtz","--command",roundtrip_export});
        require(result.exit_code==0,"CLI exact-curve native reload failed");test::check_dxf_curves(project/"roundtrip-curves.dxf");
        auto detail_part=document::PartDocument::create_default();detail_part.sketches.push_back(test::dxf_detail_fixture());detail_part.save(project/"cli-dxf-details.prtz");
        const auto detail_export=command({{"command","export.dxf"},{"arguments",{{"path","details.dxf"},{"sketch",detail_part.sketches.front().id}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-dxf-details.prtz","--command",detail_export});
        if(result.exit_code!=0)std::cerr<<result.output.toStdString()<<result.diagnostics.toStdString();
        require(result.exit_code==0&&result.results().size()==2&&result.results()[1]["data"]["model_changed"]==false,"CLI DXF corner/text export failed");
        test::check_dxf_details(project/"details.dxf",detail_part.sketches.front());
        const auto nested_stl_doc=test::nested_stl_fixture(kernel,project);
        nested_stl_doc.save(project/"cli-nested-stl.asmz");
        result=launch(executable,root,common+QStringList{"--command","open cli-nested-stl.asmz","--command","export.stl nested-output.stl","--command","documents"});
        if(result.exit_code!=0)std::cerr<<result.output.toStdString()<<result.diagnostics.toStdString();
        require(result.exit_code==0&&result.results().size()==3&&result.results()[1].at("data").at("model_changed")==false&&result.results()[2].at("data").size()==1,"CLI nested STL loaded dependencies or modified its document");
        test::check_nested_stl(project/"nested-output.stl");
        const auto component_get=command({{"command","component.get"},{"arguments",{{"instance_path",assembly::InstancePath{}.child(component_native.components[0].occurrence_id).encoded()}}}});
        const auto component_dependencies=command({{"command","component.dependencies"},{"arguments",{{"instance_path",assembly::InstancePath{}.child(component_native.components[0].occurrence_id).encoded()}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-components.asmz","--command","component.list","--command",component_get,"--command",component_dependencies});
        require(result.exit_code==0&&result.results()[3].at("data").at("blocked")==false,"Standalone CLI dependency query failed");
        require(result.exit_code==0 && result.results()[1].at("data").at("total")==2 && std::abs(result.results()[2].at("data").at("cached_volume_mm3").get<double>()-6000)<1e-7,"CLI component queries did not read native snapshots");
        const auto property_path=assembly::InstancePath{}.child(component_native.components[1].occurrence_id).encoded();
        const auto fixed_path=assembly::InstancePath{}.child(component_native.components[0].occurrence_id).encoded();
        const Json property_row={{"kind","plane_coincident"},{"offset",7},{"locked",true},{"lower_limit",2},{"upper_limit",9},
            {"component",{{"owner",step_native.document_id+":origin"},{"key","origin:plane:xy"},{"instance_path",property_path}}},
            {"target",{{"owner",step_native.document_id+":origin"},{"key","origin:plane:xy"},{"instance_path",fixed_path}}}};
        const auto property_set=command({{"command","component.set"},{"arguments",{{"instance_path",property_path},{"name","Šroub CLI"},
            {"placement_references",Json::array({property_row})}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-components.asmz","--command",property_set,"--command","undo","--command","redo","--command","save"});
        if(result.exit_code!=0)std::cerr<<result.output.toStdString()<<result.diagnostics.toStdString();
        require(result.exit_code==0&&result.results().size()==5,"Standalone component property edit failed");
        const auto property_native=assembly::AssemblyDocument::load(project/"cli-components.asmz");
        const auto& property_component=property_native.components[1];
        require(property_component.name=="Šroub CLI"&&std::abs(property_component.placement.z-7)<1e-7&&
            property_component.placement_references.size()==1&&property_component.placement_references[0].offset_locked&&
            property_component.placement_references[0].lower_limit==2&&property_component.placement_references[0].upper_limit==9&&
            std::abs(property_component.calculated_source->volume-6000)<1e-7&&property_native.components[0].placement.z==0,
            "CLI native component properties lost exact placement, limits, lock, geometry or occurrence isolation");
        const auto component_open=command({{"command","component.open"},{"arguments",{{"instance_path",assembly::InstancePath{}.child(component_native.components[0].occurrence_id).encoded()}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-components.asmz","--command",component_open,"--command","context"});
        require(result.exit_code==0 && result.results()[1].at("data").at("document")==step_native.document_id && result.results()[1].at("data").at("opened")==true && result.results()[2].at("data").at("active_document")==step_native.document_id,"CLI could not open the exact native source occurrence");
        const auto component_activate=command({{"command","component.activate"},{"arguments",{{"instance_path",fixed_path}}}});
        const auto source_metadata=command({{"command","document.settings.set"},{"arguments",{{"precision",{{"decimal_places",5}}}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-components.asmz","--command",component_activate,
            "--command","context","--command",source_metadata,"--command","undo","--command","redo","--command","save",
            "--command","component.deactivate","--command","context"});
        if(result.exit_code!=0)std::cerr<<result.output.toStdString()<<result.diagnostics.toStdString();
        require(result.exit_code==0&&result.results().size()==9&&result.results()[2].at("data").at("active_document")==step_native.document_id&&
            result.results()[2].at("data").at("displayed_document")==component_native.document_id&&
            result.results()[2].at("data").at("active_occurrence")==fixed_path&&
            result.results()[8].at("data").at("active_document")==component_native.document_id&&
            result.results()[8].at("data").at("active_occurrence")=="","CLI activation did not retain exact context while editing its source");
        require(document::PartDocument::load(project/"cli-step.prtz").document_precision.at("decimal_places")=="5"&&
            assembly::AssemblyDocument::load(project/"cli-components.asmz").components[1].name=="Šroub CLI",
            "CLI activation saved metadata to the wrong native document");
        const auto component_clear=command({{"command","component.set"},{"arguments",{{"instance_path",property_path},{"placement_references",Json::array()}}}});
        const auto component_remove=command({{"command","component.remove"},{"arguments",{{"instance_path",property_path}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-components.asmz","--command",component_clear,"--command",component_remove,
            "--command","undo","--command","redo","--command","save"});
        if(result.exit_code!=0)std::cerr<<result.output.toStdString()<<result.diagnostics.toStdString();
        require(result.exit_code==0&&result.results().size()==6&&result.results()[2].at("data").at("removed")==true,"Standalone component removal failed");
        const auto removed_native=assembly::AssemblyDocument::load(project/"cli-components.asmz");
        require(removed_native.components.size()==1&&removed_native.components.front().occurrence_id==component_native.components.front().occurrence_id&&
            std::abs(removed_native.components.front().calculated_source->volume-6000)<1e-7&&fs::exists(project/"cli-step.prtz"),
            "CLI removal changed the remaining instance or deleted its native source");
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
        auto hatch_source=drawing_source;hatch_source.document_id=document::PartDocument::create_default().document_id;
        auto hatch_section=document::create_section();static_cast<void>(hatch_section.sketch.add_segment(-100,0,100,0));hatch_source.sections={hatch_section};hatch_source.save(project/"cli-hatch.prtz",drawing_source_cache);
        auto hatch_doc=drawing::DrawingDocument::create_default();auto hatch_view=drawing::DrawingDocument::create_view(hatch_source.document_id,"cli-hatch.prtz",drawing_source_cache.back().mesh);
        hatch_view.section_id=hatch_section.id;hatch_view.section_snapshot=hatch_section;hatch_doc.sheets.front().views={hatch_view};hatch_doc.save(project/"cli-hatch.drwz");
        const auto hatch_component=hatch_source.body_history.bodies().front().scope.id;
        const auto hatch_get=command({{"command","drawing.view.hatch.get"},{"arguments",{{"view",hatch_view.id}}}});
        const auto hatch_set=command({{"command","drawing.view.hatch.set"},{"arguments",{{"view",hatch_view.id},{"components",Json::array({{{"component",hatch_component},{"mode","cut_only"},{"hatch",{{"spacing_mm",3.125},{"offset_mm",.375}}}}})}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-hatch.drwz","--command",hatch_get,"--command",hatch_set,"--command","undo","--command","redo","--command","save","--command",command({{"command","activate"},{"arguments",{{"document",hatch_source.document_id}}}}),"--command","save"});
        if(result.exit_code!=0)std::cerr<<result.output.toStdString()<<result.diagnostics.toStdString();
        require(result.exit_code==0&&result.results().size()==8&&result.results()[1]["data"]["items"][0]["component"]==hatch_component&&result.results()[2]["data"]["source_changed"]==true,"Standalone CLI hatch query/edit or history failed");
        require(document::PartDocument::load(project/"cli-hatch.prtz").sections.front().components.at(hatch_component).hatch.spacing_mm==3.125&&drawing::DrawingDocument::load(project/"cli-hatch.drwz").find_view(hatch_view.id)->hidden_hatch_components.contains(hatch_component),"Standalone hatch persistence lost source style or view visibility");
        auto measured_doc=drawing::DrawingDocument::create_default();auto measured_view=native_view;
        const auto measured_curves=drawing::projected_measurement_curves(measured_view);
        const auto measured_curve=std::ranges::find_if(measured_curves,[](const auto& c){return c.line&&c.points.size()>1&&std::hypot(c.points.back().x-c.points.front().x,c.points.back().y-c.points.front().y)>1e-6;});
        require(measured_curve!=measured_curves.end(),"CLI dimension fixture needs a straight projected original edge");
        auto measured_dimension=drawing::make_drawing_dimension(measured_view.id);
        measured_dimension.attachments={{drawing::DimensionAttachmentKind::CurvePoint,measured_curve->source,{},0},{drawing::DimensionAttachmentKind::CurvePoint,measured_curve->source,{},1}};
        drawing::refresh_drawing_dimension(measured_view,measured_dimension);
        measured_doc.sheets.front().views={measured_view};measured_doc.sheets.front().dimensions={measured_dimension};measured_doc.save(project/"cli-measured.drwz");
        const auto measured_get=command({{"command","drawing.dimension.get"},{"arguments",{{"dimension",measured_dimension.id}}}});
        const auto measured_delete=command({{"command","drawing.dimension.delete"},{"arguments",{{"dimension",measured_dimension.id}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-measured.drwz","--command",measured_get,"--command",measured_delete,"--command","drawing.dimension.list","--command","undo","--command","save"});
        require(result.exit_code==0&&result.results()[1].at("data").at("attachments").size()==2&&result.results()[3].at("data").at("total")==0,"CLI measured dimension query/delete failed");
        require(drawing::DrawingDocument::load(project/"cli-measured.drwz").sheets.front().dimensions.front()==measured_dimension,"CLI measured dimension Undo lost persisted references");
        const Json measured_ref={{"owner",measured_curve->source.owner_id},{"key",measured_curve->source.semantic_key},{"instance_path",measured_curve->source.instance_path}};
        const Json start_attachment={{"kind","curve_point"},{"reference",measured_ref},{"parameter",0}},end_attachment={{"kind","curve_point"},{"reference",measured_ref},{"parameter",.5}};
        const auto create_measured=command({{"command","drawing.dimension.create"},{"arguments",{{"view",measured_view.id},{"attachments",Json::array({start_attachment,end_attachment})}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-measured.drwz","--command",create_measured,"--command","save"});
        require(result.exit_code==0&&result.results()[1].at("data").at("state")=="resolved","Standalone CLI dimension creation failed");
        const auto new_measured=result.results()[1].at("data").at("dimension").get<std::string>();
        const auto edit_measured=command({{"command","drawing.dimension.set"},{"arguments",{{"dimension",new_measured},{"style",{{"prefix","CLI="}}},{"layouts",Json::array({Json{{"line_offset",3}}})}}}});
        auto final_attachment=end_attachment;final_attachment["parameter"]=1;
        const auto extend_measured=command({{"command","drawing.dimension.extend"},{"arguments",{{"dimension",new_measured},{"attachment",final_attachment}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-measured.drwz","--command",edit_measured,"--command",extend_measured,"--command","undo","--command","redo","--command","save"});
        require(result.exit_code==0,"Standalone CLI dimension properties/extension failed");
        const auto measured_saved=drawing::DrawingDocument::load(project/"cli-measured.drwz");const auto& new_chain=measured_saved.sheets.front().dimensions.back();
        require(new_chain.id==new_measured&&new_chain.kind==drawing::DrawingDimensionKind::Chain&&new_chain.segments.size()==2&&new_chain.style.prefix=="CLI="&&new_chain.segments[0].layout.line_offset==3,"CLI dimension properties or extension did not persist");
        auto title_doc=drawing::DrawingDocument::create_default();title_doc.source_document_id=drawing_source.document_id;title_doc.source_path=project/"cli-step.prtz";
        drawing::TitleBlockField title_name;title_name.id="NAME";title_name.expression="&name";title_name.editable=true;title_name.write_back=true;
        auto title_local=title_name;title_local.id="LOCAL";title_local.expression="&drawing.note";title_local.write_back=false;
        title_doc.sheets.front().title_block_fields={title_name,title_local};title_doc.save(project/"cli-title.drwz");
        const auto title_sheet=title_doc.sheets.front().id;
        const auto get_title=command({{"command","drawing.title.get"},{"arguments",{{"sheet",title_sheet}}}});
        const auto set_title=command({{"command","drawing.title.set"},{"arguments",{{"sheet",title_sheet},{"values",{{"NAME","CLI český název"},{"LOCAL","Poznámka"}}}}}});
        const auto save_source=command({{"command","activate"},{"arguments",{{"document",drawing_source.document_id}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-title.drwz","--command",get_title,"--command",set_title,"--command","save","--command",save_source,"--command","save"});
        if(result.exit_code!=0)std::cerr<<result.output.toStdString()<<result.diagnostics.toStdString();
        require(result.exit_code==0&&result.results()[2].at("data").at("source_changed")==true,"Standalone CLI title write failed");
        require(document::PartDocument::load(project/"cli-step.prtz").user_parameters.at("name")=="CLI český název"&&drawing::DrawingDocument::load(project/"cli-title.drwz").sheets.front().local_parameters.at("note")=="Poznámka","CLI title/source explicit save did not persist UTF-8 parameters");
        const auto pdf_path=project/fs::path(u8"výkres CLI.pdf");
        const auto pdf_command=command({{"command","export.pdf"},{"arguments",{{"path",document::path_to_utf8(pdf_path)}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-measured.drwz","--command",pdf_command});
        if(result.exit_code!=0)std::cerr<<result.output.toStdString()<<result.diagnostics.toStdString();
        require(result.exit_code==0&&result.results()[1].at("data").at("pages")==1&&fs::file_size(pdf_path)>1000,"Standalone offscreen CLI PDF export failed");
        result=launch(executable,root,common+QStringList{"--command","open cli-measured.drwz","--command",pdf_command});
        require(result.exit_code==1&&result.results()[1].at("code")=="file_exists","CLI PDF silently overwrote an existing file");
        const auto sheet_dxf_path=project/fs::path(u8"výkres CLI.dxf");
        const auto sheet_dxf_command=command({{"command","export.dxf"},{"arguments",{{"path",document::path_to_utf8(sheet_dxf_path)},{"sheet",measured_saved.sheets.front().id}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-measured.drwz","--command",sheet_dxf_command});
        require(result.exit_code==0&&result.results()[1].at("data").at("sheet")==measured_saved.sheets.front().id&&fs::file_size(sheet_dxf_path)>1000,"Standalone offscreen CLI Drawing DXF failed");
        result=launch(executable,root,common+QStringList{"--command","open cli-measured.drwz","--command",sheet_dxf_command});
        require(result.exit_code==1&&result.results()[1].at("code")=="file_exists","CLI Drawing DXF overwrote an existing file");
        for(const auto extension:{"png","jpg"}) {
            const auto image_path=project/fs::path(std::u8string(u8"výřez CLI.")+fs::path(extension).u8string());
            const auto image_command=command({{"command","export.image"},{"arguments",{{"path",document::path_to_utf8(image_path)},{"sheet",measured_saved.sheets.front().id},{"dpi",127},{"crop_mm",{0,0,80,60}}}}});
            result=launch(executable,root,common+QStringList{"--command","open cli-measured.drwz","--command",image_command});
            require(result.exit_code==0&&result.results()[1].at("data").at("width_px")==400&&result.results()[1].at("data").at("height_px")==300&&fs::file_size(image_path)>100,"Standalone CLI image export failed");
        }
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
        {
            test_support::ContextReferenceFixture context(kernel,project);
            auto shifted=context.calculated;auto& exact=shifted.back().mesh.original_references.edges.back();
            for(auto& p:exact.points)p.x+=.02;for(auto& p:exact.exact_spline->poles)p.x+=.02;
            context.source.save(project/"context-source.prtz",shifted);
            const auto activate_context=command({{"command","component.activate"},{"arguments",{{"instance_path",context.target_path.encoded()}}}});
            const auto refresh_context=command({{"command","sketch.reference.refresh"},{"arguments",{{"sketch",context.sketch_id}}}});
            result=launch(executable,root,common+QStringList{"--command","open context-top.asmz","--command",activate_context,"--command",refresh_context,"--command","save"});
            require(result.exit_code==0,"CLI contextual reference refresh failed");
            const auto records=result.results();require(records.size()==4&&records[2].at("data").at("changed").get<bool>()&&
                !records[2].at("data").at("body_calculated").get<bool>()&&records[2].at("data").at("broken_references").empty(),"CLI contextual refresh did not report its exact noncalculating transaction");
            const auto saved=document::PartDocument::load(project/"context-target.prtz");const auto& sketch=saved.sketches.front();
            const auto p=kernel::bspline_value(sketch.supporting_curve(context.curve_id),0);
            require(sketch.external_references.size()==4&&std::abs(p.x-8)<1e-8&&std::abs(p.y-18.02)<1e-8&&
                sketch.external_references.front().context_instance_path==context.target_path.encoded(),"CLI contextual refresh lost exact poles or native occurrence identity");
            const auto other_context=command({{"command","component.activate"},{"arguments",{{"instance_path",context.other_target_path.encoded()}}}});
            result=launch(executable,root,common+QStringList{"--command","open context-top.asmz","--command",other_context,"--command",refresh_context});
            require(result.exit_code==1&&result.results().back().at("code")=="context_reference","CLI refreshed a reference from another occurrence of the same Part");
            auto clean=context.target;const auto references=clean.sketches.front().external_references;
            for(const auto& r:references)clean.sketches.front().remove_geometry(r.id);
            clean.save(project/"context-target.prtz",{});context.inner.dependencies.clear();context.inner.cuts.clear();context.inner.sketches.clear();context.inner.save(project/"context-inner.asmz");
            const auto create_context=command({{"command","sketch.reference.create"},{"arguments",{{"sketch",context.sketch_id},{"kind","edge"},
                {"owner",context.edge.reference.owner_id},{"key",context.edge.reference.semantic_key},{"instance_path",context.source_path.encoded()},{"profile",true}}}});
            const auto activate_owner=command({{"command","component.activate"},{"arguments",{{"instance_path",assembly::InstancePath{{context.target_path.occurrence_ids.front()}}.encoded()}}}});
            const auto rename_context=command({{"command","component.set"},{"arguments",{{"instance_path",assembly::InstancePath{{context.source_path.occurrence_ids.back()}}.encoded()},
                {"name","Renamed before Part reference change"}}}});
            result=launch(executable,root,common+QStringList{"--command","open context-top.asmz","--command",activate_owner,"--command",rename_context,
                "--command",activate_context,"--command",create_context,"--command","save","--command",activate_owner,"--command","undo","--command","save"});
            require(result.exit_code==0,"CLI context creation, Assembly Undo and owner save failed");
            const auto created=result.results()[4].at("data");const auto projected_context=document::PartDocument::load(project/"context-target.prtz");
            require(projected_context.sketches.front().external_references.size()==1&&
                assembly::AssemblyDocument::load(project/"context-inner.asmz").dependencies.size()==1&&
                assembly::AssemblyDocument::load(project/"context-inner.asmz").components.front().name==context.inner.components.front().name,
                "CLI Assembly Undo did not preserve the current reference summary");
            const auto detach_context=command({{"command","sketch.reference.delete"},{"arguments",{{"sketch",context.sketch_id},{"reference",created.at("reference")}}}});
            result=launch(executable,root,common+QStringList{"--command","open context-top.asmz","--command",activate_owner,"--command",rename_context,
                "--command",activate_context,"--command",detach_context,"--command","save","--command",activate_owner,"--command","undo","--command","save"});
            require(result.exit_code==0,"CLI context detach and owner save failed");
            const auto detached_context=document::PartDocument::load(project/"context-target.prtz");
            require(detached_context.sketches.front().external_references.empty()&&
                assembly::AssemblyDocument::load(project/"context-inner.asmz").dependencies.empty(),"CLI detach left a native dependency");
            for(unsigned sample=0;sample<=256;++sample) {
                const auto p=kernel::bspline_value(detached_context.sketches.front().supporting_curve(created.at("geometry").get<std::string>()),sample/256.);
                require(std::abs((p.x-8)*(p.x-8)+(p.y-17.02)*(p.y-17.02)-1)<1e-8,"CLI detach changed the exact projected curve");
            }
            context.inner.add_dependency(assembly::AssemblyDocument::create_dependency(context.target_path.occurrence_ids.back(),
                context.source_path.occurrence_ids.back(),assembly::ComponentDependencyKind::ExternalSketchReference));
            test_support::install_owned_helical_reference(context,kernel);
            std::vector<kernel::BodyResult> before_owned;
            static_cast<void>(document::PartDocument::load(project/"context-target.prtz",&before_owned));
            result=launch(executable,root,common+QStringList{"--command","open context-top.asmz","--command",activate_context,
                "--command","component.deactivate","--command","regenerate","--command",activate_context,"--command","save"});
            require(result.exit_code==0,"CLI Assembly regeneration failed for an owned Helical reference");
            std::vector<kernel::BodyResult> after_owned;
            const auto owned=document::PartDocument::load(project/"context-target.prtz",&after_owned);
            require(owned.sketches.empty() && owned.history.front().feature_kind==document::FeatureKind::HelicalSweep,
                "Owned regeneration fixture unexpectedly has a root Sketch");
            const auto base=sketcher::Sketch::from_serialized(owned.history.front().helical.sketches[0]);
            require(base.external_references.size()==1 && !base.external_references.front().broken && base.external_references.front().exact_spline,
                "CLI regeneration lost the owned exact reference");
            const auto first=base.external_references.front().exact_spline->poles.front();
            require(std::abs(first.x-8)<1e-8 && std::abs(first.y-18.02)<1e-8,
                "CLI regeneration skipped the reference inside the Helical base Sketch");
            require(!after_owned.empty() && std::abs(after_owned.back().volume-before_owned.back().volume)<1e-7,
                "Refreshing a reference-only curve changed the Helical body");
        }
        const auto text_request=command({{"command","sketch.text.create"},{"arguments",{{"sketch",projection_sketch},{"value","Řez Ø10"},{"position",{20,30}},{"height_mm",3},{"modeling_geometry",false}}}});
        result=launch(executable,root,common+QStringList{"--command","open cli-projection.prtz","--command",text_request,"--command","save"});
        require(result.exit_code==0,"Qt-free CLI text generation failed");const auto text_document=document::PartDocument::load(project/"cli-projection.prtz");
        require(text_document.sketches.back().texts.size()==1 && text_document.sketches.back().texts.front().value=="Řez Ø10" && !text_document.sketches.back().texts.front().contours.empty() && !text_document.sketches.back().texts.front().modeling_geometry,"CLI text lost Unicode, glyph outlines or annotation mode");
        const auto curve_create=command({{"command","construction.create"},{"arguments",{{"kind","curve3d"},{"name","Spline CLI"},
            {"curve_type","interpolating_spline"},{"points",Json::array({Json{{"values",{{"x",0},{"y",0}}},{"tangent","+y"}},
                Json{{"values",{{"x",10},{"y",0}}}},Json{{"values",{{"x",10},{"y",10}}}}})}}}});
        for(const auto* type:{"part","assembly"}) {
            const std::string curve_name=std::string("cli-curve-")+type;
            result=launch(executable,root,common+QStringList{"--command",QString::fromStdString("new "+std::string(type)+" "+curve_name),
                "--command",curve_create,"--command","save"});
            require(result.exit_code==0,"Standalone CLI 3D curve creation failed");
            const auto curve_id=result.results()[1].at("data").at("construction").get<std::string>();
            const auto children=result.results()[1].at("data").at("children");
            const auto curve_file=curve_name+(std::string(type)=="part"?".prtz":".asmz");
            const auto curve_update=command({{"command","construction.set"},{"arguments",{{"construction",curve_id},
                {"curve_type","polyline"},{"rounding_enabled",true},{"points",Json::array({Json{{"construction",children[0]}},
                    Json{{"construction",children[1]},{"radius_mm",2}},Json{{"construction",children[2]}}})}}}});
            result=launch(executable,root,common+QStringList{"--command",QString::fromStdString("open "+curve_file),"--command",curve_update,
                "--command","undo","--command","redo","--command","save"});
            require(result.exit_code==0,"Standalone CLI curve point edit/Undo/Redo failed");
            const auto curves=std::string(type)=="part"?document::PartDocument::load(project/curve_file).constructions
                :assembly::AssemblyDocument::load(project/curve_file).constructions;
            const auto& curve=curves.back();
            require(curve.id==curve_id && curve.curve_points[1].id==children[1].get<std::string>() && curve.curve_points[1].curve_radius==2 &&
                curve.curve_points[0].curve_tangent==document::Curve3DTangentMode::PositiveY && document::curve3d_route(curve).segments.size()==3,
                "Native CLI curve roundtrip lost geometry or point identity");
        }
        for (const std::string kind : {"extrusion", "revolution"}) {
            const auto file="cli-profile-"+kind+".prtz";
            std::string target_setup;
            if(kind=="extrusion")for(const auto& [name,offset]:std::vector<std::pair<std::string,double>>{{"Upper",7},{"Lower",-3}})
                target_setup+=command({{"command","construction.create"},{"arguments",{{"kind","plane"},{"name",name},{"base_plane","xy"},{"offset_mm",offset}}}}).toStdString()+"\n";
            result=launch(executable,root,common+QStringList{"--stdin"},QByteArray::fromStdString(
                "new part cli-profile-"+kind+"\n"+target_setup+"sketch.create Profile XY\nsave\n"));
            require(result.exit_code==0,"CLI profile fixture failed");
            const auto setup=result.results();
            const auto sketch=setup[kind=="extrusion"?3:1].at("data").at("sketch");
            const auto rectangle=command({{"command","sketch.rectangle.create"},{"arguments",{{"sketch",sketch},{"first",{2,0}},{"second",{4,10}}}}});
            const auto line=command({{"command","sketch.segment.create"},{"arguments",{{"sketch",sketch},{"first",{0,0}},{"second",{0,10}}}}});
            result=launch(executable,root,common+QStringList{"--command",QString::fromStdString("open "+file),
                "--command",rectangle,"--command",line,"--command","save"});
            require(result.exit_code==0,"CLI profile geometry failed");
            const auto axis=result.results()[2].at("data").at("geometry");
            const auto centerline=command({{"command","sketch.segment.centerline"},{"arguments",{{"sketch",sketch},{"segment",axis},{"centerline",true}}}});
            Json args={{"sketch",sketch},{"name","Profil žluťoučký"}};
            if(kind=="extrusion")args["length_forward_mm"]=5;else args["axis"]=axis;
            result=launch(executable,root,common+QStringList{"--command",QString::fromStdString("open "+file),"--command",centerline,
                "--command",command({{"command",kind+".create"},{"arguments",args}}),"--command","save"});
            require(result.exit_code==0,"CLI profile creation failed");
            const auto id=result.results()[2].at("data").at("container").get<std::string>();
            Json patch={{"container",id},{kind=="extrusion"?"length_forward_mm":"angle_degrees",kind=="extrusion"?8:90}};
            result=launch(executable,root,common+QStringList{"--command",QString::fromStdString("open "+file),
                "--command",command({{"command",kind+".set"},{"arguments",patch}}),"--command","undo","--command","redo",
                "--command","save","--command",command({{"command",kind+".get"},{"arguments",{{"container",id}}}})});
            require(result.exit_code==0 && result.results()[5].at("data").at("sketch")==sketch,"CLI profile set/get/Undo/Redo failed");
            std::vector<kernel::BodyResult> cache;const auto native=document::PartDocument::load(project/file,&cache);
            require(native.history.front().id==id && native.history.front().name=="Profil žluťoučký" &&
                std::abs(cache.back().volume-(kind=="extrusion"?160:30*std::acos(-1.0)))<1e-6,"CLI profile persistence lost its solid or identity");
            const auto thin=command({{"command",kind+".set"},{"arguments",{{"container",id},{"result_type","thin"},{"thin_thickness_mm",.25},{"thin_mode","symmetric"}}}});
            result=launch(executable,root,common+QStringList{"--command",QString::fromStdString("open "+file),"--command",thin,
                "--command","undo","--command","redo","--command","save"});
            require(result.exit_code==0,"CLI Thin profile set/Undo/Redo failed");
            cache.clear();const auto thin_document=document::PartDocument::load(project/file,&cache);
            require(thin_document.history.front().id==id && std::abs(cache.back().volume-(kind=="extrusion"?48:9*std::acos(-1.0)))<1e-6,
                "CLI Thin profile did not persist the calculated wall");
            if(kind=="extrusion") {
                const auto upper=setup[1].at("data").at("entity"),lower=setup[2].at("data").at("entity");
                const auto targets=command({{"command","extrusion.set"},{"arguments",{{"container",id},{"result_type","solid"},{"extent","two_sides"},
                    {"end_forward","up_to"},{"end_reverse","up_to"},{"targets_forward",Json::array({{{"owner",upper},{"key","plane"},{"label","Horní rovina žluťoučká"}}})},
                    {"targets_reverse",Json::array({{{"owner",lower},{"key","plane"}}})}}}});
                result=launch(executable,root,common+QStringList{"--command",QString::fromStdString("open "+file),"--command",targets,
                    "--command","undo","--command","redo","--command","save"});
                require(result.exit_code==0,"CLI target references failed");cache.clear();
                const auto bounded=document::PartDocument::load(project/file,&cache);
                require(std::abs(cache.back().volume-200)<1e-6 && bounded.history.front().extrusion.end_targets_forward.front().label=="Horní rovina žluťoučká",
                    "CLI target reference roundtrip lost geometry or Unicode metadata");
                result=launch(executable,root,common+QStringList{"--command",QString::fromStdString("open "+file),"--command",
                    command({{"command","extrusion.set"},{"arguments",{{"container",id},{"extent","symmetric"}}}}),"--command","save"});
                require(result.exit_code==0,"CLI symmetric target failed");cache.clear();static_cast<void>(document::PartDocument::load(project/file,&cache));
                require(std::abs(cache.back().volume-280)<1e-6,"CLI symmetric target produced an asymmetric solid");
            }
        }
        for(const auto kind:{document::FeatureKind::Sweep2D,document::FeatureKind::Sweep3D,document::FeatureKind::HelicalSweep}) {
            const bool helical=kind==document::FeatureKind::HelicalSweep;
            const std::string prefix=helical?"helical":kind==document::FeatureKind::Sweep2D?"sweep2d":"sweep3d";
            const auto file="cli-"+prefix+".prtz";
            auto native=document::PartDocument::create_default();native.name="cli-"+prefix;
            const auto feature=test_support::sweep_fixture(kind);native.history={feature};
            document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Sweep"));
            graph.insert({document::PartHistoryKind::Feature,feature.id});native.set_body_history(graph);native.resolve_constructions();native.save(project/file);
            const auto get=command({{"command",prefix+".get"},{"arguments",{{"container",feature.id}}}});
            Json patch={{"container",feature.id},{"name","Tažení žluťoučké"}};
            if(helical){patch["pitch_mm"]=10;patch["left_handed"]=true;}
            else{patch["result_type"]="thin";patch["thickness_mm"]=.5;patch["thin_mode"]="symmetric";}
            if(kind==document::FeatureKind::Sweep3D) patch["path"]={{"points",Json::array({
                Json{{"construction",feature.sweep3d.path.curve_points.front().id}},
                Json{{"construction",feature.sweep3d.path.curve_points.back().id},{"values",{{"z",30}}}}
            })}};
            result=launch(executable,root,common+QStringList{"--command",QString::fromStdString("open "+file),"--command",get,
                "--command",command({{"command",prefix+".set"},{"arguments",patch}}),"--command","undo","--command","redo","--command","save","--command",get});
            require(result.exit_code==0&&result.results().size()==7,"Standalone CLI Sweep get/set or Undo/Redo failed");
            require(result.results().back().at("data").at("name")=="Tažení žluťoučké","Sweep CLI lost UTF-8 name");
            std::vector<kernel::BodyResult> cache;const auto reopened=document::PartDocument::load(project/file,&cache);
            require(reopened.history.front().id==feature.id&&reopened.history.front().name=="Tažení žluťoučké"&&!cache.empty(),"Sweep CLI lost native ownership or body cache");
            const double pi=std::acos(-1.0),expected=helical?pi*.25*std::hypot(2*pi*10,10):(kind==document::FeatureKind::Sweep3D?60:40)*pi;
            require(std::abs(cache.back().volume-expected)<(helical?expected*.001:1e-5),"Standalone CLI Sweep saved incorrect volume");
        }
        for(const auto kind:{document::FeatureKind::Sweep2D,document::FeatureKind::Sweep3D}) {
            const bool planar=kind==document::FeatureKind::Sweep2D;const std::string prefix=planar?"sweep2d":"sweep3d";
            const auto feature=test_support::sweep_fixture(kind);
            auto native=test_support::standalone_sweep_sources(feature);native.name="cli-created-"+prefix;
            const auto file=native.name+".prtz";native.save(project/file);
            const auto path=planar?native.sketches.front().id:native.constructions.front().id;
            const auto sketch=native.sketches.back().id;
            const auto point=planar?native.sketches.front().segments.front().first_point_id:native.constructions.front().curve_points.front().id;
            const auto create=command({{"command",prefix+".create"},{"arguments",{{"source_path",path},{"name","Nové tažení"},
                {"profiles",Json::array({Json{{"sketch",sketch},{"point",point}}})}}}});
            result=launch(executable,root,common+QStringList{"--command",QString::fromStdString("open "+file),
                "--command",create,"--command","undo","--command","redo","--command","save","--command",
                planar?command({{"command","sketch.get"},{"arguments",{{"sketch",path}}}})
                    :command({{"command","construction.get"},{"arguments",{{"construction",path}}}})});
            require(result.exit_code==0&&result.results().size()==6,"Standalone Sweep creation failed");
            const auto id=result.results()[1].at("data").at("container");
            require(result.results().back().at("data").at(planar?"owner":"owning_feature")==id,"Standalone creation lost path ownership");
            std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(project/file,&cache);
            const auto& profiles=planar?saved.history.front().sweep2d.profiles:saved.history.front().sweep3d.profiles;
            require(saved.history.size()==1&&saved.sketches.empty()&&saved.constructions.empty()&&
                profiles.front().sketch_id==sketch&&!cache.empty()&&
                std::abs(cache.back().volume-80*std::acos(-1.0))<1e-5,"Standalone creation duplicated inputs or saved incorrect geometry");
            if(planar)require(sketcher::Sketch::from_serialized(saved.history.front().sweep2d.path_sketch).id==path,
                "Standalone 2D creation replaced original path Sketch identity");
        }
        {
            const auto definition=test_support::sweep_fixture(document::FeatureKind::HelicalSweep);
            auto native=test_support::standalone_sweep_sources(definition);native.name="cli-created-helical";
            native.sketches.front().plane_offset=3;native.resolve_constructions();
            const auto file="cli-created-helical.prtz";native.save(project/file);
            const auto create=command({{"command","helical.create"},{"arguments",{
                {"base_sketch",native.sketches[0].id},{"guide_sketch",native.sketches[1].id},{"profile_sketch",native.sketches[2].id},
                {"circle",definition.helical.circle_id},{"start_point",definition.helical.start_point_id},
                {"guide_start_point",definition.helical.guide_start_point_id}}}});
            result=launch(executable,root,common+QStringList{"--command",QString::fromStdString("open "+std::string(file)),
                "--command",create,"--command","undo","--command","redo","--command","save"});
            require(result.exit_code==0&&result.results().size()==5,"Standalone Helical creation failed");
            const auto id=result.results()[1].at("data").at("container");
            require(result.results()[1].at("data").at("base_offset_mm")==3,"Standalone Helical creation lost base offset");
            const auto get=command({{"command","helical.get"},{"arguments",{{"container",id}}}});
            const auto set=command({{"command","helical.set"},{"arguments",{{"container",id},{"base_offset_mm",7},{"pitch_mm",10},{"left_handed",true}}}});
            result=launch(executable,root,common+QStringList{"--command",QString::fromStdString("open "+std::string(file)),
                "--command",set,"--command","save","--command",get});
            require(result.exit_code==0&&result.results().size()==4&&result.results().back().at("data").at("base_offset_mm")==7,
                "Standalone Helical offset edit failed");
            std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(project/file,&cache);
            const double pi=std::acos(-1.0),expected=pi*.25*std::hypot(2*pi*10,10);
            require(saved.sketches.empty()&&saved.history.size()==1&&!cache.empty()&&std::abs(cache.back().volume-expected)<expected*.001,
                "Standalone Helical creation duplicated inputs or saved incorrect geometry");
            for(std::size_t i=0;i<3;++i)require(sketcher::Sketch::from_serialized(saved.history.front().helical.sketches[i]).id==native.sketches[i].id,
                "Standalone Helical creation lost original Sketch identity");
            require(std::abs(sketcher::Sketch::from_serialized(saved.history.front().helical.sketches[0]).resolved_origin.z-7)<1e-8,
                "Standalone Helical save lost the base Sketch offset frame");
        }
        for(const auto kind:{document::FeatureKind::Sweep2D,document::FeatureKind::Sweep3D}) {
            const bool planar=kind==document::FeatureKind::Sweep2D;const std::string prefix=planar?"sweep2d":"sweep3d";
            const auto feature=test_support::sweep_fixture(kind);
            auto sketch=sketcher::Sketch::create_default();auto owner=document::PartDocument::create_sketch_container();
            sketch.owner_container_id=owner.id;static_cast<void>(sketch.add_circle(0,0,3));
            auto native=document::PartDocument::create_default();native.name="cli-"+prefix+"-profiles";
            native.history={owner,feature};native.sketches={sketch};document::BodyHistoryGraph graph;
            static_cast<void>(graph.create_body("Sweep"));graph.insert({document::PartHistoryKind::Feature,owner.id});
            graph.insert({document::PartHistoryKind::Feature,feature.id});native.set_body_history(graph);native.resolve_constructions();
            const auto file=native.name+".prtz";native.save(project/file);
            const auto station=planar?document::PartDocument::sweep2d_route(native.history.back()).stations.back():document::curve3d_route(feature.sweep3d.path).stations.back();
            const auto first=planar?feature.sweep2d.profiles.front().id:feature.sweep3d.profiles.front().id;
            const auto patch=command({{"command",prefix+".set"},{"arguments",{{"container",feature.id},
                {"profiles",Json::array({Json{{"profile",first}},Json{{"sketch",sketch.id},{"point",station.point_id},{"incoming",station.incoming}}})}}}});
            result=launch(executable,root,common+QStringList{"--command",QString::fromStdString("open "+file),"--command",patch,
                "--command","undo","--command","redo","--command","save","--command",
                command({{"command",prefix+".get"},{"arguments",{{"container",feature.id}}}})});
            require(result.exit_code==0&&result.results().size()==6,"Standalone profile adoption failed");
            require(result.results().back().at("data").at("profiles")[1].at("sketch")==sketch.id,"Standalone profile adoption lost Sketch ID");
            std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(project/file,&cache);
            require(saved.history.size()==1&&saved.sketches.empty()&&!cache.empty()&&std::abs(cache.back().volume-380*std::acos(-1.0)/3)<1e-4,
                "Standalone profile adoption duplicated inputs or saved incorrect frustum volume");
            if(planar) {
                const auto set_plane=command({{"command","sweep2d.set"},{"arguments",{{"container",feature.id},
                    {"path_plane",{{"owner",feature.container_origin.id},{"key","origin:plane:xy"},{"offset_mm",7}}}}}});
                result=launch(executable,root,common+QStringList{"--command",QString::fromStdString("open "+file),"--command",set_plane,"--command","save"});
                require(result.exit_code==0&&result.results().size()==3,"Standalone path plane assignment failed");
                cache.clear();const auto with_plane=document::PartDocument::load(project/file,&cache);
                const auto path_sketch=sketcher::Sketch::from_serialized(with_plane.history.front().sweep2d.path_sketch);
                require(with_plane.history.front().sweep2d.path_plane->owner_id==feature.container_origin.id&&
                    std::abs(path_sketch.resolved_origin.z-7)<1e-8&&!cache.empty()&&std::abs(cache.back().volume-380*std::acos(-1.0)/3)<1e-4,
                    "Standalone path plane lost original identity, position or calculated geometry");
            }
        }
        std::cout<<"CLI processes: native files, Unicode/config, scripts/stdin, errors, streaming and explicit geometry calculation passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
