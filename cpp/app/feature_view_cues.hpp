#pragma once
#include "workspace/workspace_internal.hpp"
#include <cmath>
#include <numbers>

namespace zima::app::workspace_detail {
struct FeatureViewCues {
    kernel::ViewerMesh dimensions;
    std::vector<viewer::ExtentManipulator> handles;
    std::vector<viewer::OperationDirectionIndicator> directions;
};

// Editing and ordinary parameter inspection consume the same authored values
// and resolved Sketch frame. No body topology or kernel calculation is needed.
inline FeatureViewCues feature_view_cues(const document::HistoryContainer& feature,
                                       const sketcher::Sketch& sketch) {
    FeatureViewCues out;
    const auto& p=feature.feature;
    if(p.type==document::FeatureType::Point)return out;
    const auto start=sketch.resolved_origin,normal=sketch.resolved_normal;
    const auto add=[](kernel::Vec3 a,kernel::Vec3 b){return kernel::Vec3{a.x+b.x,a.y+b.y,a.z+b.z};};
    const auto scale=[](kernel::Vec3 a,double b){return kernel::Vec3{a.x*b,a.y*b,a.z*b};};
    const auto linear=[&](std::string key,kernel::Vec3 a,kernel::Vec3 b,double value) {
        if(std::abs(value)<1e-12)return;
        const auto witness=scale(sketch.resolved_x_axis,8);
        out.dimensions.dimensions.push_back({a,b,add(a,witness),add(b,witness),value,
            {feature.id,"parameter:"+key,{}}});
    };
    linear("profile_offset",add(start,scale(normal,-p.profile_plane_offset)),start,p.profile_plane_offset);
    auto axis_id=p.axis_segment_id;
    if(axis_id.empty()) {
        const auto axis=std::ranges::find_if(sketch.segments,[](const auto& segment){return segment.construction&&segment.centerline;});
        if(axis!=sketch.segments.end())axis_id=axis->id;
    }
    const auto frame=revolution_cue_frame(sketch,axis_id);
    for(std::size_t side=0;side<2;++side) {
        const auto& settings=p.effective_side(side);
        const double sign=side==0?1.:-1.;
        const auto prefix=std::string("side")+(p.symmetric?"0":std::to_string(side));
        if(settings.operation==document::FeatureSideOperation::Extrusion) {
            const auto direction=scale(normal,sign);
            if(settings.extrusion_extent==document::EndCondition::Length) {
                linear(prefix+"_length",start,add(start,scale(direction,settings.length)),settings.length);
                out.handles.push_back({"feature_profile_start"+std::to_string(side),prefix+"_length",start,direction,settings.length,false});
            } else out.directions.push_back({start,direction,{},0.,false});
        } else if(settings.operation==document::FeatureSideOperation::Revolution && frame &&
                  settings.rotation_extent==document::FeatureRotationExtent::Angle) {
            const double radians=sign*settings.angle_degrees*std::numbers::pi/180.;
            const auto radial=frame->radial,axis=frame->axis;
            const kernel::Vec3 tangent{axis.y*radial.z-axis.z*radial.y,axis.z*radial.x-axis.x*radial.z,axis.x*radial.y-axis.y*radial.x};
            const auto rotated=add(scale(radial,std::cos(radians)),scale(tangent,std::sin(radians)));
            const auto endpoint=add(frame->center,rotated);
            out.directions.push_back({frame->center,axis,radial,sign*settings.angle_degrees,true});
            out.handles.push_back({prefix+"_angle",{},endpoint,
                scale(add(scale(radial,-std::sin(radians)),scale(tangent,std::cos(radians))),sign),0.,true});
            out.dimensions.dimensions.push_back({frame->center,frame->center,
                add(frame->center,scale(radial,1.28)),add(frame->center,scale(rotated,1.28)),settings.angle_degrees,
                {feature.id,"parameter:"+prefix+"_angle",{}},"","°"});
            auto& dimension=out.dimensions.dimensions.back();
            dimension.kind=kernel::ViewerDimensionKind::Angular;dimension.plane_normal=axis;
            dimension.sweep_degrees=sign*settings.angle_degrees;
        }
    }
    return out;
}
} // namespace zima::app::workspace_detail
