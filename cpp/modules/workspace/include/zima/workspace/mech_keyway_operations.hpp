#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>

namespace zima::workspace {
[[nodiscard]] const document::HistoryContainer* mech_keyway_symbol_owner(
    const document::PartDocument&,const std::string& symbol_id);
[[nodiscard]] document::HistoryContainer create_mech_keyway();
// Command-local initial constraints, using the ordinary axis/plane placement
// solver. Later edits consume the stored placement without reseeding it.
void initialize_mech_keyway_placement(document::HistoryContainer&,
    const kernel::ViewerReferenceGeometry&, const kernel::FaceReference& axial_plane = {});
struct MechKeywayDraft {
    document::HistoryContainer parent;
    document::KeywayFrame frame;
    document::KeywayCutDefinition cut;
    std::optional<document::Placement> cut_placement;
};
[[nodiscard]] MechKeywayDraft resolve_mech_keyway_draft(document::HistoryContainer,
    const std::string& cut_id,const kernel::ViewerReferenceGeometry&);
[[nodiscard]] std::vector<kernel::ViewerEdge> mech_keyway_preview_edges(const MechKeywayDraft&);
// One analytic dimension source for View inspection and Drawing Show/Erase.
[[nodiscard]] std::vector<kernel::ViewerDimension> mech_keyway_dimensions(const MechKeywayDraft&);
[[nodiscard]] document::PartDocument prepare_mech_keyway(
    const document::PartDocument&, document::HistoryContainer,
    const kernel::ViewerReferenceGeometry&);
[[nodiscard]] bool refresh_mech_keyways(document::PartDocument&,
    const kernel::ViewerReferenceGeometry&, const document::HistoryReferenceViews&);
// Bind new annotation Sketches to the calculated native faces. The resulting
// ordinary external-reference constraints survive full decomposition.
void bind_mech_keyway_annotations(const kernel::OcctKernel&,document::PartDocument&,
    const std::vector<kernel::BodyResult>&);
void bind_mech_keyway_roughness(document::PartDocument&,const std::vector<kernel::BodyResult>&);
[[nodiscard]] bool commit_mech_keyway(Workspace&,const kernel::OcctKernel&,
    const std::string& document_id,document::HistoryContainer,
    std::optional<std::vector<symbols::Placement>> annotations = std::nullopt);
[[nodiscard]] bool explode_mech_keyway(Workspace&,const std::string& document_id,
    const std::string& container_id);
}
