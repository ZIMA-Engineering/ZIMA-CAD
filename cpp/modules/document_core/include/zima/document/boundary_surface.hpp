#pragma once
#include <zima/document/part_document.hpp>
namespace zima::document {
[[nodiscard]] bool boundary_surface_support_allowed(const PartDocument&,const std::string& feature,const kernel::FaceReference&);
[[nodiscard]] HistoryContainer create_boundary_surface();
[[nodiscard]] kernel::BoundarySurfaceRequest boundary_surface_request(const PartDocument&,const HistoryContainer&);
[[nodiscard]] bool boundary_surface_source_allowed(const PartDocument&,const std::string& feature,const BoundaryCurveSource&);
}
