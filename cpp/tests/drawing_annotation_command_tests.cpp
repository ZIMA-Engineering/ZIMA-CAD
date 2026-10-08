#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_annotation_operations.hpp>
#include <zima/document/file_path.hpp>
#include <iostream>
#include "drawing_annotation_layout_test_support.hpp"
#include <zima/document/dimension_layout_json.hpp>
#include <limits>
#include <zima/kernel/profile_centerlines.hpp>
#include <zima/kernel/axis_display.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include "profile_solid_fixture.hpp"
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
Json ref(const drawing::ModelAnnotationReference& r){return {{"source_document",r.document_id},{"owner",r.owner_id},{"key",r.semantic_id},{"instance_path",r.instance_path}};}
void verify_opening_axis_bounds(const kernel::OcctKernel& kernel,fs::path directory) {
    auto part=document::PartDocument::create_default();
    auto block=test::rectangular_feature(part,{100,100,40});
    auto plain=document::PartDocument::create_thread_container();plain.placement={};plain.placement.x=-20;plain.placement.z=-20;
    plain.thread.enabled=false;plain.thread.nominal_diameter=10;plain.thread.bore_length=10;
    plain.thread.chamfer_enabled=false;plain.hole.drill_point_enabled=false;
    auto threaded=document::PartDocument::create_thread_container();threaded.placement={};threaded.placement.x=20;threaded.placement.z=-20;
    threaded.thread.nominal_diameter=10;threaded.thread.pitch=1.5;threaded.thread.length_forward=15;threaded.thread.bore_length=20;
    threaded.thread.chamfer_enabled=false;threaded.hole.drill_point_enabled=false;
    const auto first=plain.id,second=threaded.id,doc=part.document_id;
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Opening bounds"));
    for(const auto id:{block.id,plain.id,threaded.id})graph.insert({document::PartHistoryKind::Feature,id});
    part.history={block,plain,threaded};part.set_body_history(graph);
    const auto path=directory/"opening-axis-bounds.prtz";part.save(path,kernel.evaluate_history(part.kernel_operations()));
    const auto sources=workspace::drawing_annotation_sources(nullptr,doc,path);
    for(const double scale:{.5,1.,2.})for(bool head_on:{false,true}) {
        drawing::DrawingView view;view.source_document_id=doc;view.scale=scale;
        view.camera.vertical=head_on?kernel::Vec3{0,1,0}:kernel::Vec3{0,0,1};
        view.camera.depth=head_on?kernel::Vec3{0,0,1}:kernel::Vec3{0,1,0};
        drawing::refresh_model_annotations(view,sources);unsigned count{};
        for(const auto& item:view.model_annotations)if(item.kind==drawing::ModelAnnotationKind::Axis&&
                (item.source.owner_id==first||item.source.owner_id==second)) {
            const bool a=item.source.owner_id==first;const auto geometry=drawing::axis_annotation_geometry(view,item);
            require(item.model_envelope.valid,"Opening axis has no persisted native cylinder bounds");
            require(geometry.curves.size()==(head_on?4:1),"Opening axis has the wrong projected topology");
            for(const auto& curve:geometry.curves) {
                const double length=std::hypot(curve.back().x-curve.front().x,curve.back().y-curve.front().y);
                // Annotation curves retain model units; the renderer applies
                // view scale. Only the 2 mm paper overhang is converted here.
                const double expected=head_on?(a?5:4.1881)+2/scale:(a?10:20)+4/scale;
                if(std::abs(length-expected)>=1e-6)throw std::runtime_error("Opening axis length="+std::to_string(length)+
                    " expected="+std::to_string(expected)+" head_on="+std::to_string(head_on)+" plain="+std::to_string(a));
            }
            ++count;
        }
        require(count==2,"Opening axis fixture lost an independent hole axis");
        const auto loaded=drawing::deserialize_model_annotations(drawing::serialize_model_annotations(view.model_annotations));
        require(loaded==view.model_annotations,"Opening axis bounds changed during Drawing annotation roundtrip");
    }
}
void verify(const kernel::OcctKernel& kernel,fs::path directory) {
    {
        drawing::DrawingView view;view.source_document_id="axis-part";
        view.camera.vertical={0,1,0};view.camera.depth={0,0,1};
        drawing::ModelAnnotationSource source;source.document_id="axis-part";
        kernel::ViewerEdge line;line.reference={"sketch","segment:axis",{}};
        line.points={{0,0,0},{10,0,0}};line.construction=line.dash_dot=true;
        source.construction={line};
        source.axes.push_back({{5,5,0},{1,0,0},10,{"datum","axis",{}}});
        kernel::RevolutionRequest turn;turn.axis_direction={0,0,1};turn.angle_degrees=90;
        turn.centerlines.origin_enabled=true;turn.centerlines.origin={10,0,0};turn.centerlines.origin_id="origin";
        const auto refs=kernel::profile_centerlines::revolution(turn,"rotation");
        source.construction.push_back(refs.edges.front());
        drawing::refresh_model_annotations(view,std::span(&source,1));
        require(view.model_annotations.size()==3,"Drawing axis annotations missing");
        const auto& straight=view.model_annotations[0].curves.front();
        require(std::abs(std::hypot(straight.back().x-straight.front().x,straight.back().y-straight.front().y)-12)<1e-9,
            "Drawing construction axis has no 1 mm end overhang");
        const auto& curved=view.model_annotations[1].curves.front();
        require(curved.size()==refs.edges.front().points.size()+2&&std::abs(std::hypot(curved[1].x-curved[0].x,curved[1].y-curved[0].y)-1)<1e-9,
            "Drawing rotation axis lost its tangent overhang");
        const auto& axis=view.model_annotations[2].curves.front();
        require(std::abs(std::hypot(axis.back().x-axis.front().x,axis.back().y-axis.front().y)-12)<1e-9,
            "Drawing standalone axis has no matching overhang");
        require(source.construction.front().points==line.points&&source.axes.front().display_length==10,
            "Drawing presentation altered source references");
        const auto saved=drawing::deserialize_model_annotations(drawing::serialize_model_annotations(view.model_annotations));
        require(saved[0].curves==view.model_annotations[0].curves&&saved[1].curves==view.model_annotations[1].curves,
            "Drawing native annotation persistence lost overhangs");
    }
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
        // Origin coordinate axes are deliberately excluded from Drawing
        // annotations. Exercise a drawable model axis across family variants.
        shown_axis.source={"family-part","family-part","axis:main",{}};
        shown_axis.kind=drawing::ModelAnnotationKind::Axis;
        shown_axis.visible=true;
        shown_axis.paper_handles["first"]={3,4};
        family_view.model_annotations={shown_axis};
        drawing::ModelAnnotationSource member_packet;
        member_packet.document_id="family-part:family:long";
        kernel::ViewerAxis member_axis;
        member_axis.reference={"family-part:family:long","axis:main",{}};
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
        auto generic_axis=member_axis;generic_axis.reference.owner_id="family-part";generic_axis.point={5,0,0};
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
int main(int argc,char** argv){try{
    if(argc==3&&std::string_view(argv[1])=="--inspect-axes") {
        const auto file=fs::absolute(argv[2]);auto doc=drawing::DrawingDocument::load(file);
        for(const auto& sheet:doc.sheets)for(auto view:sheet.views) {
            const auto print=[&](const char* state) {
                for(const auto& item:view.model_annotations)if(item.kind==drawing::ModelAnnotationKind::Axis&&item.visible) {
                    const auto geometry=drawing::axis_annotation_geometry(view,item);
                    std::cout<<state<<" view="<<view.id<<" owner="<<item.source.owner_id<<" key="<<item.source.semantic_id
                        <<" envelope="<<item.model_envelope.valid;
                    for(const auto& curve:geometry.curves)if(curve.size()>1)
                        std::cout<<" stroke="<<std::hypot(curve.back().x-curve.front().x,curve.back().y-curve.front().y);
                    std::cout<<'\n';
                }
            };
            std::cout<<"Source "<<view.source_document_id<<" path="<<view.source_path<<'\n';print("stored");
            const auto path=view.source_path.is_absolute()?view.source_path:file.parent_path()/view.source_path;
            const auto sources=workspace::drawing_annotation_sources(nullptr,view.source_document_id,path);
            drawing::refresh_model_annotations(view,sources);print("current");
        }
        return 0;
    }
    kernel::OcctKernel kernel;const auto root=fs::canonical(fs::temp_directory_path());const auto dir=root/("zima-annotations-"+document::PartDocument::create_default().document_id);fs::create_directory(dir);verify(kernel,dir);verify_layouts(kernel,dir);verify_opening_axis_bounds(kernel,dir);require(dir.parent_path()==root,"Unsafe cleanup");fs::remove_all(dir);std::cout<<"Drawing annotation queries, opening bounds, exact occurrences, atomic Show/Erase, Undo and persistence passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
