#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <zima/sketcher/sketch.hpp>
#include <zima/document/relations.hpp>

#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zima::document {

enum class CombineMode { Add, Subtract };
enum class FeatureKind { Sketch, Box, Cylinder, Sphere, Cone, Pyramid, Wedge, Extrusion, Revolution, ImportedStep, Fillet, Chamfer };
enum class ExtrusionDirection { Forward, Reverse, Symmetric };
enum class ExtrusionExtent { Blind, UpToPlane, UpToSurface, ThroughAll };
enum class ProfileSource { Internal, External };
enum class ProfileResultType { Solid, Thin };
enum class ProfileExtentMode { OneSide, TwoSides, Symmetric };
enum class ThinMode { OneSide, OtherSide, Symmetric };
enum class EndCondition { Length, UpTo, ThroughAll };
enum class EndTargetKind { Point, Plane, Face };
enum class ConstructionKind { Point, Axis, Plane };
enum class LocalDatumPlane { XY, YZ, XZ };
enum class PartHistoryKind { Feature, Sketch, Construction };
struct PartHistoryEntry {
    PartHistoryKind kind{PartHistoryKind::Feature};
    std::string id;
    bool operator==(const PartHistoryEntry&) const = default;
};
enum class ConstructionDefinition {
    Absolute,
    PointReference,
    TwoPointAxis,
    AxisReference,
    ThreePointPlane,
    PlaneReference,
};

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
    bool operator==(const ConstructionReference&) const = default;
};

enum class OriginChildKind { Point, Axis, Plane };

struct OriginChild {
    std::string id;
    std::string parent_id;
    std::string name;
    OriginChildKind kind{OriginChildKind::Point};
    std::string key;
    bool locked{true};
    bool operator==(const OriginChild&) const = default;
};

struct ContainerOrigin {
    std::string id;
    std::string parent_id;
    std::string name{"Container Origin"};
    std::vector<OriginChild> children;
    bool locked{true};
    bool operator==(const ContainerOrigin&) const = default;
};

[[nodiscard]] ContainerOrigin create_container_origin(
    const std::string& parent_id);

