#include "profile_solid_fixture.hpp"
#include <zima/workspace/body_scale_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/scale_geometry.hpp>
#include <zima/interchange/step_model.hpp>
#include <zima/document/body_properties.hpp>
#include <zima/workspace/component_operations.hpp>
#include <iostream>
#include <limits>

using namespace zima;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void near(double value,double expected){require(std::abs(value-expected)<1e-7,"Incorrect scaled measure");}
template<class F>static void rejects(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,"Invalid scale accepted");}
static void verify_derived_reference_database(const kernel::OcctKernel& kernel,bool mirror) {
    auto part=document::PartDocument::create_default();auto feature=test::rectangular_feature(part,{10,8,6});part.history={feature};
    document::BodyHistoryGraph graph;const auto source=graph.create_body("Source");
    graph.insert({document::PartHistoryKind::Feature,feature.id});graph.activate({});
    document::BodyHistory derived;derived.scope.id="derived-body";derived.name="Derived";
    if(mirror) {
        derived.derived_copy=document::DerivedCopyParameters{source,{{},derived.scope.id+":origin","origin:plane:yz"}};
        static_cast<void>(graph.create_derived_copy(derived));
    } else {
        derived.scale=document::BodyScale{source,2,{}};static_cast<void>(graph.create_scale(derived));
    }
    auto suppressed=*graph.find(source);suppressed.suppressed=true;graph.update_body(suppressed);part.set_body_history(graph);
    const auto calculated=workspace::calculate_part_with_resolved_references(kernel,part);
    const auto parent_directory=std::filesystem::canonical(std::filesystem::temp_directory_path());
    const auto directory=parent_directory/("zima-derived-reference-"+part.document_id);
    std::filesystem::create_directory(directory);const auto part_path=directory/"source.prtz";
    part.save(part_path,calculated);
    std::vector<kernel::BodyResult> restored;
    const auto reopened=document::PartDocument::load(part_path,&restored);
    require(reopened.body_history.find(source)->suppressed,"Derived reference test lost source suppression");
    const auto& original=restored.back().body_outputs.at(source)->mesh.original_references;
    const auto& references=restored.back().body_outputs.at(derived.scope.id)->mesh.original_references;
    require(!references.points.empty()&&!references.edges.empty()&&!references.triangle_references.empty(),"Derived Body lost its original reference database");
    std::string face_key;
    for(const auto& face:references.triangle_references)if(face.surface) {
        const auto parent=mirror?kernel::mirror_parent_reference(face.semantic_key):kernel::scale_parent_reference(face.semantic_key);
        require(face.owner_id==derived.scope.id&&parent.has_value(),"Derived face reused the source identity");
        const auto found=std::ranges::find_if(original.triangle_references,[&](const auto& ref){return ref.owner_id==parent->owner_id&&ref.semantic_key==parent->semantic_key;});
        require(found!=original.triangle_references.end()&&found->surface,"Derived face lost its persisted source parent");
        const auto point=mirror?kernel::mirrored_point(found->surface->origin,{{},{1,0,0}}):kernel::scaled_body_point(found->surface->origin,2,{});
        near(face.surface->origin.x,point.x);near(face.surface->origin.y,point.y);near(face.surface->origin.z,point.z);
        const auto axis=mirror?kernel::mirrored_vector(found->surface->axis,{{},{1,0,0}}):found->surface->axis;
        require(face.surface->axis==axis&&face.surface->reversed==found->surface->reversed,"Derived face lost its oriented side");
        if(face_key.empty())face_key=face.semantic_key;
    }
    require(!face_key.empty(),"Derived Body has no persisted plane for mating");
    auto model=assembly::AssemblyDocument::create_default();
    auto target=assembly::AssemblyDocument::create_part_occurrence("Target",reopened.document_id,part_path,restored.back());
    auto moving=assembly::AssemblyDocument::create_part_occurrence("Moving",reopened.document_id,part_path,restored.back());
    target.grounded=true;target.placement={37,13,-5};moving.grounded=false;
    const auto reference=[&](const std::string& occurrence){return assembly::MateReference{assembly::MateReferenceKind::Face,
        assembly::InstancePath{}.child(occurrence),derived.scope.id,face_key};};
    moving.placement_references.push_back({assembly::MateKind::PlaneCoincident,reference(moving.occurrence_id),reference(target.occurrence_id)});
    model.components={target,moving};
    for(bool flip:{false,true})for(double offset:{0.0,-0.0}) {
        auto trial=model;auto& row=trial.components.back().placement_references.front();row.flip=flip;row.offset=offset;
        trial.calculate_placement_references();
        require(trial.resolve_plane(row.component_reference).status==assembly::MateStatus::Valid&&
            trial.resolve_plane(row.target_reference).status==assembly::MateStatus::Valid,"Assembly cannot resolve a derived Body plane before save");
        const auto assembly_path=directory/"assembly.asmz";trial.save(assembly_path);trial=assembly::AssemblyDocument::load(assembly_path);
        const auto& persisted=trial.components.back().placement_references.front();
        require(persisted.flip==flip&&std::signbit(persisted.offset)==std::signbit(offset),"Derived face mating lost its side choice");
        const auto a=trial.resolve_plane(persisted.component_reference),b=trial.resolve_plane(persisted.target_reference);
        require(a.status==assembly::MateStatus::Valid&&b.status==assembly::MateStatus::Valid,"Assembly cannot resolve a derived Body plane after reopen");
        const auto n=b.plane.normal,p=a.plane.normal;
        near(p.x*n.x+p.y*n.y+p.z*n.z,flip?-1:1);
        near((a.plane.point.x-b.plane.point.x)*n.x+(a.plane.point.y-b.plane.point.y)*n.y+(a.plane.point.z-b.plane.point.z)*n.z,0);
    }
    require(std::filesystem::canonical(directory).parent_path()==parent_directory,"Unexpected derived reference test directory");
    std::filesystem::remove_all(directory);
}
int main(){try{
    kernel::OcctKernel kernel;
    verify_derived_reference_database(kernel,true);verify_derived_reference_database(kernel,false);
    const auto source=kernel.evaluate_history({{"box",test::ProfilePrism{10,10,10}}}).back();
    for(double factor:{0.5,1.,2.}) {
        const kernel::Vec3 center{3,-7,11};
        const auto scaled=kernel.scale_body(source,factor,center,"scale");
        near(scaled.volume,source.volume*std::pow(factor,3));near(scaled.surface_area,source.surface_area*factor*factor);
        near(kernel.compound_bodies({{scaled}}).volume,scaled.volume);
        require(scaled.mesh.vertices.size()==source.mesh.vertices.size()&&scaled.mesh.triangles==source.mesh.triangles,"Scale changed tessellation winding");
        for(std::size_t i=0;i<source.mesh.vertices.size();++i)
            require(scaled.mesh.vertices[i]==kernel::scaled_body_point(source.mesh.vertices[i],factor,center),"Viewer scale differs from solid scale");
        for(std::size_t i=0;i<source.mesh.triangle_references.size();++i) {
            const auto& original=source.mesh.triangle_references[i];const auto& result=scaled.mesh.triangle_references[i];
            require(original.sheet_role==result.sheet_role,"Scale changed the material side");
            if(original.surface)require(result.surface&&result.surface->axis==original.surface->axis&&
                result.surface->radial==original.surface->radial&&result.surface->reversed==original.surface->reversed,
                "Scale changed the oriented surface side");
        }
        require(scaled.volume_integrals&&source.volume_integrals,"Missing exact mass properties");
        for(int i=0;i<9;++i)near(scaled.volume_integrals->inertia[i],source.volume_integrals->inertia[i]*std::pow(factor,5));
        for(const auto& ref:scaled.mesh.original_references.triangle_references)if(ref.valid()) {
            const auto parent=kernel::scale_parent_reference(ref.semantic_key);
            require(ref.owner_id=="scale"&&parent&&parent->owner_id=="box","Scale lost topology ancestry");
        }
        const auto restored=document::load_body_result(document::serialize_body_result(scaled));
        near(restored.volume,scaled.volume);
        require(restored.mesh.vertices==scaled.mesh.vertices,"Scale viewer persistence changed");
    }
    for(double factor:{0.,-1.,std::numeric_limits<double>::infinity()})rejects([&]{kernel.scale_body(source,factor,{},"invalid");});
    workspace::Workspace live;auto part=document::PartDocument::create_default();
    auto feature=test::rectangular_feature(part,{10,10,10});part.history={feature};
    document::BodyHistoryGraph graph;const auto input=graph.create_body("Source");
    graph.insert({document::PartHistoryKind::Feature,feature.id});graph.set_history_cursor(input,0);
    graph.activate({});part.set_body_history(graph);
    const auto id=part.document_id;live.add_part(part,workspace::calculate_part_with_resolved_references(kernel,part));
    auto edit=workspace::prepare_body_scale_edit(live.open_part(id)->session.document());
    document::BodyHistory body;body.scope.id=edit.object_id;body.name="Scaled";body.scale=document::BodyScale{input,2,{}};
    require(workspace::commit_body_scale(live,kernel,edit,body),"Scale did not commit");
    const auto result_volume=[&](const std::string& owner){return live.open_part(owner)->session.calculated_boundaries().back().volume;};
    near(result_volume(id),9000);
    auto current=live.open_part(id)->session.document().body_history;
    require(current.find(body.scope.id)->entries.empty(),"Scale contains feature history");
    rejects([&]{current.activate(body.scope.id);});rejects([&]{current.move_body(body.scope.id,0);});
    rejects([&]{current.move_body(input,1);});rejects([&]{current.erase_step(input);});
    require(workspace::set_part_body_suppressed(live,kernel,id,input,true),"Source was not suppressed");near(result_volume(id),8000);
    {
        const auto* state=live.open_part(id);
        const auto exported=interchange::step_product(state->session.document(),state->session.calculated_boundaries());
        require(exported.children.size()==1&&exported.children.front().definition_id==body.scope.id,"STEP includes suppressed source");
        document::BodyProperties record;record.after_object_id=body.scope.id;
        near(document::evaluate_body_properties(state->session.document(),state->session.calculated_boundaries(),record).volume,8000);
    }
    live.open_part(id)->session.undo();near(result_volume(id),9000);
    live.open_part(id)->session.redo();near(result_volume(id),8000);
    edit=workspace::prepare_body_scale_edit(live.open_part(id)->session.document(),body.scope.id);
    require(!workspace::commit_body_scale(live,kernel,edit,body),"Unchanged Scale recalculated");
    body.scale->factor=1.5;require(workspace::commit_body_scale(live,kernel,edit,body),"Scale edit failed");near(result_volume(id),3375);
    {
        auto* state=live.open_part(id);auto next=state->session.document();
        next.find_container(feature.id)->extrusion.length_forward=10;
        auto calculated=workspace::calculate_part_with_resolved_references(kernel,next,&state->session.calculated_boundaries());
        state->session.commit(std::move(next),std::move(calculated));near(result_volume(id),6750);
        state->session.undo();near(result_volume(id),3375);
    }
    auto packet=live.open_part(id)->session.document().serialized(live.open_part(id)->session.calculated_boundaries());
    std::vector<kernel::BodyResult> cache;const auto reopened=document::PartDocument::from_serialized(packet,&cache);
    require(reopened.body_history==live.open_part(id)->session.document().body_history,"Scale/source suppression lost on reopen");near(cache.back().volume,3375);
    document::FamilyTable table;table.columns={"Source","Scaled"};
    table.bindings["Source"]={"body",input,{}};table.bindings["Scaled"]={"body",body.scope.id,{}};
    table.instances={{"Original",{{"Source","yes"},{"Scaled","no"}}},{"Enlarged",{{"Source","no"},{"Scaled","yes"}}},{"Empty",{{"Source","no"},{"Scaled","no"}}}};
    static_cast<void>(workspace::set_family_table(live,id,table));
    const auto original=workspace::open_family_instance(live,kernel,id,"Original",false);
    const auto enlarged=workspace::open_family_instance(live,kernel,id,"Enlarged",false);
    const auto empty=workspace::open_family_instance(live,kernel,id,"Empty",false);
    near(result_volume(original),1000);near(result_volume(enlarged),3375);near(result_volume(empty),0);
    require(!live.open_part(enlarged)->session.document().find_container(feature.id)->suppressed,"Family Body presence suppressed source features");
    auto assembly=assembly::AssemblyDocument::create_default();const auto assembly_id=assembly.document_id;
    live.add_assembly(std::move(assembly));live.display_top_level(assembly_id);live.activate(assembly_id);
    const auto occurrence=workspace::insert_component(live,assembly_id,enlarged);
    near(live.open_assembly(assembly_id)->session.document().find_occurrence(occurrence)->calculated_source->volume,3375);
    std::cout<<"Body scale geometry, ancestry, suppression, family presence, no-op, Undo/Redo and persistence passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
