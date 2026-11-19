#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <map>
#include <set>

namespace zima::kernel::sheet_branch {
inline bool unassigned(const std::vector<HistoryOperation>& operations) {
    for(const auto& operation:operations) {
        if(operation.sheet_material&&operation.sheet_material->branch_owner_id.empty())return true;
        for(const auto& region:operation.sheet_regions)if(region.branch_owner_id.empty())return true;
    }
    return false;
}

// A single authored continuation belongs to its parent's chain. At a fork the
// parent ends its chain and each child starts its own independent branch. This
// uses history identities only; spatial proximity never creates connectivity.
inline void assign(std::vector<HistoryOperation>& operations) {
    std::map<std::string,std::set<std::string>> children;
    for(auto& operation:operations) {
        const auto add=[&](auto& region) {
            if(!operation.suppressed&&region.kind!=SheetMaterialDefinition::Kind::Form&&
                    !std::holds_alternative<LineBendRequest>(operation.primitive)&&
                    !region.parent_owner_id.empty())
                children[region.parent_owner_id].insert(region.owner_id);
        };
        if(operation.sheet_material)add(*operation.sheet_material);
        for(auto& region:operation.sheet_regions)add(region);
    }
    std::map<std::string,std::string> assigned;
    std::map<std::string,std::set<std::string>> body_branches;
    std::map<std::string,std::string> branch_region;
    for(auto& operation:operations) {
        const bool line_bend=std::holds_alternative<LineBendRequest>(operation.primitive);
        auto& input=body_branches[operation.body.id];
        if(operation.replaces_body)input.clear();
        // A body-wide Line Bend deforms each existing branch. Give each
        // material domain its own derived region, as compound sheets already
        // do, so state changes cannot reconnect overlapping branches.
        if(line_bend&&operation.sheet_material&&input.size()>1) {
            const auto source=*operation.sheet_material;
            operation.sheet_material.reset();
            for(const auto& branch:input) {
                auto region=source;
                region.owner_id=operation.owner_id+":branch:"+branch;
                region.feature_owner_id=operation.owner_id;
                region.parent_owner_id=branch_region.at(branch);
                operation.sheet_regions.push_back(std::move(region));
            }
        }
        const auto add=[&](auto& region) {
            const auto parent=assigned.find(region.parent_owner_id);
            const bool treatment=line_bend||region.kind==SheetMaterialDefinition::Kind::Form;
            region.branch_owner_id=parent!=assigned.end()&&
                    (treatment||children[region.parent_owner_id].size()==1)?parent->second:region.owner_id;
            // Line Bend deforms existing material. An unreferenced line on a
            // single input branch does not create an independent additive root.
            if(line_bend&&parent==assigned.end()&&input.size()==1)
                region.branch_owner_id=*input.begin();
            assigned[region.owner_id]=region.branch_owner_id;
            if(!operation.suppressed) {
                input.insert(region.branch_owner_id);
                branch_region[region.branch_owner_id]=region.owner_id;
            }
        };
        if(operation.sheet_material)add(*operation.sheet_material);
        for(auto& region:operation.sheet_regions)add(region);
    }
}

inline std::map<std::string,std::string> owners(const std::vector<HistoryOperation>& operations) {
    std::map<std::string,std::string> result;
    for(const auto& operation:operations)if(!operation.suppressed) {
        if(operation.sheet_material)result[operation.sheet_material->owner_id]=operation.sheet_material->branch_owner_id;
        for(const auto& region:operation.sheet_regions)result[region.owner_id]=region.branch_owner_id;
    }
    return result;
}
}
