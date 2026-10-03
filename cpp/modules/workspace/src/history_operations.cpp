#include <zima/document/solid_state_calculation.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/history_operations.hpp>
#include <zima/workspace/history_deletion.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/workspace/reference_index.hpp>
#include <algorithm>
#include <cmath>

namespace zima::workspace {
namespace {
void require_editable(const document::PartDocument& document,const std::string& id) {
    if (document.body_history.find(id) || document.body_history.find_boolean(id)) return;
    if (const auto* owner=document.body_owner_for_object(id);
        owner && owner->scope.id!=document.body_history.active_body_id())
        throw HistoryOperationError("inactive_body", "Activate the owning Body before editing its history.");
}
}
bool part_history_suppressed(const document::PartDocument& document,const std::string& id) {
    if (const auto* feature=document.find_container(id)) return feature->suppressed;
    for (const auto& construction:document.constructions) if (construction.id==id) return construction.suppressed;
    const auto sketch=std::ranges::find_if(document.sketches,[&](const auto& s){return s.id==id && s.owner_container_id.empty();});
    if (sketch!=document.sketches.end()) return sketch->suppressed;
    throw HistoryOperationError("history_not_found", "The requested history object does not exist or is owned by a feature.");
}
bool set_part_history_suppressed(Workspace& live,const std::string& document_id,const kernel::OcctKernel& kernel,const std::string& id,bool suppressed) {
    auto* state=live.open_part(document_id);
    if(!state)throw HistoryOperationError("unsupported_document","Part document is not open");
    auto& part=*state;
    const auto current=part_history_suppressed(part.session.document(),id);
    require_editable(part.session.document(),id);
    if (current==suppressed) return false;
    auto next=part.session.document();
    if (auto* feature=next.find_container(id)) feature->suppressed=suppressed;
    else if (auto* construction=next.find_construction(id)) construction->suppressed=suppressed;
    else std::ranges::find_if(next.sketches,[&](const auto& s){return s.id==id;})->suppressed=suppressed;
    auto calculated=calculate_part(kernel,next,&part.session.calculated_boundaries());
    next.resolve_constructions(calculated.empty()?kernel::ViewerReferenceGeometry{}:calculated.back().mesh.original_references);
    static_cast<void>(refresh_sketch_external_references(next,calculated));
    commit_part_document(live,document_id,std::move(next),std::move(calculated));
    return true;
}
bool move_part_history(Workspace& live,const std::string& document_id,const kernel::OcctKernel& kernel,const std::string& id,const std::string& before,bool commit) {
    auto* state=live.open_part(document_id);
    if(!state)throw HistoryOperationError("unsupported_document","Part document is not open");
    auto& part=*state;
    require_editable(part.session.document(),id);
    const auto& original=part.session.document();
    const bool body_step = original.body_history.find(id) || original.body_history.find_boolean(id);
    const auto* owner = original.body_history.owner(id);
    std::vector<std::string> order;
    if (body_step) order=original.body_history.order();
    else if (owner) {
        if (!before.empty() && original.body_history.owner(before)!=owner) throw HistoryOperationError("invalid_history_move", "The object and destination must belong to the same history scope.");
        for (const auto& entry : owner->entries) order.push_back(entry.id);
    } else for (const auto& entry : original.history_order) order.push_back(entry.id);
    if (std::ranges::find(order,id)==order.end() || (!before.empty() && std::ranges::find(order,before)==order.end())) throw HistoryOperationError("invalid_history_move", "The object and destination must belong to the same history scope.");
    const auto reordered=reordered_history(order,id,before);
    if (!history_order_preserves_dependencies(order,reordered,
            body_step ? part_body_dependencies(original) : part_history_dependencies(original)))
        throw HistoryOperationError("history_dependency", "The move would break a history dependency.");
    std::optional<zima::document::BodyHistoryGraph> changed_graph;
    if (body_step) {
        auto graph=original.body_history;
        graph.move_step(id,static_cast<std::size_t>(std::distance(reordered.begin(),std::ranges::find(reordered,id))));
        changed_graph=std::move(graph);
    } else if (owner) {
        auto graph=original.body_history;
        auto body=*owner;
        sort_history_records(body.entries,reordered,[](const auto& entry){return entry.id;});
        for (const auto& entry : body.entries) {
            const auto* feature=entry.kind==zima::document::PartHistoryKind::Feature ? original.find_container(entry.id) : nullptr;
            if (!feature || feature->suppressed || feature->feature_kind==zima::document::FeatureKind::Sketch) continue;
            if (feature->combine_mode==zima::document::CombineMode::Subtract)
                throw HistoryOperationError("history_dependency", "The first feature of a Body cannot be subtractive.");
            break;
        }
        graph.update_body(std::move(body));changed_graph=std::move(graph);
    }
    if (order==reordered) return false;
    if (!commit) return true;
    auto next=original;
    if (changed_graph) next.set_body_history(std::move(*changed_graph));
    std::vector<std::string> storage_order;
    if (body_step || owner) for (const auto& entry : next.history_order) storage_order.push_back(entry.id);
    else storage_order=reordered;
    sort_history_records(next.history_order,storage_order,[](const auto& entry){return entry.id;});
    sort_history_records(next.history,storage_order,[](const auto& entry){return entry.id;});
    sort_history_records(next.constructions,storage_order,[](const auto& entry){return entry.id;});
    sort_history_records(next.sketches,storage_order,[](const auto& entry){return entry.owner_container_id.empty() ? entry.id : entry.owner_container_id;});
    auto calculated=calculate_part_with_resolved_references(kernel,next,&part.session.calculated_boundaries());
    // Persist the calculation for the exact resolved parameters. Placement
    // equality treats signed zero as equal, whereas persisted fingerprints
    // retain the floating-point bits. Do not change placement solving here.
    const auto resolved_operations=document::solid_state_calculation_operations(next,&calculated,false);
    bool exact_calculation=calculated.size()==resolved_operations.size();
    for (std::size_t i=0;i<calculated.size() && exact_calculation;++i)
        exact_calculation=calculated[i].source_fingerprint==kernel::history_fingerprint(resolved_operations,i+1);
    if (!exact_calculation) calculated=calculate_part(kernel,next,&calculated);

    const auto old_graph=part_history_dependency_graph(original);
    const auto new_graph=part_history_dependency_graph(next);
    if (!std::includes(new_graph.references.begin(),new_graph.references.end(),
            old_graph.references.begin(),old_graph.references.end()))
        throw HistoryOperationError("history_dependency", "The move would remove a persisted reference.");
    ReferenceIndex old_refs,new_refs;
    const auto& previous=part.session.calculated_boundaries();
    if (!previous.empty()) old_refs.add_geometry(previous.back().mesh.original_references);
    if (!calculated.empty()) new_refs.add_geometry(calculated.back().mesh.original_references);
    if (!std::includes(new_refs.keys.begin(),new_refs.keys.end(),old_refs.keys.begin(),old_refs.keys.end()))
        throw HistoryOperationError("history_dependency", "The move would remove original reference geometry.");
    for (const auto& object : original.constructions) {
        const auto* changed=next.find_construction(object.id);
        if (changed && object.reference_valid && !changed->reference_valid)
            throw HistoryOperationError("history_dependency", "The move would break a construction reference.");
    }
    for (const auto& feature : original.history) {
        const auto* changed=next.find_container(feature.id);
        if (changed && feature.placement.reference_valid && !changed->placement.reference_valid)
            throw HistoryOperationError("history_dependency", "The move would break a container reference.");
    }
    for (const auto& sketch : next.sketches) {
        const auto old=std::ranges::find_if(original.sketches,[&](const auto& s){return s.id==sketch.id;});
        if (old==original.sketches.end()) continue;
        for (const auto& ref : sketch.external_references) {
            const auto old_ref=std::ranges::find_if(old->external_references,[&](const auto& r){return r.id==ref.id;});
            if (ref.broken && old_ref!=old->external_references.end() && !old_ref->broken)
                throw HistoryOperationError("history_dependency", "The move would break an external Sketch reference.");
        }
    }
    commit_part_document(live,document_id,std::move(next),std::move(calculated));
    return true;
}
void delete_part_history(Workspace& live,const std::string& document_id,const kernel::OcctKernel& kernel,const std::string& id) {
    auto* state=live.open_part(document_id);
    if(!state)throw HistoryOperationError("unsupported_document","Part document is not open");
    auto& part=*state;
    const auto& original=part.session.document();
    if (!original.body_history.find(id) && !original.body_history.find_boolean(id)) {
        static_cast<void>(part_history_suppressed(original,id));
        require_editable(original,id);
    }
    auto next=original;
    const auto deletion=plan_history_deletion(original,id);
    detach_deleted_history_references(next,deletion);
    next.erase_history_object(id);
    auto calculated=calculate_part_with_resolved_references(kernel,next);
    commit_part_document(live,document_id,std::move(next),std::move(calculated));
}
bool set_part_history_cursor(Workspace& live,const std::string& document_id,std::size_t index) {
    auto* state=live.open_part(document_id);
    if(!state)throw HistoryOperationError("unsupported_document","Part document is not open");
    auto& part=*state;
    auto next=part.session.document();
    if (index>next.history_order.size()) throw HistoryOperationError("invalid_arguments", "The history index is outside the document history.");
    if (next.effective_history_cursor()==index) return false;
    next.set_history_cursor(index);
    commit_part_document(live,document_id,std::move(next),part.session.calculated_boundaries());
    return true;
}
}
