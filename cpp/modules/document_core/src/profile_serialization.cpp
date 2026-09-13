#include <zima/document/profile_serialization.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>
namespace zima::document {
namespace {
void require_positive(double value, const char* field) {
    if (!std::isfinite(value) || value <= 0) throw std::runtime_error(std::string(field) + " must be finite and positive");
}
}
void load_profile_parameters(HistoryContainer& container, const nlohmann::json& source) {
    if (container.feature_kind == FeatureKind::Extrusion) {
            container.extrusion.sketch_id = source.at("sketch_id").get<std::string>();
            container.extrusion.profile_source = source.at("profile_source") == "internal"
                ? ProfileSource::Internal : source.at("profile_source") == "external"
                    ? ProfileSource::External
                    : throw std::runtime_error("Invalid profile source");
            container.extrusion.result_type = source.at("result_type") == "solid"
                ? ProfileResultType::Solid : source.at("result_type") == "thin"
                    ? ProfileResultType::Thin
                    : throw std::runtime_error("Invalid profile result type");
            container.extrusion.thin_thickness = source.at("thin_thickness");
            container.extrusion.profile_plane_offset =
                source.at("profile_plane_offset");
            container.extrusion.thin_mode = source.at("thin_mode") == "one_side"
                ? ThinMode::OneSide : source.at("thin_mode") == "other_side"
                    ? ThinMode::OtherSide : source.at("thin_mode") == "symmetric"
                        ? ThinMode::Symmetric
                        : throw std::runtime_error("Invalid thin mode");
            container.extrusion.extent_mode = source.at("extent_mode") == "one_side"
                ? ProfileExtentMode::OneSide : source.at("extent_mode") == "two_sides"
                    ? ProfileExtentMode::TwoSides : source.at("extent_mode") == "symmetric"
                        ? ProfileExtentMode::Symmetric
                        : throw std::runtime_error("Invalid profile extent mode");
            container.extrusion.length_forward = source.at("length_forward");
            container.extrusion.length_reverse = source.at("length_reverse");
            const auto parse_condition = [](const nlohmann::json& value) {
                return value == "length" ? EndCondition::Length
                    : value == "up_to" ? EndCondition::UpTo
                    : value == "through_all" ? EndCondition::ThroughAll
                    : throw std::runtime_error("Invalid profile end condition");
            };
            container.extrusion.end_condition_forward =
                parse_condition(source.at("end_condition_forward"));
            container.extrusion.end_condition_reverse =
                parse_condition(source.at("end_condition_reverse"));
            const auto parse_targets = [](const nlohmann::json& values) {
                std::vector<ExtrusionParameters::EndTarget> result;
                for (const auto& value : values) {
                    ExtrusionParameters::EndTarget target;
                    target.kind = value.at("kind") == "point" ? EndTargetKind::Point
                        : value.at("kind") == "plane" ? EndTargetKind::Plane
                        : value.at("kind") == "face" ? EndTargetKind::Face
                        : throw std::runtime_error("Invalid extrusion end target kind");
                    target.reference = {value.at("owner"), value.at("key"),
                                        value.at("instance_path")};
                    target.label = value.at("label");
                    target.fallback_origin = {value.at("origin").at(0),
                        value.at("origin").at(1), value.at("origin").at(2)};
                    target.fallback_normal = {value.at("normal").at(0),
                        value.at("normal").at(1), value.at("normal").at(2)};
                    for (const auto& point : value.at("triangles")) {
                        target.fallback_triangles.push_back(
                            {point.at(0), point.at(1), point.at(2)});
                    }
                    if (!target.reference.valid()) {
                        throw std::runtime_error("Invalid extrusion end target reference");
                    }
                    result.push_back(std::move(target));
                }
                return result;
            };
            container.extrusion.end_targets_forward =
                parse_targets(source.at("end_targets_forward"));
            container.extrusion.end_targets_reverse =
                parse_targets(source.at("end_targets_reverse"));
            require_positive(container.extrusion.thin_thickness, "thin thickness");
            if (!std::isfinite(container.extrusion.profile_plane_offset)) {
                throw std::runtime_error("Invalid Extrusion profile-plane offset");
            }
            require_positive(container.extrusion.length_forward, "forward length");
            require_positive(container.extrusion.length_reverse, "reverse length");
            container.extrusion.height = source.at("height").get<double>();
            const std::string direction =
                source.at("direction").get<std::string>();
            if (direction == "forward") {
                container.extrusion.direction = ExtrusionDirection::Forward;
            } else if (direction == "reverse") {
                container.extrusion.direction = ExtrusionDirection::Reverse;
            } else if (direction == "symmetric") {
                container.extrusion.direction = ExtrusionDirection::Symmetric;
            } else {
                throw std::runtime_error("Invalid Extrusion direction");
            }
            if (container.extrusion.sketch_id.empty()) {
                throw std::runtime_error("Extrusion Sketch ID is required");
            }
            require_positive(container.extrusion.height, "extrusion height");
            const std::string extent = source.at("extent").get<std::string>();
            container.extrusion.extent = extent == "up_to_plane"
                ? ExtrusionExtent::UpToPlane
                : extent == "up_to_surface" ? ExtrusionExtent::UpToSurface
                : extent == "through_all" ? ExtrusionExtent::ThroughAll
                : extent == "blind" ? ExtrusionExtent::Blind
                : throw std::runtime_error("Invalid Extrusion extent");
            if (container.extrusion.extent == ExtrusionExtent::UpToPlane) {
                if (container.extrusion.direction == ExtrusionDirection::Symmetric) {
                    throw std::runtime_error(
                        "Up-to-plane Extrusion requires forward or reverse direction");
                }
                container.extrusion.target_face = {
                    source.at("target_owner").get<std::string>(),
                    source.at("target_key").get<std::string>(),
                    source.at("target_path").get<std::string>()};
                container.extrusion.target_plane_origin = {
                    source.at("target_origin_x").get<double>(),
                    source.at("target_origin_y").get<double>(),
                    source.at("target_origin_z").get<double>()};
                container.extrusion.target_plane_normal = {
                    source.at("target_normal_x").get<double>(),
                    source.at("target_normal_y").get<double>(),
                    source.at("target_normal_z").get<double>()};
                const auto& normal = container.extrusion.target_plane_normal;
                const double normal_length = std::sqrt(
                    normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                if (!container.extrusion.target_face.valid() || normal_length <= 1e-12) {
                    throw std::runtime_error("Invalid Extrusion target plane");
                }
            }
            if (container.extrusion.extent == ExtrusionExtent::UpToSurface) {
                container.extrusion.target_face = {
                    source.at("target_owner").get<std::string>(),
                    source.at("target_key").get<std::string>(),
                    source.at("target_path").get<std::string>()};
                for (const auto& point : source.at("target_triangles")) {
                    container.extrusion.target_surface_triangles.push_back({
                        point.at(0).get<double>(), point.at(1).get<double>(),
                        point.at(2).get<double>()});
                }
                if (!container.extrusion.target_face.valid() ||
                    container.extrusion.target_surface_triangles.empty() ||
                    container.extrusion.target_surface_triangles.size() % 3 != 0) {
                    throw std::runtime_error("Invalid Extrusion target surface");
                }
            }
            if (container.extrusion.extent == ExtrusionExtent::ThroughAll &&
                container.combine_mode != CombineMode::Subtract) {
                throw std::runtime_error("Through-all Extrusion must subtract");
            }
        } else if (container.feature_kind == FeatureKind::Revolution) {
            container.revolution.sketch_id =
                source.at("sketch_id").get<std::string>();
            container.revolution.profile_source = source.at("profile_source") == "internal"
                ? ProfileSource::Internal : source.at("profile_source") == "external"
                    ? ProfileSource::External
                    : throw std::runtime_error("Invalid Revolution profile source");
            container.revolution.result_type = source.at("result_type") == "solid"
                ? ProfileResultType::Solid : source.at("result_type") == "thin"
                    ? ProfileResultType::Thin
                    : throw std::runtime_error("Invalid Revolution result type");
            container.revolution.thin_thickness = source.at("thin_thickness");
            container.revolution.profile_plane_offset =
                source.at("profile_plane_offset");
            const std::string thin_mode = source.at("thin_mode");
            container.revolution.thin_mode = thin_mode == "one_side"
                ? ThinMode::OneSide : thin_mode == "other_side"
                    ? ThinMode::OtherSide : thin_mode == "symmetric"
                        ? ThinMode::Symmetric
                        : throw std::runtime_error("Invalid Revolution thin mode");
            const std::string extent_mode = source.at("extent_mode");
            container.revolution.extent_mode = extent_mode == "one_side"
                ? ProfileExtentMode::OneSide : extent_mode == "two_sides"
                    ? ProfileExtentMode::TwoSides : extent_mode == "symmetric"
                        ? ProfileExtentMode::Symmetric
                        : throw std::runtime_error("Invalid Revolution extent mode");
            const std::string direction = source.at("direction");
            container.revolution.direction = direction == "forward"
                ? ExtrusionDirection::Forward : direction == "reverse"
                    ? ExtrusionDirection::Reverse
                    : throw std::runtime_error("Invalid Revolution direction");
            container.revolution.angle_reverse = source.at("angle_reverse");
            require_positive(container.revolution.thin_thickness, "Revolution thin thickness");
            if (!std::isfinite(container.revolution.profile_plane_offset)) {
                throw std::runtime_error("Invalid Revolution profile-plane offset");
            }
            require_positive(container.revolution.angle_reverse, "reverse angle");
            container.revolution.angle_degrees =
                source.at("angle_degrees").get<double>();
            container.revolution.axis_segment_id =
                source.at("axis_segment_id").get<std::string>();
            if (container.revolution.sketch_id.empty() ||
                !std::isfinite(container.revolution.angle_degrees) ||
                container.revolution.angle_degrees <= 0.0 ||
                container.revolution.angle_degrees > 360.0 ||
                container.revolution.angle_degrees +
                    (container.revolution.extent_mode == ProfileExtentMode::OneSide
                        ? 0.0
                        : container.revolution.extent_mode == ProfileExtentMode::Symmetric
                            ? container.revolution.angle_degrees
                            : container.revolution.angle_reverse) > 360.0) {
                throw std::runtime_error("Invalid Revolution parameters");
            }

    } else throw std::runtime_error("Expected an Extrusion or Revolution definition");
}
void save_profile_parameters(const HistoryContainer& container, nlohmann::json& serialized) {
    if (container.feature_kind == FeatureKind::Extrusion) {
            serialized["sketch_id"] = container.extrusion.sketch_id;
            serialized["profile_source"] = container.extrusion.profile_source ==
                    ProfileSource::Internal ? "internal" : "external";
            serialized["result_type"] = container.extrusion.result_type ==
                    ProfileResultType::Solid ? "solid" : "thin";
            serialized["thin_thickness"] = container.extrusion.thin_thickness;
            serialized["profile_plane_offset"] =
                container.extrusion.profile_plane_offset;
            serialized["thin_mode"] = container.extrusion.thin_mode == ThinMode::OneSide
                ? "one_side" : container.extrusion.thin_mode == ThinMode::OtherSide
                    ? "other_side" : "symmetric";
            serialized["extent_mode"] = container.extrusion.extent_mode ==
                    ProfileExtentMode::OneSide ? "one_side"
                : container.extrusion.extent_mode == ProfileExtentMode::TwoSides
                    ? "two_sides" : "symmetric";
            serialized["length_forward"] = container.extrusion.length_forward;
            serialized["length_reverse"] = container.extrusion.length_reverse;
            const auto condition_name = [](EndCondition condition) {
                return condition == EndCondition::Length ? "length"
                    : condition == EndCondition::UpTo ? "up_to" : "through_all";
            };
            serialized["end_condition_forward"] =
                condition_name(container.extrusion.end_condition_forward);
            serialized["end_condition_reverse"] =
                condition_name(container.extrusion.end_condition_reverse);
            const auto target_json = [](const auto& targets) {
                nlohmann::json result = nlohmann::json::array();
                for (const auto& target : targets) {
                    nlohmann::json triangles = nlohmann::json::array();
                    for (const auto& point : target.fallback_triangles) {
                        triangles.push_back({point.x, point.y, point.z});
                    }
                    result.push_back({{"kind", target.kind == EndTargetKind::Point
                            ? "point" : target.kind == EndTargetKind::Plane
                                ? "plane" : "face"},
                        {"owner", target.reference.owner_id},
                        {"key", target.reference.semantic_key},
                        {"instance_path", target.reference.instance_path},
                        {"label", target.label},
                        {"origin", {target.fallback_origin.x,
                            target.fallback_origin.y, target.fallback_origin.z}},
                        {"normal", {target.fallback_normal.x,
                            target.fallback_normal.y, target.fallback_normal.z}},
                        {"triangles", std::move(triangles)}});
                }
                return result;
            };
            serialized["end_targets_forward"] =
                target_json(container.extrusion.end_targets_forward);
            serialized["end_targets_reverse"] =
                target_json(container.extrusion.end_targets_reverse);
            serialized["height"] = container.extrusion.height;
            serialized["direction"] =
                container.extrusion.direction == ExtrusionDirection::Forward
                    ? "forward"
                : container.extrusion.direction == ExtrusionDirection::Reverse
                    ? "reverse" : "symmetric";
            serialized["extent"] = container.extrusion.extent == ExtrusionExtent::UpToPlane
                ? "up_to_plane"
                : container.extrusion.extent == ExtrusionExtent::UpToSurface
                    ? "up_to_surface"
                : container.extrusion.extent == ExtrusionExtent::ThroughAll
                    ? "through_all" : "blind";
            serialized["target_owner"] = container.extrusion.target_face.owner_id;
            serialized["target_key"] = container.extrusion.target_face.semantic_key;
            serialized["target_path"] = container.extrusion.target_face.instance_path;
            serialized["target_origin_x"] = container.extrusion.target_plane_origin.x;
            serialized["target_origin_y"] = container.extrusion.target_plane_origin.y;
            serialized["target_origin_z"] = container.extrusion.target_plane_origin.z;
            serialized["target_normal_x"] = container.extrusion.target_plane_normal.x;
            serialized["target_normal_y"] = container.extrusion.target_plane_normal.y;
            serialized["target_normal_z"] = container.extrusion.target_plane_normal.z;
            serialized["target_triangles"] = nlohmann::json::array();
            for (const auto& point : container.extrusion.target_surface_triangles) {
                serialized["target_triangles"].push_back({point.x, point.y, point.z});
            }
        } else if (container.feature_kind == FeatureKind::Revolution) {
            serialized["sketch_id"] = container.revolution.sketch_id;
            serialized["profile_source"] = container.revolution.profile_source ==
                    ProfileSource::Internal ? "internal" : "external";
            serialized["result_type"] = container.revolution.result_type ==
                    ProfileResultType::Solid ? "solid" : "thin";
            serialized["thin_thickness"] = container.revolution.thin_thickness;
            serialized["profile_plane_offset"] =
                container.revolution.profile_plane_offset;
            serialized["thin_mode"] = container.revolution.thin_mode == ThinMode::OneSide
                ? "one_side" : container.revolution.thin_mode == ThinMode::OtherSide
                    ? "other_side" : "symmetric";
            serialized["extent_mode"] = container.revolution.extent_mode ==
                    ProfileExtentMode::OneSide ? "one_side"
                : container.revolution.extent_mode == ProfileExtentMode::TwoSides
                    ? "two_sides" : "symmetric";
            serialized["direction"] = container.revolution.direction ==
                    ExtrusionDirection::Forward ? "forward" : "reverse";
            serialized["angle_reverse"] = container.revolution.angle_reverse;
            serialized["axis_segment_id"] =
                container.revolution.axis_segment_id;
            serialized["angle_degrees"] = container.revolution.angle_degrees;

    } else throw std::runtime_error("Expected an Extrusion or Revolution definition");
}
}
