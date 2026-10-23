#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/document/general_surface.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/kernel/face_plane_intersection.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include <set>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <cstdlib>
using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void verify_saved_reference_case(kernel::OcctKernel& engine,const std::filesystem::path& file) {
    std::vector<kernel::BodyResult> geometry;auto document=document::PartDocument::load(file,&geometry);
    workspace::Workspace live;live.add_part(document,geometry,file);live.activate(document.document_id);
    for(const auto& sketch:document.sketches)if(!sketch.external_references.empty()) {
        const auto* owner=document.find_container(sketch.owner_container_id);
        std::cout<<"Saved Sketch "<<(owner?owner->name:sketch.name)<<" references="<<sketch.external_references.size()<<'\n';
        for(const auto& reference:sketch.external_references) {
            std::cout<<"Reference "<<reference.id<<" broken="<<reference.broken;
            if(reference.intersection) {
                const auto& binding=*reference.intersection;
                std::cout<<" branch="<<binding.branch_key<<std::endl;
                try {
                    const auto curves=workspace::part_sketch_face_intersection(engine,live.open_part(document.document_id)->session,sketch,
                        {binding.face_owner_id,binding.face_semantic_key,{}});
                    std::cout<<"Query edges="<<curves.edges.size()<<" points="<<curves.points.size()<<std::endl;
                    for(const auto& edge:curves.edges)std::cout<<"Edge "<<edge.reference.owner_id<<" "<<edge.reference.semantic_key<<'\n';
                    for(const auto& point:curves.points)std::cout<<"Point "<<point.reference.owner_id<<" "<<point.reference.semantic_key<<'\n';
                } catch(const std::exception& error){std::cout<<"Query error: "<<error.what()<<std::endl;}
            } else {
                auto refreshed=workspace::prepare_sketch_external_reference(live,document.document_id,sketch,reference.kind,
                    reference.source_owner_id,reference.source_semantic_key,reference.source_instance_path);
                check(!refreshed.broken,"Saved ordinary reference cannot be prepared");std::cout<<" ordinary source accepted\n";
            }
        }
        if(owner&&owner->name=="Vytažení 005") {
            std::set<std::pair<std::string,std::string>> tested;
            for(const auto& face:geometry.back().mesh.triangle_references)if(face.valid()&&face.owner_id!=owner->id&&tested.emplace(face.owner_id,face.semantic_key).second) {
                try {
                    const auto curves=workspace::part_sketch_face_intersection(engine,live.open_part(document.document_id)->session,sketch,face);
                    std::cout<<"Visible face "<<face.owner_id<<" "<<face.semantic_key<<" -> edges="<<curves.edges.size()<<" points="<<curves.points.size()<<'\n';
                }catch(const std::exception& error){std::cout<<"Visible face "<<face.owner_id<<" "<<face.semantic_key<<" -> "<<error.what()<<'\n';}
            }
        }
    }
}
struct Fixture {document::PartDocument part;std::vector<kernel::BodyResult> bodies;kernel::FaceReference face;};
void verify_pending_profile_intersection(kernel::OcctKernel& engine,unsigned& cases) {
    for(bool placed:{false,true})for(bool intersects:{false,true}) {
        auto part=document::PartDocument::create_default();auto profile=sketcher::Sketch::create_default();
        static_cast<void>(profile.add_circle(0,0,5));auto source=document::PartDocument::create_feature_container(profile.id);
        source.feature.sides[0].length=10;profile.owner_container_id=source.id;part.history={source};part.sketches={profile};
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Pending profile source");graph.insert({document::PartHistoryKind::Feature,source.id});
        if(placed){auto scope=*graph.find(body);scope.scope.placement.x=12;scope.scope.placement.y=-7;
            scope.scope.placement.rotation_x=scope.scope.placement.absolute_rotation_x=90;graph.update_body(scope);}
        part.set_body_history(graph);part.resolve_constructions();auto calculated=engine.evaluate_history(part.kernel_operations());
        kernel::FaceReference face;
        for(const auto& reference:calculated.back().mesh.original_references.triangle_references)
            if(reference.surface&&reference.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder){face=reference;break;}
        check(face.valid(),"Pending profile cylinder source is missing");
        workspace::Workspace live;live.add_part(part,calculated);live.activate(part.document_id);
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id="pending-owner";
        sketch.plane_offset=intersects?5:20;sketch.refresh_default_frame();
        const auto branches=workspace::prepare_sketch_intersection_branches(live,engine,part.document_id,sketch,face,face,body);
        check(branches.size()==(intersects?1u:0u),"Pending intersection offered a foreign Origin point or lost its real curve");
        if(intersects)check(branches.front().kind==sketcher::ExternalReferenceKind::Edge&&
            branches.front().intersection->branch_key.starts_with("intersection:curve:"),"Pending intersection branch is not its actual native curve");
        ++cases;
    }
}
Fixture fixture(kernel::OcctKernel& kernel,bool circular,bool placed,bool separate) {
    Fixture f;f.part=document::PartDocument::create_default();
    auto source=sketcher::Sketch::create_default();
    if(circular)static_cast<void>(source.add_circle(0,0,5));else static_cast<void>(source.add_rectangle(-5,-4,5,4));
    auto extrusion=document::PartDocument::create_extrusion_container(source.id);source.owner_container_id=extrusion.id;
    extrusion.feature.sides[0].length=10;
    auto target=sketcher::Sketch::create_default();auto owner=document::PartDocument::create_sketch_container();
    target.owner_container_id=owner.id;owner.feature.sketch_id=target.id;owner.placement.z=5;
    f.part.history={extrusion,owner};f.part.sketches={source,target};
    document::BodyHistoryGraph graph;const auto body_id=graph.create_body("Source");graph.insert({document::PartHistoryKind::Feature,extrusion.id});
    if(placed){auto body=*graph.find(body_id);body.scope.placement.x=12;body.scope.placement.y=-7;body.scope.placement.rotation_x=body.scope.placement.absolute_rotation_x=90;graph.update_body(body);}
    if(separate)static_cast<void>(graph.create_body("Target"));
    graph.insert({document::PartHistoryKind::Feature,owner.id});f.part.set_body_history(graph);f.part.resolve_constructions();
    if(placed&&separate) {
        // The second Body is independent. Resolve its actual XY plane to the
        // placed cylinder's cross section using the established placement frame.
        auto& feature=f.part.history.back();feature.placement.x=12;feature.placement.y=-12;feature.placement.z=0;
        feature.placement.rotation_x=feature.placement.absolute_rotation_x=90;f.part.resolve_constructions();
    }
    f.bodies=kernel.evaluate_history(f.part.kernel_operations());
    check(!f.bodies.empty()&&f.bodies.back().calculation_errors.empty(),"Source calculation failed");
    for(const auto& face:f.bodies.back().mesh.triangle_references)if(face.owner_id==extrusion.id) {
        if(circular&&face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder){f.face=face;break;}
        if(!circular && face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane) {
            // Pick the native side by its published bounded coordinates, only
            // as fixture setup; the command itself consumes exact identities.
            const auto& mesh=f.bodies.back().mesh;
            for(std::size_t i=0;i<mesh.triangle_references.size();++i)if(mesh.triangle_references[i]==face) {
                bool side=true;for(unsigned j=0;j<3;++j){const auto& p=mesh.vertices.at(mesh.triangles.at(i*3+j));side&=placed?std::abs(p.z+4)<1e-8:std::abs(p.y+4)<1e-8;}
                if(side){f.face=face;break;}
            }
            if(f.face.valid())break;
        }
    }
    check(f.face.valid(),"Fixture face missing");return f;
}
const sketcher::SketchExternalReference& reference(const sketcher::Sketch& sketch,const std::string& id) {
    const auto found=std::ranges::find(sketch.external_references,id,&sketcher::SketchExternalReference::id);
    check(found!=sketch.external_references.end(),"Reference disappeared");return *found;
}
void verify(kernel::OcctKernel& kernel,unsigned& cases) {
    for(bool circular:{false,true})for(bool placed:{false,true})for(bool separate:{false,true}) {
        auto f=fixture(kernel,circular,placed,separate);workspace::Workspace live;
        const auto doc=f.part.document_id;live.add_part(f.part,f.bodies);
        const auto before=f.part.serialized();const auto source_packet=document::serialize_body_result(f.bodies.back());
        auto sketch=f.part.sketches.back();
        const auto branches=workspace::prepare_sketch_intersection_branches(live,kernel,doc,sketch,f.face,f.face);
        check(branches.size()==1&&branches.front().kind==sketcher::ExternalReferenceKind::Edge,"Unique branch was not offered individually");
        const auto source=branches.front();sketch.add_external_reference(source);
        static_cast<void>(sketch.add_external_profile_geometry(source.id));
        check(circular?(sketch.circles.size()==1&&sketch.bsplines.empty()):sketch.segments.size()==1,"Intersection contour lost native geometry");
        check(live.open_part(doc)->session.document().serialized()==before&&document::serialize_body_result(f.bodies.back())==source_packet,"Explicit query modified source");
        const auto no_query=sketch.serialized();
        check(!sketch.refresh_external_references(doc,{})&&sketch.serialized()==no_query,"Read-only snapshot replaced intersection with projection");
        auto duplicate=source;duplicate.id=sketcher::Sketch::create_external_reference(source.kind).id;
        bool rejected=false;try{sketch.add_external_reference(duplicate);}catch(const std::exception&){rejected=true;}
        check(rejected&&sketch.serialized()==no_query,"Duplicate branch changed Sketch");
        auto other=duplicate;other.intersection->branch_key+="-distinct";sketch.add_external_reference(other);
        check(sketch.external_references.size()>=2,"Distinct branches of one face were merged");sketch.remove_geometry(other.id);
        f.part.sketches.back()=sketch;
        for(double height:{2.,7.,5.}) {
            // A local plane displacement changes the actual intersection,
            // never the identity of this one selected branch.
            if(placed&&separate)f.part.history.back().placement.y=-7-height;
            else f.part.history.back().placement.z=height;
            f.part.resolve_constructions();
            auto& target=f.part.sketches.back();
            const document::DocumentSession session(f.part,f.bodies);
            const auto updated=workspace::calculate_part_sketch_intersections(kernel,session,target);
            check(!updated.empty()&&!updated.front().broken,"Plane displacement broke unique branch");
            static_cast<void>(target.refresh_external_references(doc,{},false,nullptr,false,&updated));
            const auto& actual=reference(target,source.id);check(actual.intersection==source.intersection&&!actual.broken,"Refresh renamed selected branch");
            if(circular)check(target.circles.size()==1&&std::abs(target.circles.front().radius-5)<1e-7,"Refresh deformed a native circle");
            else {
                check(actual.cached_points.size()>=2,"Open branch lost endpoints");
                for(const auto& ref:target.external_references)if(sketcher::is_external_endpoint_kind(ref.kind))
                    check(ref.intersection==source.intersection&&!ref.broken,"Open branch endpoint lost selected branch identity");
            }
            const auto stable=target.serialized();const auto again=workspace::calculate_part_sketch_intersections(kernel,session,target);
            check(!target.refresh_external_references(doc,{},false,nullptr,false,&again)&&target.serialized()==stable,"Repeated refresh drifted");
            ++cases;
        }
        auto& target=f.part.sketches.back();const auto stable=target.serialized();
        const auto valid=workspace::calculate_part_sketch_intersections(kernel,document::DocumentSession(f.part,f.bodies),target);
        auto missing=f.part;
        if(placed&&separate)missing.history.back().placement.y=-27;else missing.history.back().placement.z=20;
        missing.resolve_constructions();
        const auto broken=workspace::calculate_part_sketch_intersections(kernel,document::DocumentSession(missing,f.bodies),missing.sketches.back());
        check(!broken.empty()&&std::ranges::all_of(broken,[](const auto& r){return r.broken;}),"Disappeared section did not mark its exact branches broken");
        static_cast<void>(target.refresh_external_references(doc,{},false,nullptr,false,&broken));
        check(reference(target,source.id).broken&&reference(target,source.id).cached_points==reference(sketch,source.id).cached_points,"Lost branch did not freeze its own cache");
        static_cast<void>(target.refresh_external_references(doc,{},false,nullptr,false,&valid));
        check(!reference(target,source.id).broken&&target.serialized()==stable,"Restored exact branch did not recover");
        auto mismatch=valid;mismatch.front().intersection->branch_key+="-different";rejected=false;
        try{static_cast<void>(target.refresh_external_references(doc,{},false,nullptr,false,&mismatch));}catch(const std::exception&){rejected=true;}
        check(rejected&&target.serialized()==stable,"Wrong branch injection modified document");
        check(sketcher::Sketch::from_serialized(stable).serialized()==stable,"Sketch serialization lost branch identity");
        document::DocumentSession session(f.part,f.bodies);auto edited=f.part;static_cast<void>(edited.sketches.back().add_point(13,15));session.commit(edited,f.bodies);
        check(session.undo()&&session.document().sketches.back().serialized()==stable,"Undo lost branch/cache identity");
        check(session.redo()&&session.undo(),"Redo/Undo failed");
        const auto dir=std::filesystem::temp_directory_path()/("zima-sketch-intersection-"+kernel::make_stable_id());std::filesystem::create_directory(dir);
        const auto file=dir/"intersection.prtz";session.document().save(file,session.calculated_boundaries());
        std::vector<kernel::BodyResult> cold;auto reopened=document::PartDocument::load(file,&cold);
        check(reopened.sketches.back().serialized()==stable,"Native file lost selected branch or contour");
        kernel::OcctKernel cold_kernel;const auto cold_values=workspace::calculate_part_sketch_intersections(cold_kernel,document::DocumentSession(reopened,cold),reopened.sketches.back());
        check(!cold_values.empty()&&!cold_values.front().broken,"Reopened branch required hidden regeneration");
        if(!placed&&!separate) {
            workspace::Workspace original;original.add_part(f.part,f.bodies,file);const auto input=original.open_part(doc)->session.document().serialized();
            const auto copy_file=dir/"copy.prtz";static_cast<void>(original.save_copy(doc,copy_file,dir));std::vector<kernel::BodyResult> copied_bodies;
            const auto copied=document::PartDocument::load(copy_file,&copied_bodies);
            const auto& binding=reference(copied.sketches.back(),source.id);
            check(copied.document_id!=doc&&binding.source_document_id==copied.document_id&&binding.intersection==source.intersection,"Save As did not retain/rebase selected intersection identity");
            kernel::OcctKernel copied_kernel;const auto copied_values=workspace::calculate_part_sketch_intersections(copied_kernel,document::DocumentSession(copied,copied_bodies),copied.sketches.back());
            check(!copied_values.empty()&&!copied_values.front().broken&&original.open_part(doc)->session.document().serialized()==input,"Save As requires hidden regeneration or modifies source");++cases;
        }
        std::filesystem::remove_all(dir);++cases;
    }
}
void verify_assembly(kernel::OcctKernel& kernel,unsigned& cases) {
    for(bool nested:{false,true})for(bool circular:{false,true}) {
        auto f=fixture(kernel,circular,false,false);
        auto document=assembly::AssemblyDocument::create_default();
        auto occurrence=assembly::AssemblyDocument::create_part_occurrence("Source",f.part.document_id,{},f.bodies.back());
        occurrence.placement.x=20;occurrence.placement.y=-10;
        auto source_path=assembly::InstancePath{}.child(occurrence.occurrence_id);
        if(nested) {
            auto child=assembly::AssemblyDocument::create_default();child.components={occurrence};
            auto parent=assembly::AssemblyDocument::create_assembly_occurrence("Subassembly",child.document_id,{},child);
            parent.placement.x=-13;parent.placement.z=7;parent.placement.rotation_z=90;
            source_path=assembly::InstancePath{}.child(parent.occurrence_id).child(occurrence.occurrence_id);document.components={parent};
        } else document.components={occurrence};
        workspace::Workspace live;live.add_part(f.part,f.bodies);live.add_assembly(document);
        auto target=sketcher::Sketch::create_default();target.owner_container_id="assembly-sketch-fixture";
        target.resolved_origin=live.occurrence_point_to_scene(document.document_id,source_path,{0,0,5});
        target.resolved_x_axis=live.occurrence_direction_to_scene(document.document_id,source_path,{1,0,0});
        target.resolved_y_axis=live.occurrence_direction_to_scene(document.document_id,source_path,{0,1,0});
        target.resolved_normal=live.occurrence_direction_to_scene(document.document_id,source_path,{0,0,1});
        auto face=f.face;face.instance_path=source_path.encoded();
        const auto branches=workspace::prepare_sketch_intersection_branches(live,kernel,document.document_id,target,face,face);
        check(branches.size()==1&&branches.front().source_instance_path==source_path.encoded(),"Assembly source lost exact occurrence");
        target.add_external_reference(branches.front());static_cast<void>(target.add_external_profile_geometry(branches.front().id));
        check(circular?(target.circles.size()==1&&std::abs(target.circles.front().radius-5)<1e-7):target.segments.size()==1,"Assembly branch lost native contour");
        for(const auto& p:branches.front().cached_points)check(circular?std::abs(std::hypot(p[0],p[1])-5)<1e-7:std::abs(p[1]+4)<1e-7,"Assembly branch escaped Sketch support equations");
        const auto values=workspace::calculate_assembly_sketch_intersections(kernel,document,target);
        check(!values.empty()&&!values.front().broken&&values.front().intersection==branches.front().intersection,"Assembly refresh lost branch");
        auto missing=face;missing.instance_path=source_path.child("missing").encoded();bool rejected=false;
        try{static_cast<void>(workspace::prepare_sketch_intersection_branches(live,kernel,document.document_id,target,missing,missing));}catch(const std::exception&){rejected=true;}
        check(rejected,"Assembly query rebound a missing occurrence");++cases;
    }
}

