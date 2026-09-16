#pragma once
#include <zima/document/part_document.hpp>
#include <zima/document/metadata.hpp>

namespace zima::document {
[[nodiscard]] BendParameters resolved_bend_parameters(const HistoryContainer&, const SheetMetalDefaults&);
void prepare_bend_sketches(HistoryContainer&,const sketcher::Sketch&,const SheetMetalDefaults&);
void accept_bend_sketch(HistoryContainer&,const sketcher::Sketch&,std::size_t,sketcher::Sketch,const SheetMetalDefaults&);
[[nodiscard]] std::array<double,2> bend_profile_extensions(const HistoryContainer&);
void set_bend_profile_extensions(HistoryContainer&,double first,double last);
[[nodiscard]] kernel::FeatureGroupRequest bend_request(const HistoryContainer&, const sketcher::Sketch&, const SheetMetalDefaults&);
[[nodiscard]] kernel::ViewerMesh bend_preview(const HistoryContainer&, const sketcher::Sketch&, const SheetMetalDefaults&);
}
