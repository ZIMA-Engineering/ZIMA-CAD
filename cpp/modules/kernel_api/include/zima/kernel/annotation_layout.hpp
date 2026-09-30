#pragma once
#include <zima/kernel/dimension_layout.hpp>
namespace zima::kernel {
// Shared nearest-end policy for dimension shelves and symbol shelves.
inline bool shelf_joins_left(Vec3 contact,Vec3 first,Vec3 last) {
    const auto a=dimension_sub(contact,first),b=dimension_sub(contact,last);
    return dimension_dot(a,a)<dimension_dot(b,b);
}
inline Vec3 annotation_grip(const AnnotationStroke& a,Vec3 right,Vec3 up,bool join_left,bool spatial,bool fixed_length=false) {
    if(!spatial||!a.perpendicular)return a.grip;
    auto direction=dimension_sub(a.direction_tip,a.contact);
    if(a.kind==1)direction=fixed_length?up:dimension_cross(dimension_cross(right,up),direction);
    if(a.kind==2||dimension_dot(direction,direction)<1e-16)direction=up;
    direction=dimension_unit(direction);
    const auto offset=dimension_sub(a.grip,a.contact);
    if(a.kind!=0&&dimension_dot(direction,offset)<0)direction=dimension_scale(direction,-1);
    // Intersect the normal with the requested shelf height. When viewed edge-on,
    // use distance along the normal; never invent an intermediate diagonal leg.
    const double vertical=dimension_dot(direction,up);
    // Model leaders retain their authored distance in model units while orbiting.
    // Paper layout keeps its existing intersection with the requested shelf height.
    const double distance=fixed_length?std::sqrt(dimension_dot(offset,offset)):
        (std::abs(vertical)>1e-6?dimension_dot(offset,up)/vertical:dimension_dot(offset,direction));
    const auto join=dimension_add(a.contact,dimension_scale(direction,std::max(a.arrow_length*2.,distance)));
    return dimension_add(join,dimension_scale(right,(join_left?1.:-1.)*(a.short_shelf?a.shelf_length:std::max(0.,a.right-a.left)*.5+(a.framed?a.shelf_length:0.))));
}
inline std::vector<Vec3> annotation_stroke(const AnnotationStroke& a,Vec3 right,Vec3 up,bool join_left,bool spatial,bool fixed_length=false) {
    const auto add=dimension_add,sub=dimension_sub;const auto scale=dimension_scale;
    const double width=std::max(0.,a.right-a.left);
    const auto grip=annotation_grip(a,right,up,join_left,spatial,fixed_length);
    const auto first=a.short_shelf?(join_left?add(grip,scale(right,-a.shelf_length)):grip):add(grip,scale(right,-width*.5));
    const auto last=a.short_shelf?(join_left?grip:add(grip,scale(right,a.shelf_length))):add(grip,scale(right,width*.5));
    const auto attachment=join_left?first:last;
    const auto join=a.framed?add(attachment,scale(right,(join_left?-1.:1.)*a.shelf_length)):attachment;
    if(a.role==0) {
        auto out=a.local_points;for(auto& p:out)p=add(grip,add(scale(right,p.x-(a.short_shelf?0.:(a.left+a.right)*.5)),scale(up,p.y-(a.short_shelf?0.:a.bottom))));
        return out;
    }
    if(a.role==3)return a.framed?std::vector<Vec3>{join,attachment}:std::vector<Vec3>{first,last};
    if(a.role==1)return {a.contact,join};
    auto delta=sub(join,a.contact);const double length=std::sqrt(dimension_dot(delta,delta));
    if(length<1e-9)return {};
    const auto direction=scale(delta,1/length);auto wing=dimension_cross(direction,dimension_cross(right,up));
    if(dimension_dot(wing,wing)<1e-12)wing=right;else wing=dimension_unit(wing);
    const double h=std::min(a.arrow_length,length*.5);const auto base=add(a.contact,scale(direction,h));
    if(a.role==4)return {base,add(a.contact,scale(wing,h*.5773502691896257)),add(a.contact,scale(wing,-h*.5773502691896257)),base};
    if(a.role==5||a.role==6) {
        std::vector<Vec3> circle;constexpr int segments=32;
        const auto center=a.role==6?join:a.contact;const double radius=a.role==6?a.arrow_length*.35:h*.25;
        for(int i=0;i<=segments;++i){const double angle=i*2.*std::acos(-1.)/segments;
            circle.push_back(add(center,add(scale(right,radius*std::cos(angle)),scale(up,radius*std::sin(angle)))));}
        return circle;
    }
    return {add(base,scale(wing,h*.1763269807)),a.contact,add(base,scale(wing,-h*.1763269807))};
}
// Three grips share the exact rendering geometry: arrow tip, elbow, shelf end.
inline std::array<Vec3,3> annotation_handles(AnnotationStroke a,Vec3 right,Vec3 up,bool left,bool spatial,bool fixed_length=false) {
    a.role=3;const auto shelf=annotation_stroke(a,right,up,left,spatial,fixed_length);
    return {a.contact,a.framed||left?shelf.front():shelf.back(),a.framed||left?shelf.back():shelf.front()};
}
struct AnnotationDrag {Vec3 grip;double shelf_length;};
inline AnnotationDrag drag_annotation(const AnnotationStroke& a,Vec3 right,Vec3 up,bool left,bool spatial,int handle,Vec3 target,bool fixed_length=false) {
    const auto points=annotation_handles(a,right,up,left,spatial,fixed_length);
    const auto grip=annotation_grip(a,right,up,left,spatial,fixed_length);
    if(handle==2&&(a.short_shelf||a.framed)){
        const double length=std::max(.1,dimension_dot(dimension_sub(target,points[1]),right)*(left?1.:-1.));
        if(fixed_length&&spatial&&a.perpendicular)return {a.grip,length};
        return {dimension_add(points[1],dimension_scale(right,(left?1.:-1.)*(length+(a.framed?std::max(0.,a.right-a.left)*.5:0.)))),length};
    }
    if(fixed_length&&spatial&&a.perpendicular) {
        const auto offset=dimension_sub(a.grip,a.contact);
        const double stored_length=std::sqrt(dimension_dot(offset,offset));
        const auto direction=dimension_unit(dimension_sub(points[1],a.contact));
        const auto delta=dimension_sub(target,points[handle]);
        const double change=dimension_dot(delta,direction);
        if(dimension_dot(delta,delta)<1e-20)return {a.grip,a.shelf_length};
        const double length=std::max(a.arrow_length*2.,std::max(a.arrow_length*2.,stored_length)+change);
        const auto moved=dimension_add(offset,delta);
        const auto stored_direction=dimension_dot(moved,moved)>1e-20?dimension_unit(moved):direction;
        return {dimension_add(a.contact,dimension_scale(stored_direction,length)),a.shelf_length};
    }
    return {dimension_add(grip,dimension_sub(target,points[handle])),a.shelf_length};
}
}
