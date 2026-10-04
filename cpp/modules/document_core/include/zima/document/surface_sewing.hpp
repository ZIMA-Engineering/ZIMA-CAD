#pragma once
#include <zima/document/part_document.hpp>
namespace zima::document {
[[nodiscard]] HistoryContainer create_surface_sewing();
[[nodiscard]] kernel::SurfaceSewingRequest surface_sewing_request(const PartDocument&,const HistoryContainer&);
}
