#pragma once
#include <zima/workspace/model_calculation.hpp>
namespace zima::workspace {
// Seed only an empty draft after ordinary sheet attachment has resolved its
// two external endpoints. Placement and existing profiles remain untouched.
[[nodiscard]] bool initialize_attached_flat_profile(sketcher::Sketch&);
// Replace a converted outline's two exact endpoint contacts by native zero
// differences. Adjacent straight sides retain their authored directions.
void initialize_converted_flat_corrections(sketcher::Sketch&);
void retain_converted_flat_corrections(sketcher::Sketch&,const sketcher::Sketch&);
[[nodiscard]] bool commit_flat(Workspace&, const kernel::OcctKernel&, const std::string&,
    document::HistoryContainer, sketcher::Sketch);
}
