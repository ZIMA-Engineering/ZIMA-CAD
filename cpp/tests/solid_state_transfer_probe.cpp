// Feasibility probe only. This does not implement automatic solid attachment
// transfer or modify the document's reference/placement calculation.
#include <zima/document/part_document.hpp>
#include <zima/document/placement_json.hpp>
#include <zima/document/history_reference_view.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>
#include <zima/kernel/sheet_material.hpp>
#include <zima/kernel/solid_straightening.hpp>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <BRep_Tool.hxx>
#include <gp_Ax1.hxx>
#include <cmath>
#include <iostream>
#include <numbers>
#include <sstream>

using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){check(std::isfinite(actual)&&std::abs(actual-expected)<1e-7,"Rigid transfer exceeds probe tolerance");}
TopoDS_Shape shape(const kernel::BodyResult& result) {
    TopoDS_Shape s;BRep_Builder builder;std::istringstream stream(result.kernel_shape);
    BRepTools::Read(s,stream,builder);check(!s.IsNull(),"Probe shape is missing");return s;
}
double volume(const TopoDS_Shape& s) {
    GProp_GProps properties;BRepGProp::VolumeProperties(s,properties);return properties.Mass();
}
void rigid_transfer() {
    kernel::OcctKernel kernel;
    kernel::RevolutionRequest source;
    source.outer_profile=kernel::ExtrusionRequest::PolygonProfile{{{100,0,0},{120,0,0},{120,20,0},{100,20,0}}};
    source.profile_region_id="section";source.outer_boundary_id="outline";
    source.outer_edge_source_ids={"a","b","c","d"};source.outer_vertex_source_ids={"p","q","r","s"};
    source.axis_direction={0,1,0};source.angle_degrees=90;
    int cases=0;
    for(double coefficient:{.8,1.,1.2})for(bool reverse:{false,true})for(double offset:{-2.,-0.,0.,2.}) {
        const auto plan=kernel.prepare_straightening(source,coefficient);
        const double length=110*std::numbers::pi/2*coefficient;
        near(plan.straight_end_centroid.x,110);near(plan.straight_end_centroid.y,10);
        near(plan.straight_end_centroid.z,-length);
        near(plan.source_end_centroid.x,0);near(plan.source_end_centroid.y,10);near(plan.source_end_centroid.z,-110);
        kernel::SheetMaterialDefinition before,after;
        before.origin=plan.straight_end_centroid;before.along={1,0,0};before.tangent={0,0,-1};before.radial={0,1,0};
        after.origin=plan.source_end_centroid;after.along={0,0,-1};after.tangent={-1,0,0};after.radial={0,1,0};
        // Reuse the sheet's rigid frame map with explicit known attachment
        // frames. Inferring those frames/ownership is deliberately not claimed.
        const kernel::sheet_material::Transition movement{before,after};
        const auto x=movement.rigid_vector({1,0,0}),y=movement.rigid_vector({0,1,0}),z=movement.rigid_vector({0,0,1}),p=movement.map({});
        gp_Trsf transform;transform.SetValues(x.x,y.x,z.x,p.x,x.y,y.y,z.y,p.y,x.z,y.z,z.z,p.z);
        kernel::ExtrusionRequest child;
        child.outer_profile=kernel::ExtrusionRequest::PolygonProfile{{{109,8.5,-length+offset},{111,8.5,-length+offset},
            {111,11.5,-length+offset},{109,11.5,-length+offset}}};
        child.direction={0,0,reverse?4.:-4.};child.profile_region_id="child-section";child.outer_boundary_id="child-outline";
        child.outer_edge_source_ids={"ca","cb","cc","cd"};child.outer_vertex_source_ids={"cp","cq","cr","cs"};
        const auto original=shape(kernel.evaluate_history({{"child",child}}).back());
        const auto moved=BRepBuilderAPI_Transform(original,transform,true).Shape();
        check(BRepCheck_Analyzer(moved).IsValid(),"Rigid transfer invalidated the child B-Rep");
        near(volume(original),24);near(volume(moved),24);
        GProp_GProps properties;BRepGProp::VolumeProperties(moved,properties);
        // Independent quarter-turn about global Y plus translation +length X.
        near(properties.CentreOfMass().X(),offset+(reverse?2.:-2.));
        near(properties.CentreOfMass().Y(),10);near(properties.CentreOfMass().Z(),-110);
        for(TopExp_Explorer vertex(original,TopAbs_VERTEX);vertex.More();vertex.Next()) {
            const auto from=BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current()));const auto to=from.Transformed(transform);
            near(to.X(),from.Z()+length);near(to.Y(),from.Y());near(to.Z(),-from.X());
        }
        const auto returned=BRepBuilderAPI_Transform(moved,transform.Inverted(),true).Shape();near(volume(returned),24);
        for(TopExp_Explorer vertex(original,TopAbs_VERTEX);vertex.More();vertex.Next()) {
            const auto from=BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current()));double distance=INFINITY;
            for(TopExp_Explorer candidate(returned,TopAbs_VERTEX);candidate.More();candidate.Next())
                distance=std::min(distance,from.Distance(BRep_Tool::Pnt(TopoDS::Vertex(candidate.Current()))));
            near(distance,0);
        }
        ++cases;
    }
    std::cout<<cases<<" explicit single-frame transfers preserve volume, section, orientation and inverse coordinates\n";
}
void kernel_input_ambiguity() {
    auto doc=document::PartDocument::create_default();
    if(doc.body_history.bodies().empty())static_cast<void>(doc.body_history.create_body("Probe"));
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(0,0,2,3));
    auto child=document::PartDocument::create_extrusion_container(sketch.id);child.extrusion.length_forward=4;
    sketch.owner_container_id=child.id;child.placement.x=110;child.placement.y=10;child.placement.z=-150;
    doc.sketches.push_back(sketch);doc.history.push_back(child);doc.insert_history_entry(document::PartHistoryKind::Feature,child.id);
    const auto fixed=doc.kernel_operations(false,false);const auto baseline=kernel::history_fingerprint(fixed,fixed.size());
    document::ConstructionReference point;point.owner_id="source";point.semantic_key="end:p";
    auto point_only=doc;point_only.find_container(child.id)->placement.references={point};
    const auto attached=point_only.kernel_operations(false,false);
    check(kernel::history_fingerprint(attached,attached.size())==baseline,
        "Kernel now receives distinct point-attachment input; reconsider the probe's conclusion");
    auto oriented=point_only;
    auto front=point;front.semantic_key="end:from:section";front.orientation_role="front";
    front.orientation_drives_rotation=true;front.orientation_only=true;
    oriented.find_container(child.id)->placement.references.push_back(front);
    const auto following=oriented.kernel_operations(false,false);
    check(kernel::history_fingerprint(following,following.size())==baseline,
        "Kernel now receives distinct orientation attachment input; reconsider the probe's conclusion");
    // An unchanged shared solver demonstrates why point-only attachment must
    // not inherit the parent's quarter-turn in the way a rigid child frame does.
    kernel::ViewerReferenceGeometry geometry;kernel::ViewerPoint end;
    end.reference={"source","end:p"};end.position={0,10,-110};geometry.points.push_back(end);
    auto placement=point_only.find_container(child.id)->placement;
    check(document::resolve_placement(placement,geometry),"Independent point attachment did not resolve");
    near(placement.x,0);near(placement.y,10);near(placement.z,-110);
    near(placement.rotation_x,0);near(placement.rotation_y,0);near(placement.rotation_z,0);
    geometry.vertices={{0,0,-100},{0,20,-100},{0,0,-120}};geometry.triangles={0,1,2};
    auto surface=std::make_shared<kernel::SurfaceGeometry>();surface->kind=kernel::SurfaceGeometry::Kind::Plane;
    surface->origin={0,10,-110};surface->axis={-1,0,0};surface->radial={0,1,0};
    geometry.triangle_references={{"source","end:from:section",{},surface}};
    for(bool flip:{false,true}) {
        auto oriented_placement=oriented.find_container(child.id)->placement;
        oriented_placement.references.back().flip=flip;
        const auto stored_references=oriented_placement.references;
        check(document::resolve_placement(oriented_placement,geometry),"Oriented attachment did not resolve");
        near(oriented_placement.x,0);near(oriented_placement.y,10);near(oriented_placement.z,-110);
        check(std::abs(oriented_placement.rotation_x)+std::abs(oriented_placement.rotation_y)+
            std::abs(oriented_placement.rotation_z)>1,"Orientation reference failed to follow the target cap");
        check(oriented_placement.references==stored_references,"Probe solver changed authored references");
    }
    check(point_only.find_container(child.id)->placement.references.size()==1&&
        doc.find_container(child.id)->placement.references.empty(),"Probe changed source definitions");
    std::cout<<"Fixed, point-only and oriented documents compile to the same current kernel geometry input\n"
        <<"Point-only attachment retains absolute orientation; unconditional rigid rotation would change its contract\n";
}
// Test-only proposed eligibility rule. No product command consumes it.
bool one_face_attachment(const document::Placement& p,const kernel::FaceReference& face) {
    if(!face.surface||face.surface->kind!=kernel::SurfaceGeometry::Kind::Plane||!p.value_locks.empty())return false;
    bool position=false,orientation=false;
    for(const auto& ref:p.references) {
        if(ref.owner_id!=face.owner_id||ref.semantic_key!=face.semantic_key||ref.instance_path!=face.instance_path||
            ref.use_axis||ref.solution_branch!=0)return false;
        if(!ref.orientation_only)position=true;
        if(ref.orientation_drives_rotation&&ref.orientation_role=="top")orientation=true;
        else if(ref.orientation_drives_rotation)return false;
    }
    return position&&orientation;
}
void face_attachment_rule() {
    kernel::OcctKernel kernel;int cases=0,tilted_mismatches=0;double tilted_max_error=0;
    for(bool tilted:{false,true})for(double angle:{30.,90.,135.})for(double coefficient:{.8,1.,1.2}) {
        kernel::RevolutionRequest source;
        source.outer_profile=kernel::ExtrusionRequest::PolygonProfile{{{100,0,0},{120,0,0},{120,20,0},{100,20,0}}};
        source.profile_region_id="section";source.outer_boundary_id="outline";
        source.outer_edge_source_ids={"a","b","c","d"};source.outer_vertex_source_ids={"p","q","r","s"};
        source.axis_direction={0,1,0};source.angle_degrees=angle;
        gp_Trsf world;
        if(tilted) {
            gp_Trsf x,z;x.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,0,0)),23*std::numbers::pi/180);
            z.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),37*std::numbers::pi/180);world=z*x;
            for(auto& point:std::get<kernel::ExtrusionRequest::PolygonProfile>(source.outer_profile).vertices) {
                const auto moved=gp_Pnt(point.x,point.y,point.z).Transformed(world);point={moved.X(),moved.Y(),moved.Z()};
            }
            const auto axis=gp_Vec(0,1,0).Transformed(world),normal=gp_Vec(0,0,1).Transformed(world);
            source.axis_direction={axis.X(),axis.Y(),axis.Z()};source.profile_normal={normal.X(),normal.Y(),normal.Z()};
        }
        const auto plan=kernel.prepare_straightening(source,coefficient);
        const auto states=kernel.evaluate_history({{"source",source},{"straight",kernel::SolidStateRequest{false,true,coefficient,{}}}});
        const auto& before=states.back().mesh.original_references;
        const auto face=std::ranges::find_if(before.triangle_references,[](const auto& ref) {
            const auto parent=kernel::solid_state_parent(ref.semantic_key);
            return ref.owner_id=="straight"&&parent&&parent->first=="source"&&parent->second.starts_with("end:from:");
        });
        check(face!=before.triangle_references.end()&&face->surface,"Calculated end face is missing");
        auto after=before;
        document::history_reference_detail::retain(after,{{"source",""}},true);
        // Evaluation fixture only: make the unchanged stored selection resolve
        // against the corresponding formed cap, without editing any document.
        const auto parent=kernel::solid_state_parent(face->semantic_key);
        for(auto& ref:after.triangle_references)if(ref.owner_id==parent->first&&ref.semantic_key==parent->second) {
            ref.owner_id=face->owner_id;ref.semantic_key=face->semantic_key;
        }
        gp_Trsf move;move.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,1,0)),angle*std::numbers::pi/180);
        move=world*move*world.Inverted();
        const auto flat=plan.straight_end_centroid,formed=plan.source_end_centroid;
        const auto rotated=gp_Pnt(flat.x,flat.y,flat.z).Transformed(move);
        move.SetTranslationPart(gp_Vec(rotated,gp_Pnt(formed.x,formed.y,formed.z)));
        for(bool flip:{false,true})for(double offset:{-2.,-0.,0.,2.})for(double roll:{0.,17.}) {
            auto doc=document::PartDocument::create_default();
            if(doc.body_history.bodies().empty())static_cast<void>(doc.body_history.create_body("Probe"));
            auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(0,0,2,3));
            auto feature=document::PartDocument::create_extrusion_container(sketch.id);feature.extrusion.length_forward=4;
            sketch.owner_container_id=feature.id;
            const auto in_plane=gp_Vec(2,3,0).Transformed(world);
            auto& p=feature.placement;p.x=flat.x+in_plane.X();p.y=flat.y+in_plane.Y();p.z=flat.z+in_plane.Z();
            p.absolute_rotation_z=roll;
            document::ConstructionReference ref;ref.owner_id=face->owner_id;ref.semantic_key=face->semantic_key;
            ref.offset=offset;ref.supports_offset=true;ref.flip=flip;
            // The contour is in local XY, so TOP (local Z) is its normal.
            auto front=ref;front.offset=0;front.supports_offset=false;front.orientation_role="top";
            front.orientation_drives_rotation=true;front.orientation_only=true;p.references={ref,front};
            check(one_face_attachment(p,*face),"Valid one-face attachment was rejected by proposed rule");
            auto mixed=p;mixed.references.back().owner_id="another-object";
            check(!one_face_attachment(mixed,*face),"Rule accepted an orientation on another object");
            mixed=p;mixed.references.back().instance_path="another-occurrence";
            check(!one_face_attachment(mixed,*face),"Rule merged two Assembly occurrences");
            mixed=p;mixed.references.pop_back();check(!one_face_attachment(mixed,*face),"Rule accepted position without orientation");
            mixed=p;mixed.references.erase(mixed.references.begin());check(!one_face_attachment(mixed,*face),"Rule accepted orientation without position");
            check(document::resolve_placement(p,before),"One-face initial placement did not resolve");
            const auto stored=p;
            doc.sketches.push_back(sketch);doc.history.push_back(feature);doc.insert_history_entry(document::PartHistoryKind::Feature,feature.id);
            doc.resolve_constructions(before);
            const auto original=shape(kernel.evaluate_history(doc.kernel_operations(false,false)).back());
            const auto carried=BRepBuilderAPI_Transform(original,move,true).Shape();
            check(BRepCheck_Analyzer(carried).IsValid(),"Local full-frame transfer invalidated the attached child");
            near(volume(carried),24);
            const auto back_shape=BRepBuilderAPI_Transform(carried,move.Inverted(),true).Shape();
            for(TopExp_Explorer vertex(original,TopAbs_VERTEX);vertex.More();vertex.Next()) {
                const auto from=BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current()));double distance=INFINITY;
                for(TopExp_Explorer candidate(back_shape,TopAbs_VERTEX);candidate.More();candidate.Next())
                    distance=std::min(distance,from.Distance(BRep_Tool::Pnt(TopoDS::Vertex(candidate.Current()))));
                near(distance,0);
            }
            auto target=p;
            const auto at=gp_Pnt(p.x,p.y,p.z).Transformed(move);target.x=at.X();target.y=at.Y();target.z=at.Z();
            check(document::resolve_placement(target,after),"One-face target placement did not resolve");
            near(target.x,at.X());near(target.y,at.Y());near(target.z,at.Z());
            const auto target_face=std::ranges::find_if(after.triangle_references,[&](const auto& candidate){return candidate==*face;});
            check(target_face!=after.triangle_references.end()&&target_face->surface,"Target plane packet missing");
            const auto& input_plane=*face->surface;const auto& target_plane=*target_face->surface;
            const gp_Vec n(input_plane.axis.x,input_plane.axis.y,input_plane.axis.z);
            const gp_Vec target_n(target_plane.axis.x,target_plane.axis.y,target_plane.axis.z);
            near(std::abs(n.Transformed(move).Normalized().Dot(target_n.Normalized())),1);
            const auto distance_to_plane=[](const gp_Pnt& point,const kernel::SurfaceGeometry& plane) {
                return std::abs(gp_Vec(gp_Pnt(plane.origin.x,plane.origin.y,plane.origin.z),point)
                    .Dot(gp_Vec(plane.axis.x,plane.axis.y,plane.axis.z).Normalized()));
            };
            near(distance_to_plane(gp_Pnt(stored.x,stored.y,stored.z),input_plane),std::abs(offset));
            near(distance_to_plane(at,target_plane),std::abs(offset));
            auto target_doc=doc;target_doc.find_container(feature.id)->placement=target;
            target_doc.resolve_constructions(after);
            const auto recalculated=shape(kernel.evaluate_history(target_doc.kernel_operations(false,false)).back());
            near(volume(original),24);near(volume(recalculated),24);
            double error=0;
            for(TopExp_Explorer vertex(original,TopAbs_VERTEX);vertex.More();vertex.Next()) {
                const auto expected=BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current())).Transformed(move);double distance=INFINITY;
                for(TopExp_Explorer candidate(recalculated,TopAbs_VERTEX);candidate.More();candidate.Next())
                    distance=std::min(distance,expected.Distance(BRep_Tool::Pnt(TopoDS::Vertex(candidate.Current()))));
                error=std::max(error,distance);
            }
            if(tilted&&error>1e-7) {++tilted_mismatches;tilted_max_error=std::max(tilted_max_error,error);}
            if(!tilted&&error>1e-7)throw std::runtime_error("One-face transfer differs from unchanged placement solver: angle="+
                std::to_string(angle)+", coefficient="+std::to_string(coefficient)+", flip="+std::to_string(flip)+
                ", roll="+std::to_string(roll)+", vertex error="+std::to_string(error)+
                ", before rotation="+std::to_string(stored.rotation_x)+","+std::to_string(stored.rotation_y)+","+std::to_string(stored.rotation_z)+
                ", after rotation="+std::to_string(target.rotation_x)+","+std::to_string(target.rotation_y)+","+std::to_string(target.rotation_z));
            const auto restored_at=gp_Pnt(target.x,target.y,target.z).Transformed(move.Inverted());
            auto returned=target;returned.x=restored_at.X();returned.y=restored_at.Y();returned.z=restored_at.Z();
            check(document::resolve_placement(returned,before),"One-face return placement did not resolve");
            near(returned.x,stored.x);near(returned.y,stored.y);near(returned.z,stored.z);
            const auto saved=nlohmann::json(stored).dump();
            const auto reopened=nlohmann::json::parse(saved).get<document::Placement>();
            check(reopened==stored&&target.references==stored.references&&returned.references==stored.references,
                "One-face transfer rewrote stored references or placement serialization");
            check(std::signbit(reopened.references.front().offset)==std::signbit(offset),"Native placement lost signed zero");
            check(doc.find_container(feature.id)->placement==stored,"Local experiment modified source placement");
            ++cases;
        }
    }
    check(tilted_mismatches>0,"Tilted one-face probe no longer exposes the in-plane orientation limitation; reassess conclusion");
    std::cout<<cases<<" local full-frame transfers preserve valid solids, 24 mm3 volume, plane offset, inverse vertices and authored reference serialization\n"
        <<"Axis-aligned cases agree with rigid frame transfer; "<<tilted_mismatches
        <<" tilted cases retain a different in-plane orientation under the unchanged solver; max vertex difference="<<tilted_max_error<<" mm\n";
}
}
int main()try {rigid_transfer();kernel_input_ambiguity();face_attachment_rule();return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
