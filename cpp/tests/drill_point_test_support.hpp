#pragma once
#include <zima/document/part_document.hpp>
#include <cmath>
#include <numbers>
namespace zima::test {
inline document::PartDocument drill_point_fixture(document::PartDocument part=document::PartDocument::create_default()) {
    auto block=document::PartDocument::create_box_container();block.box={40,40,40};
    auto large=document::PartDocument::create_cylinder_container();large.cylinder.radius=5;large.cylinder.height=21;
    large.placement.x=-8;large.combine_mode=document::CombineMode::Subtract;
    auto small=document::PartDocument::create_cylinder_container();small.cylinder.radius=3;small.cylinder.height=21;
    small.placement.x=8;small.combine_mode=document::CombineMode::Subtract;
    for(const auto& feature:{block,large,small}) {
        part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);part.history.push_back(feature);
    }
    return part;
}
inline double drilled_block_volume(double angle,bool large=true,bool small=true) {
    const double cone=(large?125.:0.)+(small?27.:0.);
    return 64000-680*std::numbers::pi-std::numbers::pi*cone/(3*std::tan(angle*std::numbers::pi/360));
}
} // namespace zima::test
