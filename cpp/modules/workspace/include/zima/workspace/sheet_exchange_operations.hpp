#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/kernel/occt_kernel.hpp>

namespace zima::workspace {
struct SheetDxfResult {sketcher::Sketch contour;double thickness{};double volume{};};
[[nodiscard]] SheetDxfResult prepare_sheet_dxf(const document::PartDocument&,
    const std::vector<kernel::BodyResult>&,const kernel::OcctKernel&);
struct SheetBodyConversion {
    document::PartDocument document;
    std::vector<kernel::BodyResult> calculated;
    std::vector<std::string> created;
    std::size_t skipped{};
};
[[nodiscard]] double suggest_sheet_thickness(const kernel::ViewerMesh&,const kernel::FaceReference&);
[[nodiscard]] SheetBodyConversion prepare_sheet_from_body(const document::PartDocument&,
    const std::vector<kernel::BodyResult>&,const kernel::FaceReference&,double thickness,
    const kernel::OcctKernel&);
}
