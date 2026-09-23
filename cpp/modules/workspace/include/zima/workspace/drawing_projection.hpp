#pragma once
#include <zima/workspace/drawing_sources.hpp>
#include <memory>
namespace zima::workspace {
// A short-lived explicit projection/edit session. Source and camera caches
// belong to this session, never to ordinary queries or document activation.
class DrawingProjection {
public:
    struct Source {
        std::string document_id;
        std::filesystem::path path;
        kernel::ViewerMesh mesh;
        std::vector<drawing::BomRow> bom;
        std::vector<drawing::ModelAnnotationSource> annotations;
        std::vector<document::SectionDefinition> sections;
    };
    struct Options {
        bool pending_hatch{};
        bool refresh_markers{};
        bool require_geometry{true};
        bool interactive{};
    };
    // Previous projections may be reused only after freshly reading and exactly
    // comparing their source packets. The donor must outlive this session.
    DrawingProjection(const Workspace*,std::filesystem::path drawing_path,
                      const DrawingProjection* previous=nullptr);
    ~DrawingProjection();
    DrawingProjection(const DrawingProjection&)=delete;
    DrawingProjection& operator=(const DrawingProjection&)=delete;
    const Source& source(const drawing::DrawingView&);
    // Placement needs only the current mesh bounds; defer BOM and annotations.
    const kernel::ViewerMesh& placement_source(const drawing::DrawingView&);
    void project(drawing::DrawingView&,Options);
    [[nodiscard]] std::size_t calculated_camera_count() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
