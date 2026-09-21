#include <zima/command_host/host.hpp>
#include <zima/workspace/holes_operations.hpp>
#include <algorithm>

namespace zima::command_host {
namespace {
const document::HistoryContainer& holes(const workspace::PartState* state, const std::string& container) {
    if (!state) throw std::invalid_argument("Otvory jsou dostupné pouze v Partu.");
    const auto* feature = state->session.document().find_container(container);
    if (!feature || feature->feature_kind != document::FeatureKind::Holes)
        throw std::invalid_argument("Kontejner Otvory neexistuje.");
    return *feature;
}
Json details(const workspace::PartState& state, const document::HistoryContainer& feature) {
    return {{"document", state.session.document().document_id}, {"container", feature.id},
        {"feature", feature.feature_id}, {"sketch", feature.holes.sketch_id},
        {"diameter_mm", feature.holes.diameter}, {"name", feature.name},
        {"combine", "subtract"}, {"revision", state.session.revision()}};
}
}
void Host::register_holes_commands() {
    dispatcher_.add({"holes.get", tr("Read Sketch-driven drilling channels without calculation."),
        {{"container", true}, {"document", false}}, false}, [this](const Json& args) {
        try {
            const auto* state = workspace_.open_part(args.value("document", workspace_.active_document_id()));
            const auto& feature = holes(state, args.at("container"));
            return Result::success(details(*state, feature));
        } catch (const std::exception& error) {return Result::failure("holes_rejected", tr(error.what()));}
    });
    for (bool create : {true, false}) {
        dispatcher_.add({create ? "holes.create" : "holes.set",
            tr("Cut finite cylindrical channels along the segments of an owned Sketch."),
            {{create ? "sketch" : "container", true}, {"diameter_mm", create, commands::ArgumentType::Number},
             {"name", false}, {"document", false}}, true}, [this, create](const Json& args) {
            if (auto result = target(args); !result.ok) return result;
            try {
                const auto id = workspace_.active_document_id();
                const auto* state = workspace_.open_part(id);
                if (!state) throw std::invalid_argument("Otvory jsou dostupné pouze v Partu.");
                auto feature = create ? workspace::holes_from_sketch(state->session.document(), args.at("sketch"))
                    : holes(state, args.at("container"));
                if (args.contains("diameter_mm")) {
                    const auto diameter = args.at("diameter_mm").get<double>();
                    if (feature.value_locks.contains("diameter") && diameter != feature.holes.diameter)
                        return Result::failure("value_locked", tr("Unlock the dimension before changing it."));
                    feature.holes.diameter = diameter;
                }
                if (create) feature.name=tr("Otvory");
                if (args.contains("name")) feature.name = args.at("name").get<std::string>();
                const auto sketch = std::ranges::find(state->session.document().sketches, feature.holes.sketch_id, &sketcher::Sketch::id);
                if (sketch == state->session.document().sketches.end()) throw std::invalid_argument("Zdrojová skica neexistuje.");
                const auto container = feature.id;
                const bool changed = workspace::commit_holes(workspace_, kernel_, id, std::move(feature), *sketch);
                if (changed) change_ = Change{ChangeKind::Model, id};
                state = workspace_.open_part(id);
                auto data = details(*state, holes(state, container)); data["changed"] = changed;
                return Result::success(std::move(data));
            } catch (const std::exception& error) {return Result::failure("holes_rejected", tr(error.what()));}
        });
    }
}
}
