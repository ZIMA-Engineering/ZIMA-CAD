#pragma once
#include <zima/document/part_document.hpp>
namespace zima::document {
[[nodiscard]] HistoryContainer create_surface_trim();
void validate_surface_trim(const HistoryContainer&);
[[nodiscard]] kernel::SurfaceTrimRequest surface_trim_request(const PartDocument&,const HistoryContainer&);
}
