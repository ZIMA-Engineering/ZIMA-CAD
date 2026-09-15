#pragma once
#include <zima/document/part_document.hpp>

namespace zima::document {
// Segment endpoints define both the drilling axis and its finite depth.
// Consumes the Sketch's resolved native frame; never resolves placement itself.
[[nodiscard]] kernel::FeatureGroupRequest holes_request(
    const HistoryContainer&, const sketcher::Sketch&);
}
