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
    EqualRadius, PointOnCircle, PointOnLine, Symmetric, Midpoint, Concentric, Tangent
};
enum class DimensionKind {
    Distance, DistanceX, DistanceY, Radius, Diameter, Angle,
    EllipseMajorRadius, EllipseMinorRadius, EllipseRotation
};
enum class TextHorizontalAlignment { Left, Center, Right };
enum class TextVerticalAlignment { Bottom, Middle, Top };
enum class SketchTextColor { Green, White, Yellow };
enum class ExternalReferenceKind { Edge, Point, Axis, Face };
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

struct SketchText {
    std::string id;
    std::string value{"TEXT"};
    double anchor_x{};
    double anchor_y{};
    double height{10.0};
    TextHorizontalAlignment horizontal{TextHorizontalAlignment::Left};
    TextVerticalAlignment vertical{TextVerticalAlignment::Bottom};
    double angle_degrees{};
    bool flipped{};
    SketchTextColor color{SketchTextColor::Green};
    std::string font{"osifont"};
    std::vector<std::vector<std::array<double, 2>>> contours;
    bool operator==(const SketchText&) const = default;
};

struct SketchExternalReference {
    std::string id;
    ExternalReferenceKind kind{ExternalReferenceKind::Edge};
    std::string source_document_id;
    std::string source_owner_id;
    std::string source_semantic_key;
    std::string source_instance_path;
    std::string context_assembly_document_id;
    std::string context_instance_path;
    std::vector<std::array<double, 2>> cached_points;
    std::vector<std::vector<std::array<double, 2>>> cached_paths;
    bool broken{};
    bool operator==(const SketchExternalReference&) const = default;
};

struct SketchConstraint {
    std::string id;
    ConstraintKind kind{ConstraintKind::Coincident};
    std::string first_point_id;
    std::string second_point_id;
    bool suppressed{};
    std::string geometry_id;
    std::string second_geometry_id;
    bool tangent_internal{};
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
    bool suppressed{};
    SketchPlane plane{SketchPlane::XY};
    double plane_offset{};
    // Persisted: when non-empty, this Sketch's frame is not one of the
    // three fixed `plane`/`plane_offset` planes above -- it instead follows
    // an arbitrary Plane construction container (identified by that
    // container's entity id), inheriting that container's own resolved
    // origin/orientation plus its own "work plane offset", exactly like any
    // other reference to a Plane container (see
    // PartDocument::resolve_constructions()/resolve_sketch_placements()).
    // `plane`/`plane_offset` above are ignored while this is set, but are
    // kept as-is so the Sketch can fall back to them if the reference is
    // ever cleared.
    std::string plane_reference_owner_id{};
    // The resolved frame actually used by world_point()/local_point()/
    // intersect_ray() and by every Feature built from this Sketch (see
    // extrusion_request()/revolution_request() in part_document.cpp).
    // Recomputed from `plane`/`plane_offset` by refresh_default_frame()
    // whenever `plane_reference_owner_id` is empty; overwritten by
    // PartDocument::resolve_constructions() from the referenced Plane
    // container's own resolved placement otherwise. Never edited directly
    // by callers -- always derived, and persisted only so a just-loaded
    // document already has a valid frame before the first resolve pass.
    zima::kernel::Vec3 resolved_origin{};
    zima::kernel::Vec3 resolved_x_axis{1.0, 0.0, 0.0};
    zima::kernel::Vec3 resolved_y_axis{0.0, 1.0, 0.0};
    zima::kernel::Vec3 resolved_normal{0.0, 0.0, 1.0};
    std::vector<SketchPoint> points;
    std::vector<SketchSegment> segments;
    std::vector<SketchCircle> circles;
    std::vector<SketchArc> arcs;
    std::vector<SketchEllipse> ellipses;
    std::vector<SketchEllipticalArc> elliptical_arcs;
    std::vector<SketchBSpline> bsplines;
    std::vector<SketchImportBlock> import_blocks;
    std::vector<SketchText> texts;
    std::vector<SketchExternalReference> external_references;
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
    [[nodiscard]] std::string add_equal_radius_constraint(
        const std::string& reference_geometry_id,
        const std::string& driven_geometry_id);
    [[nodiscard]] std::string add_point_on_circle_constraint(
        const std::string& point_id, const std::string& circle_id);
    [[nodiscard]] std::string add_point_on_line_constraint(
        const std::string& point_id, const std::string& line_id);
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
    void remove_constraint(const std::string& constraint_id);
    void remove_dimension(const std::string& dimension_id);
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
    [[nodiscard]] static SketchText create_text();
    void add_text(SketchText text);
    void update_text(SketchText text);
    [[nodiscard]] static SketchExternalReference create_external_reference(
        ExternalReferenceKind kind);
    void add_external_reference(SketchExternalReference reference);
    [[nodiscard]] bool refresh_external_references(
        const std::string& source_document_id,
        const zima::kernel::ViewerReferenceGeometry& source_geometry);
    [[nodiscard]] std::optional<std::vector<std::array<double, 2>>>
        project_external_axis(const zima::kernel::ViewerAxis& axis) const;
    [[nodiscard]] std::optional<std::vector<std::vector<std::array<double, 2>>>>
        project_external_face(
            const zima::kernel::ViewerReferenceGeometry& source_geometry,
            const zima::kernel::FaceReference& face) const;
    void transform_import_block(
        const std::string& block_id, double translation_x,
        double translation_y, double rotation);
    [[nodiscard]] SketchDimension create_segment_dimension(
        const std::string& segment_id, DimensionKind kind = DimensionKind::Distance) const;
    [[nodiscard]] SketchDimension create_point_dimension(
        const std::string& first_point_id, const std::string& second_point_id,
        DimensionKind kind = DimensionKind::Distance) const;
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
    // Recomputes resolved_origin/resolved_x_axis/resolved_y_axis/
    // resolved_normal from `plane`/`plane_offset`. Call after changing
    // either field directly (e.g. from the properties dialog) whenever
    // `plane_reference_owner_id` is empty -- when it is non-empty, the
    // owning PartDocument overwrites the resolved frame instead and this
    // call would be immediately discarded on the next resolve pass.
    void refresh_default_frame();
    // Live-computed the same way as world_point()/local_point(): from
    // `plane`/`plane_offset` while plane_reference_owner_id is empty, from
    // the resolved_* cache otherwise. Feature builders (extrusion/
    // revolution direction, see part_document.cpp) must use these instead
    // of switching on `plane` directly, so they automatically follow an
    // arbitrary Plane container reference.
    [[nodiscard]] zima::kernel::Vec3 normal() const;
    [[nodiscard]] zima::kernel::Vec3 x_axis() const;
    [[nodiscard]] zima::kernel::Vec3 y_axis() const;
    [[nodiscard]] zima::kernel::Vec3 world_point(double x, double y) const;
    [[nodiscard]] std::array<double, 2> local_point(
        const zima::kernel::Vec3& point) const;
    [[nodiscard]] std::optional<std::array<double, 2>> intersect_ray(
        const zima::kernel::Vec3& origin,
        const zima::kernel::Vec3& direction) const;
    [[nodiscard]] std::string serialized() const;
    [[nodiscard]] static Sketch from_serialized(const std::string& value);
    void save(const std::filesystem::path& path) const;
    [[nodiscard]] static Sketch load(const std::filesystem::path& path);
};

}  // namespace zima::sketcher
