#pragma once
#include <string>
#include <set>
#include <optional>
#include <vector>

namespace zima::document {

struct ConstructionReference {
    std::string instance_path;
    std::string owner_id;
    std::string semantic_key;
    double offset{};
    bool supports_offset{};
    std::string orientation_role{"none"};
    bool orientation_drives_rotation{};
    // True only for a reference that exists SOLELY to contribute a
    // FRONT/TOP direction (a genuine orientation-table entry: either the
    // separate mirrored twin of a Plane/Axis position row 0/1, or a
    // standalone pick made directly into the orientation table) --
    // matching Python's `position_role == "orientation_only"` in
    // `_solve_point_constraints()`. A Point container's automatically
    // oriented position reference (assign_automatic_orientation_role())
    // has orientation_drives_rotation == true but this stays false: it is
    // still the one-and-only copy of that reference and must keep
    // contributing its own position equation, exactly like Python's
    // `_ensure_automatic_orientation_roles()` never sets position_role on
    // it. Only the dedicated orientation-table copy is excluded from the
    // position solve.
    bool orientation_only{};
    // Inverts the resolved direction/normal derived from this reference
    // (Plane -> flips its normal/local-X axis; Axis -> flips its direction
    // vector) as a post-processing step AFTER placement_solve_position()/
    // the orientation-frame composition below has already solved the
    // position/direction from the reference geometry -- it never changes
    // the solving equations themselves. A Point reference carries this
    // field too (for a uniform, non-kind-specific reference model) but it
    // is always a no-op there: a point has no direction to invert. Angles/
    // rotations never use this flag -- the whole system is a fixed
    // right-handed frame (right-hand rule), so a positive angle already
    // has one unambiguous rotation direction.
    bool flip{};
    bool offset_locked{};
    // Transient distance measured before confirming a newly selected reference.
    std::optional<double> measured_offset;
    bool operator==(const ConstructionReference&) const = default;
};

struct Placement {
    // Resolved container origin, either entered directly (no references) or
    // solved from `references` below, exactly as ConstructionObject does for
    // a standalone Point.
    double x{};
    double y{};
    double z{};
    // Resolved final orientation actually applied to the container's local
    // frame: the FRONT/TOP reference frame (when present) composed with the
    // manual rotation_offset_* correction below. When no orientation
    // reference is set this equals the manual offset unchanged.
    double rotation_x{};
    double rotation_y{};
    double rotation_z{};
    // Persisted absolute Euler parameters. A valid orientation reference
    // overwrites every component it constrains; the one remaining free local
    // component stays user-editable until another independent reference
    // constrains it. Consequently the disabled Absolute fields show stored
    // data, not a transient UI-only preview.
    double absolute_rotation_x{};
    double absolute_rotation_y{};
    double absolute_rotation_z{};
    bool orientation_back{};
    int orientation_quarter_turns{};
    // Manual RX/RY/RZ correction the user edits directly; combined on top of
    // any FRONT/TOP reference frame during resolve_placement().
    double rotation_offset_x{};
    double rotation_offset_y{};
    double rotation_offset_z{};
    // Universal container placement references: entries with
    // orientation_drives_rotation == false position the origin (same
    // point/axis/plane equation solve as ConstructionDefinition::PointReference);
    // entries with orientation_drives_rotation == true and orientation_role
    // "front"/"top" orient the container's local frame.
    std::vector<ConstructionReference> references;
    bool reference_valid{true};
    std::set<std::string> value_locks;
    bool operator==(const Placement&) const = default;
};

} // namespace zima::document