void verify_multiple_branches(kernel::OcctKernel& kernel,unsigned& cases) {
    auto f=fixture(kernel,false,false,false);auto& profile=f.part.sketches.front();
    profile.points.clear();profile.segments.clear();profile.constraints.clear();profile.dimensions.clear();
    const std::array<std::array<double,2>,8> points{{{0,0},{100,0},{100,20},{20,20},{20,60},{100,60},{100,80},{0,80}}};
    for(std::size_t i=0;i<points.size();++i)static_cast<void>(profile.add_segment(points[i][0],points[i][1],points[(i+1)%points.size()][0],points[(i+1)%points.size()][1]));
    auto& target=f.part.sketches.back();target.plane=sketcher::SketchPlane::YZ;target.plane_auto=false;
    f.part.history.back().placement.x=50;f.part.history.back().placement.z=0;f.part.resolve_constructions();
    f.bodies=kernel.evaluate_history(f.part.kernel_operations());check(f.bodies.back().calculation_errors.empty(),"Concave profile calculation failed");
    const auto& mesh=f.bodies.back().mesh;kernel::FaceReference cap;
    for(std::size_t i=0;i<mesh.triangle_references.size();++i) {
        bool top=true;for(unsigned j=0;j<3;++j)top&=std::abs(mesh.vertices.at(mesh.triangles.at(i*3+j)).z-10)<1e-8;
        if(top){cap=mesh.triangle_references[i];break;}
    }
    check(cap.valid(),"Concave cap missing");workspace::Workspace live;live.add_part(f.part,f.bodies);
    auto pending=f.part.sketches.back();const auto branches=workspace::prepare_sketch_intersection_branches(live,kernel,f.part.document_id,pending,cap,cap);
    check(branches.size()==2&&branches[0].intersection!=branches[1].intersection,"Disconnected face sections were merged");
    for(const auto& branch:branches){pending.add_external_reference(branch);static_cast<void>(pending.add_external_profile_geometry(branch.id));}
    check(pending.segments.size()==2&&pending.external_references.size()==6,"Branch endpoints were shared across different sections");
    f.part.sketches.back()=pending;
    for(double x:{60.,40.,50.}) {
        f.part.history.back().placement.x=x;f.part.resolve_constructions();auto& actual=f.part.sketches.back();
        const auto values=workspace::calculate_part_sketch_intersections(kernel,document::DocumentSession(f.part,f.bodies),actual);
        check(values.size()==6&&std::ranges::none_of(values,[](const auto& r){return r.broken;}),"Section edit lost a branch or endpoint");
        static_cast<void>(actual.refresh_external_references(f.part.document_id,{},false,nullptr,false,&values));
        for(const auto& branch:branches)check(reference(actual,branch.id).intersection==branch.intersection,"Section edit rebound a disconnected branch");
        for(const auto& point:actual.points)check(std::abs(point.y-10)<1e-7&&(point.x<=20+1e-7||point.x>=60-1e-7),"Section contour escaped bounded face domains");
        ++cases;
    }
    auto actual=f.part.sketches.back();actual.remove_geometry(branches.front().id);
    check(actual.external_references.size()==3&&!reference(actual,branches.back().id).broken,"Removing one section removed another section's endpoints");++cases;
}
void verify_qualified_copy(kernel::OcctKernel& kernel,unsigned& cases) {
    auto f=fixture(kernel,true,false,false);const auto& local=f.bodies.back().body_boundaries.begin()->second.back();
    kernel::PatternRequest pattern;pattern.linear[0].count=3;pattern.linear[0].spacing=20;
    const auto copy=kernel.pattern_body(local,pattern,"copy",{},{},true);
    std::set<std::string> paths;
    for(const auto& face:copy.mesh.triangle_references)if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder) {
        if(!paths.insert(face.instance_path).second)continue;
        check(!face.instance_path.empty(),"Pattern fixture has no occurrence-qualified face");
        const auto before=document::serialize_body_result(copy);
        const auto curves=kernel.intersect_face_plane(copy,{face,"owning-sketch",{0,0,5},{0,0,1},.001});
        check(curves.edges.size()==1&&curves.edges.front().exact_spline.has_value(),"Qualified copy section was ambiguous or missing");
        check(document::serialize_body_result(copy)==before,"Qualified query modified copied native bindings");
        const auto cold=document::load_body_result(before);kernel::OcctKernel cold_kernel;
        const auto reopened=cold_kernel.intersect_face_plane(cold,{face,"owning-sketch",{0,0,5},{0,0,1},.001});
        check(reopened.edges.size()==1&&reopened.edges.front().reference==curves.edges.front().reference,"Cold qualified branch identity changed");++cases;
    }
    // Pattern count includes its separately owned source; this packet owns two copies.
    check(paths.size()==2,"Pattern fixture did not cover every copied occurrence");
}

