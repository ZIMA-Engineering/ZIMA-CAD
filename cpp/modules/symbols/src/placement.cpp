#include <zima/symbols/placement.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>
#include <set>

namespace zima::symbols {
namespace {
using kernel::Vec3;
double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
bool finite(Vec3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
void require(bool condition){if(!condition)throw std::invalid_argument("Invalid symbol annotation placement");}
std::array<double,3> array(Vec3 a){return {a.x,a.y,a.z};}
Vec3 vector(const nlohmann::json& j){const auto a=j.get<std::array<double,3>>();return {a[0],a[1],a[2]};}
}
void Frame::validate() const {
    require(finite(origin)&&finite(x)&&finite(y));
    require(std::abs(dot(x,x)-1)<1e-8&&std::abs(dot(y,y)-1)<1e-8&&std::abs(dot(x,y))<1e-8);
}
kernel::Vec3 Frame::world(Vec3 p) const {
    const auto z=cross(x,y);
    return {origin.x+x.x*p.x+y.x*p.y+z.x*p.z,
        origin.y+x.y*p.x+y.y*p.y+z.y*p.z,origin.z+x.z*p.x+y.z*p.y+z.z*p.z};
}
kernel::Vec3 Frame::local(Vec3 p) const {
    p=sub(p,origin);return {dot(p,x),dot(p,y),dot(p,cross(x,y))};
}
void Placement::validate() const {
    symbol.validate();frame.validate();
    require(std::isfinite(arrow_length)&&arrow_length>0);
    require(!unresolved||reference.has_value());
    if(reference) {
        require(!reference->document_id.empty()&&!reference->owner_id.empty()&&!reference->semantic_key.empty());
        require(reference->kind>=ReferenceKind::Face&&reference->kind<=ReferenceKind::Plane);
        require(std::isfinite(reference->surface_parameters[0])&&std::isfinite(reference->surface_parameters[1]));
        if(reference->surface_kind)require(*reference->surface_kind>=kernel::SurfaceGeometry::Kind::Plane&&
            *reference->surface_kind<=kernel::SurfaceGeometry::Kind::Cone&&
            (reference->kind==ReferenceKind::Face||reference->kind==ReferenceKind::Plane));
    }
    for(const auto& bend:leader_bends)require(std::isfinite(bend[0])&&std::isfinite(bend[1]));
}
void Placement::refresh_reference(const std::optional<Frame>& resolved) {
    if(!reference){unresolved=false;return;}
    if(!resolved){unresolved=true;return;}
    resolved->validate();frame=*resolved;unresolved=false;
}
kernel::ViewerMesh Placement::viewer_mesh(std::optional<double> paper_frame_angle) const {
    validate();auto result=instance_mesh(symbol,{},paper_frame_angle);
    if(!symbol.visible)return result;
    if(leader) {
        std::vector<Vec3> points{{0,0,0}};
        for(const auto& bend:leader_bends)points.push_back({bend[0],bend[1],0});
        points.push_back({symbol.x,symbol.y,0});
        const auto add=[&](std::vector<Vec3> stroke) {
            kernel::ViewerEdge edge;edge.points=std::move(stroke);
            edge.reference={symbol.id,"symbol:"+symbol.id,{}};edge.overlay=true;
            edge.color="#F5CD50";result.edges.push_back(std::move(edge));
        };
        const auto first=std::ranges::find_if(points,[](Vec3 p){return std::hypot(p.x,p.y)>1e-9;});
        if(first!=points.end()) {
            add(points);
            const double length=std::hypot(first->x,first->y),a=std::min(arrow_length,length*.5);
            const double dx=first->x/length,dy=first->y/length,w=a*.1763269807;
            add({{dx*a-dy*w,dy*a+dx*w,0},{0,0,0},{dx*a+dy*w,dy*a-dx*w,0}});
        }
    }
    for(auto& edge:result.edges)for(auto& p:edge.points)p=frame.world(p);
    return result;
}
void to_json(nlohmann::json& j,const Placement& p) {
    p.validate();j={{"symbol",p.symbol},{"frame",{{"origin",array(p.frame.origin)},{"x",array(p.frame.x)},{"y",array(p.frame.y)}}},
        {"reference",nullptr},{"unresolved",p.unresolved},{"leader",p.leader},{"leader_bends",p.leader_bends},{"arrow_length",p.arrow_length}};
    if(p.reference) {const auto& r=*p.reference;j["reference"]={{"document_id",r.document_id},{"owner_id",r.owner_id},
        {"semantic_key",r.semantic_key},{"instance_path",r.instance_path},{"kind",static_cast<int>(r.kind)},{"reversed",r.reversed},
        {"surface_kind",r.surface_kind?nlohmann::json(static_cast<int>(*r.surface_kind)):nlohmann::json(nullptr)},
        {"surface_parameters",r.surface_parameters}};}
}
void from_json(const nlohmann::json& j,Placement& output) {
    Placement p;p.symbol=j.at("symbol").get<sketcher::SymbolInstance>();const auto& f=j.at("frame");
    p.frame={vector(f.at("origin")),vector(f.at("x")),vector(f.at("y"))};
    if(!j.at("reference").is_null()) {const auto& r=j.at("reference");
        const int kind=r.at("kind");require(kind>=0&&kind<=static_cast<int>(ReferenceKind::Plane));
        p.reference=Reference{r.at("document_id"),r.at("owner_id"),r.at("semantic_key"),r.at("instance_path"),static_cast<ReferenceKind>(kind),r.at("reversed")};
        if(!r.at("surface_kind").is_null())p.reference->surface_kind=static_cast<kernel::SurfaceGeometry::Kind>(r.at("surface_kind").get<int>());
        p.reference->surface_parameters=r.at("surface_parameters").get<std::array<double,2>>();}
    p.unresolved=j.at("unresolved");p.leader=j.at("leader");
    p.leader_bends=j.at("leader_bends").get<decltype(p.leader_bends)>();p.arrow_length=j.at("arrow_length");
    p.validate();output=std::move(p);
}
nlohmann::json placements_json(const std::vector<Placement>& values) {
    auto result=nlohmann::json::array();std::set<std::string> ids;
    for(const auto& value:values){require(ids.insert(value.symbol.id).second);result.push_back(value);}return result;
}
std::vector<Placement> placements_from_json(const nlohmann::json& data) {
    require(data.is_array());auto result=data.get<std::vector<Placement>>();
    std::set<std::string> ids;for(const auto& value:result)require(ids.insert(value.symbol.id).second);return result;
}
namespace {
Vec3 multiply(Vec3 p,double s){return {p.x*s,p.y*s,p.z*s};}
Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 unit(Vec3 p){require(finite(p));const auto n=std::sqrt(dot(p,p));require(n>1e-12);return multiply(p,1/n);}
Frame surface_basis(const kernel::SurfaceGeometry& surface) {
    const auto z=unit(surface.axis);
    const auto x=unit(sub(surface.radial,multiply(z,dot(surface.radial,z))));
    Frame result{surface.origin,x,cross(z,x)};result.validate();return result;
}
Frame contact_frame(const kernel::SurfaceGeometry& surface,const Reference& ref,bool perpendicular) {
    require(ref.surface_kind==surface.kind);const auto basis=surface_basis(surface);
    const auto [u,v]=ref.surface_parameters;Frame frame;
    if(surface.kind==kernel::SurfaceGeometry::Kind::Plane)frame={basis.world({u,v,0}),basis.x,basis.y};
    else {
        const double slope=surface.kind==kernel::SurfaceGeometry::Kind::Cone?std::tan(surface.semi_angle):0;
        const double radius=surface.radius+v*slope;require(std::isfinite(radius)&&radius>1e-12);
        const auto radial=add(multiply(basis.x,std::cos(u)),multiply(basis.y,std::sin(u)));
        const auto tangent=add(multiply(basis.x,-std::sin(u)),multiply(basis.y,std::cos(u)));
        const auto axis=cross(basis.x,basis.y);
        frame={add(surface.origin,add(multiply(radial,radius),multiply(axis,v))),tangent,unit(add(axis,multiply(radial,slope)))};
    }
    if(surface.reversed!=ref.reversed)frame.y=multiply(frame.y,-1);
    if(perpendicular)frame.y=unit(cross(frame.x,frame.y));
    frame.validate();return frame;
}
}
void attach_to_surface(Placement& value,const kernel::FaceReference& face,const std::string& source_document,Vec3 contact,bool reversed) {
    require(face.valid()&&face.surface&&finite(contact));
    const auto& surface=*face.surface;const auto local=surface_basis(surface).local(contact);
    auto next=value;Reference ref{source_document,face.owner_id,face.semantic_key,face.instance_path,ReferenceKind::Face,reversed};
    ref.surface_kind=surface.kind;
    if(surface.kind==kernel::SurfaceGeometry::Kind::Plane)ref.surface_parameters={local.x,local.y};
    else {require(std::hypot(local.x,local.y)>1e-12);ref.surface_parameters={std::atan2(local.y,local.x),local.z};}
    next.reference=ref;next.frame=contact_frame(surface,ref,Definition::from_serialized(next.symbol.definition).id=="ze:surface-texture:iso21920");next.unresolved=false;next.validate();value=std::move(next);
}
bool refresh_surface_attachment(Placement& value,const kernel::ViewerReferenceGeometry& geometry) {
    value.validate();if(!value.reference){value.unresolved=false;return true;}
    const auto& ref=*value.reference;const kernel::SurfaceGeometry* surface=nullptr;
    for(const auto& face:geometry.triangle_references)
        if(face.owner_id==ref.owner_id&&face.semantic_key==ref.semantic_key&&face.instance_path==ref.instance_path&&face.surface) {
            if(surface&&*surface!=*face.surface){value.refresh_reference({});return false;}
            surface=face.surface.get();
        }
    if(!surface||ref.surface_kind!=surface->kind){value.refresh_reference({});return false;}
    try {const auto frame=contact_frame(*surface,ref,Definition::from_serialized(value.symbol.definition).id=="ze:surface-texture:iso21920");value.refresh_reference(frame);return true;}
    catch(const std::invalid_argument&){value.refresh_reference({});return false;}
}
}
