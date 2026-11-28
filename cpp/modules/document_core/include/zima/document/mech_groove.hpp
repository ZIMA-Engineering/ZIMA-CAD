#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <zima/sketcher/sketch.hpp>
#include <nlohmann/json_fwd.hpp>

namespace zima::document {
struct HistoryContainer;

enum class GrooveProfile { Rectangular, Circular };

// Width/depth describe the actual groove, not the unstressed retaining ring.
// A circular groove's width is derived from depth and cutter radius.
struct MechGrooveParameters {
    GrooveProfile profile{GrooveProfile::Rectangular};
    double width{2}, depth{1}, radius{1.1};
    std::string catalogue, row;
    double width_position{}; // 0..1 within the selected catalogue width limits.
    kernel::FaceReference support;
    double support_diameter{};
    bool internal{};
    // Exact authored section and its semantic point/curve identities. This is
    // native document content, including the last valid frame after source loss.
    std::string section;
    bool operator==(const MechGrooveParameters&) const = default;
};

void validate_mech_groove(const MechGrooveParameters&);
[[nodiscard]] nlohmann::json serialize_mech_groove(const MechGrooveParameters&);
[[nodiscard]] MechGrooveParameters load_mech_groove(const nlohmann::json&);
[[nodiscard]] HistoryContainer create_mech_groove();

// Consumes native analytic surfaces and the ordinary solved placement. No OCCT,
// fitting of display triangles, or new placement equations.
[[nodiscard]] HistoryContainer resolve_mech_groove_draft(HistoryContainer,
    const kernel::ViewerReferenceGeometry&);
[[nodiscard]] sketcher::Sketch groove_section(const std::string& owner,
    const MechGrooveParameters&, const kernel::SurfaceGeometry&, kernel::Vec3 origin);
[[nodiscard]] std::vector<kernel::ViewerEdge> mech_groove_preview_edges(const HistoryContainer&);
}