void verify_circular_edits(kernel::OcctKernel& kernel,unsigned& cases) {
    for(bool diameter:{false,true})for(bool locked:{false,true})for(bool placed:{false,true}) {
        auto f=fixture(kernel,true,placed,false);workspace::Workspace live;live.add_part(f.part,f.bodies);
        auto sketch=f.part.sketches.back();const auto branches=workspace::prepare_sketch_intersection_branches(live,kernel,f.part.document_id,sketch,f.face,f.face);
        sketch.add_external_reference(branches.front());const auto circle=sketch.add_external_profile_geometry(branches.front().id);
        auto measure=diameter?sketch.create_circle_diameter_dimension(circle):sketch.create_circle_radius_dimension(circle);
        measure.driving=false;measure.locked=false;sketch.apply_dimension(measure);f.part.sketches.back()=sketch;
        auto driving=f.part.sketches.front().create_circle_radius_dimension(f.part.sketches.front().circles.front().id);
        driving.locked=locked;f.part.sketches.front().apply_dimension(driving);
        unsigned step{};
        for(double radius:{8.,3.,5.}) {
            auto& source=f.part.sketches.front();check(source.set_dimension_value(driving.id,radius),"Source radius edit was rejected");
            const auto& center=source.circles.front().center_point_id;const double x=step==0?2.:step==1?-3.:0.,y=step==0?-1.:step==1?4.:0.;
            check(source.move_point(center,x,y),"Underconstrained source circle did not move");
            const auto source_definition=source.serialized();f.bodies=kernel.evaluate_history(f.part.kernel_operations());
            static_cast<void>(workspace::refresh_sketch_external_references(f.part,f.bodies,&kernel));
            const auto& actual=f.part.sketches.back();const auto& native=actual.circles.front();const auto* point=actual.find_point(native.center_point_id);
            check(actual.bsplines.empty()&&native.id==circle&&std::abs(native.radius-radius)<1e-7,"Source edit changed circle type/identity or failed radius equation");
            check(point&&std::hypot(point->x-x,point->y-y)<1e-7,"Source drag lost associated circle center");
            const auto measured=std::ranges::find(actual.dimensions,measure.id,&sketcher::SketchDimension::id);
            check(measured!=actual.dimensions.end()&&std::abs(measured->value-radius*(diameter?2:1))<1e-7,"Source edit left a stale reference radius/diameter");
            check(f.part.sketches.front().serialized()==source_definition,"Reference refresh modified the source definition");
            auto bound=actual;const auto stable=bound.serialized();
            check(!bound.move_point(native.center_point_id,x+7,y-9)&&bound.serialized()==stable,"Bound intersection center escaped dragging");
            static_cast<void>(bound.set_dimension_value(measure.id,123));check(bound.serialized()==stable,"Reference dimension drove read-only intersection geometry");
            check(reference(actual,branches.front().id).intersection==branches.front().intersection&&!reference(actual,branches.front().id).broken,"Source edit lost branch identity");
            ++step;++cases;
        }
        const auto serialized=f.part.sketches.back().serialized_json();auto invalid=serialized;
        invalid["external_references"][0].erase("intersection");bool rejected=false;
        try{static_cast<void>(sketcher::Sketch::from_serialized_json(invalid));}catch(const std::exception&){rejected=true;}
        check(rejected,"Intersection discriminator accepted a missing persisted binding");
        invalid=serialized;invalid["external_references"][0]["kind"]="edge";rejected=false;
        try{static_cast<void>(sketcher::Sketch::from_serialized_json(invalid));}catch(const std::exception&){rejected=true;}
        check(rejected,"Ordinary projection mode accepted an intersection binding");
        invalid=serialized;invalid["external_references"][0]["source_semantic_key"]="unrelated-original-face";rejected=false;
        try{static_cast<void>(sketcher::Sketch::from_serialized_json(invalid));}catch(const std::exception&){rejected=true;}
        check(rejected,"Intersection binding accepted unrelated original face ancestry");
    }
}

