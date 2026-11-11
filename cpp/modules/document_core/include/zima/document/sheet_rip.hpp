#pragma once
#include <zima/document/part_document.hpp>

namespace zima::document {
// Builds exact transient RIP curves from complete ordinary Sketches. The
// request is used only while Sheet-from-Body analyses its source solid; it is
// not an independently persisted history operation.
[[nodiscard]] kernel::SheetRipRequest sheet_rip_request(
    const std::vector<const sketcher::Sketch*>& sketches,double tolerance);
}
