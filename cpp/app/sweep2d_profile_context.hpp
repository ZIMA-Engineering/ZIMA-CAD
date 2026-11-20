#pragma once
#include <zima/document/part_document.hpp>
#include <map>
#include <set>

namespace zima::app {
// Use the same positioned curves and correspondence points as Properties.
// The carrier is presentation only; native per-side profile ownership is intact.
inline kernel::ViewerMesh sweep2d_profile_context(
        const document::HistoryContainer& container,unsigned active_stage) {
    if(active_stage==0)return {};
    const auto& active=container.sweep2d.profiles.at(active_stage-1);
    const auto route=document::PartDocument::sweep2d_route(container);
    auto mesh=[&] {
        if(container.sweep2d.result_type==document::ProfileResultType::Thin) {
            auto profile=container;
            // Sketch context shows the authored curve, including open profiles,
            // rather than the offset material contour of a Thin result.
            profile.sweep2d.result_type=document::ProfileResultType::Surface;
            return document::PartDocument::sweep2d_preview_mesh(profile);
        }
        return document::PartDocument::sweep2d_preview_mesh(container);
    }();
    std::map<std::string,std::size_t> station_numbers;
    std::map<std::string,std::size_t> shown_keys;
    std::set<std::string> shown_points;
    for(const auto& station:route.stations) {
        const auto [number,inserted]=station_numbers.emplace(station.point_id,station_numbers.size()+1);
        if(station.point_id==active.point_id)continue;
        const bool first=shown_points.insert(station.point_id).second;
        const bool explicit_profile=std::ranges::any_of(container.sweep2d.profiles,[&](const auto& profile){
            return profile.point_id==station.point_id&&profile.incoming==station.incoming&&
                document::sweep3d_profile_has_geometry(sketcher::Sketch::from_serialized(profile.sketch_serialized));
        });
        if(first||explicit_profile)
            shown_keys.emplace(station.point_id+(station.incoming?":in:":":out:"),number->second);
    }
    const auto number_for=[&](const std::string& semantic,std::string_view prefix)->std::optional<std::size_t> {
        if(!semantic.starts_with(prefix))return std::nullopt;
        const auto suffix=std::string_view(semantic).substr(prefix.size());
        for(const auto& [key,number]:shown_keys)if(suffix.starts_with(key))return number;
        return std::nullopt;
    };
    std::erase_if(mesh.edges,[&](const auto& edge){
        return !edge.reference.semantic_key.starts_with("sweep2d:path:")&&
            !number_for(edge.reference.semantic_key,"sweep2d:profile:");
    });
    std::erase_if(mesh.points,[&](const auto& point){return !number_for(point.reference.semantic_key,"sweep2d:profile-point:");});
    for(auto& point:mesh.points) {
        const auto marker=std::ranges::find_if(mesh.constraint_markers,[&](const auto& item){
            return item.reference.semantic_key==point.reference.semantic_key;
        });
        if(marker!=mesh.constraint_markers.end())
            point.label=std::to_string(*number_for(point.reference.semantic_key,"sweep2d:profile-point:"))+" · "+
                marker->label.substr(0,marker->label.find(' '));
    }
    mesh.constraint_markers.clear();
    return mesh;
}
}
