#pragma once
#include "profile_solid_fixture.hpp"

namespace zima::test {
inline document::PartDocument failed_history_measurement(bool following_cut) {
    auto part=document::PartDocument::create_default();
    auto stock=rectangular_feature(part,{10,8,6});
    auto failed=rectangular_feature(part,{2,2,2});failed.name="Failed end limit";
    auto plane=document::PartDocument::create_construction(document::ConstructionKind::Plane);
    plane.base_plane=document::LocalDatumPlane::XY;plane.offset=-10;
    failed.extrusion.extent_mode=document::ProfileExtentMode::OneSide;
    failed.extrusion.end_condition_forward=document::EndCondition::UpTo;
    failed.extrusion.end_targets_forward={{document::EndTargetKind::Plane,
        {plane.entity_id,"plane",{}},"Behind profile",{0,0,-10},{0,0,1},{}}};
    part.history={stock,failed};part.constructions={plane};
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Measured Body"));
    graph.insert({document::PartHistoryKind::Feature,stock.id});
    graph.insert({document::PartHistoryKind::Construction,plane.id});
    graph.insert({document::PartHistoryKind::Feature,failed.id});
    if(following_cut) {
        auto cut=rectangular_feature(part,{2,8,6});cut.combine_mode=document::CombineMode::Subtract;
        part.history.push_back(cut);graph.insert({document::PartHistoryKind::Feature,cut.id});
    }
    part.set_body_history(graph);part.resolve_constructions();
    part.physical_parameters["MASS_DENSITY"]="7850";
    part.physical_parameter_units["MASS_DENSITY"]="kg/m^3";
    return part;
}
}
