#pragma once
#include <zima/document/part_document.hpp>
#include <zima/document/relations.hpp>
#include <zima/kernel/stable_id.hpp>
#include <cmath>

namespace zima::document {
// Library graphics and free-form material/manufacturer descriptions do not
// define machining dimensions. Copy only the explicitly authored CAM inputs;
// the destination setup has no dependency on the selected library file.
inline cam::Tool cam_library_tool(const PartDocument& source) {
    try {
        const auto number=[&](const char* key){return evaluate_numeric_expression(source.user_parameters.at(key));};
        const auto cutting=source.user_parameters.at("cam_cutting_body"),shank=source.user_parameters.at("cam_shank_body");
        if(cutting==shank||!source.body_history.find(cutting)||!source.body_history.find(shank))throw std::invalid_argument("roles");
        const auto diameter=number("cam_diameter_mm"),length=number("cam_cutting_length_mm"),
            shank_diameter=number("cam_shank_diameter_mm"),overall=number("cam_overall_length_mm");
        if(!(length>0&&overall>length&&diameter>0&&shank_diameter>0))throw std::invalid_argument("dimensions");
        cam::Tool result;result.id=kernel::make_stable_id();result.name=source.name;
        result.cutting={{-length,diameter/2},{0,diameter/2}};result.non_cutting={{0,shank_diameter/2},{overall-length,shank_diameter/2}};
        result.feed_mm_min=number("cam_feed_mm_min");result.spindle_rpm=number("cam_spindle_rpm");
        cam::validate(result);return result;
    }catch(const std::exception&){throw std::invalid_argument("The tool definition needs valid CAM dimensions, cutting and shank Body roles, feed and spindle speed.");}
}
inline cam::Machine cam_library_machine(const PartDocument& source) {
    try {
        const auto number=[&](const char* key){return evaluate_numeric_expression(source.user_parameters.at(key));};
        cam::Machine result;result.id=kernel::make_stable_id();result.name=source.name;
        const auto kind=source.user_parameters.at("cam_machine_kind");
        if(kind=="milling")result.kind=cam::MachineKind::Milling;else if(kind=="turning")result.kind=cam::MachineKind::Turning;else throw std::invalid_argument("kind");
        const auto axes=number("cam_machine_axes");if(!std::isfinite(axes)||axes<2||axes>5||std::floor(axes)!=axes)throw std::invalid_argument("axes");result.axes=unsigned(axes);
        result.minimum={number("cam_minimum_x_mm"),number("cam_minimum_y_mm"),number("cam_minimum_z_mm")};
        result.maximum={number("cam_maximum_x_mm"),number("cam_maximum_y_mm"),number("cam_maximum_z_mm")};
        result.maximum_feed_mm_min=number("cam_maximum_feed_mm_min");result.maximum_spindle_rpm=number("cam_maximum_spindle_rpm");result.rapid_mm_min=number("cam_rapid_mm_min");
        result.postprocessor_id=source.user_parameters.at("cam_postprocessor");cam::validate(result);return result;
    }catch(const std::exception&){throw std::invalid_argument("The machine definition needs valid machining type, axes, travel, feed and spindle limits.");}
}
} // namespace zima::document
