#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_annotation_operations.hpp>
#include <zima/document/file_path.hpp>
#include <iostream>
#include "drawing_annotation_layout_test_support.hpp"
#include <zima/document/dimension_layout_json.hpp>
#include <limits>
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
void verify_layouts(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;const auto doc=annotation_layout_test::fixture();const auto view=doc.sheets.front().views.front();
    const auto source=view.model_annotations.front().source;const auto file=directory/"annotation-layout.drwz";
    live.add_drawing(doc,file);live.activate(doc.document_id);live.display_top_level(doc.document_id);
    bool editing=false;command_host::Options options;options.interaction=[&]{command_host::Interaction value;value.editing=editing;return value;};
    command_host::Host host(live,kernel,directory,options);auto* state=live.open_drawing(doc.document_id);
    const auto args=Json{{"view",view.id},{"reference",ref(source)}};
    const auto get=[&]{return run(host,"drawing.annotation.get",args).data;};
    const auto original=get();require(original.at("editable")==true&&original.at("value")==10&&original.at("view_layout").is_null(),"Stored annotation query is wrong");
    auto empty=args;empty["style"]=Json::object();const auto empty_revision=state->revision();
    require(run(host,"drawing.annotation.set",empty).data.at("changed")==false&&state->revision()==empty_revision&&get().at("view_layout").is_null(),"Empty style patch pinned the model defaults");
    auto set=args;set["layout"]={{"text_along",3},{"text_outward",4},{"arrows_reversed",true},{"line_offset",2}};
    set["style"]={{"prefix","REF "},{"decimals",4},{"tolerance_mode","symmetric"},{"symmetric_tolerance","0.02"}};
    const auto revision=state->revision();const auto applied=run(host,"drawing.annotation.set",set).data;
    require(applied.at("changed")==true&&state->revision()==revision+1&&applied.at("value")==10&&applied.at("text").get<std::string>().find("REF ")==0,"Style changed the measurement or failed to project");
    const auto changed=workspace::drawing_annotation(state->document(),view.id,source);
    require(changed.model_dimension==view.model_annotations[0].model_dimension&&changed.model_layout==view.model_annotations[0].model_layout&&changed.source==source,"Local edit changed source data");
    require(changed.text_anchor==drawing::Point2{8,10}&&changed.curves!=view.model_annotations[0].curves,"Layout did not move projected geometry");
    require(state->document().find_view(view.id)->model_annotations[1]==view.model_annotations[1]&&state->document().sheets.front().views[1].model_annotations==doc.sheets.front().views[1].model_annotations,"Layout leaked to another occurrence or view");
    const auto after=state->document();run(host,"undo");require(get().at("view_layout").is_null(),"Annotation layout Undo failed");run(host,"redo");require(annotation_layout_test::snapshot(state->document())==annotation_layout_test::snapshot(after),"Annotation layout Redo failed");
    const auto same=state->revision(),generation=state->data_generation();require(run(host,"drawing.annotation.set",set).data.at("changed")==false&&state->revision()==same&&state->data_generation()==generation,"No-op annotation edit created history");
    const auto reject=[&](Json request,const char* code){const auto snapshot=state->document();const auto before=state->revision();const auto r=host.execute({{"command","drawing.annotation.set"},{"arguments",request}});
        if(r.ok||r.code!=code)throw std::runtime_error(std::string("Expected ")+code+", got "+r.code+": "+r.message);
        require(state->revision()==before&&annotation_layout_test::snapshot(state->document())==annotation_layout_test::snapshot(snapshot)&&!host.change(),"Invalid annotation partially committed");};
    auto bad=set;bad["layout"]["plane_quarter_turns"]=4;reject(bad,"invalid_arguments");
    bad=set;bad["layout"]["envelope_offset"]=-1;reject(bad,"invalid_arguments");
    bad=set;bad["style"]["decimals"]=2.5;reject(bad,"invalid_arguments");
    bad=set;bad["style"]["tolerance_mode"]="bogus";reject(bad,"invalid_arguments");
    bad=set;bad["layout"]["value"]=20;reject(bad,"invalid_arguments");
    bad=set;bad["reference"]["instance_path"]="missing";reject(bad,"annotation_not_found");
    bad=set;bad["view"]="missing";reject(bad,"view_not_found");
    bad=set;bad["document"]="stale";reject(bad,"document_changed");
    bad=set;bad["reference"]=ref(view.model_annotations[3].source);reject(bad,"unsupported_annotation");
    editing=true;reject(set,"editing_in_progress");editing=false;
    bad=set;bad["reference"]=ref(view.model_annotations[2].source);run(host,"drawing.annotation.set",bad);
    require(workspace::drawing_annotation(state->document(),view.id,view.model_annotations[2].source).unresolved,"Editing cached appearance healed a missing source");
    run(host,"save");const auto loaded=drawing::DrawingDocument::load(file);require(annotation_layout_test::snapshot(loaded)==annotation_layout_test::snapshot(state->document())&&live.size()==1,"Native layout persistence changed data or opened missing source");
    auto next=state->document();drawing::ModelAnnotationSource packet;packet.document_id=source.document_id;packet.instance_path=source.instance_path;
    auto updated=*view.model_annotations[0].model_dimension;updated.value=12;updated.witness_second.x=12;updated.line_second.x=12;packet.dimensions={updated};
    auto* refreshed=next.find_view(view.id);drawing::refresh_model_annotations(*refreshed,std::span(&packet,1));
    const auto& renewed=workspace::drawing_annotation(next,view.id,source);
    require(renewed.value==12&&!renewed.unresolved&&renewed.view_layout==changed.view_layout&&renewed.text.find("12")!=std::string::npos,"Source refresh discarded local layout or froze measurement");
    {
        drawing::DrawingView family_view;
        family_view.source_document_id="family-part";
        drawing::ModelAnnotation shown_axis;
        shown_axis.source={"family-part","family-part:origin","axis:x",{}};
        shown_axis.kind=drawing::ModelAnnotationKind::Axis;
        shown_axis.visible=true;
        shown_axis.paper_handles["first"]={3,4};
        family_view.model_annotations={shown_axis};
        drawing::ModelAnnotationSource member_packet;
        member_packet.document_id="family-part:family:long";
        kernel::ViewerAxis member_axis;
        member_axis.reference={"family-part:family:long:origin","axis:x",{}};
        member_axis.point={12,0,0};member_axis.direction={0,0,1};member_axis.display_length=20;
        member_packet.axes={member_axis};
        drawing::refresh_model_annotations(family_view,std::span(&member_packet,1));
        require(family_view.model_annotations.size()==1&&family_view.model_annotations.front().visible&&
            !family_view.model_annotations.front().unresolved&&
            family_view.model_annotations.front().source.document_id==member_packet.document_id&&
            family_view.model_annotations.front().source.owner_id==member_axis.reference.owner_id&&
            family_view.model_annotations.front().paper_handles==shown_axis.paper_handles&&
            family_view.model_annotations.front().curves.front().front().x==-12,
            "Family variant refresh lost a shown axis or retained its old projection");
        drawing::refresh_model_annotations(family_view,{});
        require(family_view.model_annotations.front().visible&&family_view.model_annotations.front().unresolved,
            "A missing Family variant axis did not retain its Show intent while hidden");
        drawing::ModelAnnotationSource generic_packet;
        generic_packet.document_id="family-part";
        auto generic_axis=member_axis;generic_axis.reference.owner_id="family-part:origin";generic_axis.point={5,0,0};
        generic_packet.axes={generic_axis};
        drawing::refresh_model_annotations(family_view,std::span(&generic_packet,1));
        require(family_view.model_annotations.size()==1&&family_view.model_annotations.front().visible&&
            !family_view.model_annotations.front().unresolved&&
            family_view.model_annotations.front().source.document_id==generic_packet.document_id&&
            family_view.model_annotations.front().curves.front().front().x==-5,
            "Returning to the generic Family source did not restore and reproject its axis");
    }
    const auto invalid_before=annotation_layout_test::snapshot(next);auto invalid=renewed.view_layout.value();invalid.text_along=std::numeric_limits<double>::infinity();
    try{workspace::set_drawing_annotation_layout(next,view.id,source,invalid);throw std::logic_error("Infinite layout accepted");}catch(const workspace::DrawingOperationError&){}
    require(annotation_layout_test::snapshot(next)==invalid_before,"Shared GUI operation changed data before validation");
    auto* ambiguous=next.find_view(view.id);ambiguous->model_annotations.push_back(renewed);const auto duplicate_before=annotation_layout_test::snapshot(next);
    try{workspace::set_drawing_annotation_layout(next,view.id,source,{});throw std::logic_error("Ambiguous annotation accepted");}catch(const workspace::DrawingOperationError& error){require(std::string(error.code)=="ambiguous_reference","Wrong duplicate error");}
    require(annotation_layout_test::snapshot(next)==duplicate_before,"Ambiguous annotation changed data");
    for(const auto kind:{kernel::ViewerDimensionKind::Angular,kernel::ViewerDimensionKind::Radius,kernel::ViewerDimensionKind::Diameter}) {
        auto variant=doc;auto& target_view=variant.sheets.front().views.front();auto& item=target_view.model_annotations.front();auto& dimension=*item.model_dimension;
        dimension.kind=kind;dimension.witness_second={5,0,0};dimension.line_first={5,0,0};dimension.line_second={0,5,0};dimension.sweep_degrees=90;
        dimension.value=kind==kernel::ViewerDimensionKind::Angular?90:kind==kernel::ViewerDimensionKind::Radius?5:10;
        dimension.unit_suffix=kind==kernel::ViewerDimensionKind::Angular?"°":"mm";
        item=drawing::project_model_annotation(target_view,item);const auto original_dimension=item.model_dimension;state->commit(std::move(variant));
        auto request=args;request["layout"]={{"line_offset",2},{"text_along",2},{"text_outward",3},{"radius_rotation_degrees",45},{"arrows_reversed",true}};
        const auto result=run(host,"drawing.annotation.set",request).data;const auto& output=workspace::drawing_annotation(state->document(),view.id,source);
        require(output.model_dimension==original_dimension&&output.value==original_dimension->value&&result.at("dimension_kind")!="linear","Annotation edit changed radial/angular measurement or kind");
        if(kind==kernel::ViewerDimensionKind::Angular)require(output.curves[0].size()==31&&std::abs(output.curves[0].front().x-7)<1e-9&&std::abs(output.curves[0].front().y)<1e-9,"Angular layout did not extend radius by 2 mm");
        else {const auto end=output.curves[1].back();require(std::abs(end.x-5/std::sqrt(2.0))<1e-9&&std::abs(end.y-5/std::sqrt(2.0))<1e-9,"Radial layout did not rotate its 5 mm radius by 45 degrees");}
    }

}

}
int main(){try{kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-annotations-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);verify_layouts(kernel,dir);require(dir.parent_path()==root,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Drawing annotation queries, exact occurrences, atomic Show/Erase, Undo and persistence passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
