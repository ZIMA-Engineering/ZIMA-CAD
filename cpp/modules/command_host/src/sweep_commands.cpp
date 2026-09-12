#include "construction_parameters.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/sweep_operations.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/document/metadata.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace zima::command_host {
namespace {
using Kind = document::FeatureKind;
using Error = workspace::SweepOperationError;
const document::HistoryContainer& sweep(const workspace::PartState* state, const std::string& id, Kind kind) {
    if (!state) throw Error("unsupported_document", "Sweep operations require an open Part.");
    const auto* feature = state->session.document().find_container(id);
    if (!feature) throw Error("container_not_found", "The requested container does not exist.");
    if (feature->feature_kind != kind) throw Error("wrong_feature", "This container is not the requested Sweep type.");
    return *feature;
}
Json sweep_details(const workspace::PartState& state, const document::HistoryContainer& feature) {
    const auto& document = state.session.document();
    const auto* body = document.body_owner_for_object(feature.id);
    const bool helical = feature.feature_kind == Kind::HelicalSweep;
    const bool planar = feature.feature_kind == Kind::Sweep2D;
    Json result = {{"document", document.document_id}, {"container", feature.id}, {"feature", feature.feature_id},
        {"body", body ? body->scope.id : std::string{}}, {"name", feature.name},
        {"kind", helical ? "helical" : planar ? "sweep2d" : "sweep3d"},
        {"combine", feature.combine_mode == document::CombineMode::Add ? "add" : "subtract"},
        {"value_locks", feature.value_locks}, {"reference_valid", feature.placement.reference_valid && (helical ? feature.helical.reference_valid
            : planar ? feature.sweep2d.reference_valid : feature.sweep3d.path.reference_valid)},
        {"revision", state.session.revision()}};
    if (helical) {
        auto sketches = Json::array();
        for (const auto& data : feature.helical.sketches)
            sketches.push_back(sketcher::Sketch::from_serialized(data).id);
        result.update({{"pitch_mm", feature.helical.pitch}, {"left_handed", feature.helical.left_handed},
            {"circle", feature.helical.circle_id}, {"start_point", feature.helical.start_point_id},
            {"guide_start_point", feature.helical.guide_start_point_id}, {"sketches", std::move(sketches)}});
    } else {
        const auto type = planar ? feature.sweep2d.result_type : feature.sweep3d.result_type;
        const auto side = planar ? feature.sweep2d.thin_mode : feature.sweep3d.thin_mode;
        result.update({{"result_type", type == document::ProfileResultType::Thin ? "thin" : "solid"},
            {"thickness_mm", planar ? feature.sweep2d.thickness : feature.sweep3d.thickness},
            {"thin_mode", side == document::ThinMode::OneSide ? "one_side"
                : side == document::ThinMode::OtherSide ? "other_side" : "symmetric"}});
        auto profiles = Json::array();
        for (const auto& profile : planar ? feature.sweep2d.profiles : feature.sweep3d.profiles)
            profiles.push_back({{"profile", profile.id}, {"point", profile.point_id}, {"incoming", profile.incoming},
                {"sketch", profile.sketch_id}, {"start_point", profile.correspondence_start_point_id}});
        result["profiles"] = std::move(profiles);
        if (planar) {
            result["path_sketch"] = sketcher::Sketch::from_serialized(feature.sweep2d.path_sketch).id;
            const auto& plane = feature.sweep2d.path_plane;
            result["path_plane"] = plane ? Json{{"owner", plane->owner_id}, {"key", plane->semantic_key},
                {"instance_path", plane->instance_path}, {"offset_mm", plane->offset}} : Json(nullptr);
        } else {
            result["path"] = feature.sweep3d.path.id;
            auto stations = Json::array();
            try {
                for (const auto& station : document::curve3d_route(feature.sweep3d.path).stations) {
                    const auto found = std::ranges::find_if(feature.sweep3d.profiles, [&](const auto& profile) {
                        return profile.point_id == station.point_id && profile.incoming == station.incoming;
                    });
                    stations.push_back({{"point", station.point_id}, {"incoming", station.incoming}, {"active", station.active},
                        {"position_mm", {station.origin.x, station.origin.y, station.origin.z}},
                        {"tangent", {station.tangent.x, station.tangent.y, station.tangent.z}},
                        {"profile", found == feature.sweep3d.profiles.end() ? Json(nullptr) : Json(found->id)}});
                }
                result["stations_valid"] = true;
            } catch (const std::exception& error) {
                result["stations_valid"] = false; result["stations_error"] = error.what();
            }
            result["stations"] = std::move(stations);
            result["station_coordinate_owner"] = feature.sweep3d.path.id;
        }
    }
    return result;
}
void sweep_properties(document::HistoryContainer& value, const Json& args,
    const workspace::Workspace& live, const std::string& id, const std::string& placement_owner = {}) {
    const auto option = [&](const char* key, std::initializer_list<const char*> values) {
        const auto result = args.at(key).get<std::string>();
        if (std::ranges::none_of(values, [&](const auto* allowed) { return result == allowed; }))
            throw Error("invalid_arguments", "Unknown Sweep parameter option.");
        return result;
    };
    if (args.contains("name")) {
        const auto name = args.at("name").get<std::string>();
        document::validate_native_metadata_text(name);
        if (name.empty() || std::ranges::all_of(name, [](unsigned char c) { return std::isspace(c) != 0; }))
            throw Error("invalid_arguments", "Specify a nonempty object name.");
        value.name = name;
    }
    if (args.contains("combine")) value.combine_mode = option("combine", {"add", "subtract"}) == "add"
        ? document::CombineMode::Add : document::CombineMode::Subtract;
    if (value.feature_kind == Kind::HelicalSweep) {
        if (args.contains("pitch_mm")) value.helical.pitch = args.at("pitch_mm").get<double>();
        if (args.contains("left_handed")) value.helical.left_handed = args.at("left_handed").get<bool>();
        if (args.contains("circle")) value.helical.circle_id = args.at("circle").get<std::string>();
        if (args.contains("start_point")) value.helical.start_point_id = args.at("start_point").get<std::string>();
    } else {
        const bool planar = value.feature_kind == Kind::Sweep2D;
        auto& type = planar ? value.sweep2d.result_type : value.sweep3d.result_type;
        auto& side = planar ? value.sweep2d.thin_mode : value.sweep3d.thin_mode;
        if (args.contains("result_type")) type = option("result_type", {"solid", "thin"}) == "thin"
            ? document::ProfileResultType::Thin : document::ProfileResultType::Solid;
        if (args.contains("thin_mode")) {
            const auto mode = option("thin_mode", {"one_side", "other_side", "symmetric"});
            side = mode == "one_side" ? document::ThinMode::OneSide
                : mode == "other_side" ? document::ThinMode::OtherSide : document::ThinMode::Symmetric;
        }
        if (args.contains("thickness_mm")) (planar ? value.sweep2d.thickness : value.sweep3d.thickness) = args.at("thickness_mm").get<double>();
    }
    if (args.contains("placement")) {
        const auto& patch = args.at("placement");
        if (patch.empty()) throw Error("invalid_arguments", "Specify at least one placement parameter.");
        const auto geometry = workspace::placement_edit_geometry(live, id, placement_owner.empty() ? value.id : placement_owner);
        for (const auto& [key, number] : patch.items()) {
            if (!number.is_number() || !std::isfinite(number.get<double>()))
                throw Error("invalid_arguments", "Placement parameters must be finite JSON numbers.");
            if (!workspace::assign_placement_dimension(value.placement, geometry, key, number.get<double>()))
                throw Error("parameter_not_editable", "The placement parameter is unknown, constrained or locked.");
        }
    }
    if (args.contains("path")) {
        const auto& patch = args.at("path");
        if (patch.empty()) throw Error("invalid_arguments", "Specify at least one construction property.");
        for (const auto& [key, field] : patch.items()) {
            const bool valid = key == "curve_type" ? field.is_string()
                : key == "rounding_enabled" ? field.is_boolean() : key == "points" ? field.is_array() : false;
            if (!valid) throw Error("invalid_arguments", "Unknown or incorrectly typed Sweep path property.");
        }
        auto& path = value.sweep3d.path;
        apply_construction_properties(path, patch, {});
        apply_curve_points(path, patch, [&](document::ConstructionObject& path, std::size_t index) {
            auto geometry = workspace::placement_edit_geometry(live, id, value.id);
            auto placement = value.placement;
            static_cast<void>(document::resolve_placement(placement, geometry));
            auto frame = path;
            frame.parent_construction_id.clear(); frame.references.clear();
            frame.origin = {placement.x, placement.y, placement.z};
            frame.rotation = {placement.rotation_x, placement.rotation_y, placement.rotation_z};
            frame.absolute_rotation = frame.rotation;
            frame.curve_points = {path.curve_points[index]};
            document::PartDocument carrier; carrier.constructions.push_back(std::move(frame));
            return carrier.construction_reference_geometry_for(path.curve_points[index].id, std::move(geometry));
        });
        // Deleting a path Point deletes its station profiles in the same draft,
        // just as the 3D Sweep Properties editor does. Kernel validation still
        // rejects a route left without a usable first profile atomically.
        std::erase_if(value.sweep3d.profiles, [&](const auto& profile) {
            return std::ranges::none_of(path.curve_points, [&](const auto& point) { return point.id == profile.point_id; });
        });
    }
}
}
void Host::register_sweep_commands() {
    using Type = commands::ArgumentType;
    dispatcher_.add({"sweep3d.create", tr("Create a 3D Sweep by adopting a standalone path and profile Sketches."),
        {{"source_path", true}, {"profiles", true, Type::Array}, {"name", false}, {"combine", false},
         {"placement", false, Type::Object}, {"result_type", false}, {"thin_mode", false},
         {"thickness_mm", false, Type::Number}, {"document", false}}, true}, [this](const Json& args) {
        const auto check = target(args); if (!check.ok) return check;
        try {
            const auto id = workspace_.active_document_id(); auto* state = workspace_.open_part(id);
            if (!state || interaction().template_document) throw Error("unsupported_document", "Sweep operations require an open Part.");
            std::vector<workspace::SweepProfileSource> profiles;
            for (const auto& entry : args.at("profiles")) {
                if (!entry.is_object() || !entry.contains("sketch") || !entry.contains("point"))
                    throw Error("invalid_profile", "Each Sweep profile requires a Sketch and a path Point.");
                for (const auto& [key, field] : entry.items()) {
                    const bool valid = key == "incoming" ? field.is_boolean()
                        : (key == "sketch" || key == "point" || key == "start_point") && field.is_string();
                    if (!valid) throw Error("invalid_profile", "Unknown or incorrectly typed Sweep profile property.");
                }
                profiles.push_back({entry.at("sketch"), entry.at("point"), entry.value("incoming", false), entry.value("start_point", std::string{})});
            }
            const auto path = args.at("source_path").get<std::string>();
            auto feature = workspace::sweep3d_from_sources(state->session.document(), path,
                workspace::read_placement(workspace_, id, path).placement, profiles);
            sweep_properties(feature, args, workspace_, id, path); const auto container = feature.id;
            workspace::commit_sweep(workspace_, kernel_, id, std::move(feature), workspace::SweepEditMode::AdoptSources);
            change_ = Change{ChangeKind::Model, id, true};
            auto result = sweep_details(*state, sweep(state, container, Kind::Sweep3D)); result["changed"] = true;
            return Result::success(std::move(result));
        } catch (const Error& error) { return Result::failure(error.code, tr(error.what())); }
          catch (const workspace::PlacementEditError& error) { return Result::failure(error.code, tr(error.what())); }
          catch (const ConstructionParameterError& error) { return Result::failure(error.code, tr(error.what())); }
          catch (const std::exception& error) { return Result::failure("sweep_rejected", tr(error.what())); }
    });
    for (const auto kind : {Kind::Sweep2D, Kind::Sweep3D, Kind::HelicalSweep}) {
        const std::string prefix = kind == Kind::Sweep2D ? "sweep2d" : kind == Kind::Sweep3D ? "sweep3d" : "helical";
        dispatcher_.add({prefix + ".get", tr("Read Sweep parameters and owned profile identities without calculation."),
            {{"container", true}, {"document", false}}, false}, [this, kind](const Json& args) {
            try {
                const auto* state = workspace_.open_part(args.value("document", workspace_.active_document_id()));
                const auto& value = sweep(state, args.at("container").get<std::string>(), kind);
                return Result::success(sweep_details(*state, value));
            } catch (const Error& error) { return Result::failure(error.code, tr(error.what())); }
        });
        std::vector<commands::Argument> fields{{"container", true}, {"name", false}, {"combine", false},
            {"placement", false, Type::Object}, {"document", false}};
        if (kind == Kind::HelicalSweep) {
            fields.push_back({"pitch_mm", false, Type::Number}); fields.push_back({"left_handed", false, Type::Boolean});
            fields.push_back({"circle", false}); fields.push_back({"start_point", false});
        } else {
            fields.push_back({"result_type", false}); fields.push_back({"thin_mode", false});
            fields.push_back({"thickness_mm", false, Type::Number});
            if (kind == Kind::Sweep3D) fields.push_back({"path", false, Type::Object});
        }
        dispatcher_.add({prefix + ".set", tr("Edit and calculate a Sweep through the shared Properties transaction."),
            std::move(fields), true}, [this, kind](const Json& args) {
            const auto check = target(args); if (!check.ok) return check;
            try {
                const auto id = workspace_.active_document_id(); auto* state = workspace_.open_part(id);
                if (!state || interaction().template_document) throw Error("unsupported_document", "Sweep operations require an open Part.");
                auto value = sweep(state, args.at("container").get<std::string>(), kind);
                sweep_properties(value, args, workspace_, id); const auto container = value.id;
                workspace::commit_sweep(workspace_, kernel_, id, std::move(value), workspace::SweepEditMode::Replace);
                change_ = Change{ChangeKind::Model, id};
                auto result = sweep_details(*state, sweep(state, container, kind)); result["changed"] = true;
                return Result::success(std::move(result));
            } catch (const Error& error) { return Result::failure(error.code, tr(error.what())); }
              catch (const ConstructionParameterError& error) { return Result::failure(error.code, tr(error.what())); }
              catch (const workspace::PlacementEditError& error) { return Result::failure(error.code, tr(error.what())); }
              catch (const std::exception& error) { return Result::failure("sweep_rejected", tr(error.what())); }
        });
    }
}
} // namespace zima::command_host
