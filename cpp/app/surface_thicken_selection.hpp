#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <map>
#include <set>
#include <vector>

namespace zima::app {
// Connected surfaces are recovered from persisted native face adjacency,
// never from tessellation coordinates or live OCCT traversal while picking.
class SurfaceThickenSelection {
public:
    using Key=std::pair<std::string,std::string>;
    SurfaceThickenSelection(const kernel::ViewerMesh& mesh,const std::string& path) {
        for(const auto& face:mesh.triangle_references)if(face.surface_result&&face.instance_path==path)
            faces_.emplace(Key{face.owner_id,face.semantic_key},face);
        for(const auto& edge:mesh.edges)if(edge.reference.instance_path==path&&edge.edge_treatment_side_references.size()==2) {
            const auto& a=edge.edge_treatment_side_references[0];const auto& b=edge.edge_treatment_side_references[1];
            const Key first{a.owner_id,a.semantic_key},second{b.owner_id,b.semantic_key};
            if(faces_.contains(first)&&faces_.contains(second)) {
                adjacent_[first].insert(second);adjacent_[second].insert(first);
            }
        }
    }
    bool contains(const kernel::FaceReference& face)const{return faces_.contains({face.owner_id,face.semantic_key});}
    std::vector<kernel::FaceReference> faces(const kernel::FaceReference& anchor)const {
        const Key start{anchor.owner_id,anchor.semantic_key};if(!faces_.contains(start))return {};
        std::set<Key> visited{start};std::vector<Key> queue{start};
        for(std::size_t i=0;i<queue.size();++i)if(const auto found=adjacent_.find(queue[i]);found!=adjacent_.end())
            for(const auto& neighbor:found->second)if(visited.insert(neighbor).second)queue.push_back(neighbor);
        std::vector<kernel::FaceReference> result;
        for(const auto& key:visited)result.push_back(faces_.at(key));return result;
    }
private:
    std::map<Key,kernel::FaceReference> faces_;
    std::map<Key,std::set<Key>> adjacent_;
};
}
