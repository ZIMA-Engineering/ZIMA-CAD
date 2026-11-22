#pragma once
#include <zima/document/part_document.hpp>
#include <zima/document/extrusion_dimension_policy.hpp>
#include <zima/kernel/dimension_layout.hpp>

namespace zima::document {
// The authored extent, shared by model inspection and Drawing Show/Erase.
// Consume the resolved Sketch frame; never infer a length from result topology.
inline std::optional<kernel::ViewerDimension> feature_side_length_dimension(
        const HistoryContainer& feature, const sketcher::Sketch& sketch,
        std::size_t side) {
    const auto& p=feature.feature;
    const auto settings=p.effective_side(side);
    if((p.type!=FeatureType::Modeling&&p.type!=FeatureType::Axis)||
       (p.type==FeatureType::Axis&&!settings.axis_enabled)||
       settings.operation!=FeatureSideOperation::Extrusion||
       settings.extrusion_extent!=EndCondition::Length||
       std::abs(settings.length)<1e-12)return {};
    const auto start=sketch.resolved_origin;
    const auto end=kernel::dimension_add(start,kernel::dimension_scale(
        sketch.resolved_normal,p.side_direction_sign(side)*settings.length));
    const auto offset=kernel::dimension_scale(sketch.resolved_x_axis,8.);
    kernel::ViewerDimension result{start,end,kernel::dimension_add(start,offset),
        kernel::dimension_add(end,offset),settings.length,
        {feature.id,"parameter:side"+std::string(p.symmetric?"0":std::to_string(side))+"_length",{}}};
    result.plane_normal=kernel::dimension_unit(kernel::dimension_cross(kernel::dimension_sub(end,start),offset));
    return result;
}

inline kernel::ViewerMesh feature_length_dimensions(
        const HistoryContainer& feature,const sketcher::Sketch& sketch) {
    kernel::ViewerMesh result;
    if(feature.feature_kind==FeatureKind::Feature)
        for(std::size_t side=0;side<2;++side)
            if(auto dimension=feature_side_length_dimension(feature,sketch,side))
                result.dimensions.push_back(std::move(*dimension));
    if(feature.feature_kind==FeatureKind::Extrusion) {
        const auto& p=feature.extrusion;
        const auto& placement=feature.placement;
        const auto frame=kernel::annotation_frame({placement.x,placement.y,placement.z},
            {placement.rotation_x,placement.rotation_y,placement.rotation_z});
        const auto normal=sketch.plane==sketcher::SketchPlane::XY?kernel::Vec3{0,0,1}:
            sketch.plane==sketcher::SketchPlane::XZ?kernel::Vec3{0,1,0}:kernel::Vec3{1,0,0};
        const auto local_start=kernel::dimension_scale(normal,p.profile_plane_offset);
        const auto start=frame.world(local_start);
        for(bool reverse:{false,true})if(auto length=extrusion_length_dimension_value(p,reverse)) {
            const double sign=(p.direction==ExtrusionDirection::Reverse?-1.:1.)*(reverse?-1.:1.);
            const auto end=frame.world(kernel::dimension_add(local_start,kernel::dimension_scale(normal,sign * *length)));
            const double offset_sign=reverse?-1.:1.;
            const kernel::Vec3 preferred{8*offset_sign,8*offset_sign,0};
            const auto perpendicular=kernel::dimension_sub(preferred,kernel::dimension_scale(normal,kernel::dimension_dot(preferred,normal)));
            const auto offset=kernel::dimension_sub(frame.world(kernel::dimension_scale(kernel::dimension_unit(perpendicular),std::sqrt(128.))),frame.world({}));
            const auto key=reverse&&p.extent_mode!=ProfileExtentMode::Symmetric?"length_reverse":"length_forward";
            result.dimensions.push_back({start,end,kernel::dimension_add(start,offset),kernel::dimension_add(end,offset),*length,
                {feature.id,std::string("parameter:")+key,{}}});
            result.dimensions.back().plane_normal=kernel::dimension_unit(kernel::dimension_cross(kernel::dimension_sub(end,start),offset));
        }
    }
    return result;
}
} // namespace zima::document
