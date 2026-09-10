#include <zima/viewer/measurement.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <tuple>

namespace zima::viewer {
namespace {
using P = kernel::Vec3;
using K = kernel::MeasurementKind;
using V = kernel::MeasurementValue;
P add(P a,P b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
P sub(P a,P b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
P mul(P a,double s){return {a.x*s,a.y*s,a.z*s};}
double dot(P a,P b){return a.x*b.x+a.y*b.y+a.z*b.z;}
P cross(P a,P b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double norm2(P a){return dot(a,a);}
double norm(P a){return std::sqrt(norm2(a));}
P unit(P a){const auto n=norm(a);return n>1e-15?mul(a,1/n):P{};}
double area(const std::array<P,3>& t){return norm(cross(sub(t[1],t[0]),sub(t[2],t[0])))*.5;}
bool path_matches(const std::string& path,const std::string& owner){
    return path==owner || (!owner.empty()&&path.starts_with(owner));
}
bool matches(const auto& ref,const kernel::MeasurementReference& request){
    if(request.kind==K::Object) {
        if(request.owner_id.empty())return path_matches(ref.instance_path,request.instance_path);
        return ref.instance_path==request.instance_path &&
            (ref.owner_id==request.owner_id || ref.owner_id==request.owner_id+":entity");
    }
    return ref.owner_id==request.owner_id && ref.semantic_key==request.semantic_key &&
        ref.instance_path==request.instance_path;
}
bool straight(const std::vector<P>& points){
    if(points.size()<3)return true;
    const auto d=sub(points.back(),points.front());
    const auto scale=norm(d);
    if(scale<1e-12)return false;
    return std::ranges::all_of(points,[&](P p){return norm(cross(sub(p,points.front()),d))<=1e-9*scale*std::max(1.0,scale);});
}
bool closed_surface(const std::vector<std::array<P,3>>& triangles){
    if(triangles.size()<4)return false;
    P lo=triangles.front()[0],hi=lo;
    for(const auto& t:triangles)for(P p:t){lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
    const double eps=std::max(1e-9,norm(sub(hi,lo))*1e-9);
    using Key=std::tuple<long long,long long,long long>;
    const auto key=[&](P p){return Key{std::llround((p.x-lo.x)/eps),std::llround((p.y-lo.y)/eps),std::llround((p.z-lo.z)/eps)};};
    std::map<std::pair<Key,Key>,std::pair<int,int>> edges;
    for(const auto& t:triangles)for(int i=0;i<3;++i){
        auto a=key(t[i]),b=key(t[(i+1)%3]);if(a==b)continue;
        int sign=1;if(b<a){std::swap(a,b);sign=-1;}
        auto& count=edges[{a,b}];++count.first;count.second+=sign;
    }
    return !edges.empty()&&std::ranges::all_of(edges,[](const auto& e){return e.second.first==2&&e.second.second==0;});
}
void finish_surface(MeasurementGeometry& out){
    double mesh_area=0;
    for(const auto& t:out.triangles)mesh_area+=area(t);
    if(!out.values.area)out.values.area=V{mesh_area,true};
    if(out.values.area && std::abs(out.values.area->value-mesh_area)>1e-8*std::max(1.0,mesh_area))
        out.approximate=true; // Includes curved trim boundaries of planar faces.
    if(out.reference.kind!=K::Object)return;
    out.solid=closed_surface(out.triangles);
    if(out.solid){
        // Translate first to avoid catastrophic cancellation far from Origin.
        const auto origin=out.triangles.front()[0];double volume=0;
        for(const auto& t:out.triangles)volume+=dot(sub(t[0],origin),cross(sub(t[1],origin),sub(t[2],origin)))/6;
        out.values.volume=V{std::abs(volume),out.approximate};
    }
}
struct Pair {P a,b;double squared=std::numeric_limits<double>::infinity();};
Pair pair(P a,P b){return {a,b,norm2(sub(a,b))};}
void improve(Pair& best,Pair candidate){if(candidate.squared<best.squared)best=candidate;}
Pair swapped(Pair value){std::swap(value.a,value.b);return value;}
P closest_segment(P p,P a,P b){
    const auto d=sub(b,a);const double n=norm2(d);
    return n>1e-24?add(a,mul(d,std::clamp(dot(sub(p,a),d)/n,0.0,1.0))):a;
}
P closest_triangle(P p,const std::array<P,3>& t){
    const auto a=t[0],b=t[1],c=t[2],ab=sub(b,a),ac=sub(c,a),ap=sub(p,a);
    if(norm2(cross(ab,ac))<1e-24){
        Pair best=pair(p,closest_segment(p,a,b));
        improve(best,pair(p,closest_segment(p,b,c)));improve(best,pair(p,closest_segment(p,c,a)));return best.b;
    }
    const double d1=dot(ab,ap),d2=dot(ac,ap);if(d1<=0&&d2<=0)return a;
    const auto bp=sub(p,b);const double d3=dot(ab,bp),d4=dot(ac,bp);if(d3>=0&&d4<=d3)return b;
    const double vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0)return add(a,mul(ab,d1/(d1-d3)));
    const auto cp=sub(p,c);const double d5=dot(ab,cp),d6=dot(ac,cp);if(d6>=0&&d5<=d6)return c;
    const double vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0)return add(a,mul(ac,d2/(d2-d6)));
    const double va=d3*d6-d5*d4;if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0)return add(b,mul(sub(c,b),(d4-d3)/((d4-d3)+(d5-d6))));
    const double inv=1/(va+vb+vc);return add(a,add(mul(ab,vb*inv),mul(ac,vc*inv)));
}
Pair segment_pair(P p,P p2,P q,P q2,bool infinite_first=false){
    const auto d1=sub(p2,p),d2=sub(q2,q),r=sub(p,q);
    const double a=norm2(d1),e=norm2(d2),f=dot(d2,r);double s=0,t=0;
    if(a<1e-24&&e<1e-24)return pair(p,q);
    if(a<1e-24)t=std::clamp(f/e,0.0,1.0);
    else{
        const double c=dot(d1,r);
        if(e<1e-24)s=infinite_first?-c/a:std::clamp(-c/a,0.0,1.0);
        else{
            const double b=dot(d1,d2),denom=a*e-b*b;
            if(denom>1e-14*a*e)s=(b*f-c*e)/denom;
            if(!infinite_first)s=std::clamp(s,0.0,1.0);
            t=(b*s+f)/e;
            if(t<0){t=0;s=-c/a;if(!infinite_first)s=std::clamp(s,0.0,1.0);}
            else if(t>1){t=1;s=(b-c)/a;if(!infinite_first)s=std::clamp(s,0.0,1.0);}
        }
    }
    return pair(add(p,mul(d1,s)),add(q,mul(d2,t)));
}
Pair segment_triangle(P a,P b,const std::array<P,3>& tri,bool infinite=false){
    const auto n=cross(sub(tri[1],tri[0]),sub(tri[2],tri[0])),d=sub(b,a);
    const double denom=dot(n,d);
    if(std::abs(denom)>1e-14*norm(n)*norm(d)){
        const double t=dot(n,sub(tri[0],a))/denom;
        if(infinite||(t>=0&&t<=1)){
            const auto p=add(a,mul(d,t));
            if(norm2(sub(p,closest_triangle(p,tri)))<=1e-18)return pair(p,p);
        }
    }
    Pair best;
    if(!infinite){improve(best,pair(a,closest_triangle(a,tri)));improve(best,pair(b,closest_triangle(b,tri)));}
    for(int i=0;i<3;++i)improve(best,segment_pair(a,b,tri[i],tri[(i+1)%3],infinite));
    return best;
}
struct Primitive {std::array<P,3> p;int count;};
Pair primitives(const Primitive& a,const Primitive& b){
    if(a.count>b.count)return swapped(primitives(b,a));
    if(a.count==1){
        const auto p=b.count==1?b.p[0]:b.count==2?closest_segment(a.p[0],b.p[0],b.p[1]):closest_triangle(a.p[0],b.p);
        return pair(a.p[0],p);
    }
    if(a.count==2)return b.count==2?segment_pair(a.p[0],a.p[1],b.p[0],b.p[1]):segment_triangle(a.p[0],a.p[1],b.p);
    Pair best;
    for(int i=0;i<3;++i){
        improve(best,segment_triangle(a.p[i],a.p[(i+1)%3],b.p));
        improve(best,swapped(segment_triangle(b.p[i],b.p[(i+1)%3],a.p)));
    }
    return best;
}
struct Bounds {
    P lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};
    void include(P p){lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
};
double coordinate(P p,int axis){return axis==0?p.x:axis==1?p.y:p.z;}
double bounds_distance(const Bounds& a,const Bounds& b){
    double result=0;for(int i=0;i<3;++i){const auto gap=std::max({0.0,coordinate(a.lo,i)-coordinate(b.hi,i),coordinate(b.lo,i)-coordinate(a.hi,i)});result+=gap*gap;}return result;
}
struct Node {Bounds bounds;std::size_t start{},end{};int left=-1,right=-1;};
struct Index {
    std::vector<Primitive> items;std::vector<Node> nodes;
    explicit Index(const MeasurementGeometry& geometry){
        for(auto p:geometry.points)items.push_back({{p,p,p},1});
        for(auto s:geometry.segments)items.push_back({{s[0],s[1],s[1]},2});
        for(auto t:geometry.triangles)items.push_back({t,3});
        if(!items.empty())build(0,items.size());
    }
    int build(std::size_t start,std::size_t end){
        const int index=static_cast<int>(nodes.size());nodes.push_back({});
        Bounds bounds;for(auto i=start;i<end;++i)for(int j=0;j<items[i].count;++j)bounds.include(items[i].p[j]);
        nodes[index].bounds=bounds;nodes[index].start=start;nodes[index].end=end;
        if(end-start>8){
            const auto span=sub(bounds.hi,bounds.lo);const int axis=span.x>=span.y&&span.x>=span.z?0:span.y>=span.z?1:2;
            const auto center=[axis](const Primitive& p){double x=0;for(int j=0;j<p.count;++j)x+=coordinate(p.p[j],axis);return x/p.count;};
            const auto mid=start+(end-start)/2;std::nth_element(items.begin()+start,items.begin()+mid,items.begin()+end,[&](const auto& a,const auto& b){return center(a)<center(b);});
            const int left=build(start,mid),right=build(mid,end);nodes[index].left=left;nodes[index].right=right;
        }
        return index;
    }
};
Pair finite_pair(const Index& a,const Index& b){
    Pair best;if(a.nodes.empty()||b.nodes.empty())return best;
    using Job=std::pair<double,std::pair<int,int>>;
    std::priority_queue<Job,std::vector<Job>,std::greater<Job>> jobs;
    jobs.push({0,{0,0}});
    while(!jobs.empty()){
        const auto [bound,ids]=jobs.top();jobs.pop();if(bound>=best.squared)continue;
        const auto& na=a.nodes[ids.first];const auto& nb=b.nodes[ids.second];
        const auto enqueue=[&](int x,int y){const auto d=bounds_distance(a.nodes[x].bounds,b.nodes[y].bounds);if(d<best.squared)jobs.push({d,{x,y}});};
        if(na.left<0&&nb.left<0){
            for(auto i=na.start;i<na.end;++i)for(auto j=nb.start;j<nb.end;++j)improve(best,primitives(a.items[i],b.items[j]));
            if(best.squared<=1e-24)return best;
        }else if(nb.left<0||(na.left>=0&&na.end-na.start>=nb.end-nb.start)){
            enqueue(na.left,ids.second);enqueue(na.right,ids.second);
        }else{enqueue(ids.first,nb.left);enqueue(ids.first,nb.right);}
    }
    return best;
}
bool contains(const MeasurementGeometry& geometry,P p){
    if(!geometry.solid)return false;
    double angle=0;
    for(const auto& t:geometry.triangles){
        const auto a=sub(t[0],p),b=sub(t[1],p),c=sub(t[2],p);const auto la=norm(a),lb=norm(b),lc=norm(c);
        if(std::min({la,lb,lc})<1e-10)return true;
        angle+=2*std::atan2(dot(a,cross(b,c)),la*lb*lc+dot(a,b)*lc+dot(b,c)*la+dot(c,a)*lb);
    }
    return std::abs(angle)>2*std::acos(-1.0);
}
Pair line_finite(P origin,P direction,const Index& finite){
    Pair best;direction=unit(direction);
    for(const auto& p:finite.items){
        if(p.count==1)improve(best,pair(add(origin,mul(direction,dot(sub(p.p[0],origin),direction))),p.p[0]));
        else if(p.count==2)improve(best,segment_pair(origin,add(origin,direction),p.p[0],p.p[1],true));
        else improve(best,segment_triangle(origin,add(origin,direction),p.p,true));
    }
    return best;
}
Pair plane_finite(P origin,P normal,const Index& finite){
    Pair best;normal=unit(normal);
    for(const auto& item:finite.items){
        for(int i=0;i<item.count;++i){
            const auto p=item.p[i];const double a=dot(sub(p,origin),normal);improve(best,pair(sub(p,mul(normal,a)),p));
            const auto q=item.p[(i+1)%item.count];const double b=dot(sub(q,origin),normal);
            if(a*b<0){const auto hit=add(p,mul(sub(q,p),a/(a-b)));return pair(hit,hit);}
        }
    }
    return best;
}
Pair plane_line(P origin,P normal,P line,P direction){
    normal=unit(normal);direction=unit(direction);const double denom=dot(normal,direction),d=dot(normal,sub(origin,line));
    if(std::abs(denom)>1e-12){const auto p=add(line,mul(direction,d/denom));return pair(p,p);}
    return pair(add(line,mul(normal,d)),line);
}
}

std::optional<kernel::MeasurementReference> measurement_reference(const ViewerCandidate& candidate){
    K kind;
    switch(candidate.kind){
    case CandidateKind::Vertex:case CandidateKind::SketchPoint:kind=K::Point;break;
    case CandidateKind::Edge:case CandidateKind::SketchSegment:case CandidateKind::SketchCurve:kind=K::Curve;break;
    case CandidateKind::SketchExternalReference:kind=candidate.semantic_key.starts_with("external_point:")?K::Point:K::Curve;break;
    case CandidateKind::Face:kind=candidate.semantic_key=="plane"||candidate.semantic_key.starts_with("origin:plane:")?K::Plane:K::Face;break;
    case CandidateKind::Plane:kind=K::Plane;break;
    case CandidateKind::Axis:case CandidateKind::SketchAxis:kind=K::Axis;break;
    case CandidateKind::Container:case CandidateKind::Occurrence:kind=K::Object;break;
    default:return {};
    }
    // Display body topology is not a stable reference owner.
    if((kind==K::Face||kind==K::Curve)&&candidate.geometry==CandidateGeometry::Display&&
       (candidate.semantic_key.empty()||candidate.semantic_key=="container:display"))return {};
    return kernel::MeasurementReference{kind,candidate.owner_id,candidate.semantic_key,candidate.instance_path};
}
std::optional<MeasurementGeometry> measure_entity(const kernel::ViewerMesh& mesh,const kernel::MeasurementReference& request){
    MeasurementGeometry out;out.reference=request;
    const auto collect=[&](const auto& source){
        if(request.kind==K::Point||request.kind==K::Object)
            for(const auto& p:source.points)if(matches(p.reference,request)){
                if(request.kind==K::Point){out.points.push_back(p.position);out.values.position=p.position;return;}
            }
        if(request.kind==K::Axis)for(const auto& axis:source.axes)if(matches(axis.reference,request)&&norm(axis.direction)>1e-12){
            out.axis={{axis.point,unit(axis.direction)}};out.values.position=axis.point;return;
        }
        if(request.kind==K::Curve||request.kind==K::Axis)
            for(const auto& edge:source.edges)if(matches(edge.reference,request)&&edge.points.size()>1){
                if(edge.infinite||request.kind==K::Axis){out.axis={{edge.points.front(),unit(sub(edge.points.back(),edge.points.front()))}};out.values.position=edge.points.front();return;}
                double length=0;
                for(std::size_t i=1;i<edge.points.size();++i){out.segments.push_back({edge.points[i-1],edge.points[i]});length+=norm(sub(edge.points[i],edge.points[i-1]));}
                out.approximate=!straight(edge.points);out.values.length=V{edge.measured_length.value_or(length),!edge.measured_length&&out.approximate};return;
            }
        if(request.kind==K::Face||request.kind==K::Plane||request.kind==K::Object){
            std::map<std::tuple<std::string,std::string,std::string>,std::optional<double>> face_areas;
            for(std::size_t i=0;i<source.triangle_references.size()&&i*3+2<source.triangles.size();++i){
                const auto& ref=source.triangle_references[i];if(!matches(ref,request))continue;
                const auto a=source.triangles[3*i],b=source.triangles[3*i+1],c=source.triangles[3*i+2];
                if(std::max({a,b,c})>=source.vertices.size())continue;
                out.triangles.push_back({source.vertices[a],source.vertices[b],source.vertices[c]});
                face_areas[{ref.owner_id,ref.semantic_key,ref.instance_path}]=ref.measured_area;
                if(!ref.surface||ref.surface->kind!=kernel::SurfaceGeometry::Kind::Plane)out.approximate=true;
                if(request.kind==K::Plane){
                    const auto& t=out.triangles.back();const auto normal=unit(cross(sub(t[1],t[0]),sub(t[2],t[0])));
                    if(norm(normal)>0){out.plane={{t[0],normal}};out.values.position=t[0];out.triangles.clear();out.approximate=false;return;}
                }
            }
            if(!face_areas.empty()){
                double sum=0;bool known=true;for(const auto& [key,area]:face_areas){if(area)sum+=*area;else known=false;}
                if(known)out.values.area=V{sum,false};
            }
        }
        if(request.kind==K::Object&&out.triangles.empty()){
            for(const auto& edge:source.edges)if(matches(edge.reference,request)&&!edge.parameter_seam){
                for(std::size_t i=1;i<edge.points.size();++i)out.segments.push_back({edge.points[i-1],edge.points[i]});
                out.approximate|=!straight(edge.points);
            }
            if(out.segments.empty())for(const auto& point:source.points)if(matches(point.reference,request))out.points.push_back(point.position);
        }
    };
    const auto empty=[&]{return out.points.empty()&&out.segments.empty()&&out.triangles.empty()&&!out.axis&&!out.plane;};
    if(request.kind==K::Object&&request.owner_id.empty())collect(mesh);
    if(empty())collect(mesh.original_references);
    if(empty())collect(mesh);
    if(empty())return {};
    if(!out.triangles.empty())finish_surface(out);
    return out;
}
std::optional<kernel::MeasurementDistance> measure_distance(const MeasurementGeometry& a,const MeasurementGeometry& b){
    const Index ai(a),bi(b);Pair best;
    if(a.plane&&b.plane){
        const auto [p,n]=*a.plane;const auto [q,m]=*b.plane;const auto d=cross(n,m);const double s=norm2(d);
        if(s<1e-24)best=pair(p,sub(p,mul(m,dot(sub(p,q),m))));
        else {const auto x=add(p,mul(cross(d,n),dot(m,sub(q,p))/s));best=pair(x,x);}
    }else if(a.plane&&b.axis)best=plane_line(a.plane->first,a.plane->second,b.axis->first,b.axis->second);
    else if(a.axis&&b.plane)best=swapped(plane_line(b.plane->first,b.plane->second,a.axis->first,a.axis->second));
    else if(a.axis&&b.axis){
        const auto [p,u]=*a.axis;const auto [q,v]=*b.axis;const double d=dot(u,v),denom=1-d*d;
        const auto r=sub(p,q);const double s=denom>1e-14?(d*dot(v,r)-dot(u,r))/denom:0;
        const auto x=add(p,mul(u,s));best=pair(x,add(q,mul(v,dot(sub(x,q),v))));
    }else if(a.axis)best=line_finite(a.axis->first,a.axis->second,bi);
    else if(b.axis)best=swapped(line_finite(b.axis->first,b.axis->second,ai));
    else if(a.plane)best=plane_finite(a.plane->first,a.plane->second,bi);
    else if(b.plane)best=swapped(plane_finite(b.plane->first,b.plane->second,ai));
    else{
        best=finite_pair(ai,bi);
        if(!ai.items.empty()&&contains(b,ai.items.front().p[0]))best=pair(ai.items.front().p[0],ai.items.front().p[0]);
        else if(!bi.items.empty()&&contains(a,bi.items.front().p[0]))best=pair(bi.items.front().p[0],bi.items.front().p[0]);
    }
    if(!std::isfinite(best.squared))return {};
    return kernel::MeasurementDistance{{std::sqrt(std::max(0.0,best.squared)),a.approximate||b.approximate},best.a,best.b};
}
} // namespace zima::viewer