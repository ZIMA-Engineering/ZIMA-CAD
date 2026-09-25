#include <zima/symbols/placement.hpp>
#include <zima/kernel/annotation_layout.hpp>
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
    require(!paper_tangent||finite(*paper_tangent));require(!paper_extension_start||finite(*paper_extension_start));
    require(std::isfinite(shelf_length)&&shelf_length>=.1);
    require(std::isfinite(arrow_length)&&arrow_length>0&&std::isfinite(offset_z));
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
kernel::ViewerMesh Placement::viewer_mesh(std::optional<double> paper_frame_angle,bool retain_layout) const {
    validate();auto result=instance_mesh(symbol,{},paper_frame_angle);
    if(!symbol.visible)return result;
    if(leader) {
        auto glyph=symbol;glyph.x=glyph.y=0;glyph.angle_degrees=0;
        result=instance_mesh(glyph);
        kernel::AnnotationStroke info;info.contact=frame.origin;
        info.grip=frame.world({symbol.x,symbol.y,offset_z});info.arrow_length=arrow_length;info.perpendicular=perpendicular_leader;
        info.short_shelf=short_shelf;info.shelf_length=shelf_length;
        info.kind=reference?static_cast<int>(reference->kind):2;
        if(info.kind==3)info.kind=0;
        auto direction=cross(frame.x,frame.y);
        if(reference&&reference->kind==ReferenceKind::Edge)direction=frame.x;
        else if(Definition::from_serialized(symbol.definition).id=="ze:surface-texture:iso21920")direction=frame.y;
        info.direction_tip={frame.origin.x+direction.x,frame.origin.y+direction.y,frame.origin.z+direction.z};
        info.left=info.bottom=1e100;info.right=-1e100;
        for(const auto& edge:result.edges)for(auto p:edge.points){info.left=std::min(info.left,p.x);info.right=std::max(info.right,p.x);info.bottom=std::min(info.bottom,p.y);}
        if(info.left>info.right)return {};
        for(auto& edge:result.edges){info.local_points=edge.points;edge.annotation=info;}
        info.local_points.clear();
        for(int role:{1,2,3}){info.role=role;kernel::ViewerEdge edge;edge.annotation=info;edge.reference={symbol.id,"symbol:"+symbol.id+(role==3?":shelf":""),{}};edge.overlay=true;edge.color="#F5CD50";result.edges.push_back(std::move(edge));}
        auto right=frame.x,up=frame.y;
        if(paper_frame_angle){const double a=*paper_frame_angle*std::acos(-1.)/180.;
            right={frame.x.x*std::cos(a)-frame.y.x*std::sin(a),frame.x.y*std::cos(a)-frame.y.y*std::sin(a),frame.x.z*std::cos(a)-frame.y.z*std::sin(a)};
            up={frame.x.x*std::sin(a)+frame.y.x*std::cos(a),frame.x.y*std::sin(a)+frame.y.y*std::cos(a),frame.x.z*std::sin(a)+frame.y.z*std::cos(a)};}
        const bool left=dot(sub(info.contact,info.grip),right)<=0;
        for(auto& edge:result.edges){
            bool perpendicular=!paper_frame_angle;
            if(paper_frame_angle&&paper_tangent&&perpendicular_leader) {
                auto normal=cross(cross(right,up),*paper_tangent);
                if(dot(normal,sub(info.grip,info.contact))<0)normal={-normal.x,-normal.y,-normal.z};
                edge.annotation->direction_tip={info.contact.x+normal.x,info.contact.y+normal.y,info.contact.z+normal.z};
                edge.annotation->kind=0;perpendicular=true;
            }
            if(!perpendicular)edge.annotation->perpendicular=false;
            edge.points=kernel::annotation_stroke(*edge.annotation,right,up,left,perpendicular);if(paper_frame_angle&&!retain_layout)edge.annotation.reset();
        }
        if(paper_frame_angle&&paper_extension_start) {
            auto delta=sub(frame.origin,*paper_extension_start);const double length=std::sqrt(dot(delta,delta));
            if(length>1e-9){kernel::ViewerEdge edge;edge.reference={symbol.id,"symbol:"+symbol.id,{}};edge.overlay=true;edge.color="#F5CD50";
                edge.points={*paper_extension_start,{frame.origin.x+delta.x/length*2,frame.origin.y+delta.y/length*2,0}};result.edges.push_back(std::move(edge));}
        }
        return result;
    }
    for(auto& edge:result.edges)for(auto& p:edge.points)p=frame.world(p);
    if(paper_frame_angle&&paper_extension_start) {
        const auto end=frame.world({symbol.x,symbol.y,0});auto delta=sub(end,*paper_extension_start);const double length=std::sqrt(dot(delta,delta));
        if(length>1e-9){kernel::ViewerEdge edge;edge.reference={symbol.id,"symbol:"+symbol.id,{}};edge.overlay=true;edge.color="#F5CD50";
            edge.points={*paper_extension_start,{end.x+delta.x/length*2,end.y+delta.y/length*2,0}};result.edges.push_back(std::move(edge));}
    }
    return result;
}
void to_json(nlohmann::json& j,const Placement& p) {
    p.validate();j={{"symbol",p.symbol},{"frame",{{"origin",array(p.frame.origin)},{"x",array(p.frame.x)},{"y",array(p.frame.y)}}},
        {"reference",nullptr},{"unresolved",p.unresolved},{"leader",p.leader},{"leader_bends",p.leader_bends},{"arrow_length",p.arrow_length},{"offset_z",p.offset_z},{"perpendicular_leader",p.perpendicular_leader},{"short_shelf",p.short_shelf},{"shelf_length",p.shelf_length}};
    j["paper_tangent"]=p.paper_tangent?nlohmann::json(array(*p.paper_tangent)):nlohmann::json(nullptr);
    j["paper_extension_start"]=p.paper_extension_start?nlohmann::json(array(*p.paper_extension_start)):nlohmann::json(nullptr);
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
    p.short_shelf=j.value("short_shelf",false);p.shelf_length=j.value("shelf_length",3.);
    p.unresolved=j.at("unresolved");p.leader=j.at("leader");
    p.leader_bends=j.at("leader_bends").get<decltype(p.leader_bends)>();p.arrow_length=j.at("arrow_length");p.offset_z=j.value("offset_z",0.);p.perpendicular_leader=j.value("perpendicular_leader",false);
    if(j.contains("paper_tangent")&&!j.at("paper_tangent").is_null())p.paper_tangent=vector(j.at("paper_tangent"));
    if(j.contains("paper_extension_start")&&!j.at("paper_extension_start").is_null())p.paper_extension_start=vector(j.at("paper_extension_start"));
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
namespace {
Frame edge_frame(const kernel::ViewerEdge& edge,double t) {
    require(edge.points.size()>1);double length=0;
    for(std::size_t i=1;i<edge.points.size();++i)length+=std::sqrt(dot(sub(edge.points[i],edge.points[i-1]),sub(edge.points[i],edge.points[i-1])));
    require(length>1e-12);double remaining=std::clamp(t,0.,1.)*length;
    for(std::size_t i=1;i<edge.points.size();++i){const auto delta=sub(edge.points[i],edge.points[i-1]);const double span=std::sqrt(dot(delta,delta));if(span<1e-12)continue;
        if(remaining<=span||i+1==edge.points.size()) {const auto x=unit(delta);auto y=cross(std::abs(x.z)<.9?Vec3{0,0,1}:Vec3{0,1,0},x);return {add(edge.points[i-1],multiply(x,std::min(span,remaining))),x,unit(y)};}remaining-=span;}
    throw std::invalid_argument("Invalid symbol edge");
}
}
void attach_to_edge(Placement& value,const kernel::ViewerEdge& edge,const std::string& document,Vec3 contact) {
    require(edge.reference.valid()&&edge.points.size()>1);double total=0,best=1e100,along=0,chosen=0;
    for(std::size_t i=1;i<edge.points.size();++i){const auto d=sub(edge.points[i],edge.points[i-1]);const auto n=dot(d,d);if(n<1e-20)continue;
        const auto t=std::clamp(dot(sub(contact,edge.points[i-1]),d)/n,0.,1.);const auto delta=sub(contact,add(edge.points[i-1],multiply(d,t)));
        if(dot(delta,delta)<best){best=dot(delta,delta);chosen=along+t*std::sqrt(n);}along+=std::sqrt(n);}
    total=along;require(total>1e-12);const auto& r=edge.reference;
    value.reference=Reference{document,r.owner_id,r.semantic_key,r.instance_path,ReferenceKind::Edge};value.reference->surface_parameters={chosen/total,0};
    value.frame=edge_frame(edge,chosen/total);value.unresolved=false;value.validate();
}
void attach_to_point(Placement& value,const kernel::ViewerPoint& point,const std::string& document) {
    require(point.reference.valid());const auto& r=point.reference;
    value.reference=Reference{document,r.owner_id,r.semantic_key,r.instance_path,ReferenceKind::Point};value.frame={point.position};value.unresolved=false;value.validate();
}
bool refresh_surface_attachment(Placement& value,const kernel::ViewerReferenceGeometry& geometry) {
    value.validate();if(!value.reference){value.unresolved=false;return true;}
    const auto& ref=*value.reference;
    const auto matches=[&](const auto& r){return r.owner_id==ref.owner_id&&r.semantic_key==ref.semantic_key&&r.instance_path==ref.instance_path;};
    if(ref.kind==ReferenceKind::Point){for(const auto& p:geometry.points)if(matches(p.reference)){auto frame=value.frame;frame.origin=p.position;value.refresh_reference(frame);return true;}value.refresh_reference({});return false;}
    if(ref.kind==ReferenceKind::Edge){for(const auto& edge:geometry.edges)if(matches(edge.reference)){try{value.refresh_reference(edge_frame(edge,ref.surface_parameters[0]));return true;}catch(const std::invalid_argument&){break;}}value.refresh_reference({});return false;}
    const kernel::SurfaceGeometry* surface=nullptr;
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
