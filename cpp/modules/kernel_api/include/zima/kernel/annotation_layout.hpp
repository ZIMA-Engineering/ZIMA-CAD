#pragma once
#include <zima/kernel/dimension_layout.hpp>
namespace zima::kernel {
// Shared nearest-end policy for dimension shelves and symbol shelves.
inline bool shelf_joins_left(Vec3 contact,Vec3 first,Vec3 last) {
    const auto a=dimension_sub(contact,first),b=dimension_sub(contact,last);
    return dimension_dot(a,a)<dimension_dot(b,b);
}
inline Vec3 annotation_grip(const AnnotationStroke& a,Vec3 right,Vec3 up,bool join_left,bool spatial) {
    if(!spatial||!a.perpendicular)return a.grip;
    auto direction=dimension_sub(a.direction_tip,a.contact);
    if(a.kind==1)direction=dimension_cross(dimension_cross(right,up),direction);
    if(a.kind==2||dimension_dot(direction,direction)<1e-16)direction=up;
    direction=dimension_unit(direction);
    const auto offset=dimension_sub(a.grip,a.contact);
    if(a.kind!=0&&dimension_dot(direction,offset)<0)direction=dimension_scale(direction,-1);
    // Intersect the normal with the requested shelf height. When viewed edge-on,
    // use distance along the normal; never invent an intermediate diagonal leg.
    const double vertical=dimension_dot(direction,up);
    const double distance=std::abs(vertical)>1e-6?dimension_dot(offset,up)/vertical:dimension_dot(offset,direction);
    const auto join=dimension_add(a.contact,dimension_scale(direction,std::max(a.arrow_length*2.,distance)));
    return dimension_add(join,dimension_scale(right,(join_left?1.:-1.)*(a.short_shelf?a.shelf_length:std::max(0.,a.right-a.left)*.5)));
}
inline std::vector<Vec3> annotation_stroke(const AnnotationStroke& a,Vec3 right,Vec3 up,bool join_left,bool spatial) {
    const auto add=dimension_add,sub=dimension_sub;const auto scale=dimension_scale;
    const double width=std::max(0.,a.right-a.left);
    const auto grip=annotation_grip(a,right,up,join_left,spatial);
    const auto first=a.short_shelf?(join_left?add(grip,scale(right,-a.shelf_length)):grip):add(grip,scale(right,-width*.5));
    const auto last=a.short_shelf?(join_left?grip:add(grip,scale(right,a.shelf_length))):add(grip,scale(right,width*.5));
    const auto join=join_left?first:last;
    if(a.role==0) {auto out=a.local_points;for(auto& p:out)p=add(grip,add(scale(right,p.x-(a.short_shelf?0.:(a.left+a.right)*.5)),scale(up,p.y-(a.short_shelf?0.:a.bottom))));return out;}
    if(a.role==3)return {first,last};
    if(a.role==1)return {a.contact,join};
    auto delta=sub(join,a.contact);const double length=std::sqrt(dimension_dot(delta,delta));
    if(length<1e-9)return {};
    const auto direction=scale(delta,1/length);auto wing=dimension_cross(direction,dimension_cross(right,up));
    if(dimension_dot(wing,wing)<1e-12)wing=right;else wing=dimension_unit(wing);
    const double h=std::min(a.arrow_length,length*.5);const auto base=add(a.contact,scale(direction,h));
    return {add(base,scale(wing,h*.1763269807)),a.contact,add(base,scale(wing,-h*.1763269807))};
}
// Three grips share the exact rendering geometry: arrow tip, elbow, shelf end.
inline std::array<Vec3,3> annotation_handles(AnnotationStroke a,Vec3 right,Vec3 up,bool left,bool spatial) {
    a.role=3;const auto shelf=annotation_stroke(a,right,up,left,spatial);
    return {a.contact,left?shelf.front():shelf.back(),left?shelf.back():shelf.front()};
}
struct AnnotationDrag {Vec3 grip;double shelf_length;};
inline AnnotationDrag drag_annotation(const AnnotationStroke& a,Vec3 right,Vec3 up,bool left,bool spatial,int handle,Vec3 target) {
    const auto points=annotation_handles(a,right,up,left,spatial);
    const auto grip=annotation_grip(a,right,up,left,spatial);
    if(handle==2&&a.short_shelf){
        const double length=std::max(.1,dimension_dot(dimension_sub(target,points[1]),right)*(left?1.:-1.));
        return {dimension_add(points[1],dimension_scale(right,(left?1.:-1.)*length)),length};
    }
    return {dimension_add(grip,dimension_sub(target,points[handle])),a.shelf_length};
}
}
