#pragma once

#include <zima/document/part_document.hpp>
#include <string>
#include <vector>

namespace zima::app::workspace_detail {

struct EdgeTreatmentPreviewPath {
    std::vector<zima::kernel::ViewerEdge> edges;
};

struct EdgeTreatmentPreviewGeometry {
    std::vector<zima::kernel::ViewerEdge> edges;
    std::vector<zima::kernel::ViewerDimension> dimensions;
};

std::vector<EdgeTreatmentPreviewPath> ordered_edge_treatment_preview_paths(
    std::vector<zima::kernel::ViewerEdge> edges);

EdgeTreatmentPreviewGeometry edge_treatment_preview_wire(
    const std::vector<std::vector<zima::kernel::ViewerEdge>>& groups,
    const zima::document::EdgeTreatmentParameters& parameters,
    zima::document::FeatureKind kind, const std::string& owner_id,
    const zima::kernel::ViewerMesh* input_mesh);

} // namespace zima::app::workspace_detail