void verify_part_context(kernel::OcctKernel& kernel,unsigned& cases) {
    for(bool nested:{false,true}) {
        auto f=fixture(kernel,true,false,false);workspace::Workspace live;live.add_part(f.part,f.bodies);
        auto assembly_doc=assembly::AssemblyDocument::create_default();
        auto own=assembly::AssemblyDocument::create_part_occurrence("Source",f.part.document_id,{},f.bodies.back());
        auto own_path=assembly::InstancePath{}.child(own.occurrence_id);assembly_doc.components={own};live.add_assembly(assembly_doc);
        check(live.activate_occurrence(assembly_doc.document_id,own_path).has_value(),"Cannot activate own reference occurrence");
        auto offered=f.face;offered.instance_path=own_path.encoded();
        const auto local=workspace::prepare_sketch_intersection_branches(live,kernel,f.part.document_id,f.part.sketches.back(),offered,offered);
        check(local.size()==1&&local.front().source_instance_path.empty()&&local.front().context_assembly_document_id.empty(),"Own occurrence was persisted as a cross-document dependency");++cases;
        auto dependent=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        auto feature=document::PartDocument::create_sketch_container();feature.feature.sketch_id=sketch.id;feature.placement.z=5;sketch.owner_container_id=feature.id;
        dependent.history={feature};dependent.sketches={sketch};document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Target"));graph.insert({document::PartHistoryKind::Feature,feature.id});dependent.set_body_history(graph);dependent.resolve_constructions();
        live.add_part(dependent);
        own.placement.x=20;own.placement.y=-10;assembly_doc.components={own};
        auto source_path=own_path;
        if(nested) {
            auto subassembly=assembly::AssemblyDocument::create_default();subassembly.components={own};live.add_assembly(subassembly);
            auto parent=assembly::AssemblyDocument::create_assembly_occurrence("Nested",subassembly.document_id,{},subassembly);
            parent.placement.x=-13;parent.placement.rotation_z=90;
            source_path=assembly::InstancePath{}.child(parent.occurrence_id).child(own.occurrence_id);assembly_doc.components={parent};
        }
        auto target=assembly::AssemblyDocument::create_part_occurrence("Target",dependent.document_id,{},kernel::BodyResult{});
        const auto dependent_path=assembly::InstancePath{}.child(target.occurrence_id);assembly_doc.components.push_back(target);
        live.open_assembly(assembly_doc.document_id)->session.commit(assembly_doc);
        check(live.activate_occurrence(assembly_doc.document_id,dependent_path).has_value(),"Cannot activate dependent Part occurrence");
        offered=f.face;offered.instance_path=source_path.encoded();
        auto pending=dependent.sketches.back();const auto branches=workspace::prepare_sketch_intersection_branches(live,kernel,dependent.document_id,pending,offered,offered);
        check(branches.size()==1&&branches.front().context_instance_path==dependent_path.encoded()&&branches.front().source_instance_path==source_path.encoded(),"Contextual branch lost its source/dependent occurrence");
        pending.add_external_reference(branches.front());static_cast<void>(pending.add_external_profile_geometry(branches.front().id));
        const auto& originals=f.bodies.back().mesh.original_references.edges;
        const auto rim=std::ranges::find_if(originals,[](const auto& edge){return edge.exact_spline&&edge.exact_spline->degree==2;});
        check(rim!=originals.end(),"Ordinary rim reference fixture missing");
        const auto ordinary=workspace::prepare_sketch_external_reference(live,dependent.document_id,pending,
            sketcher::ExternalReferenceKind::Edge,rim->reference.owner_id,rim->reference.semantic_key,source_path.encoded());
        pending.add_external_reference(ordinary);
        check(workspace::mutate_document_sketch(live,dependent.document_id,pending.id,[&](auto& value){value=pending;}),"Contextual intersection did not commit through shared Sketch ownership");
        auto candidate=live.open_part(dependent.document_id)->session.document();
        const auto untouched=candidate.sketches.back().serialized();
        check(!workspace::refresh_sketch_reference_snapshot(live,dependent.document_id,candidate.sketches.back())&&candidate.sketches.back().serialized()==untouched,"Read-only context snapshot recalculated an intersection");
        auto source=live.open_part(f.part.document_id)->session.document();auto dimension=source.sketches.front().create_circle_radius_dimension(source.sketches.front().circles.front().id);dimension.value=7;source.sketches.front().apply_dimension(dimension);
        auto changed=kernel.evaluate_history(source.kernel_operations());live.open_part(f.part.document_id)->session.commit(source,changed);live.refresh_source_geometry();
        check(workspace::refresh_context_sketch_intersections(live,kernel,candidate),"Explicit contextual refresh ignored source radius edit");
        const auto& actual=candidate.sketches.back();const auto* center=actual.find_point(actual.circles.front().center_point_id);
        const auto expected=live.occurrence_point_to_scene(assembly_doc.document_id,source_path,{0,0,5});
        if(!center||std::abs(actual.circles.front().radius-7)>=1e-7||std::hypot(center->x-expected.x,center->y-expected.y)>=1e-7)
            std::cerr<<"context nested="<<nested<<" radius="<<actual.circles.front().radius<<" center="<<(center?center->x:999)<<","<<(center?center->y:999)<<" expected="<<expected.x<<","<<expected.y<<" broken="<<reference(actual,branches.front().id).broken<<std::endl;
        check(std::abs(actual.circles.front().radius-7)<1e-7&&center&&std::hypot(center->x-expected.x,center->y-expected.y)<1e-7,"Contextual branch escaped actual occurrence equations");
        check(!reference(actual,branches.front().id).broken,"Contextual branch lost its source identity");
        check(reference(actual,ordinary.id)==ordinary,"Intersection refresh modified an ordinary projection from the same source");++cases;
        if(!nested) {
            auto altered=live.open_assembly(assembly_doc.document_id)->session.document();
            altered.components.front().calculated_source=kernel.mirror_body(changed.back(),{{50,0,0},{1,0,0}});
            live.open_assembly(assembly_doc.document_id)->session.commit(altered);
            bool rejected=false;try{static_cast<void>(workspace::prepare_sketch_intersection_branches(live,kernel,dependent.document_id,actual,offered,offered));}catch(const std::exception&){rejected=true;}
            check(rejected,"Part reference silently replaced Assembly-owned modified geometry with untrimmed source");++cases;
        }
    }
}

void verify_standalone_assembly(kernel::OcctKernel& kernel,unsigned& cases) {
    auto f=fixture(kernel,true,false,false);
    const auto dir=std::filesystem::temp_directory_path()/("zima-assembly-intersection-"+kernel::make_stable_id());std::filesystem::create_directory(dir);
    const auto source_file=dir/"source.prtz";f.part.save(source_file,f.bodies);
    auto document=assembly::AssemblyDocument::create_default();
    auto component=assembly::AssemblyDocument::create_part_occurrence("Source",f.part.document_id,source_file,f.bodies.back());component.placement.x=20;component.placement.y=-10;
    document.components.push_back(component);document.insert_history_entry(assembly::AssemblyDocument::HistoryKind::Component,component.occurrence_id);
    auto sketch=sketcher::Sketch::create_default();auto owner=document::PartDocument::create_sketch_container();owner.placement.z=5;sketch.owner_container_id=owner.id;
    document.insert_sketch(sketch,owner);document.resolve_constructions();sketch=document.sketches.front();
    workspace::Workspace live;live.add_part(f.part,f.bodies,source_file);live.add_assembly(document);
    auto face=f.face;face.instance_path=assembly::InstancePath{}.child(component.occurrence_id).encoded();
    const auto branches=workspace::prepare_sketch_intersection_branches(live,kernel,document.document_id,sketch,face,face);
    sketch.add_external_reference(branches.front());const auto circle=sketch.add_external_profile_geometry(branches.front().id);document.sketches.front()=sketch;
    const auto initial=document;
    auto dimension=f.part.sketches.front().create_circle_radius_dimension(f.part.sketches.front().circles.front().id);f.part.sketches.front().apply_dimension(dimension);
    for(double radius:{6.,8.,7.}) {
        check(f.part.sketches.front().set_dimension_value(dimension.id,radius),"Assembly source radius edit failed");
        f.bodies=kernel.evaluate_history(f.part.kernel_operations());f.part.save(source_file,f.bodies);
        document.components.front().calculated_source=f.bodies.back();
        workspace::calculate_resolved_assembly_cuts(kernel,document);
        const auto& actual=document.sketches.front();check(actual.circles.front().id==circle&&std::abs(actual.circles.front().radius-radius)<1e-7&&!reference(actual,branches.front().id).broken,"Standalone Assembly Sketch did not regenerate its intersection");
        ++cases;
    }
    const auto assembly_file=dir/"reference.asmz";document.save(assembly_file);
    auto reopened=assembly::AssemblyDocument::load(assembly_file);kernel::OcctKernel cold;
    const auto values=workspace::calculate_assembly_sketch_intersections(cold,reopened,reopened.sketches.front());
    check(values.size()==1&&!values.front().broken&&values.front().intersection==branches.front().intersection,"Cold Assembly lost selected branch definition or native source");++cases;
    workspace::Workspace current;current.add_part(f.part,f.bodies,source_file);current.add_assembly(initial,assembly_file);
    workspace::regenerate_assembly(current,kernel,initial.document_id);
    auto& session=current.open_assembly(initial.document_id)->session;
    check(session.can_undo()&&std::abs(session.document().sketches.front().circles.front().radius-7)<1e-7,"Assembly regeneration did not commit changed standalone intersection");
    check(session.undo()&&std::abs(session.document().sketches.front().circles.front().radius-5)<1e-7&&session.redo()&&std::abs(session.document().sketches.front().circles.front().radius-7)<1e-7,"Assembly regeneration Undo/Redo lost associated contour");++cases;
    // A later real Assembly cut must not become an input to this preceding
    // standalone Sketch. Its stored pre-cut native packet survives reopen.
    auto tool=document::PartDocument::create_default();auto profile=sketcher::Sketch::create_default();static_cast<void>(profile.add_rectangle(20,-20,30,0));
    auto extrusion=document::PartDocument::create_extrusion_container(profile.id);profile.owner_container_id=extrusion.id;extrusion.extrusion.length_forward=10;
    tool.history={extrusion};tool.sketches={profile};document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Cutter"));graph.insert({document::PartHistoryKind::Feature,extrusion.id});tool.set_body_history(graph);tool.resolve_constructions();
    const auto cutter=kernel.evaluate_history(tool.kernel_operations());check(cutter.back().calculation_errors.empty(),"Assembly boundary cutter failed");
    assembly::AssemblyCut cut;cut.definition=extrusion;cut.definition.combine_mode=document::CombineMode::Subtract;cut.target_occurrence_ids={component.occurrence_id};cut.input_component_bodies.emplace(component.occurrence_id,f.bodies.back());
    document.sketches.push_back(profile);document.cuts.push_back(cut);document.insert_history_entry(assembly::AssemblyDocument::HistoryKind::Cut,extrusion.id);
    document.components.front().calculated_source=kernel.subtract_bodies(f.bodies.back(),cutter.back(),{20,-10,0},{});
    check(document.components.front().calculated_source->volume<f.bodies.back().volume-1,"Assembly boundary fixture did not actually remove material");
    workspace::Workspace rolled;rolled.add_assembly(document);
    const auto before=workspace::prepare_sketch_intersection_branches(rolled,kernel,document.document_id,document.sketches.front(),face,face);
    check(before.size()==1&&before.front().exact_spline,"Standalone Sketch queried downstream cut geometry instead of its input");
    auto expected=sketcher::Sketch::create_default();expected.add_external_reference(before.front());static_cast<void>(expected.add_external_profile_geometry(before.front().id));
    check(expected.circles.size()==1&&std::abs(expected.circles.front().radius-7)<1e-7,"Input boundary reference became a trimmed downstream arc");
    document.save(assembly_file);reopened=assembly::AssemblyDocument::load(assembly_file);
    const auto boundary_values=workspace::calculate_assembly_sketch_intersections(cold,reopened,reopened.sketches.front());
    check(!boundary_values.empty()&&!boundary_values.front().broken,"Cold standalone Sketch lost its actual pre-cut native boundary");++cases;
    std::filesystem::remove_all(dir);
}

