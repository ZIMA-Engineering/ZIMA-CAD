#include <zima/workspace/assembly_history_operations.hpp>
#include <zima/workspace/assembly_cut_operations.hpp>
#include <zima/workspace/history_policy.hpp>
#include <algorithm>

namespace zima::workspace {
bool move_assembly_history(Workspace& live, const std::string& document_id,
    const kernel::OcctKernel& kernel, const std::string& object,
    const std::string& before, bool commit) {
    auto* state = live.open_assembly(document_id);
    if (!state) throw HistoryOperationError("unsupported_document",
        "History operations require an open Assembly.");
    const auto& original = state->session.document();
    if (original.find_cut(object))
        return move_assembly_cut(live, kernel, document_id, object, before, commit);

    enum class Kind { Component, Construction, Sketch };
    Kind kind;
    std::vector<std::string> order;
    if (original.find_occurrence(object)) {
        kind = Kind::Component;
        for (const auto& value : original.components) order.push_back(value.occurrence_id);
    } else if (original.find_construction(object)) {
        kind = Kind::Construction;
        for (const auto& value : original.constructions) order.push_back(value.id);
    } else if (original.find_sketch_container(object)) {
        kind = Kind::Sketch;
        for (const auto& value : original.sketch_containers) order.push_back(value.id);
    } else throw HistoryOperationError("history_not_found",
        "The requested history object does not exist or is owned by a feature.");
    if (!before.empty() && std::ranges::find(order, before) == order.end())
        throw HistoryOperationError("history_scope",
            "The destination must belong to the same Assembly history list.");

    HistoryDependencies dependencies;
    if (kind == Kind::Component) dependencies = assembly_component_dependencies(original);
    else {
        document::PartDocument carrier;
        carrier.constructions = original.constructions;
        carrier.sketches = original.sketches;
        carrier.history = original.sketch_containers;
        for (const auto& cut : original.cuts) carrier.history.push_back(cut.definition);
        const auto all = part_history_dependencies(carrier);
        // Assembly lists are separate: a dependency may pass through another
        // list (Sketch -> datum -> Sketch). Project the complete graph onto
        // the reordered list instead of discarding those intermediate nodes.
        std::map<std::string,std::vector<std::string>> consumers;
        for (const auto& [source, consumer] : all) consumers[source].push_back(consumer);
        const std::set<std::string> members(order.begin(),order.end());
        for (const auto& source : order) {
            std::vector<std::string> pending{source};
            std::set<std::string> visited{source};
            while (!pending.empty()) {
                auto current=std::move(pending.back());pending.pop_back();
                const auto found=consumers.find(current);
                if (found==consumers.end()) continue;
                for (const auto& consumer : found->second) if (visited.insert(consumer).second) {
                    if (members.contains(consumer)) dependencies.emplace(source,consumer);
                    pending.push_back(consumer);
                }
            }
        }
    }
    const auto reordered = reordered_history(order, object, before);
    if (!history_order_preserves_dependencies(order, reordered, dependencies))
        throw HistoryOperationError("history_dependency",
            "The move would place a reference before its source.");
    if (reordered == order) return false;
    if (!commit) return true;
    auto next = original;
    switch (kind) {
    case Kind::Component:
        sort_history_records(next.components, reordered, [](const auto& value) { return value.occurrence_id; });
        break;
    case Kind::Construction:
        sort_history_records(next.constructions, reordered, [](const auto& value) { return value.id; });
        break;
    case Kind::Sketch:
        sort_history_records(next.sketch_containers, reordered, [](const auto& value) { return value.id; });
        break;
    }
    state->session.commit(std::move(next));
    return true;
}
}
