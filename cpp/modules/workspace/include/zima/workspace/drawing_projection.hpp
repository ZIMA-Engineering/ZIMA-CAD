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
    };
    DrawingProjection(const Workspace*,std::filesystem::path drawing_path);
    ~DrawingProjection();
    DrawingProjection(const DrawingProjection&)=delete;
    DrawingProjection& operator=(const DrawingProjection&)=delete;
    const Source& source(const drawing::DrawingView&);
    void project(drawing::DrawingView&,Options);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