void verify_general_surface_frames(kernel::OcctKernel& kernel,unsigned& cases) {
    for(bool placed:{false,true}) {
        auto f=fixture(kernel,true,placed,false);const auto old_target=f.part.history.back().id;
        f.part.history.pop_back();f.part.sketches.pop_back();
        auto graph=f.part.body_history;auto source=*graph.find(graph.active_body_id());
        std::erase_if(source.entries,[&](const auto& entry){return entry.id==old_target;});source.cursor=source.entries.size();graph.update_body(source);
        const auto target_id=graph.create_body("Surface");auto target=*graph.find(target_id);target.scope.placement=source.scope.placement;graph.update_body(target);
        auto parent=document::create_general_surface();
        parent.placement.x=2;parent.placement.y=-3;parent.placement.z=5;parent.placement.rotation_z=parent.placement.absolute_rotation_z=90;
        const std::array<std::array<double,2>,4> corners{{{0,0},{2,0},{2,2},{0,2}}};
        for(unsigned i=0;i<4;++i) {
            auto& boundary=parent.general_surface.boundaries[i];auto sketch=sketcher::Sketch::from_serialized(boundary.sketch_serialized);
            static_cast<void>(sketch.add_segment(corners[i][0],corners[i][1],corners[(i+1)%4][0],corners[(i+1)%4][1]));boundary.sketch_serialized=sketch.serialized();
        }
        document::reframe_general_surface(parent);graph.insert({document::PartHistoryKind::Feature,parent.id});f.part.history.push_back(parent);f.part.set_body_history(graph);f.part.resolve_constructions();
        f.bodies=kernel.evaluate_history(f.part.kernel_operations());check(!f.bodies.empty()&&f.bodies.back().calculation_errors.empty(),"General Surface frame fixture failed native calculation");
        const auto& feature=f.part.history.back();const auto& boundary=feature.general_surface.boundaries.front();
        auto definition=sketcher::Sketch::from_serialized(boundary.sketch_serialized);const auto display=document::general_surface_display_sketch(feature,boundary);
        workspace::Workspace live;live.add_part(f.part,f.bodies);
        const auto branches=workspace::prepare_sketch_intersection_branches(live,kernel,f.part.document_id,display,f.face,f.face,target_id);
        check(branches.size()==1,"Body-frame owned Sketch did not offer its source section");
        for(const auto& p:branches.front().cached_points) {
            const auto point=display.world_point(p[0],p[1]);
            check(std::abs(point.z-5)<1e-7&&std::abs(std::hypot(point.x,point.y)-5)<1e-7,"Owned display Sketch applied its parent placement more than once");
        }
        const auto points=definition.points;const auto segments=definition.segments;
        definition.add_external_reference(branches.front());
        const auto values=workspace::calculate_part_sketch_intersections(kernel,document::DocumentSession(f.part,f.bodies),definition);
        check(values.size()==1&&!values.front().broken&&values.front().intersection==branches.front().intersection,"Definition-frame refresh lost the same selected branch");
        for(std::size_t i=0;i<values.front().cached_points.size();++i) {
            const auto& a=values.front().cached_points[i];const auto& b=branches.front().cached_points[i];
            check(std::hypot(a[0]-b[0],a[1]-b[1])<1e-7,"Body-frame and stored-frame queries disagree");
        }
        static_cast<void>(definition.refresh_external_references({}, {},false,nullptr,false,&values));
        check(!reference(definition,branches.front().id).broken&&definition.points==points&&definition.segments==segments,"Reference refresh changed unrelated owned profile geometry");
        ++cases;
    }
}

