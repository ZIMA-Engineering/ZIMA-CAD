#pragma once
#include <zima/sketcher/curve_fillet.hpp>
#include <zima/viewer/picking.hpp>
namespace zima::app::workspace_detail {
inline std::string sketch_fillet_candidate_id(const zima::sketcher::Sketch* sketch,
        const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch || candidate.owner_id != sketch->id ||
        (candidate.kind != zima::viewer::CandidateKind::SketchSegment &&
         candidate.kind != zima::viewer::CandidateKind::SketchCurve)) return {};
    const auto colon = candidate.semantic_key.find(':');
    if (colon == std::string::npos) return {};
    const auto id = candidate.semantic_key.substr(colon + 1);
    return zima::sketcher::fillet_curve_endpoints(*sketch, id) ? id : std::string{};
}
}
