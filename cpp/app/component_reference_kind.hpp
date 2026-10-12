#pragma once
#include <zima/assembly/assembly_document.hpp>
#include <zima/viewer/picking.hpp>
#include <map>
#include <tuple>

namespace zima::app {
// Disposable type lookup for the View's current persisted source packet.
// Display triangles intentionally need not duplicate analytic surface data.
class ComponentReferenceKindIndex {
    using Key=std::tuple<std::string,std::string,std::string>;
    std::map<Key,assembly::MateReferenceKind> faces_;
public:
    void rebuild(const kernel::ViewerReferenceGeometry& source) {
        faces_.clear();
        const kernel::FaceReference* previous{};
        for(const auto& face:source.triangle_references) {
            if(!face.surface)continue;
            if(previous && previous->instance_path==face.instance_path &&
                    previous->owner_id==face.owner_id && previous->semantic_key==face.semantic_key)
                continue;
            faces_.try_emplace(Key{face.instance_path,face.owner_id,face.semantic_key},
                face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder
                    ?assembly::MateReferenceKind::CylinderFace:assembly::MateReferenceKind::Face);
            previous=&face;
        }
    }
    [[nodiscard]] assembly::MateReferenceKind kind(const viewer::ViewerCandidate& candidate) const {
        if(candidate.kind==viewer::CandidateKind::Vertex)return assembly::MateReferenceKind::Point;
        if(candidate.kind==viewer::CandidateKind::Axis)return assembly::MateReferenceKind::Axis;
        if(candidate.kind==viewer::CandidateKind::Face)
            if(const auto found=faces_.find({candidate.instance_path,candidate.owner_id,candidate.semantic_key});found!=faces_.end())
                return found->second;
        return assembly::MateReferenceKind::Face;
    }
};
} // namespace zima::app
