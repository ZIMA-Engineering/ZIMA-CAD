#pragma once
#include <zima/document/part_document.hpp>
namespace zima::document {
[[nodiscard]] HistoryContainer create_general_surface();
[[nodiscard]] GeneralSurfaceBoundary create_general_surface_sketch(const HistoryContainer&);
[[nodiscard]] GeneralSurfaceBoundary create_general_surface_curve(const HistoryContainer&);
void validate_general_surface(const HistoryContainer&);
void reframe_general_surface(HistoryContainer&);
void resolve_general_surface_sketch(GeneralSurfaceBoundary&, const kernel::ViewerReferenceGeometry&);
[[nodiscard]] sketcher::Sketch general_surface_display_sketch(const HistoryContainer&,const GeneralSurfaceBoundary&);
[[nodiscard]] kernel::ViewerReferenceGeometry general_surface_boundary_reference_geometry(
    const HistoryContainer&,std::size_t,const kernel::ViewerReferenceGeometry&);
[[nodiscard]] ConstructionObject general_surface_display_curve(const HistoryContainer&,const ConstructionObject&);
[[nodiscard]] kernel::ViewerReferenceGeometry general_surface_local_reference_geometry(const HistoryContainer&,kernel::ViewerReferenceGeometry);
[[nodiscard]] kernel::ViewerMesh general_surface_place_definition_mesh(const Placement&,kernel::ViewerMesh);
[[nodiscard]] kernel::BoundarySurfaceRequest general_surface_request(const PartDocument&,const HistoryContainer&);
[[nodiscard]] kernel::ViewerMesh general_surface_definition_mesh(const HistoryContainer&);
} // namespace zima::document