struct ConstructionObject {
    std::string id;
    std::string entity_id;
    std::string entity_parent_id;
    std::string name;
    ConstructionKind kind{ConstructionKind::Point};
    ContainerOrigin container_origin;
    zima::kernel::Vec3 origin;
    // Plane-kind containers only: the resolved position of the visible
    // plane quad/entity itself, i.e. `origin` translated along `direction`
    // by `offset` (see resolve_construction()). Kept separate from
    // `origin` (the CONTAINER's own position, matching its Container
    // Origin preview axes/planes) so the work-plane offset moves only the
    // rendered plane entity -- exactly like Python's
    // `entity.coordinate_system.origin = self._plane_local_offset(...)`
    // being local to, and distinct from, `obj.coordinate_system.origin`.
    // Equals `origin` whenever `offset` is zero. Unused for Point/Axis.
    zima::kernel::Vec3 entity_origin;
    // Composed rotation: the FRONT/TOP orientation-reference base frame (when
    // orientation-driving references are present) combined with the manual
    // rotation_offset_* correction below, matching Placement's
    // rotation_x/y/z + rotation_offset_x/y/z split (see resolve_placement()).
    // When no orientation references are present, this equals the manual
    // rotation entered by the user (rotation_offset_* stays zero in that
    // case, mirroring Python's `_rotation_with_local_offset`).
    zima::kernel::Vec3 rotation;
    zima::kernel::Vec3 absolute_rotation;
    bool orientation_back{};
    int orientation_quarter_turns{};
    double rotation_offset_x{};
    double rotation_offset_y{};
    double rotation_offset_z{};
    // Transient (not persisted): the FRONT/TOP base rotation alone, without
    // the manual correction, for the dialog's read-only "Absolutní" column.
    // Recomputed by resolve_construction(); zero/unused when there are no
    // orientation-driving references.
    zima::kernel::Vec3 rotation_base{};
    // Transient (not persisted): true when `rotation`/`rotation_base` above
    // were NOT derived from an explicit FRONT/TOP orientation reference, but
    // instead auto-inherited from the first PLANE position reference (a
    // Plane container, built-in Origin plane, or coplanar model face) -- the
    // "parallel to / based on this plane" shortcut. The dialog uses this to
    // tell the user the Plane's orientation is already resolved and disable
    // the (otherwise misleading) FRONT/TOP orientation-reference table.
    // Recomputed by resolve_construction(); always false for Point/Axis.
    bool orientation_inherited_from_reference{false};
    zima::kernel::Vec3 direction{0.0, 0.0, 1.0};
    // Axis only: which local frame axis ("x"/"y"/"z", matching the dialog's
    // "Směr" combo) `direction` is picked from once an orientation-driving
    // reference exists ("x"=left, "y"=normal/reference direction itself,
    // "z"=up -- see resolve_construction()). Unused (kept at the default)
    // for Point/Plane and for an Axis with no orientation reference, where
    // the combo instead selects a plain world axis directly.
    std::string direction_axis{"y"};
    // Plane only: which plane of this container's already-resolved local
    // Container Origin is used as the un-offset construction plane. This is
    // a semantic local choice, never a self-reference to viewer geometry.
    LocalDatumPlane base_plane{LocalDatumPlane::YZ};
    double display_size{100.0};
    ConstructionDefinition definition{ConstructionDefinition::Absolute};
    std::vector<ConstructionReference> references;
    // Plane-kind containers only: persisted work-plane offset (mm). The JSON
    // key intentionally remains the legacy generic "offset" because this
    // field was previously persisted-but-dead; reusing it keeps save/load
    // compatibility while repurposing it to the Python-equivalent semantics.
    double offset{};
    bool reference_valid{true};
    bool suppressed{};
    bool operator==(const ConstructionObject&) const = default;
};

[[nodiscard]] bool resolve_construction(
    ConstructionObject& object,
    const zima::kernel::ViewerReferenceGeometry& references);
[[nodiscard]] int point_constraint_remaining_dof(
    const std::vector<ConstructionReference>& references,
    const zima::kernel::ViewerReferenceGeometry& geometry);
struct PointConstraintState {
    int remaining_dof{3};
    std::array<bool, 3> constrained_axes{};
};
struct Placement;
[[nodiscard]] PointConstraintState point_constraint_state(
    const std::vector<ConstructionReference>& references,
    const zima::kernel::ViewerReferenceGeometry& geometry);
// A Point exposes six possible editable dimension slots: absolute X/Y/Z
// coordinates and the offsets of up to three position-reference rows.
// Only slots whose matching dialog fields are editable are returned.
[[nodiscard]] std::vector<zima::kernel::ViewerDimension>
construction_point_dimensions(
    const ConstructionObject& object,
    const zima::kernel::ViewerReferenceGeometry& geometry);
// The same six dimension slots (absolute X/Y/Z plus three reference offsets)
// for every container that owns a universal Placement section.
[[nodiscard]] std::vector<zima::kernel::ViewerDimension>
container_placement_dimensions(
    const std::string& owner_id, const Placement& placement,
    const zima::kernel::ViewerReferenceGeometry& geometry);
[[nodiscard]] int orientation_constraint_remaining_dof(
    const std::vector<ConstructionReference>& references,
    const zima::kernel::ViewerReferenceGeometry& geometry,
    bool marked_only,
    const zima::kernel::Vec3& orientation_origin = {});
// Whether `reference` resolves to a plain point (an Origin/Sketch/other
// container's point, or a solid vertex) rather than an axis/edge or
// plane/face -- shared by resolve_construction()'s "2 points define an
// axis"/"3 points define a plane" shortcut and the UI's row-acceptance
// logic, which both need to recognize the same plain-point references.
[[nodiscard]] bool construction_reference_is_point(
    const ConstructionReference& reference,
    const zima::kernel::ViewerReferenceGeometry& geometry);

