#pragma once
#include <zima/sketcher/sketch.hpp>
#include <map>
#include <array>
#include <string>
#include <vector>

namespace zima::document {
struct HatchStyle {
    double angle{45}, spacing_mm{2}, offset_mm{};
    int pattern{}; // 0 parallel, 1 cross, 2 dashed
    bool operator==(const HatchStyle&) const = default;
};
struct SectionComponent {
    int mode{}; // 0 cut + hatch, 1 cut only, 2 uncut
    HatchStyle hatch;
    bool custom_hatch{};
};
struct SectionDefinition {
    std::string id, name{"A–A"};
    zima::sketcher::Sketch sketch;
    zima::kernel::Vec3 plane_origin{}, plane_x{1,0,0}, plane_y{0,1,0};
    std::map<std::string,std::string> component_names;
    bool reversed{}, show_plane{}, show_cut{};
    std::map<std::string,SectionComponent> components;
    // Derived from the current Part history, never a new topology identity.
    std::map<std::string,std::string> body_owners;
};
struct SectionFrame { zima::kernel::Vec3 origin, horizontal, vertical, normal; };
struct SectionPatch {
    std::string component;
    std::vector<std::array<zima::kernel::Vec3,2>> boundary;
    std::vector<std::array<zima::kernel::Vec3,3>> triangles;
};
struct SectionResult {
    zima::kernel::ViewerMesh mesh;
    std::vector<SectionPatch> patches;
};
SectionFrame section_frame(const SectionDefinition&);
void validate_hatch(const HatchStyle&);
std::string serialize_sections(const std::vector<SectionDefinition>&);
std::vector<SectionDefinition> parse_sections(const std::string&);
SectionResult calculate_section(const zima::kernel::ViewerMesh&, const SectionDefinition&);
// Paper-space hatching is clipped by the exact polygon represented by the
// section's persisted display triangles. Returned helpers have no topology ID.
std::vector<zima::kernel::ViewerEdge> section_hatch_lines(const SectionPatch&,
    const SectionFrame&, const HatchStyle&, double view_scale);
} // namespace zima::document
