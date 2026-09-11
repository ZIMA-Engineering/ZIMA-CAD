#include <zima/workspace/history_operations.hpp>
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
bool same_point(const kernel::Vec3& a,const kernel::Vec3& b) {
    return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z)<=1.0e-6;
}
bool viewer_edges_same_geometry(
    const zima::kernel::ViewerEdge& first,
    const zima::kernel::ViewerEdge& second) {
    if (first.points.empty() || first.points.size() != second.points.size()) {
        return false;
    }
    const auto same_order = [&](bool reverse) {
        for (std::size_t index = 0; index < first.points.size(); ++index) {
            const auto second_index = reverse
                ? second.points.size() - index - 1
                : index;
            if (!same_point(
                    first.points[index], second.points[second_index])) {
                return false;
            }
        }
        return true;
    };
    return same_order(false) || same_order(true);
}

std::size_t restore_surviving_edge_references_after_history_delete(
    zima::document::PartDocument& document,
    const std::string& deleted_owner,
    const zima::kernel::BodyResult& input_before_deleted,
    const std::vector<zima::kernel::BodyResult>& old_boundaries) {
    const auto old_edge = [&](const zima::kernel::EdgeReference& reference)
            -> const zima::kernel::ViewerEdge* {
        for (const auto& boundary : old_boundaries) {
            const auto found = std::ranges::find_if(
                boundary.mesh.edges, [&](const auto& edge) {
                    return edge.reference == reference;
                });
            if (found != boundary.mesh.edges.end()) return &*found;
        }
        return nullptr;
    };
    const auto surviving_reference =
        [&](const zima::kernel::EdgeReference& reference)
            -> std::optional<zima::kernel::EdgeReference> {
        const auto* stale_edge = old_edge(reference);
        if (stale_edge == nullptr) return std::nullopt;
        std::vector<zima::kernel::EdgeReference> matches;
        for (const auto& candidate : input_before_deleted.mesh.edges) {
            if (!candidate.reference.valid() ||
                candidate.reference.owner_id == deleted_owner ||
                !viewer_edges_same_geometry(*stale_edge, candidate)) {
                continue;
            }
            if (std::ranges::find(matches, candidate.reference) == matches.end()) {
                matches.push_back(candidate.reference);
            }
        }
        // Repair only the unambiguous case: the referenced operational edge
        // was already present, geometrically unchanged, before the deleted
        // feature. A truly generated/modified edge remains dependent and the
        // normal calculation rejects its deletion.
        return matches.size() == 1
            ? std::optional<zima::kernel::EdgeReference>{matches.front()}
            : std::nullopt;
    };

    std::size_t restored{};
    for (auto& container : document.history) {
        if (container.feature_kind != zima::document::FeatureKind::Fillet &&
            container.feature_kind != zima::document::FeatureKind::Chamfer) {
            continue;
        }
        for (auto& route : container.edge_treatment.routes) {
            for (auto& reference : route) {
                if (reference.owner_id != deleted_owner) continue;
                if (const auto replacement = surviving_reference(reference)) {
                    reference = *replacement;
                    ++restored;
                }
            }
        }
    }
    return restored;
}

}
bool part_history_suppressed(const document::PartDocument& document,const std::string& id) {
    if (const auto* feature=document.find_container(id)) return feature->suppressed;
    for (const auto& construction:document.constructions) if (construction.id==id) return construction.suppressed;
    const auto sketch=std::ranges::find_if(document.sketches,[&](const auto& s){return s.id==id && s.owner_container_id.empty();});
    if (sketch!=document.sketches.end()) return sketch->suppressed;
    throw HistoryOperationError("history_not_found", "The requested history object does not exist or is owned by a feature.");
}
bool set_part_history_suppressed(PartState& part,const kernel::OcctKernel& kernel,const std::string& id,bool suppressed) {
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
    part.session.commit(std::move(next),std::move(calculated));
    return true;
}
bool move_part_history(PartState& part,const kernel::OcctKernel& kernel,const std::string& id,const std::string& before,bool commit) {
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
    const auto resolved_operations=next.kernel_operations();
    bool exact_calculation=calculated.size()==resolved_operations.size();
    for (std::size_t i=0;i<calculated.size() && exact_calculation;++i)
        exact_calculation=calculated[i].source_fingerprint==kernel::history_fingerprint(resolved_operations,i+1);
    if (!exact_calculation) calculated=calculate_part(kernel,next);

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
    part.session.commit(std::move(next),std::move(calculated));
    return true;
}
void delete_part_history(PartState& part,const kernel::OcctKernel& kernel,const std::string& id) {
    const auto& original=part.session.document();
    if (!original.body_history.find(id) && !original.body_history.find_boolean(id)) {
        static_cast<void>(part_history_suppressed(original,id));
        require_editable(original,id);
    }
    auto next=original;
    const auto rollback=part.session.rollback_boundary(id);
    next.erase_history_object(id);
    if (rollback && rollback->input_body)
        static_cast<void>(restore_surviving_edge_references_after_history_delete(next,id,*rollback->input_body,part.session.calculated_boundaries()));
    auto calculated=calculate_part_with_resolved_references(kernel,next);
    part.session.commit(std::move(next),std::move(calculated));
}
bool set_part_history_cursor(PartState& part,std::size_t index) {
    auto next=part.session.document();
    if (index>next.history_order.size()) throw HistoryOperationError("invalid_arguments", "The history index is outside the document history.");
    if (next.effective_history_cursor()==index) return false;
    next.set_history_cursor(index);
    part.session.commit(std::move(next),part.session.calculated_boundaries());
    return true;
}
}
