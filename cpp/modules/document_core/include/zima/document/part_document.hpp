#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <zima/sketcher/sketch.hpp>
#include <zima/document/relations.hpp>

#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace zima::document {

enum class CombineMode { Add, Subtract };
enum class FeatureKind { Box, Cylinder, Sphere, Cone, Pyramid, Wedge, Extrusion, Revolution, ImportedStep, Fillet, Chamfer };
enum class ExtrusionDirection { Forward, Reverse, Symmetric };
enum class ExtrusionExtent { Blind, UpToPlane, UpToSurface, ThroughAll };
enum class ProfileSource { Internal, External };
enum class ProfileResultType { Solid, Thin };
enum class ProfileExtentMode { OneSide, TwoSides, Symmetric };
enum class ThinMode { OneSide, OtherSide, Symmetric };
enum class EndCondition { Length, UpTo, ThroughAll };
enum class EndTargetKind { Point, Plane, Face };
enum class RevolutionAxis { SketchX, SketchY };
enum class ConstructionKind { Point, Axis, Plane };
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
    zima::kernel::Vec3 rotation;
    zima::kernel::Vec3 direction{0.0, 0.0, 1.0};
    double display_size{100.0};
    ConstructionDefinition definition{ConstructionDefinition::Absolute};
    std::vector<ConstructionReference> references;
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
[[nodiscard]] PointConstraintState point_constraint_state(
    const std::vector<ConstructionReference>& references,
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
    ProfileSource profile_source{ProfileSource::Internal};
    ProfileResultType result_type{ProfileResultType::Solid};
    double thin_thickness{1.0};
    ThinMode thin_mode{ThinMode::OneSide};
    ProfileExtentMode extent_mode{ProfileExtentMode::OneSide};
    ExtrusionDirection direction{ExtrusionDirection::Forward};
    double angle_reverse{360.0};
    RevolutionAxis axis{RevolutionAxis::SketchX};
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
    bool operator==(const ImportedStepParameters&) const = default;
};

struct Placement {
    double x{};
    double y{};
    double z{};
    double rotation_x{};
    double rotation_y{};
    double rotation_z{};
    bool operator==(const Placement&) const = default;
};

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
    [[nodiscard]] zima::kernel::ViewerMesh origin_viewer_mesh() const;
    [[nodiscard]] zima::kernel::ViewerMesh construction_viewer_mesh(
        const std::string& editing_object_id = {}) const;
    void resolve_constructions(
        zima::kernel::ViewerReferenceGeometry source_geometry = {});
    [[nodiscard]] std::vector<zima::kernel::ViewerEdge> extrusion_preview_edges(
        const HistoryContainer& container, double through_all_span = 1000.0) const;
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

}  // namespace zima::document
