#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace zima::drawing {
inline std::vector<ViewBreak> ordered_breaks(const DrawingView& v) {
    auto b=v.breaks;std::ranges::sort(b,{},&ViewBreak::start);return b;
}
inline void validate_view_breaks(const DrawingView& v) {
    if(v.breaks.size()>32)throw std::invalid_argument("Nejvýše 32 přerušení v jednom pohledu.");
    double end=-std::numeric_limits<double>::infinity();std::set<std::string> ids;
    for(const auto& b:ordered_breaks(v)) {
        if(b.id.empty()||!ids.insert(b.id).second||!std::isfinite(b.start)||!std::isfinite(b.length)||!std::isfinite(b.gap)||
           std::abs(b.start)>1e9||b.length<=.001||b.length>1e9||b.gap<.1||b.gap>100||
           b.start<=end||b.vertical!=v.breaks.front().vertical||b.mark<BreakMark::None||b.mark>BreakMark::Zigzag)
            throw std::invalid_argument("Přerušení musí mít kladnou délku a mezeru, stejný směr a nesmí se překrývat.");
        end=b.start+b.length;
    }
}
inline double break_axis(Point2 p,bool vertical){return vertical?p.y:p.x;}
inline bool break_hidden(const DrawingView& v,Point2 p) {
    for(const auto& b:v.breaks){auto t=break_axis(p,b.vertical);if(t>b.start+1e-9&&t<b.start+b.length-1e-9)return true;}return false;
}
inline bool break_annotation_hidden(const DrawingView& v,const ModelAnnotation& a) {
    if(v.breaks.empty()||!a.model_dimension)return false;
    const auto project=[&](kernel::Vec3 p){return Point2{kernel::dimension_dot(p,v.camera.horizontal),kernel::dimension_dot(p,v.camera.vertical)};};
    return break_hidden(v,project(a.model_dimension->witness_first))||break_hidden(v,project(a.model_dimension->witness_second));
}
// Continuous map for annotation positions; removed source geometry is clipped separately.
// Coordinates remain view model units; the visual gap alone is paper millimetres.
inline Point2 break_map(const DrawingView& v,Point2 p,bool inverse=false) {
    if(v.breaks.empty())return p;
    const bool vertical=v.breaks.front().vertical;const double t=break_axis(p,vertical);double shift=0;
    for(const auto& b:v.breaks) {
        const double gap=b.gap/v.scale;
        if(!inverse)shift-=std::clamp(t-b.start,0.,b.length)*(1-gap/b.length);
        else {
            double start=b.start;for(const auto& previous:v.breaks)if(previous.start<b.start)start-=previous.length-previous.gap/v.scale;
            shift+=std::clamp(t-start,0.,gap)*(b.length/gap-1);
        }
    }
    return vertical?Point2{p.x,t+shift}:Point2{t+shift,p.y};
}
inline Point2 break_paper(const DrawingView& v,Point2 p,bool inverse=false) {
    p=break_map(v,{p.x/v.scale,p.y/v.scale},inverse);return {p.x*v.scale,p.y*v.scale};
}
// Clip source polylines, retaining original source coordinates and identity.
inline std::vector<std::vector<Point2>> break_fragments(const DrawingView& v,const std::vector<Point2>& line) {
    if(v.breaks.empty())return {line};
    std::vector<std::vector<Point2>> out;
    for(std::size_t i=1;i<line.size();++i){auto a=line[i-1],b=line[i];std::vector<double> ts{0,1};
        const bool vertical=v.breaks.front().vertical;auto first=break_axis(a,vertical),delta=break_axis(b,vertical)-first;
        if(std::abs(delta)>1e-12)for(const auto& cut:v.breaks)for(double edge:{cut.start,cut.start+cut.length}){auto t=(edge-first)/delta;if(t>0&&t<1)ts.push_back(t);}
        std::ranges::sort(ts);const auto at=[&](double t){return Point2{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};};
        for(std::size_t j=1;j<ts.size();++j)if(ts[j]-ts[j-1]>1e-12&&!break_hidden(v,at((ts[j]+ts[j-1])/2)))out.push_back({at(ts[j-1]),at(ts[j])});
    }return out;
}
inline std::vector<ProjectedEdge> broken_edges(const DrawingView& v) {
    if(v.breaks.empty())return v.projected_edges;
    std::vector<ProjectedEdge> out;
    for(const auto& e:v.projected_edges) {
        if(e.vertex_depths.empty()) {
            for(auto points:break_fragments(v,e.points)){auto copy=e;for(auto& p:points)p=break_map(v,p);copy.points=std::move(points);out.push_back(std::move(copy));}
        }else for(std::size_t i=1;i<e.points.size();++i) {
            const auto a=e.points[i-1],b=e.points[i];const double dx=b.x-a.x,dy=b.y-a.y,den=dx*dx+dy*dy;
            for(auto points:break_fragments(v,{a,b})) {
                auto copy=e;copy.vertex_depths.clear();
                for(auto& p:points){const double t=den>0?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/den,0.,1.):0.;copy.vertex_depths.push_back(e.vertex_depths[i-1]+(e.vertex_depths[i]-e.vertex_depths[i-1])*t);p=break_map(v,p);}
                copy.points=std::move(points);out.push_back(std::move(copy));
            }
        }
    }return out;
}
inline std::vector<ProjectedTriangle> broken_triangles(const DrawingView& v) {
    if(v.breaks.empty())return v.projected_triangles;
    std::vector<ProjectedTriangle> out;const bool vertical=v.breaks.front().vertical;
    struct Vertex{Point2 p;double depth;};
    const auto clip=[&](std::vector<Vertex> polygon,double edge,bool low){std::vector<Vertex> result;if(polygon.empty())return result;
        auto a=polygon.back();for(auto b:polygon){auto x=break_axis(a.p,vertical)-edge,y=break_axis(b.p,vertical)-edge;bool ai=low?x<=0:x>=0,bi=low?y<=0:y>=0;
            if(ai!=bi){auto t=x/(x-y);result.push_back({{a.p.x+(b.p.x-a.p.x)*t,a.p.y+(b.p.y-a.p.y)*t},a.depth+(b.depth-a.depth)*t});}if(bi)result.push_back(b);a=b;}return result;};
    for(const auto& t:v.projected_triangles){std::vector<std::vector<Vertex>> polygons{{{t.points[0],t.vertex_depths[0]},{t.points[1],t.vertex_depths[1]},{t.points[2],t.vertex_depths[2]}}};
        for(const auto& b:ordered_breaks(v)){std::vector<std::vector<Vertex>> next;for(auto& p:polygons){auto left=clip(p,b.start,true),right=clip(p,b.start+b.length,false);if(left.size()>2)next.push_back(std::move(left));if(right.size()>2)next.push_back(std::move(right));}polygons=std::move(next);}
        for(const auto& p:polygons)for(std::size_t i=2;i<p.size();++i){auto copy=t;const std::array<Vertex,3> vertices{p[0],p[i-1],p[i]};for(int j=0;j<3;++j){copy.points[j]=break_map(v,vertices[j].p);copy.vertex_depths[j]=vertices[j].depth;}out.push_back(copy);}
    }return out;
}
inline std::vector<std::vector<Point2>> break_marks(const DrawingView& v) {
    std::vector<std::vector<Point2>> out;
    for(const auto& b:v.breaks)if(b.mark!=BreakMark::None)for(auto cut:{b.start,b.start+b.length}){
        double low=std::numeric_limits<double>::infinity(),high=-low;
        for(const auto& e:v.projected_edges)if(!e.hatch)for(std::size_t i=1;i<e.points.size();++i){auto a=e.points[i-1],p=e.points[i];auto x=break_axis(a,b.vertical),y=break_axis(p,b.vertical);
            if(cut<std::min(x,y)-1e-9||cut>std::max(x,y)+1e-9)continue;
            if(std::abs(y-x)<1e-12){low=std::min({low,break_axis(a,!b.vertical),break_axis(p,!b.vertical)});high=std::max({high,break_axis(a,!b.vertical),break_axis(p,!b.vertical)});}
            else {auto t=(cut-x)/(y-x),z=break_axis(a,!b.vertical)+(break_axis(p,!b.vertical)-break_axis(a,!b.vertical))*t;low=std::min(low,z);high=std::max(high,z);}}
        if(!std::isfinite(low))continue;low-=1/v.scale;high+=1/v.scale;
        const auto point=[&](double along,double cross){return break_map(v,b.vertical?Point2{cross,along}:Point2{along,cross});};
        std::vector<Point2> line{point(cut,low)};
        if(b.mark==BreakMark::Zigzag){auto m=(low+high)/2,size=std::min(1.5/v.scale,(high-low)/6);auto center=point(cut,m);const auto add=[&](double x,double y){return b.vertical?Point2{center.x+y,center.y+x}:Point2{center.x+x,center.y+y};};
            // The two sloping arms meet at 20 degrees (2 * atan(rise / size)).
            const double rise=size*std::tan(10.0*3.14159265358979323846/180.0);
            line.push_back(add(0,-2*rise));line.push_back(add(size,-rise));line.push_back(add(-size,rise));line.push_back(add(0,2*rise));}
        line.push_back(point(cut,high));out.push_back(std::move(line));
    }return out;
}
}
