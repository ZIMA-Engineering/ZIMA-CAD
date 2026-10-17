#include "profile_solid_fixture.hpp"
#include "assembly_profile_test_support.hpp"
#include <zima/workspace/assembly_history_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/measurement_operations.hpp>
#include <zima/command_host/host.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <filesystem>
#include <iostream>
using namespace zima;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static void verify_mixed_history(const kernel::OcctKernel& kernel,const std::filesystem::path& directory) {
    assembly_profile_test::Fixture f(kernel,directory,"mixed-cursor");
    f.run("history.cursor",{{"index",1}});
    const std::string point=f.run("construction.create",{{"kind","point"},{"name","Middle point"}}).at("construction");
    const std::string axis=f.run("construction.create",{{"kind","axis"},{"name","Middle axis"}}).at("construction");
    const std::string plane=f.run("construction.create",{{"kind","plane"},{"name","Middle plane"}}).at("construction");
    const auto station=[](double x,double y){return commands::Json{{"values",{{"x",x},{"y",y},{"z",0}}}};};
    const std::string curve=f.run("construction.create",{{"kind","curve3d"},{"name","Middle curve"},{"curve_type","polyline"},
        {"points",{station(0,0),station(2,0),station(2,2)}}}).at("construction");
    const auto sketch=f.rectangle(-4,-4,1,1);
    const auto owner=f.doc().sketches.back().owner_container_id;
    require(f.doc().history_order.back().id==f.second&&f.doc().effective_history_cursor()==6,
        "Mixed entries did not insert before the later component");
    const auto uncut=f.doc().find_occurrence(f.first)->calculated_source;
    const auto cut=f.run("feature.create",{{"sketch",sketch},{"parameters",zima::test::feature_profile_parameters(2)},{"targets",{f.first}}}).at("container").get<std::string>();
    require(cut==owner&&f.doc().history_order[5].kind==assembly::AssemblyDocument::HistoryKind::Cut,
        "Sketch conversion moved its history identity");
    assembly_profile_test::near(f.volume(f.first),998);
    const auto final=f.doc().find_occurrence(f.first)->calculated_source;
    f.run("history.cursor",{{"index",5}});
    auto prefix=f.doc().history_view_document();
    require(prefix.components.size()==1&&prefix.cuts.empty()&&prefix.sketches.empty()&&prefix.constructions.size()==4&&
        prefix.history_visible(prefix.find_construction(curve)->curve_points.front().id),
        "Cut boundary did not hide its owned Sketch and later component");
    require(document::serialize_body_result(prefix.find_occurrence(f.first)->calculated_source)==
        document::serialize_body_result(f.doc().find_cut(cut)->input_component_bodies.at(f.first)),
        "Cut boundary did not restore its exact persisted input snapshot");
    require(f.doc().find_occurrence(f.first)->calculated_source.shares_with(final),
        "Cursor replaced authoritative final source data");
    require(prefix.find_occurrence(f.first)->calculated_source->kernel_shape==uncut->kernel_shape,
        "Cut boundary did not restore the exact uncut native shape");
    assembly_profile_test::near(prefix.find_occurrence(f.first)->calculated_source->volume,1000);
    const auto inspection_revision=f.state().session.revision();
    const auto inspection=workspace::resolve_measurement(f.live,f.owner,
        {kernel::MeasurementKind::Object,{},{},assembly::InstancePath{}.child(f.first).encoded()},
        workspace::measurement_scene(f.live,f.owner));
    require(inspection&&inspection->values.volume&&inspection->values.area,"History input measurement is unavailable");
    assembly_profile_test::near(inspection->values.volume->value,1000);
    assembly_profile_test::near(inspection->values.area->value,600);
    require(f.state().session.revision()==inspection_revision&&f.doc().find_occurrence(f.first)->calculated_source.shares_with(final),
        "History input measurement modified the source or created Undo");
    f.set_feature(cut,[](auto& parameters){parameters["sides"][0]["length"]=10;});
    const auto input_scene=workspace::measurement_scene(f.live,f.owner);
    const auto path=assembly::InstancePath{}.child(f.first).encoded();
    const auto top=std::ranges::find_if(input_scene.original_references.triangle_references,[&](const auto& face){
        return face.instance_path==path&&face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
            face.surface->origin.z>4.99&&std::abs(face.surface->axis.z)>.99;
    });
    require(top!=input_scene.original_references.triangle_references.end(),"Input top face is unavailable");
    const kernel::MeasurementReference face_request{kernel::MeasurementKind::Face,top->owner_id,top->semantic_key,path};
    const auto measure_area=[&](const kernel::ViewerMesh& scene){
        const auto measured=workspace::resolve_measurement(f.live,f.owner,face_request,scene);
        require(measured&&measured->values.area,"Input face area is unavailable");return measured->values.area->value;
    };
    assembly_profile_test::near(measure_area(input_scene),100);
    f.run("history.cursor",{{"index",6}});
    // Assembly cuts retain original component face references. Their area
    // remains the original face's area, while whole-component volume follows
    // the displayed calculated solid boundary.
    const auto cut_scene=workspace::measurement_scene(f.live,f.owner);
    assembly_profile_test::near(measure_area(cut_scene),100);
    const auto cut_measurement=workspace::resolve_measurement(f.live,f.owner,
        {kernel::MeasurementKind::Object,{},{},path},cut_scene);
    require(cut_measurement&&cut_measurement->values.volume,"Calculated cut inspection is unavailable");
    assembly_profile_test::near(cut_measurement->values.volume->value,995);
    f.run("history.cursor",{{"index",5}});
    const auto read_revision=f.state().session.revision();
    assembly_profile_test::near(measure_area(workspace::measurement_scene(f.live,f.owner)),100);
    require(f.state().session.revision()==read_revision,"Requested input face analysis created Undo");
    f.run("history.cursor",{{"index",2}});prefix=f.doc().history_view_document();
    require(prefix.find_construction(point)&&!prefix.find_construction(axis)&&!prefix.find_construction(plane),
        "Construction visibility does not follow unified history");
    const auto later=f.rectangle(2,2,1,1);
    f.reject("feature.create",{{"sketch",later},{"parameters",zima::test::feature_profile_parameters(1)},{"targets",{f.second}}},"profile_rejected");
    f.run("undo");f.run("redo");
    f.run("history.cursor",{{"index",f.doc().history_order.size()}});f.run("save");
    const auto restored=assembly::AssemblyDocument::load(directory/"mixed-cursor.asmz",{},false);
    require(restored.serialized()==f.doc().serialized(),"Mixed history native roundtrip changed definitions or calculated data");
    f.run("new",{{"type","assembly"},{"name","mixed-parent"}});
    const auto parent=f.live.active_document_id();
    const std::string passive=f.run("component.insert",{{"source",f.owner}}).at("occurrence");
    const std::string active=f.run("component.insert",{{"source",f.owner}}).at("occurrence");
    f.run("component.activate",{{"instance_path",assembly::InstancePath{}.child(active).encoded()}});
    const auto current=f.doc().complete_history_order();
    f.run("history.cursor",{{"index",std::distance(current.begin(),std::ranges::find(current,cut,&assembly::AssemblyDocument::HistoryEntry::id))}});
    const auto parent_revision=f.live.open_assembly(parent)->session.revision(),local_revision=f.state().session.revision();
    const auto nested_scene=workspace::measurement_scene(f.live,parent);
    for(const auto& [outer,expected]:std::array<std::pair<std::string,double>,2>{{{passive,995},{active,1000}}}) {
        const auto measured=workspace::resolve_measurement(f.live,parent,
            {kernel::MeasurementKind::Object,{},{},assembly::InstancePath{}.child(outer).child(f.first).encoded()},nested_scene);
        require(measured&&measured->values.volume,"Nested history measurement is unavailable");
        assembly_profile_test::near(measured->values.volume->value,expected);
    }
    require(f.live.open_assembly(parent)->session.revision()==parent_revision&&f.state().session.revision()==local_revision,
        "Nested input inspection modified its owner or parent");
}
int main(){try {
    kernel::OcctKernel kernel;workspace::Workspace live;
    auto source=document::PartDocument::create_default();source.history.clear();source.sketches.clear();
    auto stock=test::rectangular_feature(source,{30,30,10});source.history.push_back(stock);
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Stock"));
    graph.insert({document::PartHistoryKind::Feature,stock.id});source.set_body_history(graph);source.resolve_constructions();
    const auto source_id=source.document_id;live.add_part(source,kernel.evaluate_history(source.kernel_operations()));
    auto model=assembly::AssemblyDocument::create_default();const auto id=model.document_id;
    live.add_assembly(std::move(model));live.activate(id);
    const auto first=live.insert_open_part(id,source_id,"First");
    const auto second=live.insert_open_part(id,source_id,"Second");
    auto* state=live.open_assembly(id);const auto saved_body=state->session.document().find_occurrence(first)->calculated_source;
    require(workspace::set_assembly_history_cursor(live,id,1),"Assembly cursor did not move");
    const auto prefix=state->session.document().history_view_document();
    require(prefix.components.size()==1&&prefix.components.front().occurrence_id==first,"Cursor did not hide later component");
    require(state->session.document().find_occurrence(first)->calculated_source.shares_with(saved_body),"Cursor changed calculated source data");
    const auto third=live.insert_open_part(id,source_id,"Inserted");
    const auto& order=state->session.document().history_order;
    require(order.size()==3&&order[0].id==first&&order[1].id==third&&order[2].id==second&&
        state->session.document().effective_history_cursor()==2,"Component did not insert at the cursor");
    require(state->session.undo()&&!state->session.document().find_occurrence(third)&&
        state->session.document().effective_history_cursor()==1,"Insertion Undo lost cursor or component identity");
    require(state->session.redo()&&state->session.document().find_occurrence(third),"Insertion Redo lost identity");
    const auto root=state->session.document().serialized();
    const auto restored=assembly::AssemblyDocument::from_serialized(root);
    require(restored.history_order==state->session.document().history_order&&restored.effective_history_cursor()==2,
        "Native definition lost mixed history or cursor");
    auto invalid=root;invalid["history_order"][0]["id"]="missing";bool rejected=false;
    try{static_cast<void>(assembly::AssemblyDocument::from_serialized(invalid));}catch(const std::exception&){rejected=true;}
    require(rejected,"Native history accepted a missing identity");
    const auto path=std::filesystem::temp_directory_path()/("zima-assembly-history-"+id+".asmz");
    state->session.document().save(path);
    require(assembly::AssemblyDocument::load(path,{},false).serialized()==root,"Native save/reopen changed history or source data");
    std::filesystem::remove(path);
    require(workspace::set_assembly_history_cursor(live,id,0)&&state->session.document().history_view_document().components.empty(),
        "Start cursor did not hide all components");
    require(workspace::set_assembly_history_cursor(live,id,3)&&state->session.document().history_view_document().components.size()==3,
        "End cursor did not restore all components");
    const auto revision=state->session.revision();
    require(!workspace::set_assembly_history_cursor(live,id,3)&&state->session.revision()==revision,"Unchanged cursor created an Undo step");
    const auto parent=std::filesystem::canonical(std::filesystem::temp_directory_path());
    const auto directory=parent/("zima-mixed-cursor-"+id);std::filesystem::create_directory(directory);
    verify_mixed_history(kernel,directory);
    require(std::filesystem::canonical(directory).parent_path()==parent,"Unsafe fixture cleanup");std::filesystem::remove_all(directory);
    std::cout<<"Assembly history cursor: cached component visibility, middle insertion, identities, Undo/Redo and native reopen passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
