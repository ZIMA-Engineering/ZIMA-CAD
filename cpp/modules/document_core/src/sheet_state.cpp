#include <zima/document/sheet_state.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/flat.hpp>

namespace zima::document {
kernel::SheetMaterialDefinition sheet_material_definition(const HistoryContainer& feature,
        const sketcher::Sketch& sketch,const kernel::PrimitiveRequest& primitive,const SheetMetalDefaults& defaults) {
    using namespace kernel::sheet_material;
    if(feature.feature_kind==FeatureKind::Bend)return bend_material_definition(feature,sketch,defaults);
    kernel::SheetMaterialDefinition region;region.owner_id=feature.id;
    region.origin=sketch.resolved_origin;region.along=sketch.resolved_x_axis;
    region.tangent=sketch.resolved_y_axis;region.radial=sketch.resolved_normal;
    if(feature.feature_kind==FeatureKind::Flat) {region.thickness=flat_thickness(feature,defaults);return region;}
    const auto& request=std::get<kernel::RevolutionRequest>(primitive);
    const auto& segment=*std::ranges::find_if(sketch.segments,[](const auto& s){return !s.construction;});
    const auto* first=sketch.find_point(segment.first_point_id);const auto* last=sketch.find_point(segment.second_point_id);
    const auto a=sketch.world_point(first->x,first->y),b=sketch.world_point(last->x,last->y);
    const auto along=unit(sub(b,a));const auto offset=unit(cross(sketch.resolved_normal,along));
    const auto axis=unit(request.axis_direction);
    region.origin=add(a,mul(offset,request.wall->first_offset));
    const auto radial=sub(sub(region.origin,request.axis_point),mul(axis,dot(sub(region.origin,request.axis_point),axis)));
    region.radius=std::sqrt(dot(radial,radial));region.radial=unit(radial);
    region.along=axis;region.tangent=cross(axis,region.radial);
    region.kind=std::abs(dot(along,axis))>1-1e-9?kernel::SheetMaterialDefinition::Kind::Cylinder:kernel::SheetMaterialDefinition::Kind::Cone;
    region.thickness=request.wall->second_offset-request.wall->first_offset;
    auto normal=region.radial;
    if(region.kind==kernel::SheetMaterialDefinition::Kind::Cone) {
        const double axial=dot(along,axis),radial_slope=dot(along,region.radial);
        if(std::abs(axial)<1e-9||std::abs(radial_slope)<1e-9)
            throw std::invalid_argument("Revolved sheet material requires a cylinder or a nondegenerate cone.");
        region.along=mul(axis,axial*radial_slope>0?1:-1);
        region.cone_half_angle=std::atan(std::abs(radial_slope/axial));
        normal=cone_normal(region);
    }
    region.thickness_sign=dot(offset,normal)>0?1:-1;
    region.neutral_radius=region.radius+region.thickness_sign*region.thickness*
        (region.thickness_sign>0?defaults.k_factor:1-defaults.k_factor)*std::cos(region.cone_half_angle);
    region.angle=request.angle_degrees*std::numbers::pi/180;
    const double angle=request.start_angle_degrees*std::numbers::pi/180;
    const auto rotate=[&](kernel::Vec3 v){return add(add(mul(v,std::cos(angle)),mul(cross(axis,v),std::sin(angle))),mul(axis,dot(axis,v)*(1-std::cos(angle))));};
    region.origin=add(request.axis_point,rotate(sub(region.origin,request.axis_point)));
    region.tangent=rotate(region.tangent);region.radial=rotate(region.radial);
    return region;
}
}
