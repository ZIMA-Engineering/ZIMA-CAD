#pragma once
#include <zima/workspace/body_operations.hpp>

namespace zima::workspace {
// Shared read-only source access for non-material Bodies. No CAD evaluation.
void validate_body_graph_edit(const document::DocumentSession&,const BodyGraphEdit&);
[[nodiscard]] const kernel::BodySnapshot* calculated_material_source(
    const document::DocumentSession&,const std::string& source_id);
[[nodiscard]] std::vector<std::string> calculated_material_candidates(
    const document::DocumentSession&,const BodyGraphEdit&);
[[nodiscard]] std::string material_source_frame_owner(
    const document::BodyHistoryGraph&,std::string source_id);
[[nodiscard]] kernel::ViewerReferenceGeometry material_source_placement_geometry(
    const document::PartDocument&,const kernel::BodySnapshot&,const std::string& source_id);
} // namespace zima::workspace
