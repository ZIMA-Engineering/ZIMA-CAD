#include <zima/document/section.hpp>
#include <nlohmann/json.hpp>
#include <zima/document/placement_json.hpp>
#include <zima/document/part_document.hpp>
#include <zima/kernel/stable_id.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <tuple>
#include <stdexcept>

namespace zima::document {
namespace {
using V=zima::kernel::Vec3;
V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V mul(V a,double s){return {a.x*s,a.y*s,a.z*s};}
double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
V unit(V a){const auto n=std::sqrt(dot(a,a));if(!std::isfinite(n)||n<1e-12)throw std::invalid_argument("Řez potřebuje nenulovou čáru a platnou orientaci.");return mul(a,1/n);}
V mix(V a,V b,double t){return add(a,mul(sub(b,a),t));}
struct P {double x,y;};
using Segment=std::array<P,2>;
P project(V a,const SectionFrame& f){a=sub(a,f.origin);return {dot(a,f.horizontal),dot(a,f.vertical)};}
V world(P p,const SectionFrame& f){return add(f.origin,add(mul(f.horizontal,p.x),mul(f.vertical,p.y)));}
std::string group(const std::string& owner,const std::string& path,const SectionDefinition& s){
    if(!path.empty())return path;
    const auto found=s.body_owners.find(owner);return found==s.body_owners.end()?std::string{}:found->second;
}
int mode(const SectionDefinition& s,const std::string& g){const auto it=s.components.find(g);return it==s.components.end()?0:it->second.mode;}
void triangle(zima::kernel::ViewerMesh& mesh,const std::array<V,3>& t,zima::kernel::FaceReference ref={}){
    const auto n=cross(sub(t[1],t[0]),sub(t[2],t[0]));if(dot(n,n)<1e-24)return;
    const auto base=static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(),t.begin(),t.end());
    mesh.triangles.insert(mesh.triangles.end(),{base,base+1,base+2});mesh.triangle_references.push_back(std::move(ref));
}
// Triangulate parallel strips with even/odd material occupancy. Holes and
// disconnected regions are handled together without filling their interiors.
std::vector<std::array<P,3>> fill(const std::vector<Segment>& edges,double eps){
    std::vector<double> levels;for(const auto& e:edges)for(const auto& p:e)levels.push_back(p.y);
    std::ranges::sort(levels);levels.erase(std::unique(levels.begin(),levels.end(),[&](double a,double b){return std::abs(a-b)<eps;}),levels.end());
    std::vector<std::array<P,3>> result;
    for(std::size_t i=1;i<levels.size();++i){
        const double lo=levels[i-1],hi=levels[i],mid=(lo+hi)*.5;if(hi-lo<eps)continue;
        struct Hit{double x,lo,hi;};std::vector<Hit> hits;
        for(const auto& e:edges)if(mid>std::min(e[0].y,e[1].y)&&mid<std::max(e[0].y,e[1].y)){
            const auto x=[&](double y){return e[0].x+(e[1].x-e[0].x)*(y-e[0].y)/(e[1].y-e[0].y);};hits.push_back({x(mid),x(lo),x(hi)});
        }
        std::ranges::sort(hits,{},&Hit::x);
        if(hits.size()%2)throw std::runtime_error("Řez nemá uzavřený obrys. Regenerujte zdrojovou geometrii.");
        for(std::size_t k=1;k<hits.size();k+=2){const auto& a=hits[k-1];const auto& b=hits[k];
            if(b.x-a.x<eps)continue;
            result.push_back({P{a.lo,lo},P{b.lo,lo},P{b.hi,hi}});
            result.push_back({P{a.lo,lo},P{b.hi,hi},P{a.hi,hi}});
        }
    }
    return result;
}
}
SectionDefinition create_section(){
    SectionDefinition s;s.id=zima::kernel::make_stable_id();
    s.container_origin=create_container_origin(s.id);s.sketch=zima::sketcher::Sketch::create_default();
    reframe_section(s);return s;
}
void reframe_section(SectionDefinition& s){
    const auto& p=s.placement;
    const auto rotate=[&](V v){
        const double x=p.rotation_x*std::numbers::pi/180,y=p.rotation_y*std::numbers::pi/180,z=p.rotation_z*std::numbers::pi/180;
        v={v.x,v.y*std::cos(x)-v.z*std::sin(x),v.y*std::sin(x)+v.z*std::cos(x)};
        v={v.x*std::cos(y)+v.z*std::sin(y),v.y,-v.x*std::sin(y)+v.z*std::cos(y)};
        return V{v.x*std::cos(z)-v.y*std::sin(z),v.x*std::sin(z)+v.y*std::cos(z),v.z};
    };
    s.sketch.plane_reference_owner_id.clear();s.sketch.refresh_default_frame();
    s.plane_origin=add(V{p.x,p.y,p.z},rotate(s.sketch.resolved_origin));
    s.plane_x=rotate(s.sketch.resolved_x_axis);s.plane_y=rotate(s.sketch.resolved_y_axis);
    s.sketch.owner_container_id=s.id;s.sketch.plane_reference_owner_id=s.container_origin.id;
    s.sketch.resolved_origin=s.plane_origin;s.sketch.resolved_x_axis=s.plane_x;s.sketch.resolved_y_axis=s.plane_y;
    s.sketch.resolved_normal=cross(s.plane_x,s.plane_y);
}
void resolve_section_placements(std::vector<SectionDefinition>& sections,const zima::kernel::ViewerReferenceGeometry& geometry){
    for(auto& section:sections){static_cast<void>(resolve_placement(section.placement,geometry));reframe_section(section);}
}
std::vector<std::array<double,2>> section_path(const SectionDefinition& s){
    const auto profile=[](const auto& curves){return std::ranges::any_of(curves,[](const auto& c){return !c.construction;});};
    if(profile(s.sketch.circles)||profile(s.sketch.arcs)||profile(s.sketch.ellipses)||profile(s.sketch.elliptical_arcs)||profile(s.sketch.bsplines)||!s.sketch.import_blocks.empty()||!s.sketch.texts.empty())
        throw std::invalid_argument("Řez potřebuje otevřenou čáru nebo lomenou čáru z úseček.");
    using XY=std::array<double,2>;std::vector<XY> points;std::vector<std::array<std::size_t,2>> edges;
    const auto index=[&](const auto* p){
        if(!p||!std::isfinite(p->x)||!std::isfinite(p->y))throw std::invalid_argument("Řezu chybí platný koncový bod.");
        for(std::size_t i=0;i<points.size();++i)if(std::hypot(points[i][0]-p->x,points[i][1]-p->y)<1e-7)return i;
        points.push_back({p->x,p->y});return points.size()-1;
    };
    for(const auto& line:s.sketch.segments){if(line.construction)continue;
        auto a=index(s.sketch.find_point(line.first_point_id)),b=index(s.sketch.find_point(line.second_point_id));
        if(a==b)throw std::invalid_argument("Řez obsahuje nulovou úsečku.");edges.push_back({a,b});
    }
    if(edges.empty())throw std::invalid_argument("Nakreslete ve skice otevřenou čáru řezu.");
    std::vector<std::vector<std::size_t>> adjacent(points.size());
    for(const auto& e:edges){adjacent[e[0]].push_back(e[1]);adjacent[e[1]].push_back(e[0]);}
    std::vector<std::size_t> ends;for(std::size_t i=0;i<adjacent.size();++i){if(adjacent[i].size()==1)ends.push_back(i);else if(adjacent[i].size()!=2)throw std::invalid_argument("Čára řezu se nesmí větvit.");}
    if(ends.size()!=2)throw std::invalid_argument("Čára řezu musí být otevřená a souvislá.");
    std::vector<std::size_t> order;std::size_t prev=points.size(),cur=ends.front();
    while(true){order.push_back(cur);std::size_t next=points.size();for(auto n:adjacent[cur])if(n!=prev){next=n;break;}if(next==points.size())break;prev=cur;cur=next;if(order.size()>points.size())throw std::invalid_argument("Čára řezu obsahuje smyčku.");}
    if(order.size()!=points.size())throw std::invalid_argument("Čára řezu musí být souvislá.");
    const auto first=std::ranges::find(order,edges.front()[0]),second=std::ranges::find(order,edges.front()[1]);
    if(first>second)std::ranges::reverse(order);
    std::vector<XY> result;for(auto i:order)result.push_back(points[i]);
    // Consecutive collinear segments describe one plane, not a cap seam.
    for(std::size_t i=1;i+1<result.size();){auto a=result[i-1],b=result[i],c=result[i+1];const double dx=b[0]-a[0],dy=b[1]-a[1],ex=c[0]-b[0],ey=c[1]-b[1];
        if(std::abs(dx*ey-dy*ex)<1e-9*std::max(1.,std::hypot(dx,dy)*std::hypot(ex,ey))&&dx*ex+dy*ey>0)result.erase(result.begin()+i);else ++i;
    }return result;
}
std::vector<SectionFrame> section_frames(const SectionDefinition& s){
    const auto finite=[](V p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
    if(!finite(s.plane_origin)||!finite(s.plane_x)||!finite(s.plane_y)||std::abs(dot(s.plane_x,s.plane_x)-1)>1e-7||std::abs(dot(s.plane_y,s.plane_y)-1)>1e-7||std::abs(dot(s.plane_x,s.plane_y))>1e-7)throw std::invalid_argument("Neplatné umístění skici řezu.");
    const auto path=section_path(s);std::vector<SectionFrame> frames;
    for(std::size_t i=1;i<path.size();++i){const auto& a=path[i-1];const auto& b=path[i];
        const auto origin=add(s.plane_origin,add(mul(s.plane_x,a[0]),mul(s.plane_y,a[1])));
        const auto h=unit(add(mul(s.plane_x,b[0]-a[0]),mul(s.plane_y,b[1]-a[1])));
        const auto n=mul(unit(cross(h,cross(s.plane_x,s.plane_y))),s.reversed?-1:1);
        frames.push_back({origin,h,unit(cross(n,h)),n});
    }return frames;
}
SectionFrame section_frame(const SectionDefinition& s){return section_frames(s).front();}
void validate_hatch(const HatchStyle& h){
    if(!std::isfinite(h.angle)||!std::isfinite(h.spacing_mm)||!std::isfinite(h.offset_mm)||h.spacing_mm<.1||h.spacing_mm>100||h.pattern<0||h.pattern>2)
        throw std::invalid_argument("Neplatné šrafování: rozteč musí být 0,1 až 100 mm.");
}
std::string serialize_sections(const std::vector<SectionDefinition>& values){
    auto root=nlohmann::json::array();
    for(const auto& s:values){auto comps=nlohmann::json::object();for(const auto& [id,c]:s.components){validate_hatch(c.hatch);comps[id]={{"mode",c.mode},{"custom_hatch",c.custom_hatch},{"angle",c.hatch.angle},{"spacing",c.hatch.spacing_mm},{"offset",c.hatch.offset_mm},{"pattern",c.hatch.pattern}};}
        auto children=nlohmann::json::array();for(const auto& c:s.container_origin.children)children.push_back({{"id",c.id},{"parent",c.parent_id},{"name",c.name},{"kind",static_cast<int>(c.kind)},{"key",c.key},{"locked",c.locked}});
        const auto& o=s.container_origin;
        root.push_back({{"placement",s.placement},{"origin",{{"id",o.id},{"parent",o.parent_id},{"name",o.name},{"locked",o.locked},{"children",children}}},{"id",s.id},{"name",s.name},{"sketch",nlohmann::json::parse(s.sketch.serialized())},{"plane_origin",{s.plane_origin.x,s.plane_origin.y,s.plane_origin.z}},{"plane_x",{s.plane_x.x,s.plane_x.y,s.plane_x.z}},{"plane_y",{s.plane_y.x,s.plane_y.y,s.plane_y.z}},{"reversed",s.reversed},{"show_plane",s.show_plane},{"show_cut",s.show_cut},{"components",comps}});
    }return root.dump();
}
std::vector<SectionDefinition> parse_sections(const std::string& value){
    std::vector<SectionDefinition> result;std::set<std::string> ids;
    for(const auto& j:nlohmann::json::parse(value)){SectionDefinition s;s.id=j.at("id");s.name=j.at("name");
        if(s.id.empty()||!ids.insert(s.id).second)throw std::runtime_error("Neplatná identita řezu.");
        const auto vec=[](const auto& v){return V{v.at(0),v.at(1),v.at(2)};};s.plane_origin=vec(j.at("plane_origin"));s.plane_x=vec(j.at("plane_x"));s.plane_y=vec(j.at("plane_y"));
        s.placement=j.at("placement").get<Placement>();const auto& o=j.at("origin");
        s.container_origin={o.at("id"),o.at("parent"),o.at("name"),{},o.at("locked")};
        for(const auto& c:o.at("children"))s.container_origin.children.push_back({c.at("id"),c.at("parent"),c.at("name"),static_cast<OriginChildKind>(c.at("kind").get<int>()),c.at("key"),c.at("locked")});
        s.sketch=zima::sketcher::Sketch::from_serialized(j.at("sketch").dump());reframe_section(s);s.reversed=j.at("reversed");s.show_plane=j.at("show_plane");s.show_cut=j.at("show_cut");
        for(const auto& [id,c]:j.at("components").items()){SectionComponent item;item.mode=c.at("mode");item.custom_hatch=c.at("custom_hatch");item.hatch={c.at("angle"),c.at("spacing"),c.at("offset"),c.at("pattern")};validate_hatch(item.hatch);if(item.mode<0||item.mode>2)throw std::runtime_error("Neplatný režim komponenty řezu.");s.components[id]=item;}
        static_cast<void>(section_frame(s));result.push_back(std::move(s));
    }return result;
}
static SectionResult planar_section(const zima::kernel::ViewerMesh& source,const SectionDefinition& s,const SectionFrame& f){
    SectionResult result;
    double size=1;for(const auto& p:source.vertices)size=std::max(size,std::sqrt(dot(sub(p,f.origin),sub(p,f.origin))));
    const double eps=std::max(1e-9,size*1e-9);
    const auto distance=[&](V p){double d=dot(sub(p,f.origin),f.normal);return std::abs(d)<eps?0:d;};
    std::map<std::string,bool> interior;
    for(std::size_t i=0;i+2<source.triangles.size();i+=3){const auto ref=i/3<source.triangle_references.size()?source.triangle_references[i/3]:zima::kernel::FaceReference{};if(ref.is_thread_surface())continue;
        auto& inside=interior[group(ref.owner_id,ref.instance_path,s)];for(int k=0;k<3;++k)inside|=distance(source.vertices.at(source.triangles[i+k]))<0;
    }
    std::map<std::string,std::vector<std::array<V,2>>> contours;
    for(std::size_t i=0;i+2<source.triangles.size();i+=3){
        std::array<V,3> t{source.vertices.at(source.triangles[i]),source.vertices.at(source.triangles[i+1]),source.vertices.at(source.triangles[i+2])};
        const auto ref=i/3<source.triangle_references.size()?source.triangle_references[i/3]:zima::kernel::FaceReference{};
        const auto g=group(ref.owner_id,ref.instance_path,s);
        if(mode(s,g)==2){triangle(result.mesh,t,ref);continue;}
        if(!interior[g])continue;
        std::vector<V> clipped,intersections;
        bool removed=false,kept=false;
        for(const auto& p:t){removed|=distance(p)>0;kept|=distance(p)<0;}
        for(int k=0;k<3;++k){auto a=t[k],b=t[(k+1)%3];const double da=distance(a),db=distance(b);
            if(da<=0)clipped.push_back(a);
            if((da<0&&db>0)||(da>0&&db<0)){auto p=mix(a,b,da/(da-db));clipped.push_back(p);intersections.push_back(p);}
            if(da==0)intersections.push_back(a);
        }
        for(std::size_t k=2;k<clipped.size();++k)triangle(result.mesh,{clipped[0],clipped[k-1],clipped[k]},ref);
        if(!ref.is_thread_surface() && removed && (kept || intersections.size()==2) && intersections.size()==2 && dot(sub(intersections[0],intersections[1]),sub(intersections[0],intersections[1]))>eps*eps)
            contours[g].push_back({intersections[0],intersections[1]});
    }
    for(const auto& edge:source.edges){
        if(mode(s,group(edge.reference.owner_id,edge.reference.instance_path,s))==2){result.mesh.edges.push_back(edge);continue;}
        if(!interior[group(edge.reference.owner_id,edge.reference.instance_path,s)])continue;
        for(std::size_t k=1;k<edge.points.size();++k){auto a=edge.points[k-1],b=edge.points[k];double da=distance(a),db=distance(b);if(da>0&&db>0)continue;
            if(da>0)a=mix(a,b,da/(da-db));else if(db>0)b=mix(a,b,da/(da-db));
            auto clipped=edge;clipped.points={a,b};clipped.edge_treatment_side_directions.clear();
            if(edge.edge_treatment_side_directions.size()==2){for(const auto& side:edge.edge_treatment_side_directions)if(side.size()==edge.points.size())clipped.edge_treatment_side_directions.push_back({side[k-1],side[k]});}
            result.mesh.edges.push_back(std::move(clipped));
        }
    }
    for(auto& [g,segments]:contours){
        SectionPatch patch;patch.component=g;patch.frame=f;
        using Key=std::array<long long,6>;std::map<Key,std::array<V,2>> unique;
        for(const auto& e:segments){auto a=project(e[0],f),b=project(e[1],f);if(std::tie(b.x,b.y)<std::tie(a.x,a.y))std::swap(a,b);
            Key key{std::llround(a.x/eps),std::llround(a.y/eps),0,std::llround(b.x/eps),std::llround(b.y/eps),0};unique.try_emplace(key,e);}
        std::vector<Segment> planar;for(const auto& [key,e]:unique){patch.boundary.push_back(e);planar.push_back({project(e[0],f),project(e[1],f)});
            zima::kernel::ViewerEdge rim;rim.points={e[0],e[1]};result.mesh.edges.push_back(std::move(rim));}
        for(const auto& t:fill(planar,eps)){std::array<V,3> cap{world(t[0],f),world(t[1],f),world(t[2],f)};patch.triangles.push_back(cap);triangle(result.mesh,cap);}
        result.patches.push_back(std::move(patch));
    }
    for(const auto& point:source.points)if((distance(point.position)<=0 && interior[group(point.reference.owner_id,point.reference.instance_path,s)]) || mode(s,group(point.reference.owner_id,point.reference.instance_path,s))==2)result.mesh.points.push_back(point);
    // Selection uses only surviving persisted geometry. Caps/rims deliberately
    // have invalid references and never become placement reference owners.
    result.mesh.original_references={result.mesh.vertices,result.mesh.triangles,result.mesh.triangle_references,result.mesh.edges,result.mesh.points,{}};
    return result;
}

namespace {
double cross2(P a,P b,P c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
bool near(P a,P b,double eps){return std::hypot(a.x-b.x,a.y-b.y)<eps;}
// The directed chain, extended at both ends, divides a finite envelope of the
// source. Its left-hand polygon is a through-all cutting prism. Triangulating
// that polygon handles concave and offset cuts without intersecting halfspaces.
std::vector<std::array<P,3>> cut_region(const zima::kernel::ViewerMesh& source,const SectionDefinition& s,double eps){
    std::vector<P> path;for(auto p:section_path(s))path.push_back({p[0],p[1]});if(s.reversed)std::ranges::reverse(path);
    double xmin=path.front().x,xmax=xmin,ymin=path.front().y,ymax=ymin;
    const auto include=[&](P p){xmin=std::min(xmin,p.x);xmax=std::max(xmax,p.x);ymin=std::min(ymin,p.y);ymax=std::max(ymax,p.y);};
    for(auto p:path)include(p);for(auto v:source.vertices){v=sub(v,s.plane_origin);include({dot(v,s.plane_x),dot(v,s.plane_y)});}
    const double pad=4*std::max({1.,xmax-xmin,ymax-ymin});xmin-=pad;xmax+=pad;ymin-=pad;ymax+=pad;
    const double width=xmax-xmin,height=ymax-ymin,perimeter=2*(width+height);
    const auto extend=[&](P p,P away){const P d{p.x-away.x,p.y-away.y};double t=1e100;
        if(d.x>eps)t=std::min(t,(xmax-p.x)/d.x);if(d.x<-eps)t=std::min(t,(xmin-p.x)/d.x);
        if(d.y>eps)t=std::min(t,(ymax-p.y)/d.y);if(d.y<-eps)t=std::min(t,(ymin-p.y)/d.y);
        return P{p.x+d.x*t,p.y+d.y*t};};
    const auto perimeter_pos=[&](P p){if(std::abs(p.y-ymin)<eps*10)return p.x-xmin;if(std::abs(p.x-xmax)<eps*10)return width+p.y-ymin;if(std::abs(p.y-ymax)<eps*10)return width+height+xmax-p.x;return 2*width+height+ymax-p.y;};
    const auto entry=extend(path.front(),path[1]),exit=extend(path.back(),path[path.size()-2]);
    std::vector<P> polygon{entry};polygon.insert(polygon.end(),path.begin(),path.end());polygon.push_back(exit);
    const double from=perimeter_pos(exit);double to=perimeter_pos(entry);if(to<=from)to+=perimeter;
    const std::array<P,4> corners{P{xmin,ymin},P{xmax,ymin},P{xmax,ymax},P{xmin,ymax}};
    const std::array<double,4> offsets{0,width,width+height,2*width+height};
    for(int lap=0;lap<2;++lap)for(std::size_t i=0;i<4;++i)if(offsets[i]+lap*perimeter>from+eps&&offsets[i]+lap*perimeter<to-eps)polygon.push_back(corners[i]);
    // Remove redundant collinear vertices, but reject overlapping/backtracking
    // segments and any self-intersection (including the extended end rays).
    for(bool changed=true;changed&&polygon.size()>3;){changed=false;for(std::size_t i=0;i<polygon.size();++i){auto a=polygon[(i+polygon.size()-1)%polygon.size()],b=polygon[i],c=polygon[(i+1)%polygon.size()];
        if(near(a,b,eps)||(std::abs(cross2(a,b,c))<eps*std::hypot(c.x-a.x,c.y-a.y)&&(b.x-a.x)*(c.x-b.x)+(b.y-a.y)*(c.y-b.y)>=0)){polygon.erase(polygon.begin()+i);changed=true;break;}}}
    const auto on=[&](P a,P b,P c){return std::abs(cross2(a,b,c))<=eps*std::max(1.,std::hypot(b.x-a.x,b.y-a.y))&&c.x>=std::min(a.x,b.x)-eps&&c.x<=std::max(a.x,b.x)+eps&&c.y>=std::min(a.y,b.y)-eps&&c.y<=std::max(a.y,b.y)+eps;};
    for(std::size_t i=0;i<polygon.size();++i)for(std::size_t j=i+1;j<polygon.size();++j){if(j==i+1||(i==0&&j+1==polygon.size()))continue;
        auto a=polygon[i],b=polygon[(i+1)%polygon.size()],c=polygon[j],d=polygon[(j+1)%polygon.size()];
        if((cross2(a,b,c)*cross2(a,b,d)<0&&cross2(c,d,a)*cross2(c,d,b)<0)||on(a,b,c)||on(a,b,d)||on(c,d,a)||on(c,d,b))throw std::invalid_argument("Čára řezu ani prodloužení jejích konců se nesmí křížit.");}
    double signed_area=0;for(std::size_t i=0;i<polygon.size();++i){auto a=polygon[i],b=polygon[(i+1)%polygon.size()];signed_area+=a.x*b.y-a.y*b.x;}
    if(signed_area<0)std::ranges::reverse(polygon);
    std::vector<std::array<P,3>> result;
    while(polygon.size()>3){bool found=false;
        for(std::size_t i=0;i<polygon.size();++i){const std::size_t before=(i+polygon.size()-1)%polygon.size(),after=(i+1)%polygon.size();auto a=polygon[before],b=polygon[i],c=polygon[after];if(cross2(a,b,c)<=eps*eps)continue;
            bool occupied=false;for(std::size_t k=0;k<polygon.size();++k)if(k!=before&&k!=i&&k!=after){auto p=polygon[k];if(cross2(a,b,p)>=-eps*eps&&cross2(b,c,p)>=-eps*eps&&cross2(c,a,p)>=-eps*eps){occupied=true;break;}}
            if(occupied)continue;result.push_back({a,b,c});polygon.erase(polygon.begin()+i);found=true;break;
        }if(!found)throw std::invalid_argument("Čára řezu nevytváří jednoznačnou stranu řezu.");
    }
    result.push_back({polygon[0],polygon[1],polygon[2]});return result;
}
struct ClipPlane {V origin,normal;};
std::vector<V> clip_polygon(std::vector<V> polygon,const ClipPlane& plane,double eps){
    std::vector<V> result;if(polygon.empty())return result;
    const auto distance=[&](V p){auto d=dot(sub(p,plane.origin),plane.normal);return std::abs(d)<eps?0:d;};
    for(std::size_t i=0;i<polygon.size();++i){auto a=polygon[i],b=polygon[(i+1)%polygon.size()];double da=distance(a),db=distance(b);if(da<=0)result.push_back(a);if((da<0&&db>0)||(da>0&&db<0))result.push_back(mix(a,b,da/(da-db)));}return result;
}
std::optional<std::array<V,2>> clip_edge(V a,V b,const std::vector<ClipPlane>& planes,double eps){
    for(const auto& plane:planes){double da=dot(sub(a,plane.origin),plane.normal),db=dot(sub(b,plane.origin),plane.normal);if(std::abs(da)<eps)da=0;if(std::abs(db)<eps)db=0;if(da>0&&db>0)return {};if(da>0)a=mix(a,b,da/(da-db));else if(db>0)b=mix(a,b,da/(da-db));}
    if(dot(sub(b,a),sub(b,a))<=eps*eps)return {};return std::array<V,2>{a,b};
}
std::vector<std::array<double,2>> merge_intervals(std::vector<std::array<double,2>> intervals,double eps){
    std::ranges::sort(intervals);std::vector<std::array<double,2>> result;for(auto interval:intervals){if(result.empty()||result.back()[1]<interval[0]-eps)result.push_back(interval);else result.back()[1]=std::max(result.back()[1],interval[1]);}return result;
}
}
SectionResult calculate_section(const zima::kernel::ViewerMesh& source,const SectionDefinition& s){
    if(!s.placement.reference_valid)throw std::invalid_argument("Reference umístění řezu není dostupná. Upravte umístění řezu.");
    const auto frames=section_frames(s);if(frames.size()==1)return planar_section(source,s,frames.front());
    double size=1;for(auto v:source.vertices)size=std::max(size,std::sqrt(dot(sub(v,s.plane_origin),sub(v,s.plane_origin))));const double eps=size*1e-9;
    const auto region=cut_region(source,s,eps);std::vector<std::vector<ClipPlane>> prisms;
    const V vertical=unit(cross(s.plane_x,s.plane_y));
    for(const auto& t:region){std::vector<ClipPlane> planes;for(int i=0;i<3;++i){auto a=t[i],b=t[(i+1)%3];const auto h=unit(add(mul(s.plane_x,b.x-a.x),mul(s.plane_y,b.y-a.y)));planes.push_back({add(s.plane_origin,add(mul(s.plane_x,a.x),mul(s.plane_y,a.y))),cross(h,vertical)});}prisms.push_back(std::move(planes));}
    SectionResult result;
    for(std::size_t i=0;i+2<source.triangles.size();i+=3){const auto ref=i/3<source.triangle_references.size()?source.triangle_references[i/3]:zima::kernel::FaceReference{};
        std::array<V,3> t{source.vertices.at(source.triangles[i]),source.vertices.at(source.triangles[i+1]),source.vertices.at(source.triangles[i+2])};
        if(mode(s,group(ref.owner_id,ref.instance_path,s))==2){triangle(result.mesh,t,ref);continue;}
        // Split every source triangle by the SAME planes. Independent prism
        // clipping creates T-junctions at ear diagonals which the silhouette
        // projector would mistake for visible model edges.
        std::vector<std::vector<V>> pieces{std::vector<V>(t.begin(),t.end())};
        for(const auto& frame:frames){std::vector<std::vector<V>> next;
            for(auto& polygon:pieces){bool positive=false,negative=false;for(auto p:polygon){const double d=dot(sub(p,frame.origin),frame.normal);positive|=d>eps;negative|=d<-eps;}
                if(positive&&negative){next.push_back(clip_polygon(polygon,{frame.origin,frame.normal},eps));next.push_back(clip_polygon(std::move(polygon),{frame.origin,mul(frame.normal,-1)},eps));}
                else next.push_back(std::move(polygon));
            }pieces=std::move(next);
        }
        const auto outward=cross(sub(t[1],t[0]),sub(t[2],t[0]));const double magnitude=std::sqrt(dot(outward,outward));if(magnitude<eps*eps)continue;
        for(const auto& polygon:pieces){if(polygon.size()<3)continue;V center{};for(auto p:polygon)center=add(center,p);center=mul(center,1./polygon.size());
            const auto inside=sub(center,mul(outward,4*eps/magnitude));
            if(std::ranges::none_of(prisms,[&](const auto& planes){return std::ranges::all_of(planes,[&](const auto& p){return dot(sub(inside,p.origin),p.normal)<=eps;});}))continue;
            // A center fan preserves every boundary subdivision, including
            // collinear points, so adjacent source triangles remain conforming.
            for(std::size_t k=0;k<polygon.size();++k)triangle(result.mesh,{center,polygon[k],polygon[(k+1)%polygon.size()]},ref);
        }
    }
    for(const auto& e:source.edges){if(mode(s,group(e.reference.owner_id,e.reference.instance_path,s))==2){result.mesh.edges.push_back(e);continue;}
        for(std::size_t i=1;i<e.points.size();++i){const auto a=e.points[i-1],b=e.points[i],d=sub(b,a);const double length2=dot(d,d);if(length2<eps*eps)continue;
            std::vector<std::array<double,2>> intervals;for(const auto& planes:prisms)if(auto edge=clip_edge(a,b,planes,eps))intervals.push_back({dot(sub((*edge)[0],a),d)/length2,dot(sub((*edge)[1],a),d)/length2});
            for(auto range:merge_intervals(std::move(intervals),eps/std::sqrt(length2))){auto edge=e;edge.points={mix(a,b,range[0]),mix(a,b,range[1])};edge.edge_treatment_side_directions.clear();
                for(const auto& side:e.edge_treatment_side_directions)if(side.size()==e.points.size())edge.edge_treatment_side_directions.push_back({mix(side[i-1],side[i],range[0]),mix(side[i-1],side[i],range[1])});result.mesh.edges.push_back(std::move(edge));}
        }
    }
    const auto path=section_path(s);
    for(std::size_t leg=0;leg<frames.size();++leg){const auto& f=frames[leg];const double length=std::hypot(path[leg+1][0]-path[leg][0],path[leg+1][1]-path[leg][1]);
        std::vector<ClipPlane> limits;if(leg>0)limits.push_back({f.origin,mul(f.horizontal,-1)});if(leg+1<frames.size())limits.push_back({add(f.origin,mul(f.horizontal,length)),f.horizontal});
        // Each planar contour is intersected with this leg's interval. Closing
        // bars at bends come from even/odd material intervals, preserving holes.
        for(const auto& full:planar_section(source,s,f).patches){SectionPatch patch;patch.component=full.component;patch.frame=f;std::vector<Segment> edges;
            for(auto e:full.boundary)if(auto clipped=clip_edge(e[0],e[1],limits,eps))edges.push_back({project((*clipped)[0],f),project((*clipped)[1],f)});
            for(const auto& limit:limits){const double x=dot(sub(limit.origin,f.origin),f.horizontal);std::vector<double> ys;
                for(auto e:full.boundary){auto a=project(e[0],f),b=project(e[1],f);if(x>=std::min(a.x,b.x)&&x<std::max(a.x,b.x))ys.push_back(a.y+(b.y-a.y)*(x-a.x)/(b.x-a.x));}
                std::ranges::sort(ys);for(std::size_t i=1;i<ys.size();i+=2)if(ys[i]-ys[i-1]>eps)edges.push_back({P{x,ys[i-1]},P{x,ys[i]}});
            }
            for(auto t:fill(edges,eps)){std::array<V,3> cap{world(t[0],f),world(t[1],f),world(t[2],f)};if(dot(cross(sub(cap[1],cap[0]),sub(cap[2],cap[0])),cross(sub(cap[1],cap[0]),sub(cap[2],cap[0])))>eps*eps*eps*eps){patch.triangles.push_back(cap);triangle(result.mesh,cap);}}
            if(patch.triangles.empty())continue;
            for(auto e:edges){const auto a=world(e[0],f),b=world(e[1],f);patch.boundary.push_back({a,b});zima::kernel::ViewerEdge rim;rim.points={a,b};result.mesh.edges.push_back(std::move(rim));}
            result.patches.push_back(std::move(patch));
        }
    }
    for(const auto& point:source.points){bool keep=mode(s,group(point.reference.owner_id,point.reference.instance_path,s))==2;for(const auto& planes:prisms)keep|=std::ranges::all_of(planes,[&](const auto& p){return dot(sub(point.position,p.origin),p.normal)<=eps;});if(keep)result.mesh.points.push_back(point);}
    result.mesh.original_references={result.mesh.vertices,result.mesh.triangles,result.mesh.triangle_references,result.mesh.edges,result.mesh.points,{}};return result;
}
std::vector<zima::kernel::ViewerEdge> section_hatch_lines(const SectionPatch& patch,const SectionFrame& f,const HatchStyle& style,double scale){
    validate_hatch(style);if(!std::isfinite(scale)||scale<=0)throw std::invalid_argument("Neplatné měřítko řezu.");
    std::vector<zima::kernel::ViewerEdge> output;
    for(int pass=0;pass<(style.pattern==1?2:1);++pass){
        const double angle=(style.angle+90*pass)*std::numbers::pi/180,co=std::cos(angle),si=std::sin(angle);
        const auto rotate=[&](P p){return P{p.x*co+p.y*si,-p.x*si+p.y*co};};
        const auto inverse=[&](P p){return P{p.x*co-p.y*si,p.x*si+p.y*co};};
        std::vector<Segment> edges;double lo=1e100,hi=-1e100;
        for(const auto& e:patch.boundary){Segment seg{rotate(project(e[0],f)),rotate(project(e[1],f))};edges.push_back(seg);for(const auto& p:seg){lo=std::min(lo,p.y);hi=std::max(hi,p.y);}}
        const double spacing=style.spacing_mm/scale,offset=style.offset_mm/scale;
        const double count=(hi-lo)/spacing;if(count>100000)throw std::runtime_error("Příliš husté šrafování. Zvětšete rozteč.");
        for(double y=std::ceil((lo-offset)/spacing)*spacing+offset;y<hi;y+=spacing){
            std::vector<double> xs;for(const auto& e:edges)if(y>=std::min(e[0].y,e[1].y)&&y<std::max(e[0].y,e[1].y))xs.push_back(e[0].x+(e[1].x-e[0].x)*(y-e[0].y)/(e[1].y-e[0].y));
            std::ranges::sort(xs);for(std::size_t i=1;i<xs.size();i+=2){zima::kernel::ViewerEdge edge;edge.points={world(inverse({xs[i-1],y}),f),world(inverse({xs[i],y}),f)};output.push_back(std::move(edge));}
        }
    }return output;
}
HatchStyle section_component_hatch(const SectionDefinition& section,const std::string& component){
    const auto setting=section.components.find(component);
    if(setting!=section.components.end()&&setting->second.custom_hatch)return setting->second.hatch;
    HatchStyle style;const auto named=section.component_names.find(component);
    if(named!=section.component_names.end())style.angle+=(std::distance(section.component_names.begin(),named)%2)*90;
    return style;
}
zima::kernel::ViewerMesh section_display_mesh(SectionResult cut,const SectionDefinition& section){
    for(const auto& patch:cut.patches){
        const auto setting=section.components.find(patch.component);
        if(setting!=section.components.end()&&setting->second.mode!=0)continue;
        for(auto edge:section_hatch_lines(patch,patch.frame,section_component_hatch(section,patch.component),1)){
            edge.color="#00C000";
            // Move helpers just outside the cap to avoid depth-buffer fighting.
            for(auto& p:edge.points){p.x+=patch.frame.normal.x*1e-5;p.y+=patch.frame.normal.y*1e-5;p.z+=patch.frame.normal.z*1e-5;}
            cut.mesh.edges.push_back(std::move(edge));
        }
    }
    return std::move(cut.mesh);
}
} // namespace zima::document
