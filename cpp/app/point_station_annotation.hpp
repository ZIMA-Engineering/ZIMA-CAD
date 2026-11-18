#pragma once
#include <zima/document/placement_types.hpp>
#include <zima/kernel/geometry_kernel.hpp>
#include <optional>

namespace zima::app {
// Display the already authored route station. The existing reference-offset
// parameter remains the sole editable value; inspection never solves a route.
inline std::optional<kernel::ViewerDimension> point_station_annotation(
        const std::string& owner,const kernel::Vec3& point,
        const std::vector<document::ConstructionReference>& references,bool valid) {
    if(!valid)return {};
    std::size_t index{};
    for(const auto& reference:references) {
        if(reference.orientation_only||(reference.owner_id.empty()&&reference.semantic_key.empty()))continue;
        const auto row=index++;
        if(reference.station_mode==document::PlacementStationMode::None)continue;
        const bool fraction=reference.station_mode==document::PlacementStationMode::Fraction;
        kernel::ViewerDimension dimension;
        dimension.reference={owner,"parameter:placement:reference_offset:"+std::to_string(row),{}};
        dimension.value=reference.offset;dimension.label_only=true;
        dimension.label_prefix=fraction?"t = ":"s = ";dimension.unit_suffix=fraction?"":"mm";
        dimension.witness_first=dimension.witness_second=dimension.line_first=dimension.line_second=point;
        dimension.label_position=kernel::Vec3{point.x+8,point.y+8,point.z};
        dimension.locked=reference.offset_locked;
        dimension.value_lock_key="placement:reference_offset:"+std::to_string(row);
        return dimension;
    }
    return {};
}
}