struct BoxParameters {
    double length{100.0};
    double width{80.0};
    double height{50.0};
    bool operator==(const BoxParameters&) const = default;
};

struct CylinderParameters {
    double radius{40.0};
    double height{50.0};
    bool operator==(const CylinderParameters&) const = default;
};

struct SphereParameters {
    double radius{40.0};
    bool operator==(const SphereParameters&) const = default;
};
struct ConeParameters {
    double bottom_radius{20.0};
    double top_radius{};
    double height{50.0};
    bool operator==(const ConeParameters&) const = default;
};
struct PyramidParameters {
    double length{40.0}; double width{40.0}; double height{50.0};
    bool operator==(const PyramidParameters&) const = default;
};
struct WedgeParameters {
    double length{60.0}; double width{40.0}; double height{40.0}; double top_offset{30.0};
    bool operator==(const WedgeParameters&) const = default;
};

struct ExtrusionParameters {
    std::string sketch_id;
    double profile_plane_offset{};
    ProfileSource profile_source{ProfileSource::Internal};
    ProfileResultType result_type{ProfileResultType::Solid};
    double thin_thickness{1.0};
    ThinMode thin_mode{ThinMode::OneSide};
    ProfileExtentMode extent_mode{ProfileExtentMode::OneSide};
    double length_forward{10.0};
    double length_reverse{60.0};
    EndCondition end_condition_forward{EndCondition::Length};
    EndCondition end_condition_reverse{EndCondition::Length};
    struct EndTarget {
        EndTargetKind kind{EndTargetKind::Face};
        zima::kernel::FaceReference reference;
        std::string label;
        zima::kernel::Vec3 fallback_origin;
        zima::kernel::Vec3 fallback_normal{0.0, 0.0, 1.0};
        std::vector<zima::kernel::Vec3> fallback_triangles;
        bool operator==(const EndTarget&) const = default;
    };
    std::vector<EndTarget> end_targets_forward;
    std::vector<EndTarget> end_targets_reverse;
    double height{10.0};
    ExtrusionDirection direction{ExtrusionDirection::Forward};
    ExtrusionExtent extent{ExtrusionExtent::Blind};
    zima::kernel::FaceReference target_face;
    zima::kernel::Vec3 target_plane_origin;
    zima::kernel::Vec3 target_plane_normal{0.0, 0.0, 1.0};
    std::vector<zima::kernel::Vec3> target_surface_triangles;
    bool operator==(const ExtrusionParameters&) const = default;
};

struct RevolutionParameters {
    std::string sketch_id;
    // Stable ZIMA Sketch segment used as the unbounded revolution axis.
    // It must identify a green construction centerline in the owned Sketch.
    std::string axis_segment_id;
    double profile_plane_offset{};
    ProfileSource profile_source{ProfileSource::Internal};
    ProfileResultType result_type{ProfileResultType::Solid};
    double thin_thickness{1.0};
    ThinMode thin_mode{ThinMode::OneSide};
    ProfileExtentMode extent_mode{ProfileExtentMode::OneSide};
    ExtrusionDirection direction{ExtrusionDirection::Forward};
    double angle_reverse{360.0};
    double angle_degrees{360.0};
    bool operator==(const RevolutionParameters&) const = default;
};

struct EdgeTreatmentParameters {
    std::vector<zima::kernel::EdgeReference> edges;
    zima::kernel::EdgeSelectionOrigin origin{
        zima::kernel::EdgeSelectionOrigin::OriginalEntity};
    double size{1.0};
    bool operator==(const EdgeTreatmentParameters&) const = default;
};

struct ImportedStepParameters {
    std::string source_path;
    std::string component_path;
    std::shared_ptr<const std::string> frozen_brep;
    bool operator==(const ImportedStepParameters& other) const {
        if (source_path != other.source_path ||
            component_path != other.component_path) return false;
        if (frozen_brep == other.frozen_brep) return true;
        return frozen_brep && other.frozen_brep &&
            *frozen_brep == *other.frozen_brep;
    }
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
    bool operator==(const Placement&) const = default;
};

