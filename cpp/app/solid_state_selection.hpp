#pragma once
#include <zima/kernel/solid_state_ancestry.hpp>
#include <zima/viewer/picking.hpp>
#include <map>
#include <set>

namespace zima::app {
// Interpret the common picker's calculated input candidates. Never hit-test
// separately or substitute an OCCT traversal for persisted ancestry.
class SolidStateSelection {
public:
    SolidStateSelection(const kernel::ViewerMesh& input,const std::set<std::string>& available) {
        const auto source=[&](std::string owner,std::string key) {
            while(const auto parent=kernel::solid_state_parent(key)){owner=parent->first;key=parent->second;}
            return available.contains(owner)?owner:std::string{};
        };
        for(const auto& face:input.triangle_references) {
            const auto owner=source(face.owner_id,face.semantic_key);if(owner.empty())continue;
            faces_[{face.owner_id,face.semantic_key}]=owner;
            containers_[face.owner_id].insert(owner);
        }
        for(const auto& edge:input.edges) {
            std::set<std::string> sources;
            if(auto owner=source(edge.reference.owner_id,edge.reference.semantic_key);!owner.empty())sources.insert(owner);
            for(const auto& face:edge.edge_treatment_side_references)
                if(auto owner=source(face.owner_id,face.semantic_key);!owner.empty())sources.insert(owner);
            // A shared seam between independently selectable sources is not
            // an unambiguous source entry. Its adjacent faces remain available.
            if(sources.size()==1)edges_[{edge.reference.owner_id,edge.reference.semantic_key}]=*sources.begin();
            for(const auto& owner:sources)wires_[owner].insert({edge.reference.owner_id,edge.reference.semantic_key,{}});
        }
        for(const auto& owner:available)containers_[owner].insert(owner);
    }
    std::string resolve(const viewer::ViewerCandidate& candidate)const {
        if(candidate.kind==viewer::CandidateKind::Container) {
            const auto found=containers_.find(candidate.owner_id);
            return found!=containers_.end()&&found->second.size()==1?*found->second.begin():std::string{};
        }
        const auto* map=candidate.kind==viewer::CandidateKind::Face?&faces_:
            candidate.kind==viewer::CandidateKind::Edge?&edges_:nullptr;
        if(!map)return {};
        const auto found=map->find({candidate.owner_id,candidate.semantic_key});
        return found==map->end()?std::string{}:found->second;
    }
    std::set<viewer::EdgeKey> wire(const std::vector<std::string>& owners,const std::string& path)const {
        std::set<viewer::EdgeKey> result;
        for(const auto& owner:owners)if(const auto found=wires_.find(owner);found!=wires_.end())
            for(auto edge:found->second){edge.instance_path=path;result.insert(std::move(edge));}
        return result;
    }
private:
    std::map<std::pair<std::string,std::string>,std::string> faces_,edges_;
    std::map<std::string,std::set<std::string>> containers_;
    std::map<std::string,std::set<viewer::EdgeKey>> wires_;
};
}
