#include "construction_parameters.hpp"
#include <zima/command_host/host.hpp>
#include <zima/document/placement_json.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/workspace/construction_removal.hpp>
#include <zima/workspace/history_operations.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace zima::command_host {
namespace {
using Object = document::ConstructionObject;
struct QueryError : std::runtime_error {
    std::string code;
    QueryError(std::string code, const char* message)
        : std::runtime_error(message), code(std::move(code)) {}
};
struct Source {
    const std::vector<Object>& objects;
    const document::PartDocument* part;
    std::string id;
    std::uint64_t revision;
};
Source source(const workspace::Workspace& live, const Json& args) {
    const auto id = args.value("document", live.active_document_id());
    if (const auto* state = live.open_part(id))
        return {state->session.document().constructions, &state->session.document(), id, state->session.revision()};
    if (const auto* state = live.open_assembly(id))
        return {state->session.document().constructions, nullptr, id, state->session.revision()};
    throw QueryError("unsupported_document", "Construction queries require an open Part or Assembly.");
}
const char* kind(document::ConstructionKind value) {
    using Kind = document::ConstructionKind;
    switch (value) {
    case Kind::Point: return "point";
    case Kind::Axis: return "axis";
    case Kind::Plane: return "plane";
    case Kind::Curve3D: return "curve3d";
    }
    throw std::logic_error("Unknown construction kind");
}
const char* definition(document::ConstructionDefinition value) {
    using Definition = document::ConstructionDefinition;
    switch (value) {
    case Definition::Absolute: return "absolute";
    case Definition::PointReference: return "point_reference";
    case Definition::TwoPointAxis: return "two_point_axis";
    case Definition::AxisReference: return "axis_reference";
    case Definition::ThreePointPlane: return "three_point_plane";
    case Definition::PlaneReference: return "plane_reference";
    }
    throw std::logic_error("Unknown construction definition");
}
Json vec(kernel::Vec3 value) { return Json::array({value.x, value.y, value.z}); }
std::size_t size_arg(const Json& args, const char* key, std::size_t fallback,
    std::size_t maximum, bool positive = false) {
    if (!args.contains(key)) return fallback;
    if (args[key] < 0 || args[key].get<std::uint64_t>() > maximum || (positive && args[key] == 0))
        throw QueryError("invalid_arguments", "Construction pagination is outside the supported range.");
    return args[key].get<std::size_t>();
}
struct Item {
    const Object* object;
    std::string parent, body;
    bool parent_suppressed;
    std::string feature;
};
// Only the requested open document owns these definitions. No dependency file,
// occurrence snapshot, shape, reference solver or viewer projection is queried.
template<class Visitor> void visit(const Source& source, Visitor&& visitor) {
    std::vector<Item> roots;
    for (const auto& root : source.objects) {
        const auto* body = source.part ? source.part->body_owner_for_object(root.id) : nullptr;
        const auto body_id = body ? body->scope.id : std::string{};
        roots.push_back({&root, body ? body_id : source.id, body_id, false, {}});
    }
    if (source.part) for (const auto& feature : source.part->history) {
        if (feature.feature_kind != document::FeatureKind::Sweep3D) continue;
        const auto* body = source.part->body_owner_for_object(feature.id);
        roots.push_back({&feature.sweep3d.path, feature.id, body ? body->scope.id : std::string{}, feature.suppressed, feature.id});
    }
    for (const auto& root : roots) {
        std::vector<Item> pending{root};
        while (!pending.empty()) {
            auto item = std::move(pending.back()); pending.pop_back();
            if (!visitor(item)) return;
            for (auto point = item.object->curve_points.rbegin(); point != item.object->curve_points.rend(); ++point)
                pending.push_back({&*point, item.object->id, item.body,
                    item.parent_suppressed || item.object->suppressed, item.feature});
        }
    }
}

Json summary(const Source& source, const Item& item) {
    const auto& value = *item.object;
    return {{"document", source.id}, {"construction", value.id}, {"kind", kind(value.kind)},
        {"name", value.name}, {"parent", item.parent}, {"body", item.body}, {"owning_feature", item.feature},
        {"parent_construction", value.parent_construction_id},
        {"entity", value.entity_id}, {"entity_parent", value.entity_parent_id},
        {"origin", value.container_origin.id}, {"reference_valid", value.reference_valid},
        {"suppressed", value.suppressed}, {"parent_suppressed", item.parent_suppressed},
        {"child_count", value.curve_points.size()}};
}
Json details(const Source& source, const Item& item, std::size_t limit) {
    const auto& value = *item.object;
    auto result = summary(source, item);
    result["revision"] = source.revision;
    result["coordinate_system"] = !item.feature.empty() && item.parent == item.feature ? "container"
        : !value.parent_construction_id.empty() ? "parent_construction"
        : !item.body.empty() ? "body" : "document";
    result["coordinate_owner"] = !value.parent_construction_id.empty() ? value.parent_construction_id
        : !item.body.empty() ? item.body : source.id;
    result["length_unit"] = "mm";
    result["angle_unit"] = "degrees";
    result["origin_mm"] = vec(value.origin);
    result["rotation_degrees"] = vec(value.rotation);
    result["absolute_rotation_degrees"] = vec(value.absolute_rotation);
    result["rotation_offset_degrees"] = Json::array({value.rotation_offset_x, value.rotation_offset_y, value.rotation_offset_z});
    result["orientation_back"] = value.orientation_back;
    result["orientation_quarter_turns"] = value.orientation_quarter_turns;
    result["definition"] = definition(value.definition);
    result["display_size_mm"] = value.display_size;
    result["value_locks"] = value.value_locks;
    auto references = Json::array();
    for (std::size_t i = 0; i < std::min(limit, value.references.size()); ++i)
        references.push_back(value.references[i]);
    result["references"] = std::move(references);
    result["reference_count"] = value.references.size();
    result["references_truncated"] = value.references.size() > limit;
    result["limit"] = limit;
    if (value.kind == document::ConstructionKind::Axis || value.kind == document::ConstructionKind::Plane)
        result["direction"] = vec(value.direction);
    if (value.kind == document::ConstructionKind::Axis) result["direction_axis"] = value.direction_axis;
    if (value.kind == document::ConstructionKind::Plane) {
        result["base_plane"] = value.base_plane == document::LocalDatumPlane::XY ? "xy"
            : value.base_plane == document::LocalDatumPlane::XZ ? "xz" : "yz";
        result["offset_mm"] = value.offset;
        result["entity_origin_mm"] = vec(value.entity_origin);
    }
    if (value.kind == document::ConstructionKind::Curve3D) {
        result["curve_type"] = value.curve_type == document::Curve3DType::Polyline ? "polyline" : "interpolating_spline";
        result["rounding_enabled"] = value.curve_rounding_enabled;
        auto children = Json::array();
        for (std::size_t i = 0; i < std::min(limit, value.curve_points.size()); ++i)
            children.push_back(value.curve_points[i].id);
        result["children"] = std::move(children);
        result["children_truncated"] = value.curve_points.size() > limit;
    }
    if (value.kind == document::ConstructionKind::Curve3D || !value.parent_construction_id.empty()) {
        result["tangent"] = construction_tangent_name(value.curve_tangent);
        result["tangent_enabled"] = value.curve_tangent_enabled;
        result["radius_mm"] = value.curve_radius;
    }
    return result;
}

const Object* find(const Source& source, const std::string& id) {
    const Object* result = nullptr;
    visit(source, [&](const Item& item) {
        if (item.object->id != id) return true;
        result = item.object; return false;
    });
    if (!result) throw QueryError("construction_not_found", "The requested construction object does not exist in this document.");
    return result;
}
void writable_body(const Source& source, const Object* object) {
    if (!source.part) return;
    const auto& graph = source.part->body_history;
    const auto* body = object ? source.part->body_owner_for_object(object->id) : graph.find(graph.active_body_id());
    if ((!object && !graph.bodies().empty() && !body) || (body && graph.active_body_id() != body->scope.id))
        throw QueryError("inactive_body", "Activate the owning Body before editing a construction.");
    if (body && body->derived_copy)
        throw QueryError("read_only_body", "A derived Body cannot be edited directly.");
}

}
void Host::register_construction_commands() {
    dispatcher_.add({"construction.list", tr("List stored construction objects and their owned points without calculation."),
        {{"kind", false}, {"parent", false}, {"document", false},
         {"offset", false, commands::ArgumentType::Integer}, {"limit", false, commands::ArgumentType::Integer}}, false},
        [this](const Json& args) {
            try {
                const auto values = source(workspace_, args);
                const auto filter = args.value("kind", std::string{});
                if (!filter.empty() && filter != "point" && filter != "axis" && filter != "plane" && filter != "curve3d")
                    throw QueryError("invalid_arguments", "Construction kind must be point, axis, plane or curve3d.");
                const auto offset = size_arg(args, "offset", 0, 100000000);
                const auto limit = size_arg(args, "limit", 500, 5000, true);
                auto items = Json::array(); std::size_t total = 0;
                visit(values, [&](const Item& item) {
                    if ((!filter.empty() && filter != kind(item.object->kind)) ||
                        (args.contains("parent") && args["parent"] != item.parent)) return true;
                    if (total >= offset && items.size() < limit) items.push_back(summary(values, item));
                    ++total; return true;
                });
                const bool more = offset < total && items.size() < total - offset;
                return Result::success({{"document", values.id}, {"revision", values.revision},
                    {"items", std::move(items)}, {"total", total}, {"offset", offset}, {"more", more},
                    {"next_offset", more ? Json(offset + limit) : Json(nullptr)}});
            } catch (const QueryError& error) { return Result::failure(error.code, tr(error.what())); }
        });
    dispatcher_.add({"construction.get", tr("Read stored construction geometry, references and curve point parameters."),
        {{"construction", true}, {"document", false}, {"limit", false, commands::ArgumentType::Integer}}, false},
        [this](const Json& args) {
            try {
                const auto values = source(workspace_, args);
                const auto limit = size_arg(args, "limit", 500, 5000, true);
                const auto id = args.at("construction").get<std::string>(); Json result = nullptr;
                visit(values, [&](const Item& item) {
                    if (item.object->id != id) return true;
                    result = details(values, item, limit); return false;
                });
                if (result.is_null()) throw QueryError("construction_not_found", "The requested construction object does not exist in this document.");
                return Result::success(std::move(result));
            } catch (const QueryError& error) { return Result::failure(error.code, tr(error.what())); }
        });
    dispatcher_.add({"construction.delete",tr("Delete an independent construction using its document's shared removal transaction."),
        {{"construction",true},{"document",false}},true},[this](const Json& args) {
        const auto check=target(args);if(!check.ok)return check;
        if(interaction().template_document)return Result::failure("unsupported_document",tr("Construction operations require an open Part or Assembly."));
        try {
            const auto id=source(workspace_,args).id,object=args.at("construction").get<std::string>();
            workspace::delete_construction(workspace_,kernel_,id,object);
            change_=Change{ChangeKind::Model,id};
            const auto* part=workspace_.open_part(id);
            Json result={{"document",id},{"construction",object},{"removed",true},{"changed",true},
                {"revision",source(workspace_,args).revision},{"body_calculated",part!=nullptr}};
            if(part&&!part->session.calculated_boundaries().empty())result["calculation_errors"]=part->session.calculated_boundaries().back().calculation_errors;
            return Result::success(std::move(result));
        }catch(const QueryError& error){return Result::failure(error.code,tr(error.what()));}
         catch(const workspace::ConstructionRemovalError& error){return Result::failure(error.code,tr(error.what()));}
         catch(const workspace::HistoryOperationError& error){return Result::failure(error.code,tr(error.what()));}
         catch(const std::exception& error){return Result::failure("construction_rejected",tr(error.what()));}
    });
    for (const bool create : {true, false}) {
        std::vector<commands::Argument> fields{{create ? "kind" : "construction", true}, {"name", create},
            {"values", false, commands::ArgumentType::Object}, {"direction_axis", false}, {"base_plane", false},
            {"display_size_mm", false, commands::ArgumentType::Number}, {"offset_mm", false, commands::ArgumentType::Number},
            {"document", false}, {"curve_type", false}, {"rounding_enabled", false, commands::ArgumentType::Boolean},
            {"points", false, commands::ArgumentType::Array}, {"radius_mm", false, commands::ArgumentType::Number},
            {"tangent", false}, {"tangent_enabled", false, commands::ArgumentType::Boolean}};
        dispatcher_.add({create ? "construction.create" : "construction.set", create
            ? tr("Create a Point, Axis, Plane or 3D curve using the shared Properties transaction.")
            : tr("Edit construction properties and placement in one transaction."), std::move(fields), true},
            [this, create](const Json& args) {
                const auto check = target(args); if (!check.ok) return check;
                if (interaction().template_document) return Result::failure("unsupported_document", tr("Construction operations require an open Part or Assembly."));
                try {
                    const auto before = source(workspace_, args);
                    const auto* existing = create ? nullptr : find(before, args.at("construction").get<std::string>());
                    writable_body(before, existing);
                    if (existing && before.part && !before.part->find_construction(existing->id))
                        throw QueryError("embedded_construction", "Edit an embedded path through its owning Sweep command.");
                    auto value = existing ? *existing : Object{};
                    if (create) {
                        const auto type = args.at("kind").get<std::string>();
                        if (type != "point" && type != "axis" && type != "plane" && type != "curve3d")
                            throw QueryError("invalid_arguments", "New construction kind must be point, axis, plane or curve3d.");
                        value = document::PartDocument::create_construction(type == "point" ? document::ConstructionKind::Point
                            : type == "axis" ? document::ConstructionKind::Axis
                            : type == "plane" ? document::ConstructionKind::Plane : document::ConstructionKind::Curve3D);
                        if (type == "curve3d" && !args.contains("points"))
                            throw QueryError("invalid_arguments", "A 3D curve requires between 2 and 5000 points.");
                    }
                    const auto id = value.id, document = before.id;
                    const auto geometry = existing && args.contains("values")
                        ? workspace::placement_edit_geometry(workspace_, document, id) : kernel::ViewerReferenceGeometry{};
                    const auto* parent = value.parent_construction_id.empty() ? nullptr : find(before, value.parent_construction_id);
                    apply_construction_properties(value, args, geometry, parent);
                    apply_curve_points(value, args, [&](Object& value, std::size_t i) {
                        // Parent and children may change together. Resolve the proposed parent
                        // in its Body/document frame before testing a child's editable axes.
                        // Reuse one local reference packet for the whole point-list edit.
                        auto geometry = workspace::placement_edit_geometry(workspace_, document, value.id);
                        static_cast<void>(document::resolve_construction(value, geometry));
                        document::PartDocument carrier; carrier.constructions.push_back(value);
                        // Only the frame is needed here. New points have not received their
                        // coordinates yet, so the temporary list is not a valid route.
                        carrier.constructions.back().curve_points = {value.curve_points[i]};
                        return carrier.construction_reference_geometry_for(value.curve_points[i].id, std::move(geometry));
                    });
                    const bool changed = create || value != *existing;
                    if (changed) {
                        static_cast<void>(workspace::commit_construction(workspace_, document, std::move(value), create
                            ? workspace::ConstructionEditMode::Create : workspace::ConstructionEditMode::Replace));
                        change_ = Change{ChangeKind::Model, document};
                    }
                    const auto after = source(workspace_, args);
                    Json result;
                    visit(after, [&](const Item& item) {
                        if (item.object->id != id) return true;
                        result = details(after, item, 500); return false;
                    });
                    result["changed"] = changed;
                    return Result::success(std::move(result));
                } catch (const QueryError& error) { return Result::failure(error.code, tr(error.what())); }
                  catch (const ConstructionParameterError& error) { return Result::failure(error.code, tr(error.what())); }
                  catch (const workspace::PlacementEditError& error) { return Result::failure(error.code, tr(error.what())); }
                  catch (const std::exception& error) { return Result::failure("construction_rejected", tr(error.what())); }
            });
    }

}
} // namespace zima::command_host
