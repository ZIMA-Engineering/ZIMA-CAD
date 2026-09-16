#pragma once
#include <zima/document/part_document.hpp>
#include <zima/document/metadata.hpp>

namespace zima::document {
// Edge-first attachment uses the narrow planar joining face as the start
// profile plane (local XY). The sign aligns profile +Y into that face.
// Consumes only persisted original viewer references; no kernel calculation.
[[nodiscard]] std::optional<double> bend_attachment_profile_direction(
    const std::vector<ConstructionReference>&, const kernel::ViewerReferenceGeometry&);
void initialize_bend_start_profile(sketcher::Sketch&, double width);
// Creation-time layout only; preserve the width and all authored identities.
void orient_bend_start_toward_edge(sketcher::Sketch&, const Placement&, const kernel::ViewerReferenceGeometry&);
[[nodiscard]] BendParameters resolved_bend_parameters(const HistoryContainer&, const SheetMetalDefaults&);
void prepare_bend_sketches(HistoryContainer&,const sketcher::Sketch&,const SheetMetalDefaults&);
void accept_bend_sketch(HistoryContainer&,const sketcher::Sketch&,std::size_t,sketcher::Sketch,const SheetMetalDefaults&);
[[nodiscard]] std::array<double,2> bend_profile_extensions(const HistoryContainer&);
void set_bend_profile_extensions(HistoryContainer&,double first,double last);
[[nodiscard]] kernel::FeatureGroupRequest bend_request(const HistoryContainer&, const sketcher::Sketch&, const SheetMetalDefaults&);
[[nodiscard]] kernel::ViewerMesh bend_preview(const HistoryContainer&, const sketcher::Sketch&, const SheetMetalDefaults&);
}
