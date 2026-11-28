#include <zima/document/mech_groove.hpp>
#include <zima/document/groove_catalog.hpp>
#include <zima/document/placement_orientation.hpp>
#include <zima/document/part_document.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace zima::document {
namespace {
using V=kernel::Vec3;
double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V mul(V a,double k){return {a.x*k,a.y*k,a.z*k};}
V sub(V a,V b){return add(a,mul(b,-1));}
V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
V unit(V a){const auto length=std::sqrt(dot(a,a));
    if(!std::isfinite(length)||length<1e-12)throw std::invalid_argument("Groove support frame is invalid");
    return mul(a,1/length);}
}

void validate_mech_groove(const MechGrooveParameters& p) {
    if(p.profile!=GrooveProfile::Rectangular&&p.profile!=GrooveProfile::Circular)
        throw std::invalid_argument("Invalid groove profile");
    for(const double value:{p.width,p.depth,p.radius})
        if(!std::isfinite(value)||value<=1e-9||value>1e6)
            throw std::invalid_argument("Groove dimensions must be positive and finite");
    if(!std::isfinite(p.support_diameter)||p.support_diameter<0||
            !std::isfinite(p.width_position)||p.width_position<0||p.width_position>1)
        throw std::invalid_argument("Invalid groove catalogue selection");
    if(p.profile==GrooveProfile::Circular&&p.depth>=2*p.radius)
        throw std::invalid_argument("Circular groove depth must be smaller than its cutter diameter");
    if(!p.section.empty())static_cast<void>(sketcher::Sketch::from_serialized(p.section));
}

nlohmann::json serialize_mech_groove(const MechGrooveParameters& p) {
    validate_mech_groove(p);
    return {{"version",1},{"profile",static_cast<int>(p.profile)},
        {"width",p.width},{"depth",p.depth},{"radius",p.radius},
        {"catalogue",p.catalogue},{"row",p.row},{"width_position",p.width_position},
        {"support",{{"owner",p.support.owner_id},{"key",p.support.semantic_key},{"occurrence",p.support.instance_path}}},
        {"support_diameter",p.support_diameter},{"internal",p.internal},{"section",p.section}};
}

MechGrooveParameters load_mech_groove(const nlohmann::json& j) {
    if(j.at("version")!=1)throw std::invalid_argument("Unsupported groove definition version");
    const int profile=j.at("profile");
    if(profile<0||profile>1)throw std::invalid_argument("Invalid groove profile");
    MechGrooveParameters p;p.profile=static_cast<GrooveProfile>(profile);
    p.support={j.at("support").at("owner"),j.at("support").at("key"),j.at("support").at("occurrence")};
#define LOAD(field) j.at(#field).get_to(p.field)
    LOAD(width);LOAD(depth);LOAD(radius);LOAD(catalogue);LOAD(row);LOAD(width_position);
    LOAD(support_diameter);LOAD(internal);LOAD(section);
#undef LOAD
    validate_mech_groove(p);return p;
}

HistoryContainer create_mech_groove() {
    auto result=PartDocument::create_sketch_container();
    result.feature_kind=FeatureKind::MechGroove;
    result.combine_mode=CombineMode::Subtract;result.name="Groove";
    return result;
}

sketcher::Sketch groove_section(const std::string& owner,const MechGrooveParameters& p,
        const kernel::SurfaceGeometry& support,V origin) {
    validate_mech_groove(p);
    if(owner.empty()||support.kind!=kernel::SurfaceGeometry::Kind::Cylinder||
            !std::isfinite(support.radius)||support.radius<=1e-9||p.internal!=support.reversed)
        throw std::invalid_argument("Groove requires a cylindrical material face");
    if(!p.internal&&p.depth>=support.radius)
        throw std::invalid_argument("Groove depth reaches the shaft axis");
    const auto axis=unit(support.axis);
    const auto radial=unit(sub(support.radial,mul(axis,dot(support.radial,axis))));
    auto sketch=sketcher::Sketch::create_default();
    sketch.id=owner+":groove-section";sketch.owner_container_id=owner;
    sketch.resolved_origin=origin;sketch.resolved_x_axis=radial;
    sketch.resolved_y_axis=axis;sketch.resolved_normal=cross(radial,axis);sketch.plane_auto=false;
    const auto point=[&](const char* role,double x,double y,bool construction=false){
        const auto id=sketch.id+":"+role;sketch.points.push_back({id,x,y,false,construction});return id;};
    const auto line=[&](const char* role,const std::string& a,const std::string& b){
        sketch.segments.push_back({sketch.id+":"+role,a,b});};
    const double r=support.radius, sign=p.internal?1.:-1.;
    if(p.profile==GrooveProfile::Rectangular) {
        const auto a=point("first-mouth",r,-p.width/2);
        const auto b=point("first-floor",r+sign*p.depth,-p.width/2);
        const auto c=point("last-floor",r+sign*p.depth,p.width/2);
        const auto d=point("last-mouth",r,p.width/2);
        line("first-wall",a,b);line("floor",b,c);line("last-wall",c,d);line("mouth",d,a);
    } else {
        const double half=std::sqrt(p.depth*(2*p.radius-p.depth));
        const double alpha=std::acos(std::clamp(1-p.depth/p.radius,-1.,1.));
        const double first_angle=p.internal?-alpha:std::numbers::pi-alpha;
        const double last_angle=p.internal?alpha:std::numbers::pi+alpha;
        const auto center=point("cutter-centre",r+sign*(p.depth-p.radius),0,true);
        const auto first=point("first-mouth",r,p.internal?-half:half);
        const auto last=point("last-mouth",r,p.internal?half:-half);
        sketch.arcs.push_back({sketch.id+":floor",center,first,last,p.radius,first_angle,last_angle});
        line("mouth",last,first);
    }
    const auto a=point("axis-first",0,-1,true),b=point("axis-last",0,1,true);
    sketch.segments.push_back({sketch.id+":axis",a,b,true,true});
    sketch.validate();return sketch;
}

