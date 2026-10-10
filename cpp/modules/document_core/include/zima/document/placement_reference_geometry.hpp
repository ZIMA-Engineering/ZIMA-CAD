#pragma once
#include <zima/document/placement_types.hpp>
#include <zima/kernel/geometry_kernel.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace zima::document {
// Entry-scoped, disposable lookup of persisted geometry. The owner must rebuild
// this index when the source changes. Solvers still consume ordinary packets;
// selection retains every matching entity in its original traversal order.
class PlacementReferenceGeometryIndex {
    using Key = std::tuple<std::string, std::string, std::string>;
    struct Entries { std::vector<std::size_t> faces, edges, points, axes; };
    const kernel::ViewerReferenceGeometry* source_;
    std::map<Key, Entries> entries_;
    template<class R> static Key key(const R& ref) {
        return {ref.instance_path, ref.owner_id, ref.semantic_key};
    }
    PlacementReferenceGeometryIndex(const kernel::ViewerReferenceGeometry& source,
        const std::vector<ConstructionReference>* requested)
        : source_(&source) {
        std::set<Key> wanted;
        if(requested)for(const auto& ref:*requested)wanted.insert(key(ref));
        const auto accepts=[&](const auto& ref){return !requested||wanted.contains(key(ref));};
        const kernel::FaceReference* previous{};Entries* faces{};
        for(std::size_t i=0;i<source.triangle_references.size();++i) {
            const auto& ref=source.triangle_references[i];
            if(!previous || previous->instance_path!=ref.instance_path ||
                    previous->owner_id!=ref.owner_id || previous->semantic_key!=ref.semantic_key)
                faces=accepts(ref)?&entries_[key(ref)]:nullptr;
            if(faces)faces->faces.push_back(i);
            previous=&ref;
        }
        for(std::size_t i=0;i<source.edges.size();++i)
            if(accepts(source.edges[i].reference))entries_[key(source.edges[i].reference)].edges.push_back(i);
        for(std::size_t i=0;i<source.points.size();++i)
            if(accepts(source.points[i].reference))entries_[key(source.points[i].reference)].points.push_back(i);
        for(std::size_t i=0;i<source.axes.size();++i)
            if(accepts(source.axes[i].reference))entries_[key(source.axes[i].reference)].axes.push_back(i);
    }
public:
    explicit PlacementReferenceGeometryIndex(const kernel::ViewerReferenceGeometry& source)
        : PlacementReferenceGeometryIndex(source,nullptr) {}
    PlacementReferenceGeometryIndex(const kernel::ViewerReferenceGeometry& source,
        const std::vector<ConstructionReference>& requested)
        : PlacementReferenceGeometryIndex(source,&requested) {}
    bool contains(const ConstructionReference& ref) const { return entries_.contains(key(ref)); }
    kernel::ViewerReferenceGeometry select(const std::vector<ConstructionReference>& refs) const {
        Entries selected;
        for(const auto& ref:refs)if(const auto found=entries_.find(key(ref));found!=entries_.end()) {
            const auto append=[](auto& to,const auto& from){to.insert(to.end(),from.begin(),from.end());};
            append(selected.faces,found->second.faces);append(selected.edges,found->second.edges);
            append(selected.points,found->second.points);append(selected.axes,found->second.axes);
        }
        const auto ordered=[](auto& values) {
            std::ranges::sort(values);values.erase(std::unique(values.begin(),values.end()),values.end());
        };
        ordered(selected.faces);ordered(selected.edges);ordered(selected.points);ordered(selected.axes);
        kernel::ViewerReferenceGeometry result;
        // Remap only storage indices. Values, face identities, analytic packets,
        // triangle winding and ordering are copied exactly, including signed zero.
        std::vector<std::uint32_t> vertices;
        for(const auto i:selected.faces) {
            // Some analytic-only construction packets intentionally omit mesh
            // storage. Preserve that established input without inventing it.
            if(i*3+2>=source_->triangles.size())return *source_;
            for(int c=0;c<3;++c) {
                const auto vertex=source_->triangles[i*3+c];
                if(vertex>=source_->vertices.size())return *source_;
                vertices.push_back(vertex);
            }
        }
        ordered(vertices);
        for(const auto i:vertices)result.vertices.push_back(source_->vertices.at(i));
        for(const auto i:selected.faces) {
            result.triangle_references.push_back(source_->triangle_references[i]);
            for(int c=0;c<3;++c)result.triangles.push_back(static_cast<std::uint32_t>(
                std::lower_bound(vertices.begin(),vertices.end(),source_->triangles[i*3+c])-vertices.begin()));
        }
        for(const auto i:selected.edges)result.edges.push_back(source_->edges[i]);
        for(const auto i:selected.points)result.points.push_back(source_->points[i]);
        for(const auto i:selected.axes)result.axes.push_back(source_->axes[i]);
        return result;
    }
};
} // namespace zima::document
