#include <zima/document/bend.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace zima::document {
namespace {
using V=kernel::Vec3;
V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V scale(V a,double s){return {a.x*s,a.y*s,a.z*s};}
V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
struct BendFrame {
    V first,last,along,inward,normal;
    double width{};
    std::string segment,first_point,last_point;
};
BendFrame frame(const HistoryContainer& feature,const sketcher::Sketch& sketch) {
    if(feature.feature_kind!=FeatureKind::Bend||feature.combine_mode!=CombineMode::Add)
        throw std::invalid_argument("Bend can only add material in a Part.");
    if(sketch.id!=feature.bend.sketch_id||sketch.owner_container_id!=feature.id)
        throw std::invalid_argument("Bend requires its own Sketch.");
    sketch.validate();
    const auto unsupported=[](const auto& curves){return std::ranges::any_of(curves,[](const auto& c){return !c.construction;});};
    if(unsupported(sketch.circles)||unsupported(sketch.arcs)||unsupported(sketch.ellipses)||
        unsupported(sketch.elliptical_arcs)||unsupported(sketch.bsplines)||!sketch.texts.empty()||
        std::ranges::count_if(sketch.segments,[](const auto& s){return !s.construction;})!=1)
        throw std::invalid_argument("Bend requires exactly one non-construction straight segment.");
    const auto& segment=*std::ranges::find_if(sketch.segments,[](const auto& s){return !s.construction;});
    const auto* first=sketch.find_point(segment.first_point_id);const auto* last=sketch.find_point(segment.second_point_id);
    if(!first||!last)throw std::invalid_argument("Bend segment endpoint is missing.");
    BendFrame f;f.first=sketch.world_point(first->x,first->y);f.last=sketch.world_point(last->x,last->y);
    const auto direction=add(f.last,scale(f.first,-1));f.width=std::hypot(direction.x,direction.y,direction.z);
    if(!std::isfinite(f.width)||f.width<.001)throw std::invalid_argument("Bend width must be at least 0.001 mm.");
    f.along=scale(direction,1/f.width);f.normal=sketch.resolved_normal;f.inward=cross(f.normal,f.along);
    f.segment=segment.id;f.first_point=segment.first_point_id;f.last_point=segment.second_point_id;return f;
}
std::string child(const HistoryContainer& feature,const std::string& role,const std::string& parent) {
    return "bend:"+feature.feature_id+":"+role+":from:"+std::to_string(parent.size())+":"+parent;
}
template<class Request> void profile(Request& request,const HistoryContainer& feature,const BendFrame& f,double thickness) {
    request.outer_profile=kernel::ExtrusionRequest::PolygonProfile{{f.first,f.last,
        add(f.last,scale(f.inward,thickness)),add(f.first,scale(f.inward,thickness))}};
    // The same authored section and ancestry are used in both states. OCCT
    // only locates these identities, including the Start/End region children.
    request.profile_region_id=child(feature,"section",f.segment);
    request.outer_boundary_id=child(feature,"boundary",f.segment);
    request.outer_edge_source_ids={child(feature,"outer",f.segment),child(feature,"last",f.last_point),
        child(feature,"inner",f.segment),child(feature,"first",f.first_point)};
    request.outer_vertex_source_ids={child(feature,"outer",f.first_point),child(feature,"outer",f.last_point),
        child(feature,"inner",f.last_point),child(feature,"inner",f.first_point)};
}
}
BendParameters resolved_bend_parameters(const HistoryContainer& feature,const SheetMetalDefaults& defaults) {
    auto p=feature.bend;
    if(!p.thickness_override)p.thickness=defaults.thickness_mm.value_or(1.0);
    if(!p.k_factor_override)p.k_factor=defaults.k_factor;
    validate_sheet_metal_defaults({p.thickness,p.k_factor});
    if(!std::isfinite(p.radius)||p.radius<.001||p.radius>1000000)
        throw std::invalid_argument("Bend inner radius must be between 0.001 and 1000000 mm.");
    if(!std::isfinite(p.angle_degrees)||p.angle_degrees<0||p.angle_degrees>180)
        throw std::invalid_argument("Bend angle must be between 0 and 180 degrees.");
    // Validate stored overrides as well, even while their controls are disabled.
    validate_sheet_metal_defaults({feature.bend.thickness,feature.bend.k_factor});
    return p;
}
kernel::FeatureGroupRequest bend_request(const HistoryContainer& feature,const sketcher::Sketch& sketch,const SheetMetalDefaults& defaults) {
    const auto p=resolved_bend_parameters(feature,defaults);const auto f=frame(feature,sketch);
    const double length=p.angle_degrees*std::numbers::pi/180*(p.radius+p.k_factor*p.thickness);
    kernel::FeatureGroupRequest group;
    const auto axis_start=add(f.first,p.unbend?scale(f.normal,length*.5):scale(f.inward,p.radius+p.thickness));
    group.axes.push_back({add(axis_start,scale(f.along,f.width*.5)),f.along,f.width+2,
        {feature.id,child(feature,"axis",f.segment),{}},p.unbend?"Osa ohybu":"Osa rotace ohybu"});
    if(p.angle_degrees==0)return group; // Explicit zero-material history boundary.
    if(p.unbend) {
        kernel::ExtrusionRequest request;profile(request,feature,f,p.thickness);
        request.direction=scale(f.normal,length);group.children.emplace_back(std::move(request));
    } else {
        kernel::RevolutionRequest request;profile(request,feature,f,p.thickness);
        request.profile_normal=f.normal;request.axis_point=axis_start;request.axis_direction=scale(f.along,-1);
        request.angle_degrees=p.angle_degrees;group.children.emplace_back(std::move(request));
    }
    return group;
}
kernel::ViewerMesh bend_preview(const HistoryContainer& feature,const sketcher::Sketch& sketch,const SheetMetalDefaults& defaults) {
    const auto request=bend_request(feature,sketch,defaults);const auto p=resolved_bend_parameters(feature,defaults);const auto f=frame(feature,sketch);
    kernel::ViewerMesh mesh;mesh.axes=request.axes;
    const double angle=p.angle_degrees*std::numbers::pi/180;
    const double developed=angle*(p.radius+p.k_factor*p.thickness);
    const auto point=[&](double along,bool inner,double fraction) {
        auto result=add(f.first,scale(f.along,along));
        if(p.unbend)return add(add(result,scale(f.inward,inner?p.thickness:0)),scale(f.normal,developed*fraction));
        const double r=p.radius+(inner?0:p.thickness),a=angle*fraction;
        return add(add(result,scale(f.inward,p.radius+p.thickness-r*std::cos(a))),scale(f.normal,r*std::sin(a)));
    };
    const auto edge=[&](std::string role,std::vector<V> points){kernel::ViewerEdge e;e.reference={feature.id,"preview:"+child(feature,role,f.segment),{}};e.points=std::move(points);mesh.edges.push_back(std::move(e));};
    for(bool inner:{false,true}) {
        edge(inner?"inner-start":"outer-start",{point(0,inner,0),point(f.width,inner,0)});
        edge(inner?"inner-end":"outer-end",{point(0,inner,1),point(f.width,inner,1)});
        for(bool last:{false,true}) {
            std::vector<V> points;const int samples=p.unbend?1:std::max(1,static_cast<int>(std::ceil(p.angle_degrees/3)));
            for(int i=0;i<=samples;++i)points.push_back(point(last?f.width:0,inner,static_cast<double>(i)/samples));
            edge(std::string(inner?"inner-":"outer-")+(last?"last":"first"),std::move(points));
        }
    }
    for(bool last:{false,true})for(bool end:{false,true})edge(std::string(last?"last-":"first-")+(end?"end":"start"),
        {point(last?f.width:0,false,end?1:0),point(last?f.width:0,true,end?1:0)});
    return mesh;
}
}
