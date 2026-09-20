#pragma once
#include <zima/drawing/view_breaks.hpp>
#include <zima/drawing/drawing_document.hpp>
#include <cmath>
#include <algorithm>
namespace zima::drawing {
struct AnnotationGuide { Point2 first,second; };
// Paper millimetres relative to this view only. Uses persisted geometry.
inline std::vector<AnnotationGuide> annotation_guides(const DrawingView& view) {
    std::vector<AnnotationGuide> result;
    if(view.scale<=0||view.dimension_guide_offset<=0||view.dimension_guide_spacing<=0)return result;
    // Use the actual displayed projection, never a projected 3D envelope or
    // annotation text. Build a paper-axis rectangle even for an oblique view.
    double x0=0,x1=0,y0=0,y1=0;bool valid=false;
    const auto include=[&](Point2 p) {
        const double x=p.x*view.scale,y=p.y*view.scale;
        if(!valid){x0=x1=x;y0=y1=y;valid=true;}
        else{x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);}
    };
    if(view.breaks.empty()) {
        for(const auto& edge:view.projected_edges)if(!edge.hatch)for(auto p:edge.points)include(p);
        for(const auto& triangle:view.projected_triangles)for(auto p:triangle.points)include(p);
    } else {
        for(const auto& edge:broken_edges(view))if(!edge.hatch)for(auto p:edge.points)include(p);
        for(const auto& triangle:broken_triangles(view))for(auto p:triangle.points)include(p);
    }
    if(valid)for(int level=0;level<view.dimension_guide_count;++level) {
        const auto d=view.dimension_guide_offset+level*view.dimension_guide_spacing;
        // Callers store and drag in original view coordinates; map back once.
        const auto a=break_paper(view,{x0-d,y0-d},true),b=break_paper(view,{x1+d,y0-d},true),
                   c=break_paper(view,{x1+d,y1+d},true),e=break_paper(view,{x0-d,y1+d},true);
        result.insert(result.end(),{{a,b},{b,c},{c,e},{e,a}});
    }
    return result;
}
struct AnnotationSnap { Point2 point;std::optional<AnnotationGuide> guide; };
inline AnnotationSnap snap_annotation(const DrawingView& view,Point2 point,double tolerance) {
    AnnotationSnap result{point,{}};double best=tolerance;point=break_paper(view,point);
    for(const auto& original:annotation_guides(view)) {
        const AnnotationGuide line{break_paper(view,original.first),break_paper(view,original.second)};
        const double x=line.second.x-line.first.x,y=line.second.y-line.first.y,length=x*x+y*y;
        if(length<1e-18)continue;
        const auto t=std::clamp(((point.x-line.first.x)*x+(point.y-line.first.y)*y)/length,0.,1.);
        const Point2 p{line.first.x+t*x,line.first.y+t*y};const auto distance=std::hypot(point.x-p.x,point.y-p.y);
        if(distance<best){best=distance;result={break_paper(view,p,true),original};}
    }
    return result;
}
}
