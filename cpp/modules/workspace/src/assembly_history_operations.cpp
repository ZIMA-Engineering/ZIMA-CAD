#include <zima/workspace/assembly_history_operations.hpp>
#include <zima/workspace/assembly_cut_operations.hpp>
#include <zima/workspace/history_policy.hpp>
#include <algorithm>

namespace zima::workspace {
std::vector<assembly::AssemblyDocument::HistoryEntry> reordered_assembly_history(
        const assembly::AssemblyDocument& document,const std::string& object,const std::string& before) {
    const auto current=document.complete_history_order();std::vector<std::string> ids;
    std::map<std::string,assembly::AssemblyDocument::HistoryEntry> entries;
    for(const auto& entry:current){ids.push_back(entry.id);entries.emplace(entry.id,entry);}
    const auto changed=reordered_history(ids,object,before);std::vector<assembly::AssemblyDocument::HistoryEntry> result;
    for(const auto& id:changed)result.push_back(entries.at(id));return result;
}
void validate_assembly_history_order(const assembly::AssemblyDocument& document,
        const std::vector<assembly::AssemblyDocument::HistoryEntry>* proposed) {
    const auto order=proposed?*proposed:document.complete_history_order();std::map<std::string,std::size_t> positions;
    for(std::size_t i=0;i<order.size();++i)positions.emplace(order[i].id,i);
    auto dependencies=assembly_component_dependencies(document);
    const auto local=part_history_dependency_graph(document.feature_document());
    dependencies.insert(local.edges.begin(),local.edges.end());
    for(const auto& [consumer,path,owner,key]:local.references)if(!path.empty()) {
        const auto address=assembly::InstancePath::decode(path);
        if(!address.occurrence_ids.empty()&&document.find_occurrence(address.occurrence_ids.front()))
            dependencies.emplace(address.occurrence_ids.front(),consumer);
    }
    for(const auto& cut:document.cuts)for(const auto& target:cut.target_occurrence_ids)
        dependencies.emplace(target,cut.definition.id);
    for(const auto& [source,consumer]:dependencies)if(positions.contains(source)&&positions.contains(consumer)&&
        source!=consumer&&positions.at(source)>=positions.at(consumer))
        throw HistoryOperationError("history_dependency","The move would place a reference before its source.");
}
bool set_assembly_history_cursor(Workspace& live,const std::string& id,std::size_t cursor) {
    auto* state=live.open_assembly(id);
    if(!state)throw HistoryOperationError("unsupported_document","History operations require an open Assembly.");
    auto next=state->session.document();next.synchronize_history();
    if(cursor>next.history_order.size())throw HistoryOperationError("invalid_arguments","The history index is outside the document history.");
    if(cursor==next.effective_history_cursor())return false;
    next.history_cursor=cursor;state->session.commit(std::move(next));return true;
}
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
    const auto history=reordered_assembly_history(original,object,before);
    validate_assembly_history_order(original,&history);
    if (!commit) return true;
    auto next = original;
    next.history_order=history;
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