// Resolves a container's origin (from position references, falling back to
// the entered x/y/z when none are set) and orientation (FRONT/TOP reference
// frame composed with the manual rotation_offset_* correction) the same way
// for every container kind (primitive solids, Extrusion, Revolution, ...).
// Returns whether every reference in `placement.references` could be found
// in `geometry`; on failure the previous x/y/z/rotation_* fields are kept as
// the under-constrained fallback.
[[nodiscard]] bool resolve_placement(
    Placement& placement, const zima::kernel::ViewerReferenceGeometry& geometry,
    zima::kernel::Vec3* base_rotation = nullptr,
    bool* orientation_from_reference = nullptr);

struct HistoryContainer {
    std::string id;
    std::string feature_id;
    std::string feature_parent_id;
    std::string name{"Kvádr"};
    FeatureKind feature_kind{FeatureKind::Box};
    ContainerOrigin container_origin;
    CombineMode combine_mode{CombineMode::Add};
    Placement placement;
    BoxParameters box;
    CylinderParameters cylinder;
    SphereParameters sphere;
    ConeParameters cone;
    PyramidParameters pyramid;
    WedgeParameters wedge;
    ExtrusionParameters extrusion;
    RevolutionParameters revolution;
    ImportedStepParameters imported_step;
    EdgeTreatmentParameters edge_treatment;
    bool suppressed{};
    bool operator==(const HistoryContainer&) const = default;
};

class PartDocument {
public:
    std::string document_id;
    std::string name{"Nový díl"};
    std::map<std::string, std::string> user_parameters;
    std::vector<std::string> user_parameter_order;
    std::map<std::string, std::map<std::string, std::string>> user_parameter_labels;
    std::map<std::string, std::map<std::string, std::string>> user_parameter_values;
    std::vector<ModelRelation> relations;
    std::map<std::string, std::string> document_units{
        {"Length", "mm"}, {"Angle", "deg"}, {"Mass", "kg"},
        {"Time", "s"}, {"Temperature", "C"}, {"Stress", "MPa"}};
    std::map<std::string, std::string> document_precision{
        {"linear_tolerance", "0.001"}, {"angular_tolerance", "0.001"},
        {"mesh_deflection", "0.1"}, {"decimal_places", "3"}};
    std::map<std::string, std::string> physical_parameters;
    std::map<std::string, std::string> physical_parameter_units;
    std::map<std::string, std::map<std::string, std::string>>
        material_parameter_descriptions;
    std::string family_table{"{\"columns\":[],\"instances\":[]}"};
    // JSON-encoded array of custom named camera views saved from the
    // "Pohled kolmo" orientation dialog, mirroring Python's
    // document.document_settings["named_views"].
    std::string named_views{"[]"};
    // Display colour of the calculated body.  It is presentation metadata;
    // changing it never invalidates or recalculates OCCT geometry.
    std::string body_color{"#B9C2CC"};
    std::vector<HistoryContainer> history;
    std::vector<zima::sketcher::Sketch> sketches;
    std::vector<ConstructionObject> constructions;
    std::vector<PartHistoryEntry> history_order;
    std::size_t history_cursor{std::numeric_limits<std::size_t>::max()};

    [[nodiscard]] std::size_t effective_history_cursor() const;
    void set_history_cursor(std::size_t cursor);
    void insert_history_entry(PartHistoryKind kind, std::string id);

