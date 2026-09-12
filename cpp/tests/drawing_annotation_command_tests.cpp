#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_annotation_operations.hpp>
#include <zima/document/file_path.hpp>
#include <iostream>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
Json ref(const drawing::ModelAnnotationReference& r){return {{"source_document",r.document_id},{"owner",r.owner_id},{"key",r.semantic_id},{"instance_path",r.instance_path}};}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;auto doc=drawing::DrawingDocument::create_default();
    auto first=drawing::DrawingDocument::create_view("unavailable-source","missing.prtz",{});
    drawing::ModelAnnotation a;a.source={"original-part","sketch","dimension:width","assembly/first"};a.curves={{{0,0},{10,0}}};a.text="10";a.value=10;
    auto b=a;b.source.instance_path="assembly/second";
    auto c=a;c.source.semantic_id="axis:primary";c.kind=drawing::ModelAnnotationKind::Axis;c.visible=true;
    auto missing=a;missing.source.semantic_id="dimension:removed";missing.unresolved=true;missing.visible=true;
    first.model_annotations={a,b,c,missing};auto second=first;second.id=drawing::DrawingDocument::create_view("unavailable-source","missing.prtz",{}).id;
    doc.sheets.front().views={first,second};live.add_drawing(doc,directory/"annotations.drwz");live.activate(doc.document_id);live.display_top_level(doc.document_id);
    command_host::Host host(live,kernel,directory);auto* state=live.open_drawing(doc.document_id);
    const auto initial=state->revision();const auto rows=run(host,"drawing.annotation.list",{{"view",first.id},{"mode","show"},{"kind","dimension"}}).data;
    require(rows.at("total")==2&&rows.at("items")[0].at("reference")!=rows.at("items")[1].at("reference")&&state->revision()==initial,"Query collapsed occurrences, offered missing data or changed history");
    Json show={{"view",first.id},{"mode","show"},{"selected",Json::array({ref(a.source)})}};
    run(host,"drawing.annotation.show_erase",{{"views",Json::array({show})}});
    auto current=state->document().find_view(first.id)->model_annotations;
    require(current[0].visible&&!current[1].visible&&current[2].visible&&current[3].visible,"Show affected another occurrence or annotation kind");
    require(current[0].curves==a.curves&&current[0].value==10,"Visibility modified measuring geometry");
    run(host,"undo");require(!state->document().find_view(first.id)->model_annotations[0].visible,"Show Undo failed");run(host,"redo");
    Json erase={{"view",first.id},{"mode","erase"},{"selection","remove_selected"},{"selected",Json::array({ref(a.source)})}};
    Json show_other={{"view",second.id},{"selected",Json::array({ref(b.source)})}};
    const auto revision=state->revision();run(host,"drawing.annotation.show_erase",{{"views",Json::array({erase,show_other})}});
    require(state->revision()==revision+1&&!state->document().find_view(first.id)->model_annotations[0].visible&&state->document().find_view(second.id)->model_annotations[1].visible,"Multi-view Show/Erase was not one transaction");
    run(host,"undo");require(state->document().find_view(first.id)->model_annotations[0].visible&&!state->document().find_view(second.id)->model_annotations[1].visible,"Batch Undo did not restore both views");
    const auto before=state->revision(),generation=state->data_generation();
    Json wrong={{"view",second.id},{"selected",Json::array({ref(missing.source)})}};
    require(!host.execute({{"command","drawing.annotation.show_erase"},{"arguments",{{"views",Json::array({erase,wrong})}}}}).ok,"Unresolved annotation was accepted");
    require(state->revision()==before&&state->data_generation()==generation&&state->document().find_view(first.id)->model_annotations[0].visible,"Late invalid selection partly committed first view");
    auto bad=show_other;bad["selected"][0]["instance_path"]="assembly/nonexistent";
    require(!host.execute({{"command","drawing.annotation.show_erase"},{"arguments",{{"views",Json::array({bad})}}}}).ok,"Unknown occurrence accepted");
    bad=show_other;bad["selected"][0]["extra"]=true;
    require(!host.execute({{"command","drawing.annotation.show_erase"},{"arguments",{{"views",Json::array({bad})}}}}).ok,"Unknown nested key accepted");
    require(!host.execute({{"command","drawing.annotation.show_erase"},{"arguments",{{"views",Json::array({erase,erase})}}}}).ok,"Duplicate view accepted");
    const auto no_op=run(host,"drawing.annotation.show_erase",{{"views",Json::array({Json{{"view",second.id},{"kinds",Json::array({"dimension"})},{"selected",Json::array()}}})}}).data;
    require(no_op.at("changed")==false&&state->revision()==before,"No-op Show added history");
    run(host,"save");const auto loaded=drawing::DrawingDocument::load(directory/"annotations.drwz");require(loaded.find_view(first.id)->model_annotations==state->document().find_view(first.id)->model_annotations,"Native visibility persistence lost original data");
    auto draft=state->document();const auto visibility=draft.find_view(first.id)->model_annotations[0].visible;
    try{workspace::set_drawing_annotation_visibility(draft,{{first.id,a.source,false},{second.id,missing.source,false}});throw std::logic_error("Unresolved batch accepted");}catch(const workspace::DrawingOperationError&){}
    require(draft.find_view(first.id)->model_annotations[0].visible==visibility,"Shared GUI batch partially changed the draft");
}
}
int main(){try{kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-annotations-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);require(dir.parent_path()==root,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Drawing annotation queries, exact occurrences, atomic Show/Erase, Undo and persistence passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
