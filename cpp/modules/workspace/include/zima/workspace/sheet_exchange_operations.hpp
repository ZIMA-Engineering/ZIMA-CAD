#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/interchange/dxf.hpp>
#include <optional>

namespace zima::workspace {
struct SheetDxfResult {sketcher::Sketch contour;interchange::DxfExportOptions export_options;double thickness{};double volume{};};
[[nodiscard]] SheetDxfResult prepare_sheet_dxf(const document::PartDocument&,
    const std::vector<kernel::BodyResult>&,const kernel::OcctKernel&);
struct SheetBodyConversion {
    document::PartDocument document;
    std::vector<kernel::BodyResult> calculated;
    std::string container_id;
    std::vector<std::string> created;
    std::size_t skipped{};
};
// Stage the shared editor's transient RIP Sketches in a private document.
// The caller publishes this together with the conversion in one transaction.
[[nodiscard]] bool stage_sheet_from_body_rips(document::PartDocument&,
    const std::string& conversion_id,const std::vector<std::string>& selected,
    const std::vector<document::GeneralSurfaceBoundary>& drafts);
// Preserve a failed command's authored inputs in a private, repairable
// definition. Publication is owned by the common second-OK confirmation.
void stage_unresolved_sheet_from_body(document::PartDocument&,
    document::HistoryContainer marker,const kernel::FaceReference&,double thickness,
    bool thickness_override,const std::vector<kernel::FaceReference>& included,
    const std::vector<std::string>& rip_sketches);
// Insert only the existing same-Body replacement boundary. No source face is
// inferred and no native sheet children are generated.
[[nodiscard]] SheetBodyConversion prepare_sheet_from_body_boundary(
    const document::PartDocument&,const std::vector<kernel::BodyResult>&,
    double thickness,const kernel::OcctKernel&,bool thickness_override=false);
[[nodiscard]] double suggest_sheet_thickness(const kernel::ViewerMesh&,const kernel::FaceReference&);
[[nodiscard]] double suggest_sheet_thickness(const Workspace&,const std::string& document,
    const kernel::ViewerMesh&,const kernel::FaceReference&);
[[nodiscard]] SheetBodyConversion prepare_sheet_from_body(const document::PartDocument&,
    const std::vector<kernel::BodyResult>&,const kernel::FaceReference&,double thickness,
    const kernel::OcctKernel&,bool thickness_override=true,
    std::vector<kernel::FaceReference> included_faces={},
    std::vector<std::string> rip_sketches={});
[[nodiscard]] SheetBodyConversion prepare_edited_sheet_from_body(
    const document::PartDocument&,const std::vector<kernel::BodyResult>&,
    const std::string& conversion_id,const std::optional<kernel::FaceReference>&,
    double thickness,const kernel::OcctKernel&,bool thickness_override=true,
    std::vector<kernel::FaceReference> included_faces={},
    std::vector<std::string> rip_sketches={});
// Refreshes only bound, non-detached native Flat/Bend children whose stable
// source regions changed. Missing source topology keeps the last valid child
// geometry and marks its binding invalid for later repair.
[[nodiscard]] bool refresh_sheet_from_body_associations(
    document::PartDocument&,const std::vector<kernel::BodyResult>&,
    const kernel::OcctKernel&);
// Prepare the ordinary full release in a caller-owned transaction. Retains
// the conversion boundary and freezes each child's last valid native frame.
[[nodiscard]] bool release_sheet_from_body_bindings(
    document::PartDocument&,const std::string& conversion_id);
// Detaches one generated native Flat/Bend when child_id is present. Without a
// child, releases every generated feature and retains the conversion boundary.
// Both forms are one calculated Undo transaction.
[[nodiscard]] bool explode_sheet_from_body(
    Workspace&,const kernel::OcctKernel&,const std::string& document_id,
    const std::string& conversion_id,
    std::optional<std::string> child_id=std::nullopt);
}
