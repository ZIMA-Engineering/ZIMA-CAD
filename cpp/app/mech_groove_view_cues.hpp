#pragma once
#include <zima/document/part_document.hpp>
#include <zima/kernel/dimension_layout.hpp>

namespace zima::app {
inline std::vector<kernel::ViewerDimension> mech_groove_view_dimensions(const document::HistoryContainer& owner) {
    using namespace kernel;
    const auto& p=owner.mech_groove;if(p.section.empty())return {};
    const auto section=sketcher::Sketch::from_serialized(p.section);
    const auto point=[&](double r,double z){return dimension_add(section.resolved_origin,
        dimension_add(dimension_scale(section.resolved_x_axis,r),dimension_scale(section.resolved_y_axis,z)));};
    const double radius=p.support_diameter/2,floor=radius+(p.internal?p.depth:-p.depth);
    std::vector<ViewerDimension> result;
    const auto linear=[&](const char* key,Vec3 a,Vec3 b,double value,Vec3 offset,bool driving) {
        ViewerDimension d{a,b,dimension_add(a,offset),dimension_add(b,offset),value,{owner.id,std::string("parameter:groove_")+key,{}}};
        d.plane_normal=section.resolved_normal;d.driving=driving;result.push_back(std::move(d));
    };
    linear("width",point(radius,-p.width/2),point(radius,p.width/2),p.width,
        dimension_scale(section.resolved_x_axis,8),p.profile==document::GrooveProfile::Rectangular);
    linear("depth",point(radius,0),point(floor,0),p.depth,
        dimension_scale(section.resolved_y_axis,p.width/2+8),p.catalogue.empty());
    if(p.profile==document::GrooveProfile::Circular) {
        ViewerDimension d;d.reference={owner.id,"parameter:groove_radius",{}};d.kind=ViewerDimensionKind::Radius;
        d.witness_first=point(radius+(p.internal?1.:-1.)*(p.depth-p.radius),0);
        d.witness_second=point(floor,0);d.line_first=d.witness_first;d.line_second=d.witness_second;
        d.value=p.radius;d.plane_normal=section.resolved_normal;d.driving=p.catalogue.empty();
        d.label_position=dimension_add(d.witness_second,dimension_scale(section.resolved_y_axis,-p.width/2-8));result.push_back(std::move(d));
    }
    return result;
}
}
