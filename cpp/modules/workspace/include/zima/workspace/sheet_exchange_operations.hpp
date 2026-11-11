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
[[nodiscard]] double suggest_sheet_thickness(const kernel::ViewerMesh&,const kernel::FaceReference&);
[[nodiscard]] double suggest_sheet_thickness(const Workspace&,const std::string& document,
    const kernel::ViewerMesh&,const kernel::FaceReference&);
[[nodiscard]] SheetBodyConversion prepare_sheet_from_body(const document::PartDocument&,
    const std::vector<kernel::BodyResult>&,const kernel::FaceReference&,double thickness,
    const kernel::OcctKernel&,bool thickness_override=true,
    std::vector<kernel::FaceReference> included_faces={},
    std::vector<std::string> rip_sketches={});
// Refreshes only bound, non-detached native Flat/Bend children whose stable
// source regions changed. Missing source topology keeps the last valid child
// geometry and marks its binding invalid for later repair.
[[nodiscard]] bool refresh_sheet_from_body_associations(
    document::PartDocument&,const std::vector<kernel::BodyResult>&,
    const kernel::OcctKernel&);
// Detaches one generated native Flat/Bend when child_id is present. Without a
// child, removes the conversion owner and keeps every generated feature as
// ordinary Body history. Both forms are one calculated Undo transaction.
[[nodiscard]] bool explode_sheet_from_body(
    Workspace&,const kernel::OcctKernel&,const std::string& document_id,
    const std::string& conversion_id,
    std::optional<std::string> child_id=std::nullopt);
}