    [[nodiscard]] static PartDocument create_default();
    [[nodiscard]] static HistoryContainer create_box_container();
    [[nodiscard]] static HistoryContainer create_sketch_container();
    [[nodiscard]] static HistoryContainer create_cylinder_container();
    [[nodiscard]] static HistoryContainer create_sphere_container();
    [[nodiscard]] static HistoryContainer create_cone_container();
    [[nodiscard]] static HistoryContainer create_pyramid_container();
    [[nodiscard]] static HistoryContainer create_wedge_container();
    [[nodiscard]] static ConstructionObject create_construction(
        ConstructionKind kind);
    [[nodiscard]] ConstructionObject* find_construction(const std::string& id);
    [[nodiscard]] const ConstructionObject* find_construction(
        const std::string& id) const;
    // Origin's on-screen size is a fixed constant, independent of the
    // model/scene size and camera zoom (see kDocumentOriginAxisLength in
    // part_document.cpp). `reference_scene_size` is unused and kept only
    // for source compatibility with existing call sites.
    [[nodiscard]] zima::kernel::ViewerMesh origin_viewer_mesh(
        double reference_scene_size = 0.0) const;
    // A container's own editing-mode Origin is likewise a fixed constant
    // (kContainerOriginAxisLength), the same for every ConstructionKind and
    // distinct from the document's own Origin constant so the two remain
    // visually distinguishable. `reference_scene_size` is unused and kept
    // only for source compatibility with existing call sites.
    [[nodiscard]] zima::kernel::ViewerMesh construction_viewer_mesh(
        const std::string& editing_object_id = {},
        double reference_scene_size = 0.0) const;
    void resolve_constructions(
        zima::kernel::ViewerReferenceGeometry source_geometry = {});
    [[nodiscard]] std::vector<zima::kernel::ViewerEdge> extrusion_preview_edges(
        const HistoryContainer& container, double through_all_span = 1000.0,
        double through_all_reverse_span = 0.0) const;
    [[nodiscard]] std::vector<zima::kernel::ViewerEdge> extrusion_preview_edges(
        const HistoryContainer& container,
        const zima::kernel::ViewerMesh& through_all_input) const;
    [[nodiscard]] std::vector<zima::kernel::ViewerEdge> primitive_preview_edges(
        const HistoryContainer& container) const;
    [[nodiscard]] std::vector<zima::kernel::ViewerEdge> revolution_preview_edges(
        const HistoryContainer& container) const;
    [[nodiscard]] static HistoryContainer create_extrusion_container(
        std::string sketch_id);
    [[nodiscard]] static HistoryContainer create_revolution_container(
        std::string sketch_id);
    [[nodiscard]] static HistoryContainer create_fillet_container(
        std::vector<zima::kernel::EdgeReference> edges);
    [[nodiscard]] static HistoryContainer create_chamfer_container(
        std::vector<zima::kernel::EdgeReference> edges);
    [[nodiscard]] static HistoryContainer create_imported_step_container(
        std::filesystem::path source_path, std::string component_path = {},
        std::string component_name = {});
    [[nodiscard]] HistoryContainer* find_container(const std::string& id);
    [[nodiscard]] const HistoryContainer* find_container(const std::string& id) const;
    [[nodiscard]] std::optional<std::size_t> history_index(
        const std::string& id) const;
    [[nodiscard]] std::vector<zima::kernel::HistoryOperation> kernel_operations(
        bool allow_persisted_external_target = false) const;
    [[nodiscard]] static PartDocument load(
        const std::filesystem::path& path,
        std::vector<zima::kernel::BodyResult>* calculated_boundaries = nullptr);
    void save(
        const std::filesystem::path& path,
        const std::vector<zima::kernel::BodyResult>& calculated_boundaries = {}) const;
};

// Bounding-box diagonal of a mesh's vertices/edges/points, matching Python's
// `_scene_diagonal()`. Used to scale the Origin axes/planes proportionally
// to the scene instead of using a fixed size. Origin geometry itself
// (semantic keys starting with "origin:") is excluded from the bounds, same
// as Python treats the origin as a "reference" rather than scene content.
[[nodiscard]] double viewer_mesh_bounds_diagonal(
    const zima::kernel::ViewerMesh& mesh);

}  // namespace zima::document
