#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/mirror_geometry.hpp>
#include <zima/document/part_document.hpp>
#include <zima/assembly/assembly_document.hpp>
#include <zima/workspace/workspace.hpp>
#include <iostream>
#include <set>
#include <filesystem>
using namespace zima;
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static void close(double a,double b){require(std::abs(a-b)<1e-7,"Mirror changed a geometric measure");}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,"Invalid Mirror accepted");}
static kernel::BoxRequest box(double length){kernel::BoxRequest result{length,5,7};result.translation={4,2,1};return result;}
int main(){try{
    kernel::OcctKernel kernel;
    const auto source=kernel.evaluate_history({{"box",box(3)}}).back();
    for(auto plane:{kernel::MirrorPlane{{0,0,0},{1,0,0}},kernel::MirrorPlane{{3,-2,5},{1,2,3}},kernel::MirrorPlane{{4,0,0},{0,1,0}}}) {
        const auto result=kernel.mirror_body(source,plane,"mirror");close(result.volume,source.volume);close(result.surface_area,source.surface_area);
        close(kernel.compound_bodies({{result,{},{}}}).volume,source.volume);
        const auto twice=kernel.mirror_body(kernel.mirror_body(source,plane),plane);
        require(twice.mesh.vertices.size()==source.mesh.vertices.size(),"Reflection changed tessellation identity");
        for(std::size_t i=0;i<source.mesh.vertices.size();++i){const auto a=source.mesh.vertices[i],b=twice.mesh.vertices[i];close(a.x,b.x);close(a.y,b.y);close(a.z,b.z);}
        require(result.mesh.triangles[1]==source.mesh.triangles[2],"Mirror did not reverse winding");
        for(const auto& ref:result.mesh.original_references.triangle_references)if(ref.valid()){
            require(ref.owner_id=="mirror","Mirror reused the original body owner");
            const auto parent=kernel::mirror_parent_reference(ref.semantic_key);require(parent&&parent->owner_id=="box","Mirror lost its persisted topology parent");}
    }
    document::BodyHistoryGraph graph;const auto source_id=graph.create_body("Zdroj");graph.insert({document::PartHistoryKind::Feature,"box"});
    document::BodyHistory body;body.name="Zrcadlo";body.scope.id="mirror-body";body.derived_copy=document::DerivedCopyParameters{source_id,{{},"mirror-body:origin","origin:plane:yz"}};
    body.scope.placement.x=2;document::PartDocument::resolve_copy_reference(*body.derived_copy,body.scope.id,body.scope.placement,{});close(body.derived_copy->resolved_plane.point.x,2);
    const auto mirrored_id=graph.create_derived_copy(body);
    const auto compile=[&](double length){return graph.compile([&](const auto& e)->std::optional<kernel::HistoryOperation>{return kernel::HistoryOperation{e.id,box(length)};});};
    auto result=kernel.evaluate_history(compile(3));close(result.back().volume,210);
    close(result.back().body_outputs.at(mirrored_id).volume,105);
    result=kernel.evaluate_history_incremental(compile(6),result);close(result.back().body_outputs.at(mirrored_id).volume,210);
    require(graph.available_before(graph.order().size()).size()==2,"Mirror consumed its source");
    rejects([&]{graph.activate(mirrored_id);});rejects([&]{graph.move_step(mirrored_id,0);});
    auto restored=document::BodyHistoryGraph::from_serialized(graph.serialized());require(restored.find(mirrored_id)->derived_copy==body.derived_copy,"Mirror parameters lost on reload");
    auto boolean_graph=graph;const auto joined=boolean_graph.create_boolean("Součet",kernel::BodyCombination::Add,source_id,mirrored_id);
    const auto boolean_operations=boolean_graph.compile([&](const auto& e)->std::optional<kernel::HistoryOperation>{return kernel::HistoryOperation{e.id,box(3)};});
    const auto boolean_body=kernel.evaluate_history(boolean_operations).back().body_outputs.at(joined);
    const auto mirrored_boolean=kernel.mirror_body(boolean_body,{{20,0,0},{1,0,0}},"copy-of-boolean");
    close(kernel.compound_bodies({{mirrored_boolean,{},{}}}).volume,210);
    require(std::ranges::all_of(mirrored_boolean.mesh.triangle_references,[](const auto& ref){return ref.owner_id=="copy-of-boolean";}),"Boolean copy has no whole-container display ownership");
    require(std::ranges::none_of(mirrored_boolean.mesh.original_references.triangle_references,[](const auto& ref){return ref.semantic_key=="container:display";}),"Display ownership leaked into original topology");
    assembly::AssemblyDocument assembly=assembly::AssemblyDocument::create_default();
    auto component=assembly::AssemblyDocument::create_part_occurrence("Zdroj","source-part",{},source);component.placement.x=10;
    auto mirror=assembly::AssemblyDocument::create_part_occurrence("Zrcadlo","source-part",{},{});
    mirror.derived_copy=document::DerivedCopyParameters{component.occurrence_id,{{},mirror.occurrence_id+":origin","origin:plane:yz"}};
    mirror.copy_placement.x=1;assembly.components={component,mirror};assembly.calculate_derived_copies(kernel);
    const auto& reflected=assembly.components.back();close(reflected.calculated_source.volume,source.volume);
    close(reflected.calculated_source.mesh.vertices.front().x,2-(source.mesh.vertices.front().x+10));
    require(assembly.derived_source(mirror.occurrence_id)->occurrence_id==component.occurrence_id,"Mirror did not resolve its source occurrence");
    const auto snapshot=reflected.calculated_source.mesh.vertices;
    assembly.components.front().placement.x=20;static_cast<void>(assembly.build_scene());require(assembly.components.back().calculated_source.mesh.vertices==snapshot,"Scene refresh recalculated Mirror");
    assembly.calculate_derived_copies(kernel);close(assembly.components.back().calculated_source.mesh.vertices.front().x,2-(source.mesh.vertices.front().x+20));
    const auto file=std::filesystem::temp_directory_path()/"zima-mirror-contract.asmz";assembly.save(file);auto loaded=assembly::AssemblyDocument::load(file);std::filesystem::remove(file);
    require(loaded.components.back().derived_copy==assembly.components.back().derived_copy,"Assembly reload lost Mirror source/plane");
    auto cycle=assembly;cycle.components.front().derived_copy=document::DerivedCopyParameters{mirror.occurrence_id,{{},component.occurrence_id+":origin","origin:plane:yz"}};
    rejects([&]{cycle.calculate_derived_copies(kernel);});
    auto followed=assembly;auto follower=component;follower.occurrence_id="follower";follower.placement={};follower.grounded=false;
    const auto vertex=source.mesh.original_references.points.front().reference;
    follower.placement_references.push_back({assembly::MateKind::PointCoincident,
        {assembly::MateReferenceKind::Point,assembly::InstancePath{}.child(follower.occurrence_id),vertex.owner_id,vertex.semantic_key},
        {assembly::MateReferenceKind::Point,assembly::InstancePath{}.child(mirror.occurrence_id),vertex.owner_id,vertex.semantic_key}});
    followed.components.push_back(follower);followed.calculate_derived_copies(kernel);
    followed.components.front().placement.x+=13;followed.calculate_derived_copies(kernel);
    const auto& row=followed.components.back().placement_references.front();
    const auto moving=followed.resolve_point(row.component_reference),target=followed.resolve_point(row.target_reference);
    require(moving.status==assembly::MateStatus::Valid&&target.status==assembly::MateStatus::Valid,"Follower lost its copied reference");
    close(moving.point.x,target.point.x);close(moving.point.y,target.point.y);close(moving.point.z,target.point.z);
    auto driven_source=followed;driven_source.components.front().placement_references=driven_source.components.back().placement_references;
    driven_source.components.front().grounded=false;rejects([&]{driven_source.calculate_derived_copies(kernel);});
    // Independent linear/circular checks: position, volume, identity, mode
    // changes, persistence and explicit source ownership inside a nested group.
    kernel::PatternRequest pattern;pattern.count=4;pattern.spacing=20;
    auto copies=kernel.pattern_body(source,pattern,"pattern");close(copies.volume,3*source.volume);
    close(kernel.compound_bodies({{copies,{},{}}}).volume,3*source.volume);
    const auto v=source.mesh.vertices.front();close(copies.mesh.vertices.front().x,v.x+20);
    for(const auto& r:copies.mesh.original_references.triangle_references)if(r.valid())
        require(r.owner_id=="pattern"&&r.semantic_key.starts_with("pattern:copy-"),"Pattern lost copy topology identity");
    pattern.circular=true;pattern.origin={2,3,4};pattern.axis={0,0,1};
    copies=kernel.pattern_body(source,pattern,"pattern");
    close(copies.mesh.vertices.front().x,2-(v.y-3));close(copies.mesh.vertices.front().y,3+(v.x-2));close(copies.mesh.vertices.front().z,v.z);
    pattern.full_circle=false;pattern.angle_degrees=180;rejects([&]{static_cast<void>(kernel.pattern_body(source,pattern));});
    pattern.full_circle=true;pattern.count=1;rejects([&]{static_cast<void>(kernel.pattern_body(source,pattern));});
    pattern.count=4;
    document::DerivedCopyParameters parameters{source_id,{{},"pattern-body:origin","origin:axis:z"}};parameters.pattern=pattern;
    document::Placement frame;frame.rotation_y=90;parameters.linear_axis=1;
    document::PartDocument::resolve_copy_reference(parameters,"pattern-body",frame,{});
    close(parameters.pattern->axis.x,1);close(parameters.pattern->axis.z,0);
    parameters.pattern->circular=false;const auto retained=parameters.reference;
    document::PartDocument::resolve_copy_reference(parameters,"pattern-body",frame,{});
    close(parameters.pattern->direction.y,1);require(parameters.reference==retained,"Linear mode discarded the stored circular axis");
    auto pattern_graph=graph;document::BodyHistory patterned;patterned.name="Pole";patterned.scope.id="pattern-body";patterned.derived_copy=parameters;
    const auto pattern_id=pattern_graph.create_derived_copy(patterned);
    auto operations=pattern_graph.compile([&](const auto& e)->std::optional<kernel::HistoryOperation>{return kernel::HistoryOperation{e.id,box(3)};});
    const auto patterned_result=kernel.evaluate_history(operations).back();close(patterned_result.body_outputs.at(pattern_id).volume,315);
    require(patterned_result.body_inputs.contains(pattern_id),"Pattern has no selectable body result");
    require(document::BodyHistoryGraph::from_serialized(pattern_graph.serialized()).find(pattern_id)->derived_copy==patterned.derived_copy,"Pattern mode/axis not persisted");
    auto patterned_assembly=assembly;auto group=mirror;group.occurrence_id="pattern-group";group.name="Pole";
    group.derived_copy->pattern=kernel::PatternRequest{};group.derived_copy->reference={{},"pattern-group:origin","origin:axis:z"};
    patterned_assembly.components.push_back(group);patterned_assembly.calculate_derived_copies(kernel);
    const auto* calculated_group=patterned_assembly.find_occurrence(group.occurrence_id);
    require(calculated_group->nested_snapshot.size()==3,"Pattern does not persist its individual occurrences");
    const auto scene=patterned_assembly.build_scene();std::set<std::string> copy_paths;
    for(const auto& r:scene.triangle_references)if(r.instance_path.starts_with(assembly::InstancePath{}.child(group.occurrence_id).encoded()))copy_paths.insert(r.instance_path);
    require(copy_paths.size()==3,"Pattern copies share one selectable occurrence path");
    workspace::Workspace workspace;workspace.add_assembly(patterned_assembly);
    require(workspace.derived_source_path(patterned_assembly.document_id,assembly::InstancePath{}.child(group.occurrence_id).child("copy-1"))==assembly::InstancePath{}.child(component.occurrence_id),"Pattern edit did not return to its exact source");
    const auto pattern_file=std::filesystem::temp_directory_path()/"zima-pattern-contract.asmz";patterned_assembly.save(pattern_file);
    const auto reloaded=assembly::AssemblyDocument::load(pattern_file);std::filesystem::remove(pattern_file);
    require(reloaded.find_occurrence(group.occurrence_id)->nested_snapshot==calculated_group->nested_snapshot,"Pattern occurrence snapshot did not round-trip");
    std::cout<<"Mirror and Pattern contracts passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
