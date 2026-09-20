#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <zima/kernel/dimension_layout.hpp>
#include <cmath>
#include <algorithm>
#include <set>
namespace zima::drawing {
struct AnnotationGuide { Point2 first,second; };
// Paper millimetres relative to this view only. Uses persisted geometry.
inline std::vector<AnnotationGuide> annotation_guides(const DrawingView& view) {
    std::vector<AnnotationGuide> result;
    if(view.scale<=0||view.dimension_guide_offset<=0||view.dimension_guide_spacing<=0)return result;
    const auto project=[&](kernel::Vec3 p){return Point2{kernel::dimension_dot(p,view.camera.horizontal)*view.scale,kernel::dimension_dot(p,view.camera.vertical)*view.scale};};
    std::set<std::pair<std::string,std::string>> owners;
    for(const auto& item:view.model_annotations)if(item.visible&&item.model_envelope.valid&&owners.emplace(item.source.owner_id,item.source.instance_path).second) {
        for(int level=0;level<view.dimension_guide_count;++level) {
            auto frame=item.model_envelope;
            const double offset=(view.dimension_guide_offset+level*view.dimension_guide_spacing)/view.scale;
            frame.minimum=kernel::dimension_sub(frame.minimum,{offset,offset,offset});frame.maximum=kernel::dimension_add(frame.maximum,{offset,offset,offset});
            const auto corners=frame.corners();
            for(unsigned i=0;i<8;++i)for(unsigned bit:{1u,2u,4u})if(!(i&bit)) {
                auto a=project(corners[i]),b=project(corners[i|bit]);
                if(std::hypot(a.x-b.x,a.y-b.y)>1e-9)result.push_back({a,b});
            }
        }
    }
    // Views without model annotations still offer a geometric frame to balloons
    // and manual dimensions. Annotation text never enlarges this frame.
    if(result.empty()) {
        double x0=0,x1=0,y0=0,y1=0;bool valid=false;
        for(const auto& edge:view.projected_edges)for(const auto& p:edge.points) {
            const double x=p.x*view.scale,y=p.y*view.scale;
            if(!valid){x0=x1=x;y0=y1=y;valid=true;}else{x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);}
        }
        if(valid)for(int level=0;level<view.dimension_guide_count;++level) {
            const auto d=view.dimension_guide_offset+level*view.dimension_guide_spacing;
            const Point2 a{x0-d,y0-d},b{x1+d,y0-d},c{x1+d,y1+d},e{x0-d,y1+d};
            result.insert(result.end(),{{a,b},{b,c},{c,e},{e,a}});
        }
    }
    return result;
}
struct AnnotationSnap { Point2 point;std::optional<AnnotationGuide> guide; };
inline AnnotationSnap snap_annotation(const DrawingView& view,Point2 point,double tolerance) {
    AnnotationSnap result{point,{}};double best=tolerance;
    for(const auto& line:annotation_guides(view)) {
        const double x=line.second.x-line.first.x,y=line.second.y-line.first.y,length=x*x+y*y;
        if(length<1e-18)continue;
        const auto t=std::clamp(((point.x-line.first.x)*x+(point.y-line.first.y)*y)/length,0.,1.);
        const Point2 p{line.first.x+t*x,line.first.y+t*y};const auto distance=std::hypot(point.x-p.x,point.y-p.y);
        if(distance<best){best=distance;result={p,line};}
    }
    return result;
}
}
