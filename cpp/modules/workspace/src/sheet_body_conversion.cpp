#include <zima/workspace/sheet_exchange_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/flat.hpp>
#include <zima/kernel/sheet_material.hpp>
#include <numbers>

namespace zima::workspace {
namespace {
using namespace kernel::sheet_material;
using V=kernel::Vec3;
using Face=kernel::FaceReference;
constexpr double join_tolerance=.05;
std::string key(const Face& face){return face.owner_id+":"+face.semantic_key;}
V normal(const Face& face,V point) {
    const auto& s=*face.surface;auto n=s.axis;
    if(s.kind==kernel::SurfaceGeometry::Kind::Cylinder) {
        const auto delta=sub(point,s.origin);n=unit(sub(delta,mul(s.axis,dot(delta,s.axis))));
    }
    return mul(n,s.reversed?-1:1);
}
bool straight(const kernel::ViewerEdge& edge) {
    if(edge.points.size()<2)return false;
    const auto delta=sub(edge.points.back(),edge.points.front());const double length=std::sqrt(dot(delta,delta));if(length<.001)return false;
    for(auto p:edge.points){const auto off=cross(sub(p,edge.points.front()),delta);if(std::sqrt(dot(off,off))/length>1e-5)return false;}return true;
}
std::map<std::string,Face> faces(const kernel::ViewerMesh& mesh) {
    std::map<std::string,Face> result;for(const auto& face:mesh.triangle_references)result.emplace(key(face),face);
    for(const auto& face:mesh.original_references.triangle_references)if(result.contains(key(face)))result[key(face)]=face;
    return result;
}
struct Node {Face face;std::string parent;kernel::ViewerEdge entry;};
std::vector<Node> skin(const kernel::ViewerMesh& mesh,const Face& first) {
    const auto available=faces(mesh);std::vector<Node> result{{first,{},{}}};std::set<std::string> visited{key(first)};
    for(std::size_t i=0;i<result.size();++i) {
        const auto current=result[i].face;
        for(const auto& edge:mesh.edges) {
            if(edge.parameter_seam||!straight(edge)||edge.edge_treatment_side_references.size()!=2)continue;
            const auto& sides=edge.edge_treatment_side_references;
            const Face* next=sides[0]==current?&sides[1]:sides[1]==current?&sides[0]:nullptr;
            if(!next||visited.contains(key(*next)))continue;
            const auto it=available.find(key(*next));if(it==available.end()||!it->second.surface)continue;
            next=&it->second;
            const auto kind=next->surface->kind;if(kind!=kernel::SurfaceGeometry::Kind::Plane&&kind!=kernel::SurfaceGeometry::Kind::Cylinder)continue;
            const auto at=mul(add(edge.points.front(),edge.points.back()),.5);
            if(dot(normal(current,at),normal(*next,at))<1-1e-6)continue;
            visited.insert(key(*next));result.push_back({*next,key(current),edge});
        }
    }
    return result;
}
void numeric_frame(document::HistoryContainer& feature,sketcher::Sketch& sketch,V origin,V x,V y,V z) {
    const double ry=std::asin(std::clamp(-x.z,-1.,1.));
    const double rx=std::abs(std::cos(ry))>1e-9?std::atan2(y.z,z.z):0;
    const double rz=std::abs(std::cos(ry))>1e-9?std::atan2(x.y,x.x):std::atan2(-y.x,y.y);
    auto& p=feature.placement;p.x=origin.x;p.y=origin.y;p.z=origin.z;
    p.absolute_rotation_x=p.rotation_x=rx*180/std::numbers::pi;
    p.absolute_rotation_y=p.rotation_y=ry*180/std::numbers::pi;
    p.absolute_rotation_z=p.rotation_z=rz*180/std::numbers::pi;
    sketch.plane=sketcher::SketchPlane::XY;sketch.plane_auto=false;
    sketch.resolved_origin=origin;sketch.resolved_x_axis=x;sketch.resolved_y_axis=y;sketch.resolved_normal=z;
}
void outline(sketcher::Sketch& sketch,const kernel::ViewerMesh& mesh,const Face& face) {
    const auto surfaces=faces(mesh);
    for(const auto& edge:mesh.edges) {
        if(edge.parameter_seam||std::ranges::find(edge.edge_treatment_side_references,face)==edge.edge_treatment_side_references.end())continue;
        if(straight(edge)) {
            const auto a=sketch.local_point(edge.points.front()),b=sketch.local_point(edge.points.back());
            static_cast<void>(sketch.add_segment(a[0],a[1],b[0],b[1],1e-6,false));continue;
        }
        std::optional<std::array<double,2>> circle_center;double radius=0;
        for(const auto& side:edge.edge_treatment_side_references)if(side!=face) {
            const auto other=surfaces.find(key(side));
            if(other==surfaces.end()||!other->second.surface)continue;
            const auto& surface=*other->second.surface;
            if(surface.kind!=kernel::SurfaceGeometry::Kind::Cylinder||std::abs(dot(surface.axis,sketch.resolved_normal))<1-1e-7)continue;
            circle_center=sketch.local_point(surface.origin);radius=surface.radius;
        }
        if(circle_center&&edge.points.size()>2) {
            const auto center=*circle_center;bool circular=true;
            for(auto p:edge.points){const auto q=sketch.local_point(p);if(std::abs(std::hypot(q[0]-center[0],q[1]-center[1])-radius)>1e-5)circular=false;}
            if(circular) {
                const auto a=sketch.local_point(edge.points.front()),b=sketch.local_point(edge.points.back()),m=sketch.local_point(edge.points[edge.points.size()/2]);
                if(std::hypot(a[0]-b[0],a[1]-b[1])<1e-6)static_cast<void>(sketch.add_circle(center[0],center[1],radius));
                else {
                    const double start=std::atan2(a[1]-center[1],a[0]-center[0]);
                    const auto angle=[&](const auto& p){double d=std::atan2(p[1]-center[1],p[0]-center[0])-start;return d<0?d+2*std::numbers::pi:d;};
                    static_cast<void>(sketch.add_arc(center[0],center[1],a[0],a[1],b[0],b[1],false,1e-6,angle(m)>angle(b)));
                }
                continue;
            }
        }
        const auto original=std::ranges::find_if(mesh.original_references.edges,[&](const auto& e){return e.reference==edge.reference;});
        const auto* exact=edge.exact_spline?&*edge.exact_spline:original!=mesh.original_references.edges.end()&&original->exact_spline?&*original->exact_spline:nullptr;
        if(exact) {
            std::vector<std::array<double,2>> poles;for(auto p:exact->poles)poles.push_back(sketch.local_point(p));
            const auto id=sketch.add_bspline(poles,exact->degree,false,false,1e-6,false);
            auto& spline=*std::ranges::find(sketch.bsplines,id,&sketcher::SketchBSpline::id);spline.knots=exact->knots;spline.weights=exact->weights;
        }else for(std::size_t i=1;i<edge.points.size();++i){const auto a=sketch.local_point(edge.points[i-1]),b=sketch.local_point(edge.points[i]);static_cast<void>(sketch.add_segment(a[0],a[1],b[0],b[1],1e-6,false));}
    }
}
}
double suggest_sheet_thickness(const kernel::ViewerMesh& mesh,const Face& selected) {
    const auto available=faces(mesh);const auto it=available.find(key(selected));
    if(it==available.end())return 0;const auto& seed=it->second;
    if(!seed.surface||seed.surface->kind!=kernel::SurfaceGeometry::Kind::Plane)return 0;
    const auto n=normal(seed,seed.surface->origin);double nearest=INFINITY;
    for(const auto& [id,face]:available)if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&face!=seed) {
        if(dot(n,normal(face,face.surface->origin))>-.999999)continue;
        if(face.measured_area&&seed.measured_area&&std::abs(*face.measured_area-*seed.measured_area)>.01*(*seed.measured_area))continue;
        const double distance=-dot(sub(face.surface->origin,seed.surface->origin),n);
        if(distance>.001)nearest=std::min(nearest,distance);
    }
    return std::isfinite(nearest)?nearest:0;
}
SheetBodyConversion prepare_sheet_from_body(const document::PartDocument& source,
        const std::vector<kernel::BodyResult>& cache,const Face& selected,double thickness,const kernel::OcctKernel& kernel) {
    const auto* target=source.body_history.find(source.body_history.active_body_id());
    if(!target||!target->entries.empty()||target->derived_copy)throw std::invalid_argument("Sheet from Body requires an empty active Body.");
    if(!(thickness>=.001)||thickness>1000000||!std::isfinite(thickness))throw std::invalid_argument("Sheet thickness must be between 0.001 and 1000000 mm.");
    if(cache.empty())throw std::invalid_argument("Sheet from Body requires a calculated source body.");
    const auto source_owner=source.body_owner_for_object(selected.owner_id);
    if(!source_owner||source_owner->scope.id==target->scope.id)throw std::invalid_argument("Select a planar face of another Body.");
    const auto& order=source.body_history.order();
    if(std::ranges::find(order,source_owner->scope.id)>=std::ranges::find(order,target->scope.id))
        throw std::invalid_argument("Source Body must precede the active Body.");
    const auto input=cache.back().body_outputs.find(source_owner->scope.id);
    if(input==cache.back().body_outputs.end()||!input->second->calculation_errors.empty())throw std::invalid_argument("Sheet from Body requires a calculated source body.");
    // Body outputs already include their Body placement. Convert that world
    // packet once to the active Body's local coordinates through the public API.
    auto mesh=input->second->mesh;
    auto local_context=sketcher::Sketch::create_default();local_context.owner_container_id=target->scope.id;
    kernel::ViewerReferenceGeometry packet{std::move(mesh.vertices),std::move(mesh.triangles),std::move(mesh.triangle_references),std::move(mesh.edges),std::move(mesh.points),std::move(mesh.axes)};
    packet=source.sketch_reference_geometry_for(local_context,std::move(packet));
    mesh.vertices=std::move(packet.vertices);mesh.triangles=std::move(packet.triangles);mesh.triangle_references=std::move(packet.triangle_references);
    mesh.edges=std::move(packet.edges);mesh.points=std::move(packet.points);mesh.axes=std::move(packet.axes);
    mesh.original_references=source.sketch_reference_geometry_for(local_context,std::move(mesh.original_references));
    const auto available=faces(mesh);const auto root=available.find(key(selected));
    if(root==available.end()||!root->second.surface||root->second.surface->kind!=kernel::SurfaceGeometry::Kind::Plane)
        throw std::invalid_argument("Select a planar face of another Body.");
    const auto nodes=skin(mesh,root->second);SheetBodyConversion result;result.document=source;result.calculated=cache;
    // Author a new root frame and new profile geometry; no source reference is
    // persisted. Subsequent features will use only newly authored references.
    auto sketch=sketcher::Sketch::create_default();auto feature=document::PartDocument::create_sketch_container();
    feature.feature_kind=document::FeatureKind::Flat;feature.name="Tabule";feature.flat.sketch_id=sketch.id;
    feature.flat.thickness_override=true;feature.flat.thickness=thickness;feature.flat.direction=document::ExtrusionDirection::Reverse;
    sketch.owner_container_id=feature.id;sketch.name=feature.name;
    const auto n=normal(root->second,root->second.surface->origin);const auto x=root->second.surface->radial;
    numeric_frame(feature,sketch,root->second.surface->origin,x,cross(n,x),n);outline(sketch,mesh,root->second);
    result.document.insert_history_entry(document::PartHistoryKind::Feature,feature.id);result.created.push_back(feature.id);
    result.document.history.push_back(std::move(feature));result.document.sketches.push_back(std::move(sketch));
    result.document.resolve_constructions(construction_reference_source_geometry(cache));
    result.calculated=kernel.evaluate_history_incremental(result.document.kernel_operations(),cache);
    std::map<std::string,std::string> authored{{key(root->second),result.created.front()}};
    for(std::size_t i=1;i<nodes.size();++i) {
        const auto& node=nodes[i];const auto parent=authored.find(node.parent);
        if(parent==authored.end()){++result.skipped;continue;}
        const auto& surface=*node.face.surface;
        const bool bend=surface.kind==kernel::SurfaceGeometry::Kind::Cylinder;
        const kernel::ViewerEdge* exit_edge=nullptr;
        if(bend) {
            for(const auto& candidate:nodes)if(candidate.parent==key(node.face)&&candidate.face.surface->kind==kernel::SurfaceGeometry::Kind::Plane){exit_edge=&candidate.entry;break;}
            // A terminal bend has no following planar wall. Its opposite
            // straight generatrix still defines the authored end section.
            if(!exit_edge)for(const auto& edge:mesh.edges) {
                if(edge.parameter_seam||!straight(edge)||edge.reference==node.entry.reference||
                    std::ranges::find(edge.edge_treatment_side_references,node.face)==edge.edge_treatment_side_references.end())continue;
                if(dot(normal(node.face,node.entry.points.front()),normal(node.face,edge.points.front()))<1-1e-6){exit_edge=&edge;break;}
            }
        }
        if(bend&&!exit_edge){++result.skipped;continue;}
        const auto geometry=construction_reference_source_geometry(result.calculated);
        const auto local_geometry=source.sketch_reference_geometry_for(local_context,geometry);
        auto a=node.entry.points.front(),b=node.entry.points.back();
        if(bend&&surface.reversed){const auto shift=mul(normal(node.face,a),-thickness);a=add(a,shift);b=add(b,shift);}
        const auto distance=[](V p,V q){return std::hypot(p.x-q.x,p.y-q.y,p.z-q.z);};
        const kernel::ViewerEdge* attachment=nullptr;double best=INFINITY;
        for(const auto& edge:local_geometry.edges) {
            if(edge.reference.owner_id!=parent->second||!straight(edge))continue;
            try {if(bend)static_cast<void>(document::bend_sheet_references(edge));else static_cast<void>(document::flat_sheet_references(edge));}
            catch(const std::invalid_argument&){continue;}
            const auto mismatch=[&](V p,V q) {
                if(bend)return distance(p,q);
                const auto n=normal(node.face,p),delta=sub(q,p);const double offset=dot(delta,n);
                const auto in_plane=sub(delta,mul(n,offset));
                return std::max(std::sqrt(dot(in_plane,in_plane)),std::min(std::abs(offset),std::abs(std::abs(offset)-thickness)));
            };
            const double error=std::min(std::max(mismatch(a,edge.points.front()),mismatch(b,edge.points.back())),
                std::max(mismatch(b,edge.points.front()),mismatch(a,edge.points.back())));
            if(error<best){best=error;attachment=&edge;}
        }
        // Reuse the native parent's exact joining edge within the agreed
        // 0.05 mm sheet conversion tolerance; never widen general CAD snapping.
        if(!attachment||best>join_tolerance){++result.skipped;continue;}
        auto next=result.document;auto profile=sketcher::Sketch::create_default();
        auto added=document::PartDocument::create_sketch_container();profile.owner_container_id=added.id;
        added.feature_kind=bend?document::FeatureKind::Bend:document::FeatureKind::Flat;
        added.name=bend?"Profil plechu":"Tabule";profile.name=added.name;
        if(bend) {
            const auto incoming=normal(node.face,node.entry.points.front()),outgoing=normal(node.face,exit_edge->points.front());
            added.bend.angle_degrees=std::acos(std::clamp(dot(incoming,outgoing),-1.,1.))*180/std::numbers::pi;
            added.bend.radius=surface.radius-(surface.reversed?0:thickness);
            if(added.bend.radius<.001||added.bend.angle_degrees<.001){++result.skipped;continue;}
            added.bend.sketch_id=profile.id;added.bend.sheet_attachment=true;added.bend.thickness_override=true;added.bend.thickness=thickness;
            document::initialize_bend_start_profile(profile,distance(a,b));
            added.placement.references=document::bend_sheet_references(*attachment);
        }else {
            added.flat.sketch_id=profile.id;added.flat.sheet_attachment=true;added.flat.thickness=thickness;
            added.flat.direction=document::ExtrusionDirection::Reverse;
            added.placement.references=document::flat_sheet_references(*attachment);
        }
        next.insert_history_entry(document::PartHistoryKind::Feature,added.id);
        next.history.push_back(added);next.sketches.push_back(profile);
        next.resolve_constructions(geometry);
        auto& placed=next.sketches.back();
        if(!bend) {
            if(std::abs(dot(placed.resolved_normal,surface.axis))<1-1e-6){++result.skipped;continue;}
            outline(placed,mesh,node.face);
        }else {
            document::prepare_bend_sketches(next.history.back(),placed,document::sheet_metal_defaults(next));
            // The imported cylinder may widen or narrow between its two
            // generatrices. Preserve that end span with the native Bend's
            // editable endpoint extensions instead of assuming equal widths.
            const auto end=sketcher::Sketch::from_serialized(next.history.back().bend.auxiliary_sketches[1]);
            const auto first=end.local_point(exit_edge->points.front());
            const auto last=end.local_point(exit_edge->points.back());
            const auto* start_reference=end.find_point(end.id+":reference:first");
            const auto* end_reference=end.find_point(end.id+":reference:last");
            document::set_bend_profile_extensions(next.history.back(),
                start_reference->x-std::min(first[0],last[0]),
                std::max(first[0],last[0])-end_reference->x);
            document::prepare_bend_sketches(next.history.back(),placed,document::sheet_metal_defaults(next));
            const auto material=document::bend_material_definition(next.history.back(),placed,document::sheet_metal_defaults(next));
            bool supported=true;
            for(const auto& edge:mesh.edges) {
                if(std::ranges::find(edge.edge_treatment_side_references,node.face)==edge.edge_treatment_side_references.end())continue;
                for(auto point:edge.points) {
                    const auto station=coordinates(material,point);
                    const bool near_start_wrap=2*std::numbers::pi*material.neutral_radius-station.length<=join_tolerance;
                    if(std::abs(station.depth+(surface.reversed?thickness:0))>join_tolerance||station.length<-join_tolerance||
                        (station.length>material.angle*material.neutral_radius+join_tolerance&&!near_start_wrap)){supported=false;break;}
                }
                if(!supported)break;
            }
            // Do not silently replace a long or oppositely directed cylinder
            // by the shorter circular sector described by its two end normals.
            if(!supported){++result.skipped;continue;}
        }
        PartCalculationPolicy policy;policy.reject_errors=true;
        auto calculated=calculate_part_with_resolved_references(kernel,next,&result.calculated,policy);
        result.document=std::move(next);result.calculated=std::move(calculated);
        result.created.push_back(added.id);authored.emplace(key(node.face),added.id);
    }
    return result;
}
}
