#pragma once
#include "profile_solid_fixture.hpp"
#include <zima/document/part_document.hpp>
#include <cmath>
#include <numbers>
namespace zima::test {
inline document::PartDocument drill_point_fixture(document::PartDocument part=document::PartDocument::create_default()) {
    auto block=rectangular_feature(part,{40,40,40});
    auto large=circular_feature(part,5,21);
    large.placement.x=-8;large.combine_mode=document::CombineMode::Subtract;
    auto small=circular_feature(part,3,21);
    small.placement.x=8;small.combine_mode=document::CombineMode::Subtract;
    for(const auto& feature:{block,large,small}) {
        part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);part.history.push_back(feature);
    }
    part.resolve_constructions();
    return part;
}
inline double drilled_block_volume(double angle,bool large=true,bool small=true) {
    const double cone=(large?125.:0.)+(small?27.:0.);
    return 64000-680*std::numbers::pi-std::numbers::pi*cone/(3*std::tan(angle*std::numbers::pi/360));
}
} // namespace zima::test
