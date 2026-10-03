#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <cmath>
#include <map>
#include <set>
#include <span>
#include <stdexcept>

namespace zima::kernel {

// Calculation-side timeline input. This is not a native document format.
// A boundary is the number of authored operations preceding the state command.
struct SolidStateChange {
    std::size_t boundary{};
    std::string owner_id, body_id;
    bool restore{}, all{true}, suppressed{};
    double coefficient{1};
    std::vector<std::string> owners;
};

struct SolidSourceState {
    // Always points into the immutable authored operation list, never into a
    // previously straightened result. Treatments remain in that list for replay.
    std::size_t source_index{};
    std::string body_id, state_owner;
    bool straight{};
    double coefficient{1};
    bool operator==(const SolidSourceState&) const = default;
};
using SolidSourceStates = std::map<std::string,SolidSourceState>;

// Structural candidacy only. Exact constant-section and trajectory validation
// still belongs to prepare_straightening during explicit calculation.
inline bool solid_state_candidate(const HistoryOperation& operation) {
    if(operation.suppressed || operation.operation!=BooleanOperation::Add ||
       operation.sheet_operation!=SheetOperation::None || operation.sheet_material ||
       !operation.sheet_regions.empty() || operation.feature_copy ||
       !operation.body.source_id.empty() || operation.body.linked_body) return false;
    if(const auto* revolution=std::get_if<RevolutionRequest>(&operation.primitive))
        return !revolution->surface_result;
    if(const auto* sweep=std::get_if<Sweep3DRequest>(&operation.primitive))
        return sweep->make_solid;
    if(const auto* group=std::get_if<FeatureGroupRequest>(&operation.primitive)) {
        bool curved=false;
        for(const auto& child:group->children) {
            if(const auto* revolution=std::get_if<RevolutionRequest>(&child)) {
                if(revolution->surface_result)return false;
                curved=true;
            } else if(const auto* sweep=std::get_if<Sweep3DRequest>(&child)) {
                if(!sweep->make_solid)return false;
                curved=true;
            } else if(std::get<ExtrusionRequest>(child).surface_result)return false;
        }
        return curved;
    }
    return false;
}

// Evaluate a history prefix without OCCT or mutation. Changes at the requested
// boundary are included in input order; later sources cannot be selected early.
// Each state applies only to its owning Body. Empty Body ID is the flat carrier.
inline SolidSourceStates solid_states_before(std::span<const HistoryOperation> operations,
        std::span<const SolidStateChange> changes, std::size_t boundary) {
    constexpr auto invalid="Solid state history is invalid.";
    if(boundary>operations.size())throw std::invalid_argument(invalid);
    std::set<std::string> identities;
    for(const auto& operation:operations)
        if(operation.owner_id.empty() || !identities.insert(operation.owner_id).second)
            throw std::invalid_argument(invalid);
    std::size_t previous=0;
    for(const auto& change:changes) {
        if(change.boundary<previous || change.boundary>operations.size() ||
           change.owner_id.empty() || !identities.insert(change.owner_id).second)
            throw std::invalid_argument(invalid);
        previous=change.boundary;
    }
    SolidSourceStates result;
    std::size_t source=0;
    const auto add_sources=[&](std::size_t until) {
        for(;source<until;++source) {
            const auto& operation=operations[source];
            if(solid_state_candidate(operation))
                result.emplace(operation.owner_id,SolidSourceState{source,operation.body.id});
        }
    };
    for(const auto& change:changes) {
        if(change.boundary>boundary)break;
        add_sources(change.boundary);
        if(change.suppressed)continue;
        if(!change.restore && (!std::isfinite(change.coefficient)||change.coefficient<=0))
            throw std::invalid_argument("Straightening coefficient must be positive and finite.");
        std::vector<std::string> selected;
        if(change.all) {
            for(const auto& [owner,state]:result)
                if(state.body_id==change.body_id && (!change.restore||state.straight))
                    selected.push_back(owner);
        } else {
            std::set<std::string> unique;
            for(const auto& owner:change.owners) {
                const auto it=result.find(owner);
                if(it==result.end() || it->second.body_id!=change.body_id ||
                   !unique.insert(owner).second)
                    throw std::invalid_argument("A selected solid element is unavailable.");
                selected.push_back(owner);
            }
        }
        if(selected.empty())throw std::invalid_argument("Select at least one eligible solid element.");
        for(const auto& owner:selected) {
            auto& state=result.at(owner);
            // Assignment, not multiplication: repeated states never accumulate
            // length changes. Restore forgets the straight coefficient entirely.
            state.straight=!change.restore;
            state.coefficient=change.restore?1:change.coefficient;
            state.state_owner=change.owner_id;
        }
    }
    add_sources(boundary);
    return result;
}
} // namespace zima::kernel
