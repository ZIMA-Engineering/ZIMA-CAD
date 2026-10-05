#pragma once
#include <zima/document/part_document.hpp>
namespace zima::document {
[[nodiscard]] HistoryContainer create_surface_thicken();
void validate_surface_thicken_parameters(const SurfaceThickenParameters&);
[[nodiscard]] kernel::SurfaceThickenRequest surface_thicken_request(const PartDocument&,const HistoryContainer&);
}
