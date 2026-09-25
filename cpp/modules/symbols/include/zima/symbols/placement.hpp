#pragma once
#include <zima/symbols/definition.hpp>
#include <optional>

namespace zima::symbols {
// Annotation coordinates, independent of feature/container placement and OCCT.
// The frame is right-handed. Its origin is the reference contact point; the
// SymbolInstance XY offset positions the grip within this plane.
struct Frame {
    kernel::Vec3 origin, x{1,0,0}, y{0,1,0};
    void validate() const;
    [[nodiscard]] kernel::Vec3 world(kernel::Vec3 local) const;
    [[nodiscard]] kernel::Vec3 local(kernel::Vec3 world) const;
    bool operator==(const Frame&) const = default;
};
enum class ReferenceKind { Face, Edge, Point, Plane };
struct Reference {
    std::string document_id, owner_id, semantic_key, instance_path;
    ReferenceKind kind{ReferenceKind::Face};
    // Explicit oriented side; zero contact distance does not merge the sides.
    bool reversed{};
    // Plane: local XY; cylinder/cone: angle in radians and axial distance.
    // Captured against the persisted analytic source, never a triangle index.
    std::optional<kernel::SurfaceGeometry::Kind> surface_kind;
    std::array<double,2> surface_parameters{};
    bool operator==(const Reference&) const = default;
};
struct Placement {
    sketcher::SymbolInstance symbol;
    Frame frame;
    std::optional<Reference> reference;
    bool unresolved{};
    bool leader{};
    // Optional bends in annotation-plane coordinates, from contact to grip.
    std::vector<std::array<double,2>> leader_bends;
    double arrow_length{2.5};
    void validate() const;
    // Called with an explicitly resolved original-geometry frame. Losing a
    // reference preserves the last valid frame AND reference identity.
    void refresh_reference(const std::optional<Frame>& resolved);
    [[nodiscard]] kernel::ViewerMesh viewer_mesh(std::optional<double> paper_frame_angle = std::nullopt) const;
    bool operator==(const Placement&) const = default;
};
void to_json(nlohmann::json&, const Placement&);
void from_json(const nlohmann::json&, Placement&);
[[nodiscard]] nlohmann::json placements_json(const std::vector<Placement>&);
[[nodiscard]] std::vector<Placement> placements_from_json(const nlohmann::json&);
void attach_to_surface(Placement&,const kernel::FaceReference&,const std::string& source_document,
    kernel::Vec3 contact,bool reversed=false);
// Resolves only the exact original face and occurrence. Missing/unsupported
// geometry preserves the complete last pose and marks the attachment unresolved.
bool refresh_surface_attachment(Placement&,const kernel::ViewerReferenceGeometry&);
}
