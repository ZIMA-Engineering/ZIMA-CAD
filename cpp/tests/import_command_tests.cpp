#include <zima/command_host/host.hpp>
#include <zima/workspace/import_operations.hpp>
#include <zima/document/file_path.hpp>
#include <zima/interchange/dxf.hpp>
#include <zima/interchange/step_model.hpp>
#include <IGESControl_Writer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <fstream>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <tuple>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
auto snapshot(const document::PartDocument& doc) {
    std::vector<std::string> sketches;for(const auto& sketch:doc.sketches)sketches.push_back(sketch.serialized());
    return std::tuple{doc.document_id,doc.name,doc.history,doc.body_history,doc.history_order,doc.history_cursor,sketches};
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);
    auto source=document::PartDocument::create_default();source.name="Box source";auto box=document::PartDocument::create_box_container();box.box.length=10;box.box.width=20;box.box.height=30;source.history.push_back(box);
    const auto body=kernel.evaluate_history(source.kernel_operations());kernel.export_step(interchange::step_product(source,body),document::path_to_utf8(dir/fs::path(u8"kvádr.step")));
    IGESControl_Writer writer("MM",1);writer.AddShape(BRepPrimAPI_MakeBox(10,20,30).Shape());require(writer.Write(document::path_to_utf8(dir/fs::path(u8"kvádr.igs")).c_str()),"Cannot write IGES fixture");
    auto profile=sketcher::Sketch::create_default();static_cast<void>(profile.add_rectangle(0,0,20,10));
    const auto dxf_path=dir/fs::path(u8"obrys žluťoučký.dxf");interchange::export_dxf(dxf_path,profile);
    const auto reject=[&](const char* command,Json args) {
        const auto id=live.active_document_id();const auto* part=live.open_part(id);const auto before=snapshot(part->session.document());const auto revision=part->session.revision();
        require(!host.execute({{"command",command},{"arguments",args}}).ok,"Invalid import accepted");
        require(snapshot(live.open_part(id)->session.document())==before && live.open_part(id)->session.revision()==revision,"Rejected import partly committed");
    };
    run(host,"new",{{"type","part"},{"name","imported-step"}});const auto step_doc=live.active_document_id();
    const auto initial=snapshot(live.open_part(step_doc)->session.document());
    const auto result=run(host,"import.step",{{"path","kvádr.step"},{"mesh_deflection_mm",2.0}}).data;
    auto* part=live.open_part(step_doc);require(result.at("bodies").size()==1 && result.at("containers").size()==1 && result.at("body_calculated")==true,"STEP report lost imported ownership");
    require(std::abs(part->session.calculated_boundaries().back().volume-6000)<1e-5,"STEP command changed box scale or volume");
    const auto imported_id=result.at("containers")[0].get<std::string>();const auto* feature=part->session.document().find_container(imported_id);
    require(feature && feature->imported_step.mesh_deflection==2.0 && !feature->imported_step.topology.empty() && feature->imported_step.frozen_brep,"STEP lost selected mesh precision or native topology");
    const auto topology=feature->imported_step.topology;const auto after=snapshot(part->session.document());
    run(host,"undo");require(snapshot(part->session.document())==initial,"Import Undo did not restore input");run(host,"redo");require(snapshot(part->session.document())==after,"Import Redo changed identities");
    reject("import.step",{{"path","kvádr.step"},{"mesh_deflection_mm",0}});reject("import.step",{{"path","kvádr.step"},{"mesh_deflection_mm",-1}});reject("import.step",{{"path","kvádr.igs"}});reject("import.step",{{"path","missing.step"}});
    run(host,"save");fs::remove(dir/fs::path(u8"kvádr.step"));run(host,"regenerate");require(part->session.document().find_container(imported_id)->imported_step.topology==topology && std::abs(part->session.calculated_boundaries().back().volume-6000)<1e-5,"Native STEP lost independent regeneration");
    std::vector<kernel::BodyResult> loaded_body;const auto loaded=document::PartDocument::load(dir/"imported-step.prtz",&loaded_body);require(loaded.find_container(imported_id)->imported_step.topology==topology && std::abs(loaded_body.back().volume-6000)<1e-5,"Saved STEP import lost geometry");
    run(host,"new",{{"type","part"},{"name","imported-iges"}});const auto iges=run(host,"import.iges",{{"path","kvádr.igs"},{"mesh_deflection_mm",1.5}}).data;
    part=live.open_part(live.active_document_id());require(iges.at("bodies").size()==1 && std::abs(part->session.calculated_boundaries().back().volume-6000)<1e-5 && part->session.document().find_container(iges.at("containers")[0].get<std::string>())->imported_step.mesh_deflection==1.5,"IGES command changed geometry or mesh choice");
    run(host,"new",{{"type","part"},{"name","imported-dxf"}});const auto dxf_doc=live.active_document_id();
    run(host,"box.create",{{"length_mm","3"},{"width_mm","4"},{"height_mm","5"}});part=live.open_part(dxf_doc);const auto cache=part->session.calculated_boundaries().back().kernel_shape;
    const auto dxf=run(host,"import.dxf",{{"path",document::path_to_utf8(dxf_path)},{"unitless_scale_mm",25.4}}).data;
    const auto sketch=dxf.at("sketch").get<std::string>();require(dxf.at("body_calculated")==false && dxf.at("imported_entities")==4 && part->session.calculated_boundaries().back().kernel_shape==cache,"DXF import calculated unrelated solid");
    const auto& imported_sketch=part->session.document().sketches.back();require(imported_sketch.name==document::path_to_utf8(dxf_path.stem()) && imported_sketch.import_blocks.front().source_path==document::path_to_utf8(dxf_path),"DXF metadata lost UTF-8 path/name");
    double xmax=0;for(const auto& point:imported_sketch.points)xmax=std::max(xmax,point.x);require(std::abs(xmax-20)<1e-8,"Explicit DXF units did not override fallback scale");
    run(host,"import.dxf",{{"path",document::path_to_utf8(dxf_path)},{"sketch",sketch}});require(part->session.document().sketches.back().import_blocks.size()==2,"DXF did not append independent block to selected Sketch");
    reject("import.dxf",{{"path",document::path_to_utf8(dxf_path)},{"maximum_entities",1}});reject("import.dxf",{{"path",document::path_to_utf8(dxf_path)},{"unitless_scale_mm",0}});reject("import.dxf",{{"path",document::path_to_utf8(dxf_path)},{"sketch","missing"}});
    std::ofstream(dir/"broken.dxf")<<"0\nSECTION\n2";reject("import.dxf",{{"path","broken.dxf"}});
    run(host,"save");const auto dxf_loaded=document::PartDocument::load(dir/"imported-dxf.prtz");require(dxf_loaded.sketches.back().serialized()==part->session.document().sketches.back().serialized(),"DXF lost native block persistence");
    // A worker may complete after a document change or close/reopen. Never commit its obsolete input.
    bool stale=false;try{static_cast<void>(workspace::import_part(live,dxf_doc,dxf_path,{},[&](auto task){task();auto changed=live.open_part(dxf_doc)->session.document();changed.name="Later edit";live.open_part(dxf_doc)->session.commit(std::move(changed),live.open_part(dxf_doc)->session.calculated_boundaries());}));}catch(const workspace::ImportOperationError& e){stale=std::string(e.code)=="document_changed";}
    require(stale && live.open_part(dxf_doc)->session.document().name=="Later edit" && live.open_part(dxf_doc)->session.document().sketches.size()==dxf_loaded.sketches.size(),"Late import overwrote a newer document edit");
    stale=false;try{static_cast<void>(workspace::import_part(live,dxf_doc,dxf_path,{},[&](auto task){task();auto copy=live.open_part(dxf_doc)->session.document();auto boundaries=live.open_part(dxf_doc)->session.calculated_boundaries();require(live.remove(dxf_doc),"Could not remove fixture");live.add_part(std::move(copy),std::move(boundaries),dir/"reopened.prtz");}));}catch(const workspace::ImportOperationError& e){stale=std::string(e.code)=="document_changed";}
    require(stale,"Late import committed into a reopened document");
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-import-command-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Import commands, exact volumes, precision, UTF-8, DXF units, atomicity, Undo and stale workers passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
