#pragma once
#include <zima/sketcher/sketch.hpp>
#include <zima/document/placement_types.hpp>
#include <zima/document/history_identity.hpp>
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
    bool operator==(const SectionComponent&) const = default;
};
struct SectionDefinition {
    std::string id, name{"A–A"};
    zima::sketcher::Sketch sketch;
    Placement placement;
    ContainerOrigin container_origin;
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
    SectionFrame frame;
    std::vector<std::array<zima::kernel::Vec3,2>> boundary;
    std::vector<std::array<zima::kernel::Vec3,3>> triangles;
};
struct SectionResult {
    zima::kernel::ViewerMesh mesh;
    std::vector<SectionPatch> patches;
};
SectionDefinition create_section();
// Derive the section sketch frame from the ordinary resolved container placement.
void reframe_section(SectionDefinition&);
void resolve_section_placements(std::vector<SectionDefinition>&, const zima::kernel::ViewerReferenceGeometry&);
// Ordered, connected open chain. Construction geometry is not part of the cut.
std::vector<std::array<double,2>> section_path(const SectionDefinition&);
std::vector<SectionFrame> section_frames(const SectionDefinition&);
SectionFrame section_frame(const SectionDefinition&);
void validate_hatch(const HatchStyle&);
HatchStyle section_component_hatch(const SectionDefinition&, const std::string& component);
// Display helpers only; no persistent topology or solid geometry is changed.
zima::kernel::ViewerMesh section_display_mesh(SectionResult, const SectionDefinition&);
// Intersection outline and hatching only; leaves the displayed solid whole.
zima::kernel::ViewerMesh section_surface_mesh(const SectionResult&, const SectionDefinition&);
std::string serialize_sections(const std::vector<SectionDefinition>&);
std::vector<SectionDefinition> parse_sections(const std::string&);
SectionResult calculate_section(const zima::kernel::ViewerMesh&, const SectionDefinition&);
// Paper-space hatching is clipped by the exact polygon represented by the
// section's persisted display triangles. Returned helpers have no topology ID.
std::vector<zima::kernel::ViewerEdge> section_hatch_lines(const SectionPatch&,
    const SectionFrame&, const HatchStyle&, double view_scale);
} // namespace zima::document
