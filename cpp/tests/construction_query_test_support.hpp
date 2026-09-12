#pragma once
#include <zima/command_host/host.hpp>
#include <stdexcept>

namespace zima::test {
inline document::PartDocument construction_query_fixture() {
    auto part = document::PartDocument::create_default();
    part.name = "Konstrukce žluťoučké";
    document::BodyHistoryGraph bodies;
    const auto body_id = bodies.create_body("Nosné těleso");
    auto body = *bodies.find(body_id);
    body.scope.placement.x = 100; body.scope.placement.rotation_z = 90;
    bodies.update_body(std::move(body));
    part.set_body_history(std::move(bodies));
    for (const auto kind : {document::ConstructionKind::Point, document::ConstructionKind::Axis,
            document::ConstructionKind::Plane, document::ConstructionKind::Curve3D}) {
        auto object = document::PartDocument::create_construction(kind);
        object.name = "Stejné jméno"; // Queries must use IDs, never labels.
        object.origin = {3, 4, 5}; object.rotation = {10, 20, 30};
        object.absolute_rotation = object.rotation;
        object.value_locks = {"placement:x"};
        if (kind == document::ConstructionKind::Plane) {
            object.base_plane = document::LocalDatumPlane::XY;
            object.direction = {0, 0, 1}; object.offset = 7;
            object.entity_origin = {3, 4, 12};
            object.reference_valid = false;
            object.references.push_back({"repeat/leaf", "missing-source", "plane", 2.5});
            object.references.back().offset_locked = true;
        }
        if (kind == document::ConstructionKind::Curve3D) {
            object.curve_type = document::Curve3DType::InterpolatingSpline;
            object.curve_rounding_enabled = true; object.suppressed = true;
            for (int i = 0; i < 3; ++i) {
                auto point = document::PartDocument::create_construction(document::ConstructionKind::Point);
                point.parent_construction_id = object.id;
                point.origin = {double(i * 10), double(i * i), -2};
                point.curve_tangent = document::Curve3DTangentMode::NegativeY;
                point.curve_tangent_enabled = i == 1;
                point.curve_radius = i == 1 ? 0.125 : 0;
                object.curve_points.push_back(std::move(point));
            }
        }
        part.insert_history_entry(document::PartHistoryKind::Construction, object.id);
        part.constructions.push_back(std::move(object));
    }
    return part;
}
inline void check_construction_child(const commands::Json& value, const document::ConstructionObject& curve,
    const std::string& body) {
    if (value.at("construction") != curve.curve_points[1].id || value.at("parent") != curve.id ||
        value.at("coordinate_owner") != curve.id || value.at("body") != body ||
        value.at("coordinate_system") != "parent_construction" ||
        value.at("origin_mm") != commands::Json::array({10, 1, -2}) ||
        value.at("radius_mm") != 0.125 || value.at("tangent") != "-y" ||
        value.at("tangent_enabled") != true || value.at("parent_suppressed") != true ||
        value.at("suppressed") != false)
        throw std::runtime_error("Construction child query lost identity, local coordinates or curve controls");
}
} // namespace zima::test
