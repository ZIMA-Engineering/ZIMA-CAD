#pragma once
#include <zima/document/part_document.hpp>

namespace zima::document {
// Segment endpoints define both the drilling axis and its finite depth.
// Consumes the Sketch's resolved native frame; never resolves placement itself.
[[nodiscard]] kernel::FeatureGroupRequest holes_request(
    const HistoryContainer&, const sketcher::Sketch&);
// Analytical cylinder wires for the properties preview. Uses the same
// resolved drilling request as calculation, without calling the solid kernel.
// Includes one diameter dimension anchored to an existing drilling segment.
// Empty drafts have no wire or dimension; deleting its segment reassigns it.
[[nodiscard]] kernel::ViewerMesh holes_preview(
    const HistoryContainer&, const sketcher::Sketch&);
}
