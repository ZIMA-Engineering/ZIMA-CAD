#pragma once
#include <zima/document/part_document.hpp>
namespace zima::document {
[[nodiscard]] HistoryContainer create_surface_intersection();
[[nodiscard]] kernel::SurfaceIntersectionRequest surface_intersection_request(const PartDocument&,const HistoryContainer&);
}
