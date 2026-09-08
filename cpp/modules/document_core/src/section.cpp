#include <zima/document/section.hpp>
#include <nlohmann/json.hpp>
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
SectionFrame section_frame(const SectionDefinition& s){
    if(s.sketch.segments.size()!=1 || !s.sketch.circles.empty() || !s.sketch.arcs.empty() || !s.sketch.ellipses.empty() || !s.sketch.elliptical_arcs.empty() || !s.sketch.bsplines.empty() || !s.sketch.import_blocks.empty() || !s.sketch.texts.empty())throw std::invalid_argument("Rovinný řez musí obsahovat jednu přímou čáru.");
    const auto finite=[](V p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
    if(!finite(s.plane_origin)||!finite(s.plane_x)||!finite(s.plane_y)||std::abs(dot(s.plane_x,s.plane_x)-1)>1e-7||std::abs(dot(s.plane_y,s.plane_y)-1)>1e-7||std::abs(dot(s.plane_x,s.plane_y))>1e-7)throw std::invalid_argument("Neplatná rovina skici řezu.");
    const auto& line=s.sketch.segments.front();
    const auto* a=s.sketch.find_point(line.first_point_id);const auto* b=s.sketch.find_point(line.second_point_id);
    if(!a||!b)throw std::invalid_argument("Řezu chybí koncový bod.");
    const auto point=[&](double x,double y){return add(s.plane_origin,add(mul(s.plane_x,x),mul(s.plane_y,y)));};
    const auto origin=point(a->x,a->y),end=point(b->x,b->y);
    const auto h=unit(sub(end,origin));const auto n=mul(unit(cross(h,unit(cross(s.plane_x,s.plane_y)))),s.reversed?-1:1);
    return {origin,h,unit(cross(n,h)),n};
}
void validate_hatch(const HatchStyle& h){
    if(!std::isfinite(h.angle)||!std::isfinite(h.spacing_mm)||!std::isfinite(h.offset_mm)||h.spacing_mm<.1||h.spacing_mm>100||h.pattern<0||h.pattern>2)
        throw std::invalid_argument("Neplatné šrafování: rozteč musí být 0,1 až 100 mm.");
}
std::string serialize_sections(const std::vector<SectionDefinition>& values){
    auto root=nlohmann::json::array();
    for(const auto& s:values){auto comps=nlohmann::json::object();for(const auto& [id,c]:s.components){validate_hatch(c.hatch);comps[id]={{"mode",c.mode},{"custom_hatch",c.custom_hatch},{"angle",c.hatch.angle},{"spacing",c.hatch.spacing_mm},{"offset",c.hatch.offset_mm},{"pattern",c.hatch.pattern}};}
        root.push_back({{"id",s.id},{"name",s.name},{"sketch",nlohmann::json::parse(s.sketch.serialized())},{"plane_origin",{s.plane_origin.x,s.plane_origin.y,s.plane_origin.z}},{"plane_x",{s.plane_x.x,s.plane_x.y,s.plane_x.z}},{"plane_y",{s.plane_y.x,s.plane_y.y,s.plane_y.z}},{"reversed",s.reversed},{"show_plane",s.show_plane},{"show_cut",s.show_cut},{"components",comps}});
    }return root.dump();
}
std::vector<SectionDefinition> parse_sections(const std::string& value){
    std::vector<SectionDefinition> result;std::set<std::string> ids;
    for(const auto& j:nlohmann::json::parse(value)){SectionDefinition s;s.id=j.at("id");s.name=j.at("name");
        if(s.id.empty()||!ids.insert(s.id).second)throw std::runtime_error("Neplatná identita řezu.");
        const auto vec=[](const auto& v){return V{v.at(0),v.at(1),v.at(2)};};s.plane_origin=vec(j.at("plane_origin"));s.plane_x=vec(j.at("plane_x"));s.plane_y=vec(j.at("plane_y"));
        s.sketch=zima::sketcher::Sketch::from_serialized(j.at("sketch").dump());s.reversed=j.at("reversed");s.show_plane=j.at("show_plane");s.show_cut=j.at("show_cut");
        for(const auto& [id,c]:j.at("components").items()){SectionComponent item;item.mode=c.at("mode");item.custom_hatch=c.at("custom_hatch");item.hatch={c.at("angle"),c.at("spacing"),c.at("offset"),c.at("pattern")};validate_hatch(item.hatch);if(item.mode<0||item.mode>2)throw std::runtime_error("Neplatný režim komponenty řezu.");s.components[id]=item;}
        static_cast<void>(section_frame(s));result.push_back(std::move(s));
    }return result;
}
SectionResult calculate_section(const zima::kernel::ViewerMesh& source,const SectionDefinition& s){
    const auto f=section_frame(s);SectionResult result;
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
        SectionPatch patch;patch.component=g;
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
} // namespace zima::document
