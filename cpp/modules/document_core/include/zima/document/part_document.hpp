#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <zima/sketcher/sketch.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace zima::document {

enum class CombineMode { Add, Subtract };
enum class FeatureKind { Box, Cylinder, Sphere, Cone, Pyramid, Wedge, Extrusion, Revolution, ImportedStep, Fillet, Chamfer };
enum class ExtrusionDirection { Forward, Reverse, Symmetric };
enum class ExtrusionExtent { Blind, UpToPlane, UpToSurface, ThroughAll };
enum class RevolutionAxis { SketchX, SketchY };
enum class ConstructionKind { Point, Axis, Plane };

struct ConstructionObject {
    std::string id;
    std::string name;
    ConstructionKind kind{ConstructionKind::Point};
    zima::kernel::Vec3 origin;
    zima::kernel::Vec3 direction{0.0, 0.0, 1.0};
    double display_size{100.0};
};

struct BoxParameters {
    double length{100.0};
    double width{80.0};
    double height{50.0};
};

struct CylinderParameters {
    double radius{40.0};
    double height{50.0};
};

struct SphereParameters { double radius{40.0}; };
struct ConeParameters {
    double bottom_radius{20.0};
    double top_radius{};
    double height{50.0};
};
struct PyramidParameters { double length{40.0}; double width{40.0}; double height{50.0}; };
struct WedgeParameters {
    double length{60.0}; double width{40.0}; double height{40.0}; double top_offset{30.0};
};

struct ExtrusionParameters {
    std::string sketch_id;
    double height{10.0};
    ExtrusionDirection direction{ExtrusionDirection::Forward};
    ExtrusionExtent extent{ExtrusionExtent::Blind};
    zima::kernel::FaceReference target_face;
    zima::kernel::Vec3 target_plane_origin;
    zima::kernel::Vec3 target_plane_normal{0.0, 0.0, 1.0};
    std::vector<zima::kernel::Vec3> target_surface_triangles;
};

struct RevolutionParameters {
    std::string sketch_id;
    RevolutionAxis axis{RevolutionAxis::SketchX};
    double angle_degrees{360.0};
};

struct EdgeTreatmentParameters {
    std::vector<zima::kernel::EdgeReference> edges;
    zima::kernel::EdgeSelectionOrigin origin{
        zima::kernel::EdgeSelectionOrigin::OriginalEntity};
    double size{1.0};
};

struct ImportedStepParameters {
    std::string source_path;
    std::string component_path;
};

struct Placement {
    double x{};
    double y{};
    double z{};
    double rotation_x{};
    double rotation_y{};
    double rotation_z{};
};

struct HistoryContainer {
    std::string id;
    std::string name{"Kvádr"};
    FeatureKind feature_kind{FeatureKind::Box};
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
};

class PartDocument {
public:
    std::string document_id;
    std::string name{"Nový díl"};
    std::vector<HistoryContainer> history;
    std::vector<zima::sketcher::Sketch> sketches;
    std::vector<ConstructionObject> constructions;

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
    [[nodiscard]] zima::kernel::ViewerMesh construction_viewer_mesh() const;
    [[nodiscard]] std::vector<zima::kernel::ViewerEdge> extrusion_preview_edges(
        const HistoryContainer& container, double through_all_span = 1000.0) const;
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
    [[nodiscard]] std::vector<zima::kernel::HistoryOperation> kernel_operations() const;
    [[nodiscard]] static PartDocument load(
        const std::filesystem::path& path,
        std::vector<zima::kernel::BodyResult>* calculated_boundaries = nullptr);
    void save(
        const std::filesystem::path& path,
        const std::vector<zima::kernel::BodyResult>& calculated_boundaries = {}) const;
};

}  // namespace zima::document
