#pragma once
#include <zima/workspace/model_calculation.hpp>

namespace zima::workspace {
[[nodiscard]] document::HistoryContainer holes_from_sketch(
    const document::PartDocument&, const std::string& sketch_id);
// Create, conversion and later edit all commit one pending Sketch + diameter.
[[nodiscard]] bool commit_holes(Workspace&, const kernel::OcctKernel&,
    const std::string& document_id, document::HistoryContainer,
    sketcher::Sketch);
}
