#pragma once

#include <zima/kernel/geometry_kernel.hpp>

#include <filesystem>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace zima::sketcher {

enum class SketchPlane { XY, XZ, YZ };
enum class ConstraintKind {
    Horizontal, Vertical, Coincident, Parallel, Perpendicular, EqualLength,
    PointOnCircle, Symmetric, Midpoint, Concentric, Tangent
};
enum class DimensionKind {
    Distance, DistanceX, DistanceY, Radius, Diameter, Angle,
    EllipseMajorRadius, EllipseMinorRadius, EllipseRotation
};
enum class SolveStatus { Solved, UnderConstrained, Conflicting, Invalid };

struct SketchPoint {
    std::string id;
    double x{};
    double y{};
    bool fixed{};
    bool construction{};
    bool operator==(const SketchPoint&) const = default;
};

struct SketchSegment {
    std::string id;
    std::string first_point_id;
    std::string second_point_id;
    bool construction{};
    bool operator==(const SketchSegment&) const = default;
};

struct SketchCircle {
    std::string id;
    std::string center_point_id;
    double radius{};
    bool construction{};
    bool operator==(const SketchCircle&) const = default;
};

struct SketchArc {
    std::string id;
    std::string center_point_id;
    std::string start_point_id;
    std::string end_point_id;
    double radius{};
    double start_angle{};
    double end_angle{};
    bool construction{};
    bool operator==(const SketchArc&) const = default;
};

struct SketchEllipse {
    std::string id;
    std::string center_point_id;
    std::string major_point_id;
    std::string minor_point_id;
    double major_radius{};
    double minor_radius{};
    double rotation{};
    bool construction{};
    bool reversed{};
    bool operator==(const SketchEllipse&) const = default;
};

struct SketchEllipticalArc {
    std::string id;
    std::string center_point_id;
    std::string major_point_id;
    std::string minor_point_id;
    std::string start_point_id;
    std::string end_point_id;
    double major_radius{};
    double minor_radius{};
    double rotation{};
    double start_parameter{};
    double end_parameter{};
    bool construction{};
    bool reversed{};
    bool operator==(const SketchEllipticalArc&) const = default;
};

struct SketchBSpline {
    std::string id;
    std::vector<std::string> control_point_ids;
    unsigned degree{3};
    bool closed{};
    bool construction{};
    bool operator==(const SketchBSpline&) const = default;
};

struct SketchImportBlock {
    std::string id;
    std::string name;
    std::string source_path;
    std::vector<std::string> geometry_ids;
    std::vector<std::string> point_ids;
    double translation_x{};
    double translation_y{};
    double rotation{};
    bool operator==(const SketchImportBlock&) const = default;
};

struct SketchConstraint {
    std::string id;
    ConstraintKind kind{ConstraintKind::Coincident};
    std::string first_point_id;
    std::string second_point_id;
    bool suppressed{};
    std::string geometry_id;
    std::string second_geometry_id;
    bool operator==(const SketchConstraint&) const = default;
};

struct SketchDimension {
    std::string id;
    DimensionKind kind{DimensionKind::Distance};
    std::string first_point_id;
    std::string second_point_id;
    double value{};
    bool driving{true};
    bool suppressed{};
    std::optional<double> lower_limit;
    std::optional<double> upper_limit;
    std::string geometry_id;
    bool operator==(const SketchDimension&) const = default;
};

struct SolveResult {
    SolveStatus status{SolveStatus::Invalid};
    std::size_t remaining_degrees_of_freedom{};
    double maximum_residual{};
};

struct RegularPolygonResult {
    std::string support_circle_id;
    std::vector<std::string> segment_ids;
    std::vector<std::string> vertex_ids;
};

struct MirroredGeometryResult {
    std::vector<std::string> geometry_ids;
    std::vector<std::string> point_ids;
};

class Sketch {
public:
    std::string id;
    std::string name{"Skica"};
    SketchPlane plane{SketchPlane::XY};
    double plane_offset{};
    std::vector<SketchPoint> points;
    std::vector<SketchSegment> segments;
    std::vector<SketchCircle> circles;
    std::vector<SketchArc> arcs;
    std::vector<SketchEllipse> ellipses;
    std::vector<SketchEllipticalArc> elliptical_arcs;
    std::vector<SketchBSpline> bsplines;
    std::vector<SketchImportBlock> import_blocks;
    std::vector<SketchConstraint> constraints;
    std::vector<SketchDimension> dimensions;

