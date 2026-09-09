#include <zima/drawing/drawing_document.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace zima::drawing {
namespace {
Point2 add(Point2 a,Point2 b){return {a.x+b.x,a.y+b.y};}
Point2 sub(Point2 a,Point2 b){return {a.x-b.x,a.y-b.y};}
Point2 mul(Point2 a,double n){return {a.x*n,a.y*n};}
double length(Point2 p){return std::hypot(p.x,p.y);}
}
std::optional<SectionTraceLayout> section_trace_layout(const DrawingView& view,const zima::document::SectionDefinition& section,
    std::span<const DrawingView* const> section_views){
    // Trace arrows follow the calculated cut, including a transient preview.
    // Prefer the explicit source-view link; otherwise a unique matching cut
    // also supports traces displayed on additional views of the same source.
    const DrawingView* linked=nullptr;int priority=0;bool ambiguous=false;
    for(const auto* cut:section_views){
        if(cut->section_id!=section.id||cut->source_document_id!=view.source_document_id||!cut->section_snapshot)continue;
        const int candidate=cut->id==view.id?3:cut->section_parent_id==view.id?2:1;
        if(candidate>priority){linked=cut;priority=candidate;ambiguous=false;}
        else if(candidate==priority&&linked->section_display_reversed!=cut->section_display_reversed)ambiguous=true;
    }
    const bool reversed=linked&&!ambiguous?linked->section_display_reversed:section.reversed;
    const double side=reversed==section.reversed?1:-1;
    const auto path=zima::document::section_path(section);
    const auto frames=zima::document::section_frames(section);
    const auto dot=[](auto a,auto b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto project=[&](auto p){return Point2{dot(p,view.camera.horizontal)*view.scale,dot(p,view.camera.vertical)*view.scale};};
    std::vector<Point2> points;
    for(auto p:path)points.push_back(project(zima::kernel::Vec3{
        section.plane_origin.x+section.plane_x.x*p[0]+section.plane_y.x*p[1],
        section.plane_origin.y+section.plane_x.y*p[0]+section.plane_y.y*p[1],
        section.plane_origin.z+section.plane_x.z*p[0]+section.plane_y.z*p[1]}));
    if(frames.size()==1 && length(project(frames.front().horizontal))<=view.scale*1e-8){
        // Looking along the sketch line collapses its projection to a point,
        // but the cutting plane still has an edge-on trace. Use the plane's
        // infinite extrusion direction, independent of the retained side.
        const auto x=section.plane_x,y=section.plane_y;
        const zima::kernel::Vec3 extrusion{x.y*y.z-x.z*y.y,x.z*y.x-x.x*y.z,x.x*y.y-x.y*y.x};
        const auto start=project(frames.front().origin);
        points={start,add(start,project(extrusion))};
    }
    SectionTraceLayout result;
    for(int end=0;end<2;++end){const auto& n=end?frames.back().normal:frames.front().normal;
        auto direction=Point2{-side*dot(n,view.camera.horizontal),-side*dot(n,view.camera.vertical)};
        const auto size=length(direction);if(size<1e-8)return {};
        result.arrow_directions[end]=mul(direction,1/size);
    }
    double xmin=std::numeric_limits<double>::infinity(),xmax=-xmin,ymin=xmin,ymax=-xmin;
    const auto include=[&](Point2 p){p=mul(p,view.scale);xmin=std::min(xmin,p.x);xmax=std::max(xmax,p.x);ymin=std::min(ymin,p.y);ymax=std::max(ymax,p.y);};
    for(const auto& edge:view.projected_edges)for(auto p:edge.points)include(p);
    for(const auto& triangle:view.projected_triangles)for(auto p:triangle.points)include(p);
    if(!std::isfinite(xmin))return {};
    // End rays are infinite in the Section definition. Crop the presentation
    // at the view bounds with a fixed paper margin, independent of sketch length.
    xmin-=7;xmax+=7;ymin-=7;ymax+=7;
    for(std::size_t i=1;i<points.size();++i){
        const auto a=points[i-1],delta=sub(points[i],a);if(length(delta)<1e-9)continue;
        double lo=i==1?-std::numeric_limits<double>::infinity():0;
        double hi=i+1==points.size()?std::numeric_limits<double>::infinity():1;
        const auto clip=[&](double start,double direction,double minimum,double maximum){
            if(std::abs(direction)<1e-10)return start>=minimum&&start<=maximum;
            auto t0=(minimum-start)/direction,t1=(maximum-start)/direction;if(t0>t1)std::swap(t0,t1);
            lo=std::max(lo,t0);hi=std::min(hi,t1);return hi>lo+1e-10;
        };
        if(clip(a.x,delta.x,xmin,xmax)&&clip(a.y,delta.y,ymin,ymax))result.chain.push_back({add(a,mul(delta,lo)),add(a,mul(delta,hi))});
    }
    if(result.chain.empty())return {};
    const auto saved=view.section_marker_offsets.find(section.id);
    for(int end=0;end<2;++end){auto& line=end?result.chain.back():result.chain.front();const auto tip=line[end?1:0];const auto direction=sub(tip,line[end?0:1]);const auto n=length(direction);
        result.minimum_offsets[end]=-std::max(0.0,n-1);
        const auto offset=std::max(result.minimum_offsets[end],saved==view.section_marker_offsets.end()?0:saved->second[end]);result.end_offsets[end]=offset;
        line[end?1:0]=add(tip,mul(direction,offset/n));
    }
    result.arrow_tips={result.chain.front()[0],result.chain.back()[1]};
    const auto accent=[&](Point2 tip,Point2 toward,double size){const auto delta=sub(toward,tip);const auto n=length(delta);if(n>1e-9)result.accents.push_back({tip,add(tip,mul(delta,std::min(size,n)/n))});};
    const auto end_accent=[&](Point2 tip,Point2 toward){const auto delta=sub(toward,tip);const auto n=length(delta);if(n>1e-9){const auto unit=mul(delta,1/n);result.accents.push_back({sub(tip,mul(unit,3)),add(tip,mul(unit,std::min(3.0,n)))});}};
    end_accent(result.chain.front()[0],result.chain.front()[1]);end_accent(result.chain.back()[1],result.chain.back()[0]);
    for(std::size_t i=1;i<result.chain.size();++i){const auto& before=result.chain[i-1];const auto& after=result.chain[i];
        if(length(sub(before[1],after[0]))>1e-7)continue;
        const auto a=sub(before[1],before[0]),b=sub(after[1],after[0]);
        if(std::abs(a.x*b.y-a.y*b.x)<1e-7*length(a)*length(b))continue;
        accent(before[1],before[0],3);accent(after[0],after[1],3);
    }
    return result;
}
Point2 section_letter_position(const SectionTraceLayout& layout,std::size_t end,Point2 text_size,
    const std::vector<std::array<Point2,2>>& obstacles,double clearance){
    const auto& line=end?layout.chain.back():layout.chain.front();
    auto outward=sub(line[end?1:0],line[end?0:1]);outward=mul(outward,1/length(outward));
    const auto tip=layout.arrow_tips[end],direction=layout.arrow_directions[end];
    const auto half=mul(text_size,.5);
    const double support=std::abs(outward.x)*half.x+std::abs(outward.y)*half.y;
    auto base=sub(add(tip,mul(outward,support+4)),mul(direction,half.y+3));
    const auto offset=sub(base,tip);const double side=offset.x*outward.x+offset.y*outward.y;
    if(side<support+clearance)base=add(base,mul(outward,support+clearance-side));
    std::vector<std::array<double,2>> forbidden;
    for(const auto& obstacle:obstacles){
        const double xmin=std::min(obstacle[0].x,obstacle[1].x)-half.x-clearance,xmax=std::max(obstacle[0].x,obstacle[1].x)+half.x+clearance;
        const double ymin=std::min(obstacle[0].y,obstacle[1].y)-half.y-clearance,ymax=std::max(obstacle[0].y,obstacle[1].y)+half.y+clearance;
        double lo=0,hi=std::numeric_limits<double>::infinity();
        const auto clip=[&](double p,double d,double minimum,double maximum){
            if(std::abs(d)<1e-10)return p>=minimum&&p<=maximum;
            auto a=(minimum-p)/d,b=(maximum-p)/d;if(a>b)std::swap(a,b);lo=std::max(lo,a);hi=std::min(hi,b);return hi>=lo;
        };
        if(clip(base.x,outward.x,xmin,xmax)&&clip(base.y,outward.y,ymin,ymax))forbidden.push_back({lo,hi});
    }
    // Jump past intersected intervals instead of a pixel/step search. This is
    // deterministic and bounded even for very large model coordinates.
    std::ranges::sort(forbidden);double distance=0;
    for(auto interval:forbidden){if(interval[0]>distance)break;distance=std::max(distance,interval[1]+.05);}
    return add(base,mul(outward,distance));
}
} // namespace zima::drawing
