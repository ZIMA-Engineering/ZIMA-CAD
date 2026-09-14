#include <zima/workspace/assembly_cut_operations.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/workspace/reference_index.hpp>
#include <algorithm>

namespace zima::workspace {
namespace {
AssemblyState& owner(Workspace& live, const std::string& id, const std::string& cut) {
    auto* state = live.open_assembly(id);
    if (!state) throw HistoryOperationError("unsupported_document", "Cut operations require an open Assembly.");
    if (!state->session.document().find_cut(cut))
        throw HistoryOperationError("container_not_found", "The requested container does not exist.");
    return *state;
}
document::PartDocument reference_carrier(const assembly::AssemblyDocument& source) {
    document::PartDocument result;
    result.constructions = source.constructions;
    result.sketches = source.sketches;
    result.history = source.sketch_containers;
    for (const auto& cut : source.cuts) result.history.push_back(cut.definition);
    return result;
}
}
bool set_assembly_cut_suppressed(Workspace& live, const kernel::OcctKernel& kernel,
    const std::string& id, const std::string& cut, bool suppressed) {
    auto& state = owner(live, id, cut);
    if (state.session.document().find_cut(cut)->definition.suppressed == suppressed) return false;
    auto next = live.prepare_assembly_calculation(id);
    next.find_cut(cut)->definition.suppressed = suppressed;
    calculate_resolved_assembly_cuts(kernel, next);
    state.session.commit(std::move(next));
    return true;
}
void remove_assembly_cut(Workspace& live, const kernel::OcctKernel& kernel,
    const std::string& id, const std::string& cut) {
    auto& state = owner(live, id, cut);
    const auto graph = part_history_dependency_graph(reference_carrier(state.session.document()));
    std::set<std::string> removed{cut};
    for (const auto& [alias, root] : graph.owners) if (root == cut) removed.insert(alias);
    auto next = live.prepare_assembly_calculation(id);
    std::erase_if(next.cuts, [&](const auto& value) { return value.definition.id == cut; });
    std::erase_if(next.sketches, [&](const auto& value) { return value.owner_container_id == cut; });
    // Keep the last projected curve while exposing the missing local source.
    for (auto& sketch : next.sketches) for (auto& reference : sketch.external_references)
        if (removed.contains(reference.source_owner_id) && reference.source_instance_path.empty() &&
            (reference.source_document_id.empty() || reference.source_document_id == id)) reference.broken = true;
    calculate_resolved_assembly_cuts(kernel, next);
    state.session.commit(std::move(next));
}
bool move_assembly_cut(Workspace& live, const kernel::OcctKernel& kernel,
    const std::string& id, const std::string& cut, const std::string& before, bool commit) {
    auto& state = owner(live, id, cut);
    const auto& original = state.session.document();
    if (!before.empty() && !original.find_cut(before))
        throw HistoryOperationError("container_not_found", "The requested container does not exist.");
    std::vector<std::string> order;
    for (const auto& value : original.cuts) order.push_back(value.definition.id);
    const auto reordered = reordered_history(order, cut, before);
    const auto old_graph = part_history_dependency_graph(reference_carrier(original));
    if (!history_order_preserves_dependencies(order, reordered, old_graph.edges))
        throw HistoryOperationError("history_dependency", "The move would place a reference before its source.");
    if (order == reordered) return false;
    if (!commit) return true;
    auto next = live.prepare_assembly_calculation(id);
    sort_history_records(next.cuts, reordered, [](const auto& value) { return value.definition.id; });
    calculate_resolved_assembly_cuts(kernel, next);
    for (const auto& value : original.cuts) {
        const auto* changed = next.find_cut(value.definition.id);
        if (!changed || (value.definition.placement.reference_valid && !changed->definition.placement.reference_valid))
            throw HistoryOperationError("history_dependency", "The move would break an Assembly cut reference.");
    }
    const auto new_graph = part_history_dependency_graph(reference_carrier(next));
    if (!std::includes(new_graph.references.begin(), new_graph.references.end(), old_graph.references.begin(), old_graph.references.end()))
        throw HistoryOperationError("history_dependency", "The move would remove a stored Assembly cut reference.");
    ReferenceIndex old_references, new_references;
    old_references.add_geometry(original.build_scene().original_references);
    new_references.add_geometry(next.build_scene().original_references);
    if (!std::includes(new_references.keys.begin(), new_references.keys.end(), old_references.keys.begin(), old_references.keys.end()))
        throw HistoryOperationError("history_dependency", "The move would remove original Assembly reference geometry.");
    state.session.commit(std::move(next));
    return true;
}
}