    [[nodiscard]] static Sketch create_default();
    [[nodiscard]] static SketchPoint create_point(double x, double y);
    [[nodiscard]] static SketchSegment create_segment(
        std::string first_point_id, std::string second_point_id,
        bool construction = false);
    [[nodiscard]] SketchPoint* find_point(const std::string& point_id);
    [[nodiscard]] const SketchPoint* find_point(const std::string& point_id) const;
    void validate() const;
    [[nodiscard]] SolveResult solve(std::size_t maximum_iterations = 100);
    [[nodiscard]] bool set_dimension_value(
        const std::string& dimension_id, double value);
    void set_point_fixed(const std::string& point_id, bool fixed);
    [[nodiscard]] bool move_point(
        const std::string& point_id, double x, double y);
    [[nodiscard]] std::string add_point(
        double x, double y, double snap_tolerance = 1.0e-6,
        bool construction = false);
    [[nodiscard]] std::string add_segment(
        double first_x, double first_y, double second_x, double second_y,
        double snap_tolerance = 1.0e-6, bool construction = false);
    [[nodiscard]] std::string add_segment_constraint(
        const std::string& segment_id, ConstraintKind kind);
    [[nodiscard]] std::string add_coincident_constraint(
        const std::string& first_point_id, const std::string& second_point_id);
    [[nodiscard]] std::string add_segment_pair_constraint(
        const std::string& first_segment_id, const std::string& second_segment_id,
        ConstraintKind kind);
    [[nodiscard]] std::string add_point_on_circle_constraint(
        const std::string& point_id, const std::string& circle_id);
    [[nodiscard]] std::string add_symmetric_constraint(
        const std::string& source_point_id,
        const std::string& mirrored_point_id,
        const std::string& axis_id);
    [[nodiscard]] std::string add_midpoint_constraint(
        const std::string& point_id, const std::string& segment_id);
    [[nodiscard]] std::string add_concentric_constraint(
        const std::string& reference_geometry_id,
        const std::string& driven_geometry_id);
    [[nodiscard]] std::string add_tangent_constraint(
        const std::string& reference_geometry_id,
        const std::string& driven_geometry_id);
    void remove_geometry(const std::string& geometry_id);
    void remove_point(const std::string& point_id);
    [[nodiscard]] std::vector<std::string> add_rectangle(
        double first_x, double first_y, double second_x, double second_y,
        double snap_tolerance = 1.0e-6);
    [[nodiscard]] RegularPolygonResult add_regular_polygon(
        double center_x, double center_y, double rim_x, double rim_y,
        unsigned sides, double snap_tolerance = 1.0e-6);
    [[nodiscard]] MirroredGeometryResult mirror_geometry(
        const std::vector<std::string>& entity_ids,
        const std::string& axis_id,
        double snap_tolerance = 1.0e-6);
    [[nodiscard]] std::string add_circle(
        double center_x, double center_y, double radius,
        bool construction = false, double snap_tolerance = 1.0e-6);
    [[nodiscard]] std::string add_arc(
        double center_x, double center_y, double start_x, double start_y,
        double end_x, double end_y, bool construction = false,
        double snap_tolerance = 1.0e-6);
    [[nodiscard]] std::string add_ellipse(
        double center_x, double center_y, double major_x, double major_y,
        double minor_x, double minor_y, bool construction = false,
        double snap_tolerance = 1.0e-6);
    [[nodiscard]] std::string add_elliptical_arc(
        double center_x, double center_y, double major_x, double major_y,
        double minor_x, double minor_y, double start_x, double start_y,
        double end_x, double end_y, bool reversed = false,
        bool construction = false, double snap_tolerance = 1.0e-6);
    [[nodiscard]] std::string add_bspline(
        const std::vector<std::array<double, 2>>& control_points,
        unsigned degree = 3, bool closed = false, bool construction = false,
        double snap_tolerance = 1.0e-6);
    [[nodiscard]] std::string add_import_block(
        std::string name, std::string source_path,
        std::vector<std::string> geometry_ids,
        std::vector<std::string> point_ids);
    void transform_import_block(
        const std::string& block_id, double translation_x,
        double translation_y, double rotation);
    [[nodiscard]] SketchDimension create_segment_dimension(
        const std::string& segment_id, DimensionKind kind = DimensionKind::Distance) const;
    void apply_dimension(SketchDimension dimension);
    [[nodiscard]] SketchDimension create_circle_radius_dimension(
        const std::string& circle_id) const;
    [[nodiscard]] SketchDimension create_circle_diameter_dimension(
        const std::string& circle_id) const;
    [[nodiscard]] SketchDimension create_arc_radius_dimension(
        const std::string& arc_id) const;
    [[nodiscard]] SketchDimension create_ellipse_radius_dimension(
        const std::string& ellipse_id, bool major) const;
    [[nodiscard]] SketchDimension create_ellipse_rotation_dimension(
        const std::string& ellipse_id) const;
    [[nodiscard]] zima::kernel::ViewerMesh viewer_mesh() const;
    [[nodiscard]] zima::kernel::Vec3 world_point(double x, double y) const;
    [[nodiscard]] std::optional<std::array<double, 2>> intersect_ray(
        const zima::kernel::Vec3& origin,
        const zima::kernel::Vec3& direction) const;
    [[nodiscard]] std::string serialized() const;
    [[nodiscard]] static Sketch from_serialized(const std::string& value);
    void save(const std::filesystem::path& path) const;
    [[nodiscard]] static Sketch load(const std::filesystem::path& path);
};

}  // namespace zima::sketcher
