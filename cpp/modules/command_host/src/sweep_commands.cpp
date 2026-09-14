#include "construction_parameters.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/sweep_operations.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/document/metadata.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>

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
            {"guide_start_point", feature.helical.guide_start_point_id}, {"sketches", std::move(sketches)},
            {"base_offset_mm", sketcher::Sketch::from_serialized(feature.helical.sketches[0]).plane_offset}});
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
        } else result["path"] = feature.sweep3d.path.id;
        auto stations = Json::array();
        const auto& owned_profiles = planar ? feature.sweep2d.profiles : feature.sweep3d.profiles;
        try {
            const auto route = planar ? document::PartDocument::sweep2d_route(feature) : document::curve3d_route(feature.sweep3d.path);
            for (const auto& station : route.stations) {
                const auto found = std::ranges::find_if(owned_profiles, [&](const auto& profile) {
                    return profile.point_id == station.point_id && profile.incoming == station.incoming;
                });
                stations.push_back({{"point", station.point_id}, {"incoming", station.incoming}, {"active", station.active},
                    {"position_mm", {station.origin.x, station.origin.y, station.origin.z}},
                    {"tangent", {station.tangent.x, station.tangent.y, station.tangent.z}},
                    {"profile", found == owned_profiles.end() ? Json(nullptr) : Json(found->id)}});
            }
            result["stations_valid"] = true;
        } catch (const std::exception& error) {
            result["stations_valid"] = false; result["stations_error"] = error.what();
        }
        result["stations"] = std::move(stations);
        result["station_coordinate_owner"] = planar ? (body ? body->scope.id : document.document_id) : feature.sweep3d.path.id;
    }
    return result;
}
workspace::SweepProfileSource profile_source(const Json& entry) {
    if (!entry.is_object() || !entry.contains("sketch") || !entry.contains("point"))
        throw Error("invalid_profile", "Each Sweep profile requires a Sketch and a path Point.");
    for (const auto& [key, field] : entry.items()) {
        const bool valid = key == "incoming" ? field.is_boolean()
            : (key == "sketch" || key == "point" || key == "start_point") && field.is_string();
        if (!valid) throw Error("invalid_profile", "Unknown or incorrectly typed Sweep profile property.");
    }
    return {entry.at("sketch"), entry.at("point"), entry.value("incoming", false), entry.value("start_point", std::string{})};
}
void validate_profile_stations(const document::HistoryContainer& value, const std::vector<document::Sweep3DProfile>& previous) {
    std::optional<document::Curve3DRoute> route;
    const bool planar = value.feature_kind == Kind::Sweep2D;
    for (const auto& profile : planar ? value.sweep2d.profiles : value.sweep3d.profiles) {
        // Existing inactive stations intentionally persist when path rounding
        // changes. Only an explicit new binding must select an offered station.
        if (std::ranges::any_of(previous, [&](const auto& old) { return old.id == profile.id &&
            old.point_id == profile.point_id && old.incoming == profile.incoming; })) continue;
        if (!route) route = planar ? document::PartDocument::sweep2d_route(value) : document::curve3d_route(value.sweep3d.path);
        if (std::ranges::none_of(route->stations, [&](const auto& station) { return station.active &&
            station.point_id == profile.point_id && station.incoming == profile.incoming; }))
            throw Error("invalid_profile", "A new or moved Sweep profile requires an active path station.");
    }
}
bool replace_profiles(document::HistoryContainer& value, const document::HistoryContainer& original,
    const document::PartDocument& part, const Json& entries) {
    if (entries.empty() || entries.size() > 5000)
        throw Error("invalid_profile", "A Sweep requires between 1 and 5000 profile Sketches.");
    const auto& previous = original.feature_kind == Kind::Sweep2D ? original.sweep2d.profiles : original.sweep3d.profiles;
    std::vector<document::Sweep3DProfile> profiles; bool adopted = false;
    for (const auto& entry : entries) {
        if (entry.is_object() && entry.contains("profile")) {
            for (const auto& [key, field] : entry.items()) {
                const bool valid = key == "incoming" ? field.is_boolean()
                    : (key == "profile" || key == "point" || key == "start_point") && field.is_string();
                if (!valid) throw Error("invalid_profile", "Unknown or incorrectly typed Sweep profile property.");
            }
            const auto found = std::ranges::find(previous, entry.at("profile").get<std::string>(), &document::Sweep3DProfile::id);
            if (found == previous.end()) throw Error("profile_not_found", "The requested Sweep profile does not belong to this feature.");
            auto profile = *found;
            if (entry.contains("point")) profile.point_id = entry.at("point").get<std::string>();
            if (entry.contains("incoming")) profile.incoming = entry.at("incoming").get<bool>();
            if (entry.contains("start_point")) profile.correspondence_start_point_id = entry.at("start_point").get<std::string>();
            profiles.push_back(std::move(profile));
        } else {
            profiles.push_back(workspace::sweep_profile_from_source(part, value.id, profile_source(entry)));
            adopted = true;
        }
    }
    (value.feature_kind == Kind::Sweep2D ? value.sweep2d.profiles : value.sweep3d.profiles) = std::move(profiles);
    validate_profile_stations(value, previous);
    return adopted;
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
        if (args.contains("guide_start_point")) value.helical.guide_start_point_id = args.at("guide_start_point").get<std::string>();
        if (args.contains("base_offset_mm")) {
            auto base = sketcher::Sketch::from_serialized(value.helical.sketches[0]);
            base.plane_offset = args.at("base_offset_mm").get<double>();
            value.helical.sketches[0] = base.serialized();
        }
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
    if (args.contains("path_plane")) {
        const auto& plane = args.at("path_plane");
        if (plane.empty()) value.sweep2d.path_plane.reset();
        else {
            if (!plane.contains("owner") || !plane.contains("key"))
                throw Error("invalid_arguments", "A path plane requires owner and key.");
            for (const auto& [key, field] : plane.items()) {
                const bool valid = key == "offset_mm" ? field.is_number()
                    : (key == "owner" || key == "key" || key == "instance_path") && field.is_string();
                if (!valid) throw Error("invalid_arguments", "Unknown or incorrectly typed path plane property.");
            }
            document::ConstructionReference reference{plane.value("instance_path", std::string{}), plane.at("owner"), plane.at("key"), 0};
            if (value.sweep2d.path_plane) {
                const auto& old = *value.sweep2d.path_plane;
                if (old.owner_id == reference.owner_id && old.semantic_key == reference.semantic_key && old.instance_path == reference.instance_path)
                    reference = old;
            }
            const auto offset = plane.value("offset_mm", reference.offset);
            if (!std::isfinite(offset) || std::abs(offset) > 1000000)
                throw Error("invalid_arguments", "The path plane offset is outside the supported range.");
            if (reference.offset_locked && offset != reference.offset)
                throw Error("value_locked", "The requested value is locked.");
            reference.offset = offset;
            value.sweep2d.path_plane = std::move(reference);
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
    for (const auto kind : {Kind::Sweep2D, Kind::Sweep3D}) {
        std::vector<commands::Argument> fields{{"source_path", true}, {"profiles", true, Type::Array}, {"name", false}, {"combine", false},
         {"placement", false, Type::Object}, {"result_type", false}, {"thin_mode", false},
         {"thickness_mm", false, Type::Number}, {"document", false}};
        if (kind == Kind::Sweep2D) fields.push_back({"path_plane", false, Type::Object});
        dispatcher_.add({kind == Kind::Sweep2D ? "sweep2d.create" : "sweep3d.create",
            tr(kind == Kind::Sweep2D ? "Create a 2D Sweep by adopting a path Sketch and profile Sketches."
                : "Create a 3D Sweep by adopting a standalone path and profile Sketches."), std::move(fields), true}, [this, kind](const Json& args) {
            const auto check = target(args); if (!check.ok) return check;
            try {
                const auto id = workspace_.active_document_id(); auto* state = workspace_.open_part(id);
                if (!state || interaction().template_document) throw Error("unsupported_document", "Sweep operations require an open Part.");
                std::vector<workspace::SweepProfileSource> profiles;
                for (const auto& entry : args.at("profiles")) profiles.push_back(profile_source(entry));
                const auto path = args.at("source_path").get<std::string>();
                auto feature = kind == Kind::Sweep2D ? workspace::sweep2d_from_sources(state->session.document(), path, profiles)
                    : workspace::sweep3d_from_sources(state->session.document(), path,
                        workspace::read_placement(workspace_, id, path).placement, profiles);
                const auto placement_owner = kind == Kind::Sweep2D
                    ? std::ranges::find(state->session.document().sketches, path, &sketcher::Sketch::id)->owner_container_id : path;
                sweep_properties(feature, args, workspace_, id, placement_owner); const auto container = feature.id;
                validate_profile_stations(feature, {});
                workspace::commit_sweep(workspace_, kernel_, id, std::move(feature), workspace::SweepEditMode::AdoptSources);
                change_ = Change{ChangeKind::Model, id, true};
                auto result = sweep_details(*state, sweep(state, container, kind)); result["changed"] = true;
                return Result::success(std::move(result));
            } catch (const Error& error) { return Result::failure(error.code, tr(error.what())); }
              catch (const workspace::PlacementEditError& error) { return Result::failure(error.code, tr(error.what())); }
              catch (const ConstructionParameterError& error) { return Result::failure(error.code, tr(error.what())); }
              catch (const std::exception& error) { return Result::failure("sweep_rejected", tr(error.what())); }
        });
    }
    dispatcher_.add({"helical.create", tr("Create a Helical Sweep by adopting three standalone Sketches."),
        {{"base_sketch", true}, {"guide_sketch", true}, {"profile_sketch", true}, {"circle", true}, {"start_point", true},
         {"guide_start_point", true}, {"pitch_mm", false, Type::Number}, {"left_handed", false, Type::Boolean},
         {"base_offset_mm", false, Type::Number}, {"placement", false, Type::Object}, {"name", false}, {"combine", false}, {"document", false}}, true},
        [this](const Json& args) {
            const auto check = target(args); if (!check.ok) return check;
            try {
                const auto id = workspace_.active_document_id(); auto* state = workspace_.open_part(id);
                if (!state || interaction().template_document) throw Error("unsupported_document", "Sweep operations require an open Part.");
                const workspace::HelicalSources inputs{{args.at("base_sketch"), args.at("guide_sketch"), args.at("profile_sketch")},
                    args.at("circle"), args.at("start_point"), args.at("guide_start_point")};
                auto feature = workspace::helical_from_sources(state->session.document(), inputs);
                const auto owner = std::ranges::find(state->session.document().sketches, inputs.sketches[0], &sketcher::Sketch::id)->owner_container_id;
                sweep_properties(feature, args, workspace_, id, owner); const auto container = feature.id;
                workspace::commit_sweep(workspace_, kernel_, id, std::move(feature), workspace::SweepEditMode::AdoptSources);
                change_ = Change{ChangeKind::Model, id, true};
                auto result = sweep_details(*state, sweep(state, container, Kind::HelicalSweep)); result["changed"] = true;
                return Result::success(std::move(result));
            } catch (const Error& error) { return Result::failure(error.code, tr(error.what())); }
              catch (const workspace::PlacementEditError& error) { return Result::failure(error.code, tr(error.what())); }
              catch (const std::exception& error) { return Result::failure("sweep_rejected", tr(error.what())); }
        });
    for (const auto kind : {Kind::Sweep2D, Kind::Sweep3D, Kind::HelicalSweep}) {
        const std::string prefix = kind == Kind::Sweep2D ? "sweep2d" : kind == Kind::Sweep3D ? "sweep3d" : "helical";
        dispatcher_.add({prefix+".reference.set",tr("Assign an original reference through the supported shared placement and feature transactions."),
            {{"container",true},{"index",true,Type::Integer},{"reference",true,Type::Object},{"offset_mm",false,Type::Number},
             {"flip",false,Type::Boolean},{"derive_orientation",false,Type::Boolean},{"document",false}},true},[this,kind](const Json& args) {
            const auto checked=target(args);if(!checked.ok)return checked;
            try {
                if(interaction().template_document)throw Error("unsupported_document","Sweep operations require an open Part.");
                const auto& ref=args.at("reference");
                const auto invalid=[](){throw Error("invalid_arguments","Specify owner, key and an optional instance_path for the placement reference.");};
                for(const auto& [key,item]:ref.items())if((key!="owner"&&key!="key"&&key!="instance_path")||!item.is_string())invalid();
                if(!ref.contains("owner")||!ref.contains("key")||args.at("index")<0||args.at("index")>4)invalid();
                const auto id=workspace_.active_document_id(),container=args.at("container").get<std::string>();
                static_cast<void>(sweep(workspace_.open_part(id),container,kind));
                document::ConstructionReference source;source.owner_id=ref.at("owner");source.semantic_key=ref.at("key");
                source.instance_path=ref.value("instance_path",std::string{});source.offset=args.value("offset_mm",0.0);source.flip=args.value("flip",false);
                const bool changed=workspace::set_sweep_placement_reference(workspace_,kernel_,id,container,
                    args.at("index").get<std::size_t>(),std::move(source),args.value("derive_orientation",true));
                const auto* state=workspace_.open_part(id);auto result=sweep_details(*state,sweep(state,container,kind));result["changed"]=changed;
                if(changed)change_=Change{ChangeKind::Model,id};return Result::success(std::move(result));
            }catch(const Error& error){return Result::failure(error.code,tr(error.what()));}
             catch(const workspace::PlacementEditError& error){return Result::failure(error.code,tr(error.what()));}
             catch(const std::exception& error){return Result::failure("sweep_rejected",tr(error.what()));}
        });
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
            fields.push_back({"guide_start_point", false}); fields.push_back({"base_offset_mm", false, Type::Number});
        } else {
            fields.push_back({"result_type", false}); fields.push_back({"thin_mode", false});
            fields.push_back({"thickness_mm", false, Type::Number});
            fields.push_back({"profiles", false, Type::Array});
            if (kind == Kind::Sweep3D) fields.push_back({"path", false, Type::Object});
            else fields.push_back({"path_plane", false, Type::Object});
        }
        dispatcher_.add({prefix + ".set", tr("Edit and calculate a Sweep through the shared Properties transaction."),
            std::move(fields), true}, [this, kind](const Json& args) {
            const auto check = target(args); if (!check.ok) return check;
            try {
                const auto id = workspace_.active_document_id(); auto* state = workspace_.open_part(id);
                if (!state || interaction().template_document) throw Error("unsupported_document", "Sweep operations require an open Part.");
                const auto& original = sweep(state, args.at("container").get<std::string>(), kind);
                auto value = original;
                sweep_properties(value, args, workspace_, id); const auto container = value.id;
                const bool adopted = args.contains("profiles") && replace_profiles(value, original, state->session.document(), args.at("profiles"));
                workspace::commit_sweep(workspace_, kernel_, id, std::move(value),
                    adopted ? workspace::SweepEditMode::ReplaceAdoptSources : workspace::SweepEditMode::Replace);
                change_ = Change{ChangeKind::Model, id, args.contains("profiles")};
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