void verify_arc_edits(kernel::OcctKernel& kernel,unsigned& cases) {
    for(bool diameter:{false,true})for(bool locked:{false,true})for(bool flipped:{false,true}) {
        auto f=fixture(kernel,true,false,false);auto source=sketcher::Sketch::create_default();source.id=f.part.sketches.front().id;source.owner_container_id=f.part.history.front().id;
        const auto source_arc=source.add_arc(0,0,5,0,0,5);static_cast<void>(source.add_segment(0,5,0,0));static_cast<void>(source.add_segment(0,0,5,0));
        source.set_point_fixed(source.arcs.front().center_point_id,true);
        auto driving=source.create_arc_radius_dimension(source_arc);driving.locked=locked;source.apply_dimension(driving);f.part.sketches.front()=source;
        f.part.history.back().placement.rotation_x=f.part.history.back().placement.absolute_rotation_x=flipped?180:0;f.part.resolve_constructions();
        f.bodies=kernel.evaluate_history(f.part.kernel_operations());check(f.bodies.back().calculation_errors.empty(),"Arc section source failed calculation");
        auto face=std::ranges::find_if(f.bodies.back().mesh.triangle_references,[](const auto& face){return face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder;});
        check(face!=f.bodies.back().mesh.triangle_references.end(),"Arc section cylinder missing");const auto support=*face;
        workspace::Workspace live;live.add_part(f.part,f.bodies);auto pending=f.part.sketches.back();
        const auto branches=workspace::prepare_sketch_intersection_branches(live,kernel,f.part.document_id,pending,support,support);
        check(branches.size()==1,"Arc section branch missing");pending.add_external_reference(branches.front());const auto arc=pending.add_external_profile_geometry(branches.front().id);
        check(pending.arcs.size()==1&&pending.bsplines.empty(),"Certified section arc became a spline");
        auto measured=diameter?pending.create_arc_diameter_dimension(arc):pending.create_arc_radius_dimension(arc);measured.driving=false;measured.locked=false;pending.apply_dimension(measured);f.part.sketches.back()=pending;
        for(double radius:{8.,3.,5.}) {
            check(f.part.sketches.front().set_dimension_value(driving.id,radius),"Source arc radius edit failed");
            const auto source_before=f.part.sketches.front().serialized();f.bodies=kernel.evaluate_history(f.part.kernel_operations());
            static_cast<void>(workspace::refresh_sketch_external_references(f.part,f.bodies,&kernel));
            const auto& actual=f.part.sketches.back();const auto& native=actual.arcs.front();
            check(native.id==arc&&actual.bsplines.empty()&&std::abs(native.radius-radius)<1e-7&&!reference(actual,branches.front().id).broken,"Arc section lost analytic identity/radius after source edit");
            const auto& original=f.part.sketches.front();const auto* source_center=original.find_point(original.arcs.front().center_point_id);
            const auto* center=actual.find_point(native.center_point_id);const auto world=actual.world_point(center->x,center->y);
            check(std::hypot(world.x-source_center->x,world.y-source_center->y,world.z-5)<1e-7,"Arc center escaped source/Sketch plane equations");
            for(const auto& id:{native.start_point_id,native.end_point_id}) {
                const auto* p=actual.find_point(id);const auto world=actual.world_point(p->x,p->y);
                check(std::abs(world.z-5)<1e-7&&std::abs(std::hypot(world.x-source_center->x,world.y-source_center->y)-radius)<1e-7,"Arc endpoint escaped its exact supports");
                auto bound=actual;const auto before=bound.serialized();check(!bound.move_point(id,p->x+2,p->y-3)&&bound.serialized()==before,"Bound arc endpoint escaped repeated dragging");
            }
            const auto dimension=std::ranges::find(actual.dimensions,measured.id,&sketcher::SketchDimension::id);
            check(dimension!=actual.dimensions.end()&&std::abs(dimension->value-radius*(diameter?2:1))<1e-7,"Arc reference radius/diameter became stale");
            check(original.serialized()==source_before,"Arc reference refresh modified its source");++cases;
        }
    }
}
void verify_dependent_geometry(kernel::OcctKernel& kernel,unsigned& cases) {
    for(bool placed:{false,true}) {
        auto f=fixture(kernel,true,placed,true);workspace::Workspace live;live.add_part(f.part,f.bodies);
        auto target=f.part.sketches.back();const auto branches=workspace::prepare_sketch_intersection_branches(live,kernel,f.part.document_id,target,f.face,f.face);
        target.add_external_reference(branches.front());static_cast<void>(target.add_external_profile_geometry(branches.front().id));f.part.sketches.back()=target;
        auto& feature=f.part.history.back();feature.feature_kind=document::FeatureKind::Feature;feature.feature.type=document::FeatureType::Modeling;
        feature.feature.sides[0].operation=document::FeatureSideOperation::Extrusion;feature.feature.sides[0].length=2;
        const auto target_body=f.part.body_history.bodies().back().scope.id;
        auto radius=f.part.sketches.front().create_circle_radius_dimension(f.part.sketches.front().circles.front().id);f.part.sketches.front().apply_dimension(radius);
        for(double value:{7.,3.,5.}) {
            check(f.part.sketches.front().set_dimension_value(radius.id,value),"Dependent extrusion source radius edit failed");
            const auto source=f.part.sketches.front().serialized();const auto previous=f.bodies;
            f.bodies=workspace::calculate_part_with_resolved_references(kernel,f.part,&previous);
            check(!f.bodies.empty()&&f.bodies.back().calculation_errors.empty()&&f.bodies.back().body_outputs.contains(target_body),"Associated contour did not calculate its dependent Body");
            auto solid=f.bodies.back().body_outputs.at(target_body).get();kernel.ensure_body_properties(solid);
            check(std::abs(solid.volume-2*std::acos(-1.)*value*value)<1e-6,"Dependent extrusion retained stale section geometry");
            check(f.part.sketches.front().serialized()==source&&!reference(f.part.sketches.back(),branches.front().id).broken,"Dependent calculation modified its source or broke the branch");++cases;
        }
        const auto dir=std::filesystem::temp_directory_path()/("zima-dependent-section-"+kernel::make_stable_id());std::filesystem::create_directory(dir);
        const auto file=dir/"dependent.prtz";f.part.save(file,f.bodies);std::vector<kernel::BodyResult> cold;auto reopened=document::PartDocument::load(file,&cold);kernel::OcctKernel cold_kernel;
        const auto restored=workspace::calculate_part_with_resolved_references(cold_kernel,reopened,&cold);
        auto solid=restored.back().body_outputs.at(target_body).get();cold_kernel.ensure_body_properties(solid);
        check(std::abs(solid.volume-50*std::acos(-1.))<1e-6&&!reference(reopened.sketches.back(),branches.front().id).broken,"Cold dependent extrusion lost its exact associated profile");std::filesystem::remove_all(dir);++cases;
    }
}
void verify_trimmed_fragment(kernel::OcctKernel& kernel,unsigned& cases) {
    for(bool placed:{false,true})for(bool flipped:{false,true}) {
    auto f=fixture(kernel,true,placed,false);
    if(flipped)f.part.history.back().placement.rotation_x=f.part.history.back().placement.absolute_rotation_x=180;auto cutter=sketcher::Sketch::create_default();static_cast<void>(cutter.add_rectangle(0,-10,10,10));
    auto cut=document::PartDocument::create_extrusion_container(cutter.id);cut.feature.sides[0].length=10;cut.combine_mode=document::CombineMode::Subtract;cutter.owner_container_id=cut.id;
    f.part.history.insert(f.part.history.end()-1,cut);f.part.sketches.insert(f.part.sketches.end()-1,cutter);
    auto graph=f.part.body_history;const auto body=graph.bodies().front().scope.id;graph.activate(body);graph.set_history_cursor(body,1);graph.insert({document::PartHistoryKind::Feature,cut.id});graph.set_history_cursor(body,graph.find(body)->entries.size());
    f.part.set_body_history(graph);f.part.resolve_constructions();f.bodies=kernel.evaluate_history(f.part.kernel_operations());
    check(f.bodies.back().calculation_errors.empty(),"Trimmed source fragment calculation failed");
    const auto offered=std::ranges::find_if(f.bodies.back().mesh.triangle_references,[](const auto& face){return face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder;});
    check(offered!=f.bodies.back().mesh.triangle_references.end(),"Actual trimmed cylindrical fragment missing");
    workspace::Workspace live;live.add_part(f.part,f.bodies);auto target=f.part.sketches.back();
    const auto branches=workspace::prepare_sketch_intersection_branches(live,kernel,f.part.document_id,target,*offered,f.face);
    check(branches.size()==1&&branches.front().source_owner_id==f.face.owner_id&&branches.front().source_semantic_key==f.face.semantic_key,"Trimmed fragment lost its oldest original source");
    target.add_external_reference(branches.front());static_cast<void>(target.add_external_profile_geometry(branches.front().id));
    check(target.arcs.size()==1&&target.circles.empty()&&target.bsplines.empty()&&std::abs(target.arcs.front().radius-5)<1e-7,"Trimmed fragment was replaced by its untrimmed original circle");
    for(const auto& p:branches.front().cached_points)check(p[0]<=1e-7&&std::abs(std::hypot(p[0],p[1])-5)<1e-7,"Section escaped actual offered half-cylinder domain");
    f.part.sketches.back()=target;const auto values=workspace::calculate_part_sketch_intersections(kernel,document::DocumentSession(f.part,f.bodies),target);
    check(!values.empty()&&!values.front().broken&&values.front().intersection==branches.front().intersection,"Trimmed fragment refresh lost exact selected branch");++cases;
    auto driving=f.part.sketches.front().create_circle_radius_dimension(f.part.sketches.front().circles.front().id);f.part.sketches.front().apply_dimension(driving);
    const auto bottom=cutter.segments.front().first_point_id,top=cutter.segments.back().first_point_id;
    for(const auto change:std::array<std::array<double,2>,3>{{{7,1},{4,-1},{5,0}}}) {
        check(f.part.sketches.front().set_dimension_value(driving.id,change[0]),"Trimmed cylinder radius did not change");
        auto& tool=f.part.sketches[1];check(tool.move_point(bottom,change[1],-10)&&tool.move_point(top,change[1],10),"Cut plane did not move");
        check(std::abs(tool.find_point(bottom)->x-change[1])<1e-7&&std::abs(tool.find_point(top)->x-change[1])<1e-7,"Cut plane fixture lost its intended equation");
        f.part.resolve_constructions();const auto before=f.part.sketches.front().serialized()+f.part.sketches[1].serialized();
        f.bodies=kernel.evaluate_history(f.part.kernel_operations());static_cast<void>(workspace::refresh_sketch_external_references(f.part,f.bodies,&kernel));
        const auto& actual=f.part.sketches.back();const auto& branch=reference(actual,branches.front().id);
        check(!branch.broken&&branch.intersection==branches.front().intersection&&actual.arcs.size()==1&&std::abs(actual.arcs.front().radius-change[0])<1e-7,"Cylinder/plane root identity changed after source edits");
        for(const auto& p:branch.cached_points) {
            const auto world=actual.world_point(p[0],p[1]);
            check(world.x<=change[1]+1e-7&&std::abs(world.z-5)<1e-7&&std::abs(std::hypot(world.x,world.y)-change[0])<1e-7,"Trimmed arc escaped its current material/plane/radius equations");
        }
        check(f.part.sketches.front().serialized()+f.part.sketches[1].serialized()==before,"Trimmed section refresh changed source geometry");++cases;
    }
    const auto dir=std::filesystem::temp_directory_path()/("zima-trimmed-section-"+kernel::make_stable_id());std::filesystem::create_directory(dir);
    const auto file=dir/"fragment.prtz";f.part.save(file,f.bodies);std::vector<kernel::BodyResult> cold;const auto reopened=document::PartDocument::load(file,&cold);kernel::OcctKernel cold_kernel;
    const auto restored=workspace::calculate_part_sketch_intersections(cold_kernel,document::DocumentSession(reopened,cold),reopened.sketches.back());
    check(!restored.empty()&&!restored.front().broken&&restored.front().intersection==branches.front().intersection,"Cold trimmed fragment lost its analytic endpoint domains");std::filesystem::remove_all(dir);++cases;
    }
}
void verify_line_dimensions(kernel::OcctKernel& kernel,unsigned& cases) {
    using Kind=sketcher::DimensionKind;
    for(bool placed:{false,true})for(bool rotated:{false,true})for(bool reversed:{false,true})for(bool locked:{false,true})
    for(const auto kind:{Kind::Distance,Kind::DistanceX,Kind::DistanceY,Kind::Angle}) {
        auto f=fixture(kernel,false,placed,false);
        if(rotated)f.part.history.back().placement.rotation_z=f.part.history.back().placement.absolute_rotation_z=90;
        f.part.resolve_constructions();workspace::Workspace live;live.add_part(f.part,f.bodies);
        auto target=f.part.sketches.back();const auto branches=workspace::prepare_sketch_intersection_branches(live,kernel,f.part.document_id,target,f.face,f.face);
        target.add_external_reference(branches.front());const auto line=target.add_external_profile_geometry(branches.front().id);
        const auto native=target.segments.front();
        auto measure=kind==Kind::Angle?target.create_segment_dimension(line,kind):target.create_point_dimension(
            reversed?native.second_point_id:native.first_point_id,reversed?native.first_point_id:native.second_point_id,kind);
        measure.driving=false;measure.locked=false;target.apply_dimension(measure);f.part.sketches.back()=target;
        auto& source=f.part.sketches.front();const auto edge=source.segments.front();auto driving=source.create_segment_dimension(edge.id);driving.locked=locked;source.apply_dimension(driving);
        for(double length:{6.,14.,10.}) {
            check(source.set_dimension_value(driving.id,length),"Source line dimension did not accept a successive edit");
            const auto* first=source.find_point(edge.first_point_id);
            check(source.move_point(edge.first_point_id,first->x+1,first->y-1),"Underconstrained source rectangle did not drag");
            const auto source_before=source.serialized();const auto* a=source.find_point(edge.first_point_id);const auto* b=source.find_point(edge.second_point_id);
            const auto source_dimension=std::ranges::find(source.dimensions,driving.id,&sketcher::SketchDimension::id);
            const double current_length=std::hypot(b->x-a->x,b->y-a->y);
            check(source_dimension!=source.dimensions.end()&&std::abs(current_length-source_dimension->value)<1e-7&&(!locked||std::abs(current_length-length)<1e-7),"Dragged source escaped its applicable locked/unlocked dimension equation");
            f.bodies=kernel.evaluate_history(f.part.kernel_operations());static_cast<void>(workspace::refresh_sketch_external_references(f.part,f.bodies,&kernel));
            const auto& actual=f.part.sketches.back();const auto& segment=actual.segments.front();
            check(segment.id==line&&!reference(actual,branches.front().id).broken,"Line section lost native identity after source edit/drag");
            const auto* p=actual.find_point(segment.first_point_id);const auto* q=actual.find_point(segment.second_point_id);
            const auto wp=actual.world_point(p->x,p->y),wq=actual.world_point(q->x,q->y);
            const auto distance=[](auto p,const auto* q){return std::hypot(p.x-q->x,p.y-q->y,p.z-5);};
            check((distance(wp,a)<1e-7&&distance(wq,b)<1e-7)||(distance(wp,b)<1e-7&&distance(wq,a)<1e-7),"Line section endpoints escaped independent source/plane equations");
            const auto value=std::ranges::find(actual.dimensions,measure.id,&sketcher::SketchDimension::id);
            const double sign=reversed&&kind!=Kind::Angle?-1.:1.,dx=sign*(q->x-p->x),dy=sign*(q->y-p->y);
            const double expected=kind==Kind::DistanceX?dx:kind==Kind::DistanceY?dy:kind==Kind::Angle?std::atan2(dy,dx)*180/std::acos(-1.):std::hypot(dx,dy);
            check(value!=actual.dimensions.end()&&std::abs(kind==Kind::Angle?std::remainder(value->value-expected,360.):value->value-expected)<1e-7,"Line reference measurement became stale");
            auto bound=actual;const auto stable=bound.serialized();
            check(!bound.move_point(p->id,p->x+3,p->y+4)&&bound.serialized()==stable&&!bound.move_point(q->id,q->x-2,q->y-3)&&bound.serialized()==stable,"Bound section endpoints escaped dragging");
            static_cast<void>(bound.set_dimension_value(measure.id,123));check(bound.serialized()==stable,"Reference dimension drove an immutable line section");
            check(source.serialized()==source_before,"Line refresh changed its read-only source");++cases;
        }
    }
}
void verify_conic_endpoint_domains(kernel::OcctKernel& engine,unsigned& cases) {
    using Kind=sketcher::DimensionKind;
    const auto directory=std::filesystem::temp_directory_path()/("zima-conic-root-domains-"+kernel::make_stable_id());
    std::filesystem::create_directory(directory);unsigned fixture_number=0;
    for(bool ellipse:{false,true})for(bool cap:{false,true})for(bool alternative:{false,true})
    for(bool locked:{false,true})for(bool fixed:{false,true})for(bool symmetric:{false,true}) {
        auto part=document::PartDocument::create_default();auto source=sketcher::Sketch::create_default();
        const auto curve=ellipse?source.add_ellipse(0,0,5,0,0,3):source.add_circle(0,0,5);
        const auto center=ellipse?source.ellipses.front().center_point_id:source.circles.front().center_point_id;
        source.find_point(center)->fixed=fixed;
        auto driving=ellipse?source.create_ellipse_radius_dimension(curve,!alternative):
            alternative?source.create_circle_diameter_dimension(curve):source.create_circle_radius_dimension(curve);
        driving.locked=locked;source.apply_dimension(driving);
        auto feature=document::PartDocument::create_feature_container(source.id);feature.feature.sides[0].length=10;
        feature.feature.symmetric=symmetric;source.owner_container_id=feature.id;
        auto target=sketcher::Sketch::create_default();target.plane=sketcher::SketchPlane::XZ;target.plane_auto=false;
        auto owner=document::PartDocument::create_sketch_container();owner.placement.y=1.37;target.owner_container_id=owner.id;
        part.history={feature,owner};part.sketches={source,target};document::BodyHistoryGraph graph;
        static_cast<void>(graph.create_body("Conic root domains"));graph.insert({document::PartHistoryKind::Feature,feature.id});
        graph.insert({document::PartHistoryKind::Feature,owner.id});part.set_body_history(graph);part.resolve_constructions();
        auto bodies=engine.evaluate_history(part.kernel_operations());kernel::FaceReference face;
        for(const auto& candidate:bodies.back().mesh.triangle_references)if(candidate.owner_id==feature.id) {
            const bool planar=candidate.surface&&candidate.surface->kind==kernel::SurfaceGeometry::Kind::Plane;
            if(cap?(planar&&std::abs(candidate.surface->axis.z)>.9):!planar){face=candidate;break;}
        }
        check(face.valid(),"Conic endpoint source face is missing");workspace::Workspace live;live.add_part(part,bodies);
        auto pending=part.sketches.back();const auto branches=workspace::prepare_sketch_intersection_branches(live,engine,part.document_id,pending,face,face);
        check(branches.size()==(cap?1u:2u),"Conic root domains lost a chord or an axial branch");
        for(const auto& branch:branches) {
            check(branch.intersection->branch_key.find(":domain:conic-plane:")!=std::string::npos,"Ambiguous conic endpoints have no analytic root domain");
            pending.add_external_reference(branch);static_cast<void>(pending.add_external_profile_geometry(branch.id));
        }
        const auto lines=pending.segments;
        for(const auto& segment:lines)for(bool reverse:{false,true})
        for(const auto kind:{Kind::Distance,Kind::DistanceX,Kind::DistanceY,Kind::Angle}) {
            auto dimension=kind==Kind::Angle?pending.create_segment_dimension(segment.id,kind):pending.create_point_dimension(
                reverse?segment.second_point_id:segment.first_point_id,reverse?segment.first_point_id:segment.second_point_id,kind);
            dimension.driving=false;dimension.locked=false;pending.apply_dimension(dimension);
        }
        part.sketches.back()=pending;document::DocumentSession history(part,bodies);
        for(unsigned step=0;step<3;++step) {
            const auto before=part.serialized();auto& source=part.sketches.front();
            const double radius=ellipse&&alternative?std::array{2.5,2.,3.}[step]:std::array{6.,4.,5.}[step];
            check(source.set_dimension_value(driving.id,radius*(!ellipse&&alternative?2:1)),"Conic source dimension edit failed");
            const auto p=*source.find_point(center);const auto stable=source.serialized();
            const bool moved=source.move_point(center,p.x+.2,p.y+.1);
            check(fixed?(!moved&&source.serialized()==stable):moved,"Conic source center violated its applicable free/fixed state");
            if(!fixed)check(std::hypot(source.find_point(center)->x-p.x-.2,source.find_point(center)->y-p.y-.1)<1e-7,"Free conic center reported a drag without moving");
            const auto source_before=source.serialized();bodies=engine.evaluate_history(part.kernel_operations());
            static_cast<void>(workspace::refresh_sketch_external_references(part,bodies,&engine));const auto& actual=part.sketches.back();
            for(const auto& branch:branches)check(!reference(actual,branch.id).broken&&reference(actual,branch.id).intersection==branch.intersection,
                "Conic edit rebound an endpoint or branch domain");
            const auto& center_point=*source.find_point(center);
            const double a=ellipse?source.ellipses.front().major_radius:source.circles.front().radius;
            const double b=ellipse?source.ellipses.front().minor_radius:source.circles.front().radius;
            for(const auto& point:actual.points) {
                const auto world=actual.world_point(point.x,point.y);const double x=(world.x-center_point.x)/a,y=(world.y-center_point.y)/b;
                check(std::abs(x*x+y*y-1)<1e-7&&std::abs(world.y-1.37)<1e-7,"Conic endpoint escaped its independent curve/plane equations");
                auto dragged=actual;const auto unchanged=dragged.serialized();
                check(!dragged.move_point(point.id,point.x+2,point.y+3)&&dragged.serialized()==unchanged,"Associated conic endpoint escaped dragging");
            }
            for(const auto& dimension:actual.dimensions) {
                const auto* p=actual.find_point(dimension.first_point_id);const auto* q=actual.find_point(dimension.second_point_id);
                if(!p||!q) {
                    const auto segment=std::ranges::find(actual.segments,dimension.geometry_id,&sketcher::SketchSegment::id);
                    check(segment!=actual.segments.end(),"Section measurement has no native line");
                    p=actual.find_point(segment->first_point_id);q=actual.find_point(segment->second_point_id);
                }
                check(p&&q,"Section measurement has no native endpoints");
                const double dx=q->x-p->x,dy=q->y-p->y;
                const double expected=dimension.kind==Kind::DistanceX?dx:dimension.kind==Kind::DistanceY?dy:
                    dimension.kind==Kind::Angle?std::atan2(dy,dx)*180/std::acos(-1.):std::hypot(dx,dy);
                check(std::abs(dimension.kind==Kind::Angle?std::remainder(dimension.value-expected,360.):dimension.value-expected)<1e-7,
                    "Conic section reference dimension became stale after a source edit");
                auto edited=actual;const auto unchanged=edited.serialized();static_cast<void>(edited.set_dimension_value(dimension.id,123));
                check(edited.serialized()==unchanged,"Reference measurement drove a conic section");
            }
            check(source.serialized()==source_before,"Intersection refresh changed its read-only conic source");
            const auto after=part.serialized();history.commit(part,bodies);
            check(history.undo()&&history.document().serialized()==before&&history.redo()&&history.document().serialized()==after,
                "Conic root-domain Undo/Redo changed the native definition");++cases;
        }
        const auto file=directory/(std::to_string(++fixture_number)+".prtz");part.save(file,bodies);
        std::vector<kernel::BodyResult> cache;const auto reopened=document::PartDocument::load(file,&cache);kernel::OcctKernel cold;
        const auto restored=workspace::calculate_part_sketch_intersections(cold,document::DocumentSession(reopened,cache),reopened.sketches.back());
        check(!restored.empty()&&std::ranges::none_of(restored,[](const auto& reference){return reference.broken;}),"Cold reopening lost a conic endpoint domain");
    }
    std::filesystem::remove_all(directory);
}
void verify_conic_domain_disappearance(kernel::OcctKernel& engine,unsigned& cases) {
    auto part=document::PartDocument::create_default();auto source=sketcher::Sketch::create_default();
    const auto arc=source.add_arc(0,0,5,0,2.5,-std::sqrt(18.75));const auto native=source.arcs.front();
    source.find_point(native.center_point_id)->fixed=true;source.find_point(native.start_point_id)->fixed=true;
    auto feature=document::PartDocument::create_feature_container(source.id);feature.feature.result_type=document::ProfileResultType::Surface;
    feature.feature.sides[0].length=10;source.owner_container_id=feature.id;
    auto target=sketcher::Sketch::create_default();target.plane=sketcher::SketchPlane::XZ;target.plane_auto=false;
    auto owner=document::PartDocument::create_sketch_container();owner.placement.y=1.37;target.owner_container_id=owner.id;
    part.history={feature,owner};part.sketches={source,target};document::BodyHistoryGraph graph;
    static_cast<void>(graph.create_body("Partial conic domains"));graph.insert({document::PartHistoryKind::Feature,feature.id});
    graph.insert({document::PartHistoryKind::Feature,owner.id});part.set_body_history(graph);part.resolve_constructions();
    auto bodies=engine.evaluate_history(part.kernel_operations());kernel::FaceReference face;
    for(const auto& candidate:bodies.back().mesh.triangle_references)if(candidate.surface&&candidate.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder){face=candidate;break;}
    check(face.valid(),"Partial conic source is missing");workspace::Workspace live;live.add_part(part,bodies);
    auto pending=part.sketches.back();const auto branches=workspace::prepare_sketch_intersection_branches(live,engine,part.document_id,pending,face,face);
    check(branches.size()==2,"Partial source did not offer both analytic roots");
    for(const auto& branch:branches){pending.add_external_reference(branch);static_cast<void>(pending.add_external_profile_geometry(branch.id));}
    part.sketches.back()=pending;
    for(bool trimmed:{true,false,true,false}) {
        auto& source=part.sketches.front();check(source.move_point(native.end_point_id,trimmed?0.:2.5,trimmed?5.:-std::sqrt(18.75)),"Partial source endpoint did not move");
        check(std::abs(source.arcs.front().radius-5)<1e-7,"Partial source endpoint drag changed its fixed radial support");
        bodies=engine.evaluate_history(part.kernel_operations());static_cast<void>(workspace::refresh_sketch_external_references(part,bodies,&engine));
        unsigned intact=0,broken=0;const auto& actual=part.sketches.back();
        for(const auto& branch:branches) {
            const auto& value=reference(actual,branch.id);check(value.intersection==branch.intersection,"A disappearing root rebound its persisted branch");
            if(value.broken){++broken;check(value.cached_points==branch.cached_points,"Missing root lost its frozen geometry");}
            else ++intact;
        }
        check(intact==(trimmed?1u:2u)&&broken==(trimmed?1u:0u),"Trimming one root broke the other or failed to restore its original identity");++cases;
    }
    static_cast<void>(arc);
}
void verify_spline_edits(kernel::OcctKernel& kernel,unsigned& cases) {
    for(bool placed:{false,true})for(bool flipped:{false,true}) {
    auto f=fixture(kernel,false,placed,false);
    if(flipped){f.part.history.back().placement.rotation_x=f.part.history.back().placement.absolute_rotation_x=180;}auto source=sketcher::Sketch::create_default();source.id=f.part.sketches.front().id;source.owner_container_id=f.part.history.front().id;
    const auto source_spline=source.add_bspline({{-5,0},{-3,5},{3,5},{5,0}});static_cast<void>(source.add_segment(5,0,-5,0));f.part.sketches.front()=source;f.part.resolve_constructions();
    f.bodies=kernel.evaluate_history(f.part.kernel_operations());check(f.bodies.back().calculation_errors.empty(),"Spline section source failed calculation");
    const auto face=std::ranges::find_if(f.bodies.back().mesh.triangle_references,[&](const auto& face){return face.owner_id==f.part.history.front().id&&(!face.surface||face.surface->kind!=kernel::SurfaceGeometry::Kind::Plane);});
    check(face!=f.bodies.back().mesh.triangle_references.end(),"Spline section side missing");const auto support=*face;
    workspace::Workspace live;live.add_part(f.part,f.bodies);auto pending=f.part.sketches.back();
    const auto branches=workspace::prepare_sketch_intersection_branches(live,kernel,f.part.document_id,pending,support,support);check(branches.size()==1,"Spline section branch missing");
    pending.add_external_reference(branches.front());const auto spline=pending.add_external_profile_geometry(branches.front().id);
    check(pending.bsplines.size()==1&&pending.circles.empty(),"Genuine nonconic section lost exact native spline");f.part.sketches.back()=pending;
    check(pending.bsplines.front().control_point_ids.size()==4&&pending.bsplines.front().degree==3,"Full-domain spline section reapproximated its source pole basis");
    const auto point_ids=pending.bsplines.front().control_point_ids;
    const auto first=source.bsplines.front().control_point_ids[1],second=source.bsplines.front().control_point_ids[2];
    for(double y:{3.,6.,4.}) {
        check(f.part.sketches.front().move_point(first,-3,y)&&f.part.sketches.front().move_point(second,3,y+1),"Free source spline poles did not move");
        const auto source_before=f.part.sketches.front().serialized();f.bodies=kernel.evaluate_history(f.part.kernel_operations());
        static_cast<void>(workspace::refresh_sketch_external_references(f.part,f.bodies,&kernel));const auto& actual=f.part.sketches.back();const auto& reference=actual.external_references.front();
        check(!reference.broken&&actual.bsplines.front().id==spline&&reference.exact_spline,"Spline section refresh lost source or native identity");
        const auto native=actual.supporting_curve(spline);
        check(actual.bsplines.front().control_point_ids==point_ids,"Spline pole identities changed after deformation");
        const auto original=f.part.sketches.front().supporting_curve(source_spline);
        const auto origin=kernel::bspline_value(original,0);const auto start=kernel::bspline_value(native,0);const auto world_start=actual.world_point(start.x,start.y);
        const bool reversed=std::hypot(origin.x-world_start.x,origin.y-world_start.y)>1e-7;
        for(unsigned i=0;i<=100;++i) {
            const double t=i/100.;const auto p=kernel::bspline_value(native,t),q=kernel::bspline_value(original,reversed?1-t:t);
            const auto world=actual.world_point(p.x,p.y);
            check(std::hypot(world.x-q.x,world.y-q.y,world.z-5)<1e-7,"Spline section escaped independent source/plane equations");
        }
        const auto topology=sketcher::sketch_trim_topology(actual,false);
        check(std::ranges::count(topology,spline,&sketcher::SketchTrimPiece::geometry_id)==1,"Coincident spline display chords split Trim topology");
        auto crossing=actual;static_cast<void>(crossing.add_segment(0,-10,0,10));
        const auto cut=sketcher::sketch_trim_topology(crossing,false);
        check(std::ranges::count(cut,spline,&sketcher::SketchTrimPiece::geometry_id)==2,"Exact spline coincidence suppression hid a real Trim crossing");
        for(unsigned i=0;i<=100;++i){const auto a=kernel::bspline_value(native,i/100.),b=kernel::bspline_value(*reference.exact_spline,i/100.);check(std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)<1e-7,"Associated spline differs from its exact section");}
        for(const auto& id:actual.bsplines.front().control_point_ids) {const auto* p=actual.find_point(id);auto bound=actual;const auto before=bound.serialized();check(!bound.move_point(id,p->x+3,p->y+2)&&bound.serialized()==before,"Bound section pole escaped dragging");}
        const auto mesh=actual.viewer_mesh();check(std::ranges::none_of(mesh.points,[&](const auto& p){return p.reference.semantic_key=="sketch_intersection:"+reference.id+"||"+spline||p.reference.semantic_key=="sketch_intersection:"+spline+"||"+reference.id;}),"Coincident exact spline section publishes display-chord intersections");
        check(f.part.sketches.front().serialized()==source_before,"Spline section refresh modified source");++cases;
    }
    const auto stable=f.part.sketches.back().serialized();document::DocumentSession session(f.part,f.bodies);auto edited=f.part;
    static_cast<void>(edited.sketches.back().add_point(20,30));session.commit(edited,f.bodies);
    check(session.undo()&&session.document().sketches.back().serialized()==stable&&session.redo()&&session.undo(),"Spline section Undo/Redo lost its native poles");
    const auto dir=std::filesystem::temp_directory_path()/("zima-spline-section-"+kernel::make_stable_id());std::filesystem::create_directory(dir);
    const auto file=dir/"spline.prtz";session.document().save(file,session.calculated_boundaries());std::vector<kernel::BodyResult> cold;
    const auto reopened=document::PartDocument::load(file,&cold);kernel::OcctKernel cold_kernel;
    const auto fresh=workspace::calculate_part_sketch_intersections(cold_kernel,document::DocumentSession(reopened,cold),reopened.sketches.back());
    check(reopened.sketches.back().serialized()==stable&&!fresh.empty()&&!fresh.front().broken&&fresh.front().exact_spline&&fresh.front().exact_spline->poles.size()==4,"Cold spline section lost exact native basis/bindings");
    std::filesystem::remove_all(dir);++cases;
    }
}

}
int main(){try{kernel::FaceMeasurementScope measurements(false);kernel::OcctKernel kernel;if(const auto* file=std::getenv("ZIMA_VERIFY_SAVED_REFERENCE_CASE")){verify_saved_reference_case(kernel,file);return 0;}unsigned cases{};verify_pending_profile_intersection(kernel,cases);verify(kernel,cases);verify_assembly(kernel,cases);verify_multiple_branches(kernel,cases);verify_qualified_copy(kernel,cases);verify_circular_edits(kernel,cases);verify_part_context(kernel,cases);verify_standalone_assembly(kernel,cases);verify_general_surface_frames(kernel,cases);verify_arc_edits(kernel,cases);verify_dependent_geometry(kernel,cases);verify_trimmed_fragment(kernel,cases);verify_line_dimensions(kernel,cases);verify_conic_endpoint_domains(kernel,cases);verify_conic_domain_disappearance(kernel,cases);verify_spline_edits(kernel,cases);std::cout<<cases<<" Sketch intersection reference cases passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
