#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/document_dependencies.hpp>
#include <zima/document/feature_serialization.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/boolean_face_ancestry.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace zima;
namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
kernel::Vec3 rotate(kernel::Vec3 v,const assembly::ComponentPlacement& p) {
    const double rx=p.rotation_x*std::numbers::pi/180,ry=p.rotation_y*std::numbers::pi/180,rz=p.rotation_z*std::numbers::pi/180;
    v={v.x,std::cos(rx)*v.y-std::sin(rx)*v.z,std::sin(rx)*v.y+std::cos(rx)*v.z};
    v={std::cos(ry)*v.x+std::sin(ry)*v.z,v.y,-std::sin(ry)*v.x+std::cos(ry)*v.z};
    return {std::cos(rz)*v.x-std::sin(rz)*v.y,std::sin(rz)*v.x+std::cos(rz)*v.y,v.z};
}
document::PartDocument box(double half_x,double half_y,double height,double body_z) {
    auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
    sketch.plane_auto=false;static_cast<void>(sketch.add_rectangle(-half_x,-half_y,half_x,half_y));
    auto feature=document::PartDocument::create_feature_container(sketch.id);feature.feature.sides[0].length=height;
    sketch.owner_container_id=feature.id;part.history={feature};part.sketches={sketch};
    document::BodyHistoryGraph graph;const auto id=graph.create_body("Reference test");
    graph.insert({document::PartHistoryKind::Feature,feature.id});auto body=*graph.find(id);
    body.scope.placement.z=body_z;body.scope.placement.rotation_z=body.scope.placement.absolute_rotation_z=17;
    graph.update_body(body);part.set_body_history(graph);part.resolve_constructions();return part;
}
void verify_curved_target(const fs::path& directory) {
    kernel::OcctKernel kernel;auto source=box(10,10,20,0),part=box(2,3,1,0);
    auto circle=sketcher::Sketch::create_default();circle.plane=sketcher::SketchPlane::XZ;circle.plane_auto=false;
    static_cast<void>(circle.add_circle(0,0,10));circle.owner_container_id=source.history.front().id;
    source.history.front().feature.sketch_id=circle.id;source.history.front().placement.y=10;source.sketches={circle};source.resolve_constructions();
    auto source_cache=workspace::calculate_part_with_resolved_references(kernel,source);
    auto part_cache=workspace::calculate_part_with_resolved_references(kernel,part);
    const auto source_file=directory/"curved_SKELETON.prtz",part_file=directory/"curved-dependent.prtz";
    source.save(source_file,source_cache);part.save(part_file,part_cache);
    kernel::FaceReference cylinder;
    for(const auto& face:source_cache.back().mesh.original_references.triangle_references)
        if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder){cylinder=face;break;}
    require(cylinder.valid(),"Native cylinder support missing");
    auto owner=assembly::AssemblyDocument::create_default();
    auto fixed=assembly::AssemblyDocument::create_part_occurrence("Cylinder Skeleton",source.document_id,source_file,source_cache.back());
    auto moving=assembly::AssemblyDocument::create_part_occurrence("Part",part.document_id,part_file,part_cache.back());owner.components={fixed,moving};
    workspace::Workspace live;live.add_part(part,part_cache,part_file);live.add_assembly(owner,directory/"curved-owner.asmz");live.activate(owner.document_id);
    static_cast<void>(live.activate_occurrence(owner.document_id,assembly::InstancePath{}.child(moving.occurrence_id)));
    document::ExtrusionParameters::EndTarget requested;requested.reference=cylinder;
    requested.reference.instance_path=assembly::InstancePath{}.child(fixed.occurrence_id).encoded();
    const auto pending=workspace::prepare_skeleton_end_target(live,part,part.history.front().id,requested);
    require(pending.kind==document::EndTargetKind::Face,"Cylinder was approximated as a plane");
    auto feature=part.history.front();feature.feature.sides[0].extrusion_extent=document::EndCondition::UpTo;feature.feature.sides[0].targets={pending};
    workspace::commit_profile(live,kernel,part.document_id,feature,workspace::ProfileEditMode::Replace);
    const auto expected=[](double radius){return 6*(2*std::sqrt(radius*radius-4)+radius*radius*std::asin(2/radius));};
    const auto volume=[&]{auto result=live.open_part(part.document_id)->session.calculated_boundaries().back();kernel.ensure_body_properties(result);
        require(result.calculation_errors.empty(),"Native curved external limit failed");return result.volume;};
    require(std::abs(volume()-expected(10))<1e-5,"Curved external limit did not match the independent cylinder integral");
    source.sketches.front().circles.front().radius=11;source_cache=workspace::calculate_part_with_resolved_references(kernel,source,&source_cache);
    live.add_part(source,source_cache,source_file);static_cast<void>(workspace::regenerate_part(live,kernel,part.document_id));
    require(std::abs(volume()-expected(11))<1e-5,"Curved native source edit reused an invalid old surface");
    const auto stored=live.open_part(part.document_id)->session.document().history.front().feature.sides[0].targets.front();
    require(stored.reference==requested.reference&&stored.external_source->source_face->kernel_shape.size()<100000,
        "Curved limit lost its native identity or copied the source Part history");
    std::cout<<"Curved Skeleton limit: exact cylinder integral before/after an unsaved radius change, native source identity and bounded face cache passed\n";
}
void verify_split_target(const fs::path& directory) {
    kernel::OcctKernel kernel;auto source=box(10,10,20,0),part=box(2,3,1,0);
    auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;
    static_cast<void>(sketch.add_rectangle(-1,-20,1,20));
    auto cut=document::PartDocument::create_feature_container(sketch.id);cut.combine_mode=document::CombineMode::Subtract;
    cut.feature.sides[0].length=25;sketch.owner_container_id=cut.id;source.sketches.push_back(sketch);source.history.push_back(cut);
    auto graph=source.body_history;graph.insert({document::PartHistoryKind::Feature,cut.id});source.set_body_history(graph);source.resolve_constructions();
    auto source_cache=workspace::calculate_part_with_resolved_references(kernel,source);
    auto part_cache=workspace::calculate_part_with_resolved_references(kernel,part);
    require(source_cache.back().calculation_errors.empty(),"Split Skeleton fixture failed");
    kernel::FaceReference fragment;
    const auto& geometry=source_cache.back().mesh;
    for(std::size_t triangle=0;triangle<geometry.triangle_references.size();++triangle) {
        const auto& reference=geometry.triangle_references[triangle];
        if(!kernel::boolean_face_parent(reference.semantic_key))continue;
        bool top=true;for(unsigned j=0;j<3;++j)top=top&&std::abs(geometry.vertices.at(geometry.triangles.at(3*triangle+j)).z-20)<1e-7;
        if(top){fragment=reference;break;}
    }
    require(fragment.valid(),"Split Skeleton did not expose a current top fragment");
    auto canonical=fragment;while(const auto parent=kernel::boolean_face_parent(canonical.semantic_key)) {
        canonical.owner_id=parent->first;canonical.semantic_key=parent->second;
    }
    require(canonical.owner_id==source.history.front().id,"Split face did not retain its oldest owner");
    const auto source_file=directory/"split_SKELETON.prtz",part_file=directory/"split-dependent.prtz";
    source.save(source_file,source_cache);part.save(part_file,part_cache);
    auto owner=assembly::AssemblyDocument::create_default();
    auto fixed=assembly::AssemblyDocument::create_part_occurrence("Split Skeleton",source.document_id,source_file,source_cache.back());
    auto moving=assembly::AssemblyDocument::create_part_occurrence("Part",part.document_id,part_file,part_cache.back());owner.components={fixed,moving};
    workspace::Workspace live;live.add_part(part,part_cache,part_file);live.add_assembly(owner,directory/"split-owner.asmz");live.activate(owner.document_id);
    static_cast<void>(live.activate_occurrence(owner.document_id,assembly::InstancePath{}.child(moving.occurrence_id)));
    document::ExtrusionParameters::EndTarget requested;requested.reference=fragment;
    requested.reference.instance_path=assembly::InstancePath{}.child(fixed.occurrence_id).encoded();canonical.instance_path=requested.reference.instance_path;
    const auto pending=workspace::prepare_skeleton_end_target(live,part,part.history.front().id,requested);
    require(pending.reference==canonical,"Picking did not canonicalize the explicit oldest source identity");
    auto feature=part.history.front();feature.feature.sides[0].extrusion_extent=document::EndCondition::UpTo;feature.feature.sides[0].targets={pending};
    workspace::commit_profile(live,kernel,part.document_id,feature,workspace::ProfileEditMode::Replace);
    auto* current=live.open_part(part.document_id);auto result=current->session.calculated_boundaries().back();kernel.ensure_body_properties(result);
    require(result.calculation_errors.empty()&&std::abs(result.volume-480)<1e-6,
        "Split original plane could not limit geometry crossing the removed strip");
    const auto& stored=current->session.document().history.front().feature.sides[0].targets.front();
    require(stored.reference==canonical&&stored.external_source->source_face->kernel_faces.size()==1,
        "Split limit stored fragments instead of the defining native face");
    require(current->session.undo()&&current->session.redo(),"Split limit Undo/Redo failed");
    const auto reopened=document::PartDocument::from_serialized(current->session.document().serialized());
    require(reopened.history.front().feature==current->session.document().history.front().feature,"Split limit native reopen changed identity");
    require(document::PartDocument::load(source_file).serialized()==source.serialized(),"Split limit modified its read-only source");
    std::cout<<"Split Skeleton limit: current Boolean fragment canonicalized to oldest native face, continuous plane across cut, exact volume, source unchanged and Undo/Redo/reopen passed\n";
}
void verify(const fs::path& directory) {
    kernel::OcctKernel kernel;unsigned cases=0;
    for(double sign:{-1.,1.})for(int rotated:{0,1,2})for(double correction:{-2.,-0.,0.,2.})for(bool nested:{false,true}) {
        auto source=box(30,30,20,4),part=box(2,3,1,2);
        auto source_cache=workspace::calculate_part_with_resolved_references(kernel,source);
        auto part_cache=workspace::calculate_part_with_resolved_references(kernel,part);
        const auto source_file=directory/("source-"+source.document_id+"_SKELETON.prtz"),part_file=directory/("part-"+part.document_id+".prtz");
        source.save(source_file,source_cache);part.save(part_file,part_cache);
        const auto& geometry=source_cache.back().mesh.original_references;
        kernel::FaceReference cap;
        for(std::size_t triangle=0;triangle<geometry.triangle_references.size();++triangle) {
            bool top=true;for(unsigned j=0;j<3;++j)top=top&&std::abs(geometry.vertices.at(geometry.triangles.at(3*triangle+j)).z-24)<1e-7;
            if(top){cap=geometry.triangle_references[triangle];break;}
        }
        require(cap.valid(),"Source top face identity missing");
        auto assembly=assembly::AssemblyDocument::create_default();
        auto prerequisite=assembly::AssemblyDocument::create_part_occurrence("Skeleton",source.document_id,source_file,source_cache.back());
        auto dependent=assembly::AssemblyDocument::create_part_occurrence("Part",part.document_id,part_file,part_cache.back());
        prerequisite.placement.z=sign*80;dependent.placement.z=5;
        if(rotated)for(auto* item:{&prerequisite,&dependent}) {
            item->placement.rotation_x=30;item->placement.rotation_y=20;item->placement.rotation_z=40;
        }
        if(rotated==2)prerequisite.placement.rotation_y=-10;
        auto source_path=assembly::InstancePath{}.child(prerequisite.occurrence_id);
        auto dependent_path=assembly::InstancePath{}.child(dependent.occurrence_id);
        assembly.components={prerequisite,dependent};
        if(nested) {
            const auto child_file=directory/("child-"+assembly.document_id+".asmz");assembly.save(child_file);
            auto wrapper=assembly::AssemblyDocument::create_default();
            auto parent=assembly::AssemblyDocument::create_assembly_occurrence("Rotated parent",assembly.document_id,child_file,assembly);
            parent.placement.x=50;parent.placement.y=-20;parent.placement.z=9;
            parent.placement.rotation_x=20;parent.placement.rotation_y=-15;parent.placement.rotation_z=40;
            source_path=assembly::InstancePath{}.child(parent.occurrence_id).child(prerequisite.occurrence_id);
            dependent_path=assembly::InstancePath{}.child(parent.occurrence_id).child(dependent.occurrence_id);
            wrapper.components={parent};assembly=std::move(wrapper);
        }
        workspace::Workspace live;live.add_part(part,part_cache,part_file);live.add_assembly(assembly,directory/("assembly-"+assembly.document_id+".asmz"));
        live.activate(assembly.document_id);static_cast<void>(live.activate_occurrence(assembly.document_id,dependent_path));
        auto requested=document::ExtrusionParameters::EndTarget{};requested.reference=cap;requested.reference.instance_path=source_path.encoded();
        const auto id=part.history.front().id;const auto part_before=live.open_part(part.document_id)->session.document().serialized();
        const auto pending=workspace::prepare_skeleton_end_target(live,part,id,requested);
        require(pending.external_source&&pending.external_source->source_face->kernel_shape.empty(),"Picking calculated or copied native source geometry");
        require(live.open_part(part.document_id)->session.document().serialized()==part_before,"Picking modified the dependent Part");
        const auto normal=rotate({0,0,1},prerequisite.placement),direction=rotate({0,0,1},dependent.placement);
        const auto source_origin=rotate({0,0,24},prerequisite.placement),dependent_origin=rotate({0,0,2},dependent.placement);
        const auto dot=[](auto a,auto b){return a.x*b.x+a.y*b.y+a.z*b.z;};
        const kernel::Vec3 separation{source_origin.x-dependent_origin.x,source_origin.y-dependent_origin.y,
            source_origin.z+sign*80-dependent_origin.z-5};
        const double denominator=dot(normal,direction),height=dot(normal,separation)/denominator;
        const double resolved_height=dot(pending.fallback_normal,pending.fallback_origin)/pending.fallback_normal.z;
        require(pending.kind==document::EndTargetKind::Plane&&std::abs(resolved_height-height)<1e-7,
            "External plane failed the independent occurrence/Body equation");
        auto feature=part.history.front();feature.feature.reversed_sides=height<0;
        feature.feature.sides[0].extrusion_extent=document::EndCondition::UpTo;feature.feature.sides[0].targets={pending};
        feature.feature.sides[0].up_to.length_correction=correction;
        workspace::commit_profile(live,kernel,part.document_id,feature,workspace::ProfileEditMode::Replace);
        auto* current=live.open_part(part.document_id);
        auto result=current->session.calculated_boundaries().back();kernel.ensure_body_properties(result);
        require(result.calculation_errors.empty()&&std::abs(result.volume-24*(std::abs(height)+correction))<1e-5,"External native plane produced the wrong solid");
        const auto& stored=current->session.document().history.front().feature.sides[0].targets.front();
        require(stored.reference==requested.reference&&!stored.external_source->source_face->kernel_shape.empty(),"External native source identity/cache was not persisted");
        require(stored.external_source->source_face->body_boundaries.empty()&&stored.external_source->source_face->kernel_shape.size()<100000,
            "External target copied a source history instead of its selected native face");
        const auto encoded=current->session.document().serialized();
        const auto reopened=document::PartDocument::from_serialized(encoded);
        require(reopened.history.front().feature==current->session.document().history.front().feature,"Native reopen changed the external target binding");
        require(std::signbit(reopened.history.front().feature.sides[0].up_to.length_correction)==std::signbit(correction),
            "Native external limit changed the signed zero correction");
        require(current->session.undo()&&!current->session.document().history.front().feature.sides[0].targets.size(),"External target Undo failed");
        require(current->session.redo()&&current->session.document().serialized()==encoded,"External target Redo failed");
        unsigned calculations=0;const auto revision=current->session.revision();
        {workspace::CalculationExecutionScope count([&](auto action){++calculations;action();});
            workspace::commit_profile(live,kernel,part.document_id,current->session.document().history.front(),workspace::ProfileEditMode::Replace);}
        require(calculations==0&&current->session.revision()==revision,"Unchanged external limit OK calculated geometry or created an Undo transaction");
        // An unsaved open source is authoritative on explicit regeneration.
        source.history.front().feature.sides[0].length=25;
        source_cache=workspace::calculate_part_with_resolved_references(kernel,source,&source_cache);
        live.add_part(source,source_cache,source_file);
        const auto immutable=live.open_part(source.document_id)->session.document().serialized();
        static_cast<void>(workspace::regenerate_part(live,kernel,part.document_id));
        result=live.open_part(part.document_id)->session.calculated_boundaries().back();kernel.ensure_body_properties(result);
        require(result.calculation_errors.empty()&&std::abs(result.volume-24*(std::abs(height+5/denominator)+correction))<1e-5,
            "Regeneration ignored the unsaved current Skeleton geometry");
        require(live.open_part(source.document_id)->session.document().serialized()==immutable,"External target changed its read-only Skeleton");
        require(document::PartDocument::load(source_file).history.front().feature.sides[0].length==20,"Regeneration overwrote the source file");
        // A native Part with a different role must not become an external limit.
        auto other=requested;other.reference.instance_path=dependent_path.encoded();bool rejected=false;
        try{static_cast<void>(workspace::prepare_skeleton_end_target(live,part,id,other));}catch(const std::exception&){rejected=true;}
        require(rejected,"An ordinary/self Part was accepted as a Skeleton limit");
        if(cases==0) {
            auto closed_source=live;static_cast<void>(closed_source.remove(source.document_id));bool cycle=false;
            try{workspace::require_acyclic_document_dependency(closed_source,source.document_id,part.document_id,part_file);}
            catch(const workspace::DocumentDependencyError& error){cycle=std::string(error.code)=="dependency_cycle";}
            require(cycle,"External limit dependency did not participate in cycle rejection");
            auto forged=pending;forged.external_source->document_id="different-native-document";bool identity_rejected=false;
            try{static_cast<void>(workspace::prepare_skeleton_end_target(live,part,id,forged));}catch(const std::exception&){identity_rejected=true;}
            require(identity_rejected,"A replaced external document identity was silently rebound");
            auto missing=live.open_part(source.document_id)->session.document();missing.history.front().feature.type=document::FeatureType::Sketch;
            auto empty=sketcher::Sketch::create_default();empty.id=missing.sketches.front().id;
            empty.owner_container_id=missing.history.front().id;empty.plane_auto=false;missing.sketches={empty};
            auto absent=workspace::calculate_part_with_resolved_references(kernel,missing,&source_cache);
            live.open_part(source.document_id)->session.commit(std::move(missing),std::move(absent));
            const auto protected_revision=live.open_part(part.document_id)->session.revision();
            const auto protected_state=live.open_part(part.document_id)->session.document().serialized(
                live.open_part(part.document_id)->session.calculated_boundaries());
            bool missing_rejected=false;
            try{static_cast<void>(workspace::regenerate_part(live,kernel,part.document_id));}catch(const std::exception&){missing_rejected=true;}
            require(missing_rejected&&live.open_part(part.document_id)->session.revision()==protected_revision&&
                live.open_part(part.document_id)->session.document().serialized(live.open_part(part.document_id)->session.calculated_boundaries())==protected_state,
                "An unavailable source changed the Part instead of retaining its repairable identity and last valid result");
        }
        ++cases;
    }
    std::cout<<"External Skeleton limits: "<<cases<<" signed/rotated plane cases, Body frames, exact native volume, read-only picking, native identity/cache, Undo/Redo and authoritative unsaved source regeneration passed\n";
    verify_curved_target(directory);
    verify_split_target(directory);
}
}
int main(){try {
    const auto root=fs::canonical(fs::temp_directory_path());const auto directory=root/("zima-external-target-"+document::PartDocument::create_default().document_id);
    fs::create_directory(directory);verify(directory);require(directory.parent_path()==root,"Unsafe test cleanup");fs::remove_all(directory);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