HistoryContainer resolve_mech_groove_draft(HistoryContainer value,
        const kernel::ViewerReferenceGeometry& refs) {
    if(value.feature_kind!=FeatureKind::MechGroove)
        throw std::invalid_argument("This container is not a groove");
    std::optional<kernel::FaceReference> support;
    for(const auto& ref:value.placement.references) {
        if(ref.orientation_only||!ref.use_axis)continue;
        const auto found=std::ranges::find_if(refs.triangle_references,[&](const auto& face){
            return face.owner_id==ref.owner_id&&face.semantic_key==ref.semantic_key&&face.instance_path==ref.instance_path;});
        if(found==refs.triangle_references.end()||!found->surface||found->surface_result||
                found->surface->kind!=kernel::SurfaceGeometry::Kind::Cylinder)continue;
        if(support&&*support!=*found)throw std::invalid_argument("Groove requires one cylindrical axis reference");
        support=*found;
    }
    if(!support&&placement_references_use_whole_origin(value.placement.references)&&value.mech_groove.support.valid()) {
        const auto found=std::ranges::find(refs.triangle_references,value.mech_groove.support);
        if(found!=refs.triangle_references.end()&&found->surface&&!found->surface_result&&
                found->surface->kind==kernel::SurfaceGeometry::Kind::Cylinder)support=*found;
    }
    if(!support)throw std::invalid_argument("Select a cylindrical material face in Axis mode for the groove");
    const auto& surface=*support->surface;
    auto placement=value.placement;
    if(!resolve_placement(placement,refs))throw std::invalid_argument("Groove placement references are unavailable");
    const V origin{placement.x,placement.y,placement.z};
    const auto axis=unit(surface.axis),delta=sub(origin,surface.origin);
    const auto off_axis=sub(delta,mul(axis,dot(delta,axis)));
    if(std::sqrt(dot(off_axis,off_axis))>1e-7)
        throw std::invalid_argument("Groove origin must lie on the selected cylinder axis");
    auto& p=value.mech_groove;p.support=*support;p.support_diameter=2*surface.radius;p.internal=surface.reversed;
    select_groove_size(p);
    if(p.profile==GrooveProfile::Circular)p.width=2*std::sqrt(p.depth*(2*p.radius-p.depth));
    p.section=groove_section(value.id,p,surface,origin).serialized();
    value.placement=placement;value.combine_mode=CombineMode::Subtract;
    return value;
}

std::vector<kernel::ViewerEdge> mech_groove_preview_edges(const HistoryContainer& feature) {
    const auto& p=feature.mech_groove;
    if(p.section.empty())return {};
    const auto section=sketcher::Sketch::from_serialized(p.section);
    std::vector<kernel::ViewerEdge> result;
    const auto radial=section.resolved_x_axis,axis=section.resolved_y_axis,tangent=cross(axis,radial);
    const auto world=[&](double r,double z,double angle){return add(section.resolved_origin,
        add(mul(axis,z),add(mul(radial,r*std::cos(angle)),mul(tangent,r*std::sin(angle)))));};
    const auto circle=[&](double r,double z,const std::string& role){
        kernel::ViewerEdge edge;edge.reference={feature.id,"preview:groove:"+role,{}};
        for(int i=0;i<=96;++i)edge.points.push_back(world(r,z,2*std::numbers::pi*i/96));
        result.push_back(std::move(edge));};
    for(const auto& point:section.points)if(!point.construction)circle(point.x,point.y,point.id);
    const auto shape=section.viewer_mesh();
    for(int meridian=0;meridian<4;++meridian)for(const auto& edge:shape.edges) {
        if(edge.reference.semantic_key.find(":axis")!=std::string::npos)continue;
        kernel::ViewerEdge preview;preview.reference={feature.id,"preview:groove:section:"+
            std::to_string(meridian)+":"+edge.reference.semantic_key,{}};
        for(const auto& point:edge.points) {
            const auto delta=sub(point,section.resolved_origin);
            preview.points.push_back(world(dot(delta,radial),dot(delta,axis),meridian*std::numbers::pi/2));
        }
        result.push_back(std::move(preview));
    }
    if(p.profile==GrooveProfile::Circular)
        circle(p.support_diameter/2+(p.internal?p.depth:-p.depth),0,"floor");
    return result;
}
}
