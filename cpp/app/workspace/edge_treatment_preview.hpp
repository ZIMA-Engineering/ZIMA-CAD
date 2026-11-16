#pragma once

#include <zima/document/part_document.hpp>
#include <string>
#include <vector>

namespace zima::app::workspace_detail {

struct EdgeTreatmentPreviewPath {
    std::vector<zima::kernel::ViewerEdge> edges;
};

struct EdgeTreatmentPreviewGeometry {
    struct TerminalLabel {
        zima::kernel::Vec3 point;
        std::string text;
        std::size_t route{};
        bool first{};
        zima::kernel::VertexReference vertex;
    };
    std::vector<zima::kernel::ViewerEdge> edges;
    std::vector<zima::kernel::ViewerDimension> dimensions;
    // View-only source nodes. Every distinct topological node along one
    // connected input path is numbered in traversal order (1.1, 1.2, ...).
    // The closing node of a loop is shown once as 1.1. Generated treatment
    // contacts do not participate in this numbering and labels are transient.
    std::vector<std::pair<zima::kernel::Vec3,std::string>> labels;
    // Open-route endpoints are separately interactive and painted purple.
    // Their stable vertex identity, rather than display traversal order, owns
    // the persisted Cut/NoCut choice.
    std::vector<TerminalLabel> terminal_labels;
    // Analytical rail contacts retained only for verification. They are kept
    // separate from the user-facing source-node labels above.
    std::vector<zima::kernel::Vec3> terminal_contacts;
};

std::vector<EdgeTreatmentPreviewPath> ordered_edge_treatment_preview_paths(
    std::vector<zima::kernel::ViewerEdge> edges);

std::vector<zima::kernel::Vec3> edge_treatment_preview_path_nodes(
    const EdgeTreatmentPreviewPath& path);

EdgeTreatmentPreviewGeometry edge_treatment_preview_wire(
    const std::vector<std::vector<zima::kernel::ViewerEdge>>& groups,
    const zima::document::EdgeTreatmentParameters& parameters,
    zima::document::FeatureKind kind, const std::string& owner_id,
    const zima::kernel::ViewerMesh* input_mesh);

} // namespace zima::app::workspace_detail
