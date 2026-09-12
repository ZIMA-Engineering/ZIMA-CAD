#include <zima/command_host/host.hpp>
#include <zima/workspace/placement_edit.hpp>
#include <zima/document/placement_json.hpp>

namespace zima::command_host {
namespace {
Json data(const workspace::Workspace& live, const std::string& id, const std::string& object) {
    const auto value = workspace::read_placement(live, id, object);
    const auto revision = live.open_part(id) ? live.open_part(id)->session.revision()
        : live.open_assembly(id)->session.revision();
    return {{"document", id}, {"object", object}, {"kind", value.kind}, {"body", value.body},
        {"coordinate_system", value.coordinate_system}, {"coordinate_owner", value.coordinate_owner},
        {"length_unit", "mm"}, {"angle_unit", "degrees"}, {"placement", value.placement}, {"revision", revision}};
}
}
void Host::register_placement_commands() {
    dispatcher_.add({"placement.get", tr("Read stored Body, feature or construction placement without calculation."),
        {{"object", true}, {"document", false}}, false}, [this](const Json& args) {
        try { return Result::success(data(workspace_, args.value("document", workspace_.active_document_id()), args.at("object").get<std::string>())); }
        catch (const workspace::PlacementEditError& error) { return Result::failure(error.code, tr(error.what())); }
    });
    dispatcher_.add({"placement.set", tr("Edit placement dimensions using the shared reference solver; values are mm or degrees."),
        {{"object", true}, {"values", true, commands::ArgumentType::Object}, {"document", false}}, true},
        [this](const Json& args) {
        const auto check = target(args); if (!check.ok) return check;
        if (interaction().template_document) return Result::failure("unsupported_document", tr("Placement operations require an open Part or Assembly."));
        try {
            workspace::PlacementValuePatch patch;
            for (const auto& [key, number] : args.at("values").items()) {
                if (!number.is_number()) return Result::failure("invalid_arguments", tr("Placement parameters must be JSON numbers."));
                patch.emplace(key, number.get<double>());
            }
            const auto id = workspace_.active_document_id(), object = args.at("object").get<std::string>();
            const bool changed = workspace::set_placement_values(workspace_, kernel_, id, object, patch);
            auto result = data(workspace_, id, object); result["changed"] = changed;
            if (changed) change_ = Change{ChangeKind::Model, id};
            return Result::success(std::move(result));
        } catch (const workspace::PlacementEditError& error) { return Result::failure(error.code, tr(error.what())); }
          catch (const std::exception& error) { return Result::failure("placement_rejected", tr(error.what())); }
    });
}
} // namespace zima::command_host
