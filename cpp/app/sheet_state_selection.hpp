#pragma once
#include <zima/viewer/picking.hpp>
#include <map>

namespace zima::app {
// Resolves a candidate from the common picker to its authored material region.
// Only calculated input metadata is read; this is not another hit-test path.
class SheetStateSelection {
public:
    explicit SheetStateSelection(const kernel::ViewerMesh& input,const std::map<std::string,std::string>& feature_owners={}) {
        const auto owner=[&](const std::string& region){const auto found=feature_owners.find(region);return found==feature_owners.end()?region:found->second;};
        for(const auto& face:input.triangle_references) {
            if(face.sheet_owner.empty())continue;
            faces_[{face.owner_id,face.semantic_key}]=owner(face.sheet_owner);
            containers_[face.owner_id].insert(owner(face.sheet_owner));
        }
        for(const auto& edge:input.edges) {
            std::set<std::string> regions;
            for(const auto& face:edge.edge_treatment_side_references)if(!face.sheet_owner.empty())regions.insert(owner(face.sheet_owner));
            if(regions.size()==1)edges_[{edge.reference.owner_id,edge.reference.semantic_key}]=*regions.begin();
            for(const auto& region:regions)wires_[region].insert({edge.reference.owner_id,edge.reference.semantic_key,{}});
        }
    }
    std::string resolve(const viewer::ViewerCandidate& candidate)const {
        if(candidate.kind==viewer::CandidateKind::Container) {
            const auto found=containers_.find(candidate.owner_id);
            return found!=containers_.end()&&found->second.size()==1?*found->second.begin():std::string{};
        }
        const auto* references=candidate.kind==viewer::CandidateKind::Face?&faces_:
            candidate.kind==viewer::CandidateKind::Edge?&edges_:nullptr;
        if(!references)return {};
        const auto found=references->find({candidate.owner_id,candidate.semantic_key});
        return found==references->end()?std::string{}:found->second;
    }
    std::set<viewer::EdgeKey> wire(const std::vector<std::string>& regions,const std::string& path)const {
        std::set<viewer::EdgeKey> result;
        for(const auto& region:regions)if(const auto found=wires_.find(region);found!=wires_.end())
            for(auto edge:found->second){edge.instance_path=path;result.insert(std::move(edge));}
        return result;
    }
private:
    std::map<std::pair<std::string,std::string>,std::string> faces_,edges_;
    std::map<std::string,std::set<std::string>> containers_;
    std::map<std::string,std::set<viewer::EdgeKey>> wires_;
};
}
