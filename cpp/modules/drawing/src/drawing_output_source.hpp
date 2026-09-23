#pragma once
#include <zima/document/viewer_packet_json.hpp>
#include <nlohmann/json.hpp>
namespace zima::drawing::detail {
// A drawing snapshot is a display packet, not only a calculated body. Preserve
// display flags and resolved analytic metadata which body packets may derive
// elsewhere from their owning Part history.
inline nlohmann::json serialize_output_source(const kernel::ViewerMesh& mesh) {
    kernel::BodyResult body;body.mesh=mesh;
    if(body.mesh.edges.empty())body.mesh.edges=mesh.original_references.edges;
    auto packet=document::serialize_body_result(body,false);
    const auto face=[](nlohmann::json& value,const kernel::FaceReference& ref){
        value["projection_surface"]=ref.surface?document::serialize_surface_geometry(*ref.surface):nlohmann::json(nullptr);
        value["projection_area"]=ref.measured_area?nlohmann::json(*ref.measured_area):nlohmann::json(nullptr);
        value["projection_surface_result"]=ref.surface_result;
    };
    for(std::size_t i=0;i<packet["triangle_references"].size();++i)face(packet["triangle_references"][i],mesh.triangle_references[i]);
    for(std::size_t i=0;i<body.mesh.edges.size();++i) {
        const auto& edge=body.mesh.edges[i];auto& saved=packet["edges"][i];
        saved["projection_flags"]={edge.construction,edge.overlay,edge.infinite,edge.dash_dot};
        saved["projection_display_owner"]=edge.reference.display_owner_id;
        for(std::size_t j=0;j<edge.edge_treatment_side_references.size();++j)face(saved["edge_treatment_side_references"][j],edge.edge_treatment_side_references[j]);
    }
    return packet;
}
inline kernel::ViewerMesh load_output_source(const nlohmann::json& packet) {
    auto mesh=document::load_body_result(packet).mesh;
    const auto face=[](const nlohmann::json& value,kernel::FaceReference& ref){
        if(!value.at("projection_surface").is_null())ref.surface=std::make_shared<const kernel::SurfaceGeometry>(document::load_surface_geometry(value.at("projection_surface")));
        if(!value.at("projection_area").is_null())ref.measured_area=value.at("projection_area").get<double>();
        ref.surface_result=value.at("projection_surface_result");
    };
    for(std::size_t i=0;i<packet.at("triangle_references").size();++i)face(packet.at("triangle_references")[i],mesh.triangle_references[i]);
    for(std::size_t i=0;i<mesh.edges.size();++i) {
        auto& edge=mesh.edges[i];const auto& saved=packet.at("edges")[i];const auto& flags=saved.at("projection_flags");
        edge.construction=flags.at(0);edge.overlay=flags.at(1);edge.infinite=flags.at(2);edge.dash_dot=flags.at(3);
        edge.reference.display_owner_id=saved.at("projection_display_owner").get<std::string>();
        for(std::size_t j=0;j<edge.edge_treatment_side_references.size();++j)face(saved.at("edge_treatment_side_references")[j],edge.edge_treatment_side_references[j]);
    }
    return mesh;
}
}
