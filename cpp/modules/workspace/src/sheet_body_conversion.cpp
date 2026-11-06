#include <zima/workspace/sheet_exchange_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/measurement_operations.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/flat.hpp>
#include <zima/kernel/sheet_material.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

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
double planar_pair_distance(const std::map<std::string,Face>& available,const Face& seed) {
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
struct Node {Face face;std::string parent;kernel::ViewerEdge entry;};
std::optional<V> sampled_normal(const kernel::ViewerMesh& mesh,const Face& face,V at) {
    double best=INFINITY;std::optional<V> result;
    for(std::size_t i=0;i<mesh.triangle_references.size();++i) {
        if(key(mesh.triangle_references[i])!=key(face)||3*i+2>=mesh.triangles.size())continue;
        const auto a=mesh.vertices.at(mesh.triangles[3*i]),b=mesh.vertices.at(mesh.triangles[3*i+1]),c=mesh.vertices.at(mesh.triangles[3*i+2]);
        const auto center=mul(add(add(a,b),c),1./3),delta=sub(center,at);
        const double distance=dot(delta,delta);const auto n=cross(sub(b,a),sub(c,a));
        if(distance<best&&dot(n,n)>1e-18){best=distance;result=unit(n);}
    }
    return result;
}
bool has_sheet_bend_exit(const kernel::ViewerMesh& mesh,const std::map<std::string,Face>& available,
        const Face& incoming,const Face& cylinder) {
    for(const auto& edge:mesh.edges) {
        if(edge.parameter_seam||edge.points.size()<2||edge.edge_treatment_side_references.size()!=2)continue;
        const auto& sides=edge.edge_treatment_side_references;
        const Face* next=sides[0]==cylinder?&sides[1]:sides[1]==cylinder?&sides[0]:nullptr;
        if(!next||*next==incoming)continue;
        const auto found=available.find(key(*next));if(found==available.end())continue;
        next=&found->second;
        if(!next->surface||next->surface->kind!=kernel::SurfaceGeometry::Kind::Plane||!straight(edge))continue;
        const auto at=edge.points[edge.points.size()/2];
        if(dot(normal(cylinder,at),normal(*next,at))<1-1e-6)continue;
        return true;
    }
    return false;
}
std::vector<Node> skin(const kernel::ViewerMesh& mesh,const Face& first) {
    const auto available=faces(mesh);std::vector<Node> result{{first,{},{}}};std::set<std::string> visited{key(first)};
    for(std::size_t i=0;i<result.size();++i) {
        const auto current=result[i].face;
        for(const auto& edge:mesh.edges) {
            if(edge.parameter_seam||edge.points.size()<2||edge.edge_treatment_side_references.size()!=2)continue;
            const auto& sides=edge.edge_treatment_side_references;
            const Face* next=sides[0]==current?&sides[1]:sides[1]==current?&sides[0]:nullptr;
            if(!next||visited.contains(key(*next)))continue;
            const auto it=available.find(key(*next));if(it==available.end())continue;
            next=&it->second;
            const auto at=edge.points[edge.points.size()/2];
            if(!next->surface||next->surface->kind==kernel::SurfaceGeometry::Kind::Cone) {
                const auto n=sampled_normal(mesh,*next,at);
                // Unknown surfaces have only the calculated mesh normal here.
                // Allow its display tessellation deviation, not a planar join.
                if(n&&std::abs(dot(normal(current,at),*n))>.95)
                    throw std::invalid_argument("Sheet conversion cannot reproduce this surface transition.");
                continue;
            }
            if(dot(normal(current,at),normal(*next,at))<1-1e-6)continue;
            if(!straight(edge))throw std::invalid_argument("Sheet conversion cannot reproduce this surface transition.");
            if(current.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
                    next->surface->kind==kernel::SurfaceGeometry::Kind::Cylinder&&
                    !has_sheet_bend_exit(mesh,available,current,*next))continue;
            if(current.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder&&
                    next->surface->kind==kernel::SurfaceGeometry::Kind::Plane) {
                const auto parent=available.find(result[i].parent);
                if(parent==available.end()||
                        !has_sheet_bend_exit(mesh,available,parent->second,current))
                    continue;
            }
            visited.insert(key(*next));result.push_back({*next,key(current),edge});
        }
    }
    return result;
}
const kernel::ViewerEdge* bend_exit_edge(const std::vector<Node>& nodes,
        const Node& bend) {
    for(const auto& candidate:nodes)
        if(candidate.parent==key(bend.face)&&candidate.face.surface&&
                candidate.face.surface->kind==
                    kernel::SurfaceGeometry::Kind::Plane)
            return &candidate.entry;
    return nullptr;
}
double meeting_setback_for_axes(V first,V second,double thickness) {
    first=unit(first);second=unit(second);
    const double sine=std::sqrt(dot(cross(first,second),cross(first,second)));
    return sine>1e-7?thickness/sine:thickness;
}
double maximum_meeting_setback(const std::vector<Node>& nodes,
        const Node& bend,double thickness) {
    double result=thickness;
    const auto distance=[](V a,V b) {
        const auto delta=sub(a,b);return std::sqrt(dot(delta,delta));
    };
    for(const auto& other:nodes) {
        if(&other==&bend||!other.face.surface||
                other.face.surface->kind!=
                    kernel::SurfaceGeometry::Kind::Cylinder)continue;
        bool meeting=false;
        for(const auto a:bend.entry.points)for(const auto b:other.entry.points)
            meeting|=distance(a,b)<=join_tolerance;
        if(meeting)result=std::max(result,meeting_setback_for_axes(
            bend.face.surface->axis,other.face.surface->axis,thickness));
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
struct RootSheetFrame {V origin,x,y,normal;};
RootSheetFrame root_sheet_frame(const document::PartDocument& source,
        const sketcher::Sketch& local_context,const kernel::ViewerMesh& mesh,const Face& face) {
    const auto n=unit(normal(face,face.surface->origin));
    auto fallback_x=unit(face.surface->radial);
    // The Part Origin is document geometry. Convert it through the same public
    // Body-local path as the selected source packet; no placement references or
    // general placement equations are changed by this conversion-only seed.
    auto origin_geometry=source.sketch_reference_geometry_for(
        local_context,source.origin_viewer_mesh().original_references);
    if(origin_geometry.points.empty()||origin_geometry.axes.size()<3)
        return {face.surface->origin,fallback_x,cross(n,fallback_x),n};
    const auto part_origin=origin_geometry.points.front().position;
    std::array<V,3> part_axes;
    for(std::size_t i=0;i<part_axes.size();++i)part_axes[i]=unit(origin_geometry.axes[i].direction);
    const auto inside=[&](V p) {
        for(std::size_t triangle=0;triangle<mesh.triangle_references.size();++triangle) {
            if(key(mesh.triangle_references[triangle])!=key(face)||3*triangle+2>=mesh.triangles.size())continue;
            const auto a=mesh.vertices.at(mesh.triangles[3*triangle]);
            const auto b=mesh.vertices.at(mesh.triangles[3*triangle+1]);
            const auto c=mesh.vertices.at(mesh.triangles[3*triangle+2]);
            const auto v0=sub(b,a),v1=sub(c,a),v2=sub(p,a);
            const double d00=dot(v0,v0),d01=dot(v0,v1),d11=dot(v1,v1);
            const double d20=dot(v2,v0),d21=dot(v2,v1),denominator=d00*d11-d01*d01;
            if(std::abs(denominator)<=1e-20)continue;
            const double v=(d11*d20-d01*d21)/denominator;
            const double w=(d00*d21-d01*d20)/denominator;
            const double u=1-v-w;
            if(u>=-1e-8&&v>=-1e-8&&w>=-1e-8&&
               std::abs(dot(sub(p,a),n))<=1e-6)return true;
        }
        return false;
    };
    const auto plane_count=[&](V p) {
        unsigned count{};
        for(const auto axis:part_axes)if(std::abs(dot(sub(p,part_origin),axis))<=1e-6)++count;
        return count;
    };
    struct Candidate {V point;unsigned planes{};double distance{};};
    std::vector<Candidate> candidates;
    const auto offer=[&](V p) {
        if(!inside(p))return;
        const auto delta=sub(p,part_origin);
        candidates.push_back({p,plane_count(p),dot(delta,delta)});
    };
    // Orthogonal projection is the least-surprising fallback whenever the
    // selected bounded face contains it.
    offer(add(part_origin,mul(n,dot(sub(face.surface->origin,part_origin),n))));
    // Intersections with a Part axis lie in two Origin planes at once.
    for(const auto axis:part_axes) {
        const double denominator=dot(n,axis);
        if(std::abs(denominator)>1e-10)
            offer(add(part_origin,mul(axis,dot(n,sub(face.surface->origin,part_origin))/denominator)));
    }
    // Otherwise use an actual bounded intersection with one Origin plane.
    // Triangle edges preserve holes and concave outlines without an untrimmed
    // supporting-plane shortcut.
    for(const auto plane_normal:part_axes)
        for(std::size_t triangle=0;triangle<mesh.triangle_references.size();++triangle) {
            if(key(mesh.triangle_references[triangle])!=key(face)||3*triangle+2>=mesh.triangles.size())continue;
            const std::array vertices{mesh.vertices.at(mesh.triangles[3*triangle]),
                mesh.vertices.at(mesh.triangles[3*triangle+1]),mesh.vertices.at(mesh.triangles[3*triangle+2])};
            for(std::size_t edge=0;edge<3;++edge) {
                const auto a=vertices[edge],b=vertices[(edge+1)%3];
                const double da=dot(sub(a,part_origin),plane_normal);
                const double db=dot(sub(b,part_origin),plane_normal);
                if(std::abs(da)<=1e-10)offer(a);
                if((da<0&&db>0)||(da>0&&db<0))offer(add(a,mul(sub(b,a),da/(da-db))));
            }
        }
    V chosen_origin=face.surface->origin;
    if(!candidates.empty()) {
        const auto chosen=std::ranges::max_element(candidates,[](const auto& a,const auto& b) {
            if(a.planes!=b.planes)return a.planes<b.planes;
            return a.distance>b.distance;
        });
        chosen_origin=chosen->point;
    }
    // Keep the selected face normal/material side.  Within that plane prefer
    // a projected Part axis closest to the source face's established radial
    // direction, so XY/XZ/YZ faces receive their natural local axes without
    // an arbitrary 90-degree turn.
    double best_alignment=-1;
    for(const auto axis:part_axes) {
        const auto projected=sub(axis,mul(n,dot(axis,n)));
        const double length=std::sqrt(dot(projected,projected));
        if(length<=1e-10)continue;
        auto direction=mul(projected,1/length);
        double alignment=dot(direction,fallback_x);
        if(alignment<0){direction=mul(direction,-1);alignment=-alignment;}
        if(alignment>best_alignment){best_alignment=alignment;fallback_x=direction;}
    }
    return {chosen_origin,fallback_x,cross(n,fallback_x),n};
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
                auto a=sketch.local_point(edge.points.front()),b=sketch.local_point(edge.points.back());
                const auto m=sketch.local_point(edge.points[edge.points.size()/2]);
                const auto project=[&](std::array<double,2> point) {
                    const double length=std::hypot(point[0]-center[0],point[1]-center[1]);
                    if(length<=1e-12)return point;
                    point[0]=center[0]+radius*(point[0]-center[0])/length;
                    point[1]=center[1]+radius*(point[1]-center[1])/length;
                    // OCCT can report a circular endpoint a few tenths of a
                    // micron away from its analytic cylinder. Keep the shared
                    // native Sketch point, but put it on the exact circle so
                    // the imported Flat stays closed and editable.
                    const auto existing=std::ranges::find_if(sketch.points,[&](const auto& candidate) {
                        return std::hypot(candidate.x-point[0],candidate.y-point[1])<=1e-5;
                    });
                    if(existing!=sketch.points.end()) {
                        existing->x=point[0];existing->y=point[1];
                    }
                    return point;
                };
                a=project(a);b=project(b);
                if(std::hypot(a[0]-b[0],a[1]-b[1])<1e-6)static_cast<void>(sketch.add_circle(center[0],center[1],radius));
                else {
                    const double start=std::atan2(a[1]-center[1],a[0]-center[0]);
                    const auto angle=[&](const auto& p){double d=std::atan2(p[1]-center[1],p[0]-center[0])-start;return d<0?d+2*std::numbers::pi:d;};
                    static_cast<void>(sketch.add_arc(center[0],center[1],a[0],a[1],b[0],b[1],false,1e-7,angle(m)>angle(b)));
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
void bind_flat_outline_to_attachment(sketcher::Sketch& sketch,
        const double endpoint_setback) {
    const auto endpoint=[&](const char* suffix) {
        return std::ranges::find(sketch.external_references,sketch.id+suffix,
            &sketcher::SketchExternalReference::id);
    };
    const auto first=endpoint(":flat-attachment:first");
    const auto last=endpoint(":flat-attachment:last");
    if(first==sketch.external_references.end()||last==sketch.external_references.end()||
            first->broken||last->broken||first->cached_points.size()!=1||
            last->cached_points.size()!=1)
        throw std::invalid_argument("Flat profile conflicts with its sheet endpoint references.");
    // Select one real outline segment by both endpoints together. Choosing
    // each nearest point independently can bind two different perimeter
    // edges at a corner: the Flat then follows only one endpoint of the Bend
    // clearance instead of moving its complete joining edge.
    const auto distance=[](const auto& point,const auto& target) {
        return std::hypot(point->x-target[0],point->y-target[1]);
    };
    const auto first_target=first->cached_points.front();
    const auto last_target=last->cached_points.front();
    const sketcher::SketchSegment* joining_segment=nullptr;
    bool reversed{};double best=INFINITY;
    for(const auto& segment:sketch.segments) {
        if(segment.construction)continue;
        const auto* a=sketch.find_point(segment.first_point_id);
        const auto* b=sketch.find_point(segment.second_point_id);
        if(!a||!b)continue;
        const double direct=std::max(
            distance(a,first_target),distance(b,last_target));
        const double reverse=std::max(
            distance(a,last_target),distance(b,first_target));
        const double error=std::min(direct,reverse);
        if(error<best) {
            best=error;joining_segment=&segment;reversed=reverse<direct;
        }
    }
    // A native solid conversion may move the Bend End vertices one
    // material thickness inward from the source cylinder edge. An imported
    // STEP already carries its finished clearances and uses no extra setback.
    if(!joining_segment||best>endpoint_setback+join_tolerance)
        throw std::invalid_argument(
            "Flat profile conflicts with its sheet endpoint references.");
    const auto first_point_id=reversed
        ?joining_segment->second_point_id:joining_segment->first_point_id;
    const auto last_point_id=reversed
        ?joining_segment->first_point_id:joining_segment->second_point_id;
    const auto first_reference_id=first->id,last_reference_id=last->id;
    // Adding the first equation replaces the Sketch transactionally and
    // invalidates every vector iterator. Persist identities before either add.
    const auto bind=[&](const std::string& point,const std::string& reference) {
        if(std::ranges::none_of(sketch.constraints,[&](const auto& constraint) {
                return !constraint.suppressed&&
                    constraint.kind==sketcher::ConstraintKind::PointReference&&
                    constraint.first_point_id==point&&
                    constraint.second_point_id==reference;
            }))
            static_cast<void>(sketch.add_point_reference_constraint(point,reference));
    };
    bind(first_point_id,first_reference_id);
    bind(last_point_id,last_reference_id);
    const auto solved=sketch.solve();
    if(solved.status==sketcher::SolveStatus::Conflicting||
            solved.status==sketcher::SolveStatus::Invalid)
        throw std::invalid_argument("Flat profile conflicts with its sheet endpoint references.");
}
}
double suggest_sheet_thickness(const kernel::ViewerMesh& mesh,const Face& selected) {
    const auto available=faces(mesh);const auto it=available.find(key(selected));
    return it==available.end()?0:planar_pair_distance(available,it->second);
}
double suggest_sheet_thickness(const Workspace& live,const std::string& id,
        const kernel::ViewerMesh& mesh,const Face& selected) {
    const auto available=faces(mesh);std::vector<Face> planar;
    for(const auto& [identity,face]:available)if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane)
        planar.push_back(face);
    const auto areas=measurement_face_areas(live,id,planar);
    kernel::ViewerMesh analysis;
    for(std::size_t i=0;i<planar.size();++i) {
        planar[i].measured_area=areas[i];analysis.triangle_references.push_back(std::move(planar[i]));
    }
    return suggest_sheet_thickness(analysis,selected);
}
SheetBodyConversion prepare_sheet_from_body(const document::PartDocument& source,
        const std::vector<kernel::BodyResult>& cache,const Face& selected,
        double thickness,const kernel::OcctKernel& kernel,
        bool thickness_override) {
    const auto* target=source.body_history.find(source.body_history.active_body_id());
    if(!target||target->derived_copy)throw std::invalid_argument("Sheet from Body requires an editable active Body.");
    if(!thickness_override)
        thickness=document::sheet_metal_defaults(source).
            thickness_mm.value_or(1.);
    if(!(thickness>=.001)||thickness>1000000||!std::isfinite(thickness))throw std::invalid_argument("Sheet thickness must be between 0.001 and 1000000 mm.");
    if(cache.empty())throw std::invalid_argument("Sheet from Body requires a calculated source body.");
    const auto source_owner=source.body_owner_for_object(selected.owner_id);
    const bool replaces_body=!target->entries.empty();
    if(!source_owner||(replaces_body&&source_owner->scope.id!=target->scope.id))throw std::invalid_argument("Select a planar face of the active Body.");
    const bool imported_step_source=std::ranges::any_of(
        source_owner->entries.begin(),
        std::next(source_owner->entries.begin(),
            static_cast<std::ptrdiff_t>(std::min(source_owner->cursor,
                source_owner->entries.size()))),
        [&](const auto& entry) {
            const auto* feature=source.find_container(entry.id);
            return feature&&feature->feature_kind==
                document::FeatureKind::ImportedStep;
        });
    // A native solid (Extrusion/Feature/Fillet...) has no manufacturing
    // clearance contract, so conversion creates the one-thickness meeting
    // setback. STEP geometry represents an already finished sheet supplied
    // by another system; preserve its authored endpoints verbatim.
    const double meeting_end_setback=imported_step_source?0.:thickness;
    const auto& order=source.body_history.order();
    if(!replaces_body&&std::ranges::find(order,source_owner->scope.id)>=std::ranges::find(order,target->scope.id))
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
        throw std::invalid_argument("Select a calculated planar source face.");
    const auto nodes=skin(mesh,root->second);
    SheetBodyConversion result;result.document=source;result.calculated=cache;
    if(replaces_body) {
        auto conversion=document::PartDocument::create_sheet_from_body_container();
        conversion.sheet_from_body.source_body_id=source_owner->scope.id;
        conversion.sheet_from_body.thickness=thickness;
        conversion.sheet_from_body.thickness_override=thickness_override;
        conversion.sheet_from_body.replaces_body=true;
        result.container_id=conversion.id;
        result.document.insert_history_entry(
            document::PartHistoryKind::Feature,conversion.id);
        result.document.history.push_back(std::move(conversion));
    }
    // Author a new root frame and new profile geometry; no source reference is
    // persisted. Subsequent features will use only newly authored references.
    auto sketch=sketcher::Sketch::create_default();auto feature=document::PartDocument::create_sketch_container();
    feature.feature_kind=document::FeatureKind::Flat;feature.name="Tabule";feature.flat.sketch_id=sketch.id;
    feature.flat.thickness_override=thickness_override;
    feature.flat.thickness=thickness;
    feature.flat.direction=document::ExtrusionDirection::Reverse;
    sketch.owner_container_id=feature.id;sketch.name=feature.name;
    const auto frame=root_sheet_frame(source,local_context,mesh,root->second);
    numeric_frame(feature,sketch,frame.origin,frame.x,frame.y,frame.normal);outline(sketch,mesh,root->second);
    result.document.insert_history_entry(document::PartHistoryKind::Feature,feature.id);result.created.push_back(feature.id);
    result.document.history.push_back(std::move(feature));result.document.sketches.push_back(std::move(sketch));
    if(replaces_body)
        result.document.find_container(result.container_id)->
            sheet_from_body.generated_features=result.created;
    result.document.resolve_constructions(construction_reference_source_geometry(cache));
    result.calculated=kernel.evaluate_history_incremental(result.document.kernel_operations(),cache);
    std::map<std::string,std::string> authored{{key(root->second),result.created.front()}};
    for(std::size_t i=1;i<nodes.size();++i) {
        const auto& node=nodes[i];
        const auto skip=[&](const char*) {
            ++result.skipped;
        };
        const auto parent=authored.find(node.parent);
        if(parent==authored.end()){skip("parent");continue;}
        const auto& surface=*node.face.surface;
        const bool bend=surface.kind==kernel::SurfaceGeometry::Kind::Cylinder;
        const auto* exit_edge=bend?bend_exit_edge(nodes,node):nullptr;
        // A Bend is useful only when it leads to a newly authored planar
        // wall. In a closed skin the visited-face guard makes the returning
        // cylinder terminal; omitting it creates the required finite seam.
        // An open terminal cylinder is omitted for the same reason: a native
        // sheet history must never end with an unsupported Bend.
        if(bend&&!exit_edge)continue;
        const auto geometry=construction_reference_source_geometry(result.calculated);
        const auto local_geometry=source.sketch_reference_geometry_for(local_context,geometry);
        auto a=node.entry.points.front(),b=node.entry.points.back();
        if(bend&&surface.reversed){const auto shift=mul(normal(node.face,a),-thickness);a=add(a,shift);b=add(b,shift);}
        const auto distance=[](V p,V q){return std::hypot(p.x-q.x,p.y-q.y,p.z-q.z);};
        const auto source_tangent=unit(sub(b,a));
        double allowed_longitudinal_setback=meeting_end_setback;
        if(!bend&&!imported_step_source) {
            const auto parent_node=std::ranges::find_if(nodes,[&](const auto& candidate) {
                return key(candidate.face)==node.parent;
            });
            if(parent_node!=nodes.end()&&parent_node->face.surface&&
                    parent_node->face.surface->kind==
                        kernel::SurfaceGeometry::Kind::Cylinder)
                allowed_longitudinal_setback=maximum_meeting_setback(
                    nodes,*parent_node,thickness);
        }
        const kernel::ViewerEdge* attachment=nullptr;double best=INFINITY;
        for(const auto& edge:local_geometry.edges) {
            if(edge.reference.owner_id!=parent->second||!straight(edge))continue;
            try {if(bend)static_cast<void>(document::bend_sheet_references(edge));else static_cast<void>(document::flat_sheet_references(edge));}
            catch(const std::invalid_argument&){continue;}
            const auto mismatch=[&](V p,V q) {
                if(bend)return distance(p,q);
                const auto n=normal(node.face,p),delta=sub(q,p);const double offset=dot(delta,n);
                const auto in_plane=sub(delta,mul(n,offset));
                const double along=dot(in_plane,source_tangent);
                const auto across=sub(in_plane,mul(source_tangent,along));
                // A native-solid Bend is deliberately shortened at every
                // meeting end. Its following Flat still originates from the
                // source cylinder edge, so accept exactly that longitudinal
                // setback while keeping every other match strict. Imported
                // STEP endpoints are preserved and therefore match exactly.
                return std::max({std::sqrt(dot(across,across)),
                    std::min(std::abs(offset),
                        std::abs(std::abs(offset)-thickness)),
                    std::max(0.,std::abs(along)-
                        allowed_longitudinal_setback)});
            };
            const double error=std::min(std::max(mismatch(a,edge.points.front()),mismatch(b,edge.points.back())),
                std::max(mismatch(b,edge.points.front()),mismatch(a,edge.points.back())));
            if(error<best){best=error;attachment=&edge;}
        }
        // Reuse the native parent's exact joining edge within the agreed
        // 0.05 mm sheet conversion tolerance; never widen general CAD snapping.
        if(!attachment||best>join_tolerance){skip("attachment");continue;}
        auto next=result.document;auto profile=sketcher::Sketch::create_default();
        auto added=document::PartDocument::create_sketch_container();profile.owner_container_id=added.id;
        added.feature_kind=bend?document::FeatureKind::Bend:document::FeatureKind::Flat;
        added.name=bend?"Ohyb":"Tabule";profile.name=added.name;
        if(bend) {
            const auto incoming=normal(node.face,node.entry.points.front()),outgoing=normal(node.face,exit_edge->points.front());
            added.bend.angle_degrees=std::acos(std::clamp(dot(incoming,outgoing),-1.,1.))*180/std::numbers::pi;
            added.bend.radius=surface.radius-(surface.reversed?0:thickness);
            // The source may expose an outer radius exactly equal to the
            // requested material thickness. That is the legitimate native
            // zero-inside-radius Bend, not a missing transition. Clamp only
            // numerical noise around zero; a materially negative radius is
            // still unreproducible.
            if(std::abs(added.bend.radius)<=1e-7)added.bend.radius=0;
            if(added.bend.radius<0||added.bend.angle_degrees<.001){skip("bend parameters");continue;}
            added.bend.sketch_id=profile.id;added.bend.sheet_attachment=true;
            added.bend.thickness_override=thickness_override;
            added.bend.thickness=thickness;
            document::initialize_bend_start_profile(profile,distance(a,b));
            added.placement.references=document::bend_sheet_references(*attachment);
        }else {
            added.flat.sketch_id=profile.id;added.flat.sheet_attachment=true;
            added.flat.thickness_override=thickness_override;
            added.flat.thickness=thickness;
            added.flat.direction=document::ExtrusionDirection::Reverse;
            added.placement.references=document::flat_sheet_references(*attachment);
        }
        next.insert_history_entry(document::PartHistoryKind::Feature,added.id);
        next.history.push_back(added);next.sketches.push_back(profile);
        if(replaces_body)
            next.find_container(result.container_id)->
                sheet_from_body.generated_features.push_back(added.id);
        next.resolve_constructions(geometry);
        auto& placed=next.sketches.back();
        if(!bend) {
            if(std::abs(dot(placed.resolved_normal,surface.axis))<1-1e-6){skip("flat frame");continue;}
            outline(placed,mesh,node.face);
            // A converted outline is real editable Sketch geometry.  Bind its
            // two joining vertices to the stable End vertices exactly like a
            // normally authored attached Flat.  Merely storing the placement
            // edge leaves the outline behind when corner closure shortens a
            // Bend end and can reverse the apparent segment orientation.
            bind_flat_outline_to_attachment(
                placed,allowed_longitudinal_setback);
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
            const double source_start=std::min(first[0],last[0]);
            const double source_end=std::max(first[0],last[0]);
            std::array endpoint_setbacks{
                meeting_end_setback,meeting_end_setback};
            if(!imported_step_source)for(const auto& other:nodes) {
                if(&other==&node||!other.face.surface||
                        other.face.surface->kind!=
                            kernel::SurfaceGeometry::Kind::Cylinder)continue;
                for(const auto point:node.entry.points)
                    for(const auto other_point:other.entry.points) {
                        if(distance(point,other_point)>join_tolerance)continue;
                        const auto local=end.local_point(point);
                        const std::size_t endpoint=
                            std::abs(local[0]-source_start)<=
                                std::abs(local[0]-source_end)?0:1;
                        endpoint_setbacks[endpoint]=meeting_setback_for_axes(
                            surface.axis,other.face.surface->axis,thickness);
                        // Two rounded source branches meeting at this endpoint
                        // need the native zero-gap corner loft as well as the
                        // final axial setback. It tapers the end through the
                        // Bend angle instead of sweeping overlapping end
                        // sections.
                        next.history.back().bend.corner[endpoint]=true;
                        next.history.back().bend.corner_gap=0;
                    }
            }
            const auto adjusted_meeting_extension=[&](double extension,
                    double setback) {
                if(std::abs(extension)<=join_tolerance)return 0.;
                const double remaining=std::max(0.,
                    std::abs(extension)-setback);
                return std::copysign(remaining,extension);
            };
            // A native solid's cylinder reaches the selected outside skin.
            // At a meeting end, stop the authored Bend at the neighbour's
            // inner side. A STEP source is already finished sheet geometry,
            // so meeting_end_setback is zero and its endpoints remain exact.
            document::set_bend_profile_extensions(next.history.back(),
                adjusted_meeting_extension(
                    start_reference->x-source_start,endpoint_setbacks[0]),
                adjusted_meeting_extension(
                    source_end-end_reference->x,endpoint_setbacks[1]));
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
            if(!supported){skip("bend span");continue;}
        }
        PartCalculationPolicy policy;policy.reject_errors=true;
        auto calculated=calculate_part_with_resolved_references(kernel,next,&result.calculated,policy);
        result.document=std::move(next);result.calculated=std::move(calculated);
        result.created.push_back(added.id);authored.emplace(key(node.face),added.id);
    }
    if(result.skipped)throw std::invalid_argument("Sheet conversion cannot reproduce every connected face.");
    result.document.validate_sheet_from_body();
    return result;
}
}
