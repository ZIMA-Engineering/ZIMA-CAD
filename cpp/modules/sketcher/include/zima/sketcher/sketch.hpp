#pragma once

#include <zima/kernel/geometry_kernel.hpp>

#include <filesystem>
#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zima::sketcher {

enum class SketchPlane { XY, XZ, YZ };
// Converts a signed displacement along the Sketch frame normal into the
// persisted plane-offset convention. XZ uses +Y for increasing offset while
// its right-handed frame normal points toward -Y.
[[nodiscard]] double plane_offset_delta_for_normal_displacement(
    SketchPlane plane, double normal_displacement) noexcept;
enum class ConstraintKind {
    Horizontal, Vertical, Coincident, PointReference,
    Parallel, Perpendicular, EqualLength,
    EqualRadius, PointOnCircle, PointOnLine, MidpointOnLine, Symmetric,
    Midpoint, Concentric, Tangent
};
// Single presentation symbol shared by the Sketch view and every constraint
// list. An empty symbol means that the relation is represented by merged
// topology rather than by a separate in-view marker.
[[nodiscard]] std::string constraint_marker_label(ConstraintKind kind);
enum class DimensionKind {
    Distance, DistanceX, DistanceY, DistancePointLine, DistanceSymmetric,
    DistanceLine,
    Radius, Diameter, Angle, AngleThreePoint,
    AngleBetween,
    // Symmetric line-pair dimensions treat a sketch axis / construction line
    // as a mirror. With one real reference line the value is doubled, as if
    // a mirrored counterpart line existed on the other side of the axis;
    // with two real reference lines the value is their individual
    // angle/distance from the axis summed together. Both kinds drive their
    // owned line(s) independently toward half of the persisted value.
    AngleSymmetric, DistanceLineSymmetric,
    EllipseMajorRadius, EllipseMinorRadius, EllipseRotation
};
[[nodiscard]] DimensionKind classify_linear_dimension(
    const std::array<double, 2>& first,
    const std::array<double, 2>& second,
    const std::array<double, 2>& cursor);
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
    // A true centerline (Python geometry type "construction") is unbounded.
    // `construction && !centerline` is finite auxiliary geometry.
    bool centerline{};
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
    // False: points are B-spline control vertices. True: the sampled curve
    // interpolates every persisted point in order.
    bool interpolating{};
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
    bool infinite{};
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
    // Form and references are independent. A projected X/Y point-pair
    // dimension owns first_point_id + second_point_id and leaves geometry_id
    // empty. A direct coordinate dimension owns first_point_id + the selected
    // sketch_axis:x/y in geometry_id and leaves second_point_id empty.
    std::string geometry_id;
    // Second persisted line owner for line-to-line distance and angle.
    // Built-in sketch axes use the stable IDs sketch_axis:x / sketch_axis:y.
    std::string second_geometry_id;
    // Optional third persisted line owner. Only AngleSymmetric and
    // DistanceLineSymmetric use it, for the two-real-line form of a
    // symmetric line-pair dimension (geometry_id is the mirror axis,
    // second_geometry_id the first real line, third_geometry_id the
    // optional second real line).
    std::string third_geometry_id;
    std::optional<std::array<double, 2>> placement;
    // -1 while interactively choosing a sector, 0 for the directed/base
    // angle sectors, 1 for their supplementary sectors.
    int angle_sector{-1};
    // Solver storage may put a fixed Sketch axis first even when the user
    // selected it second. Presentation preserves the user's input order.
    bool angle_presentation_reversed{};
    // Presentation metadata belongs to the persisted ZIMA dimension, not to
    // a transient viewer label. Tolerance mode is: empty, symmetric,
    // single_deviation, or deviations.
    std::string prefix;
    std::string suffix;
    std::string tolerance_mode;
    std::string symmetric_tolerance;
    std::string single_tolerance;
    std::string upper_tolerance;
    std::string lower_tolerance;
    // Locked is an editing policy, independent of solver ownership. Both
    // locked and unlocked driving dimensions constrain geometry; a locked
    // value cannot be changed until explicitly unlocked.
    bool locked{};
    // Unsigned point-to-line and line-to-line distances persist their
    // magnitude in value and their selected normal branch independently.
    // This keeps the displayed value positive while allowing a negative user
    // entry to mean "move to the opposite side".
    int solution_side{1};
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

struct CornerFilletResult {
    std::string arc_id;
    std::string first_tangent_point_id;
    std::string second_tangent_point_id;
};

// A non-destructive corner treatment.  The source segments keep sharing the
// original vertex; viewer/body evaluation derives their tangent trims and the
// child arc from this persisted record.
struct SketchCornerRadius {
    std::string id;
    std::string vertex_id;
    std::string first_segment_id;
    std::string second_segment_id;
    double radius{};
    bool suppressed{};
    bool dimension_visible{};
    std::optional<std::array<double, 2>> dimension_placement;
    std::string equal_radius_group;
    bool operator==(const SketchCornerRadius&) const = default;
};

class Sketch {
public:
    std::string id;
    // The single history container that owns this Sketch. The same owner is
    // retained when the container is transformed in-place into Extrusion or
    // Revolution.
    std::string owner_container_id;
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
    std::vector<SketchCornerRadius> corner_radii;

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
    [[nodiscard]] bool set_dimension_placement(
        const std::string& dimension_id, double x, double y);
    void set_point_fixed(const std::string& point_id, bool fixed);
    void set_geometry_construction(
        const std::string& geometry_id, bool construction);
    void set_segment_centerline(const std::string& segment_id, bool centerline);
    [[nodiscard]] bool move_point(
        const std::string& point_id, double x, double y);
    [[nodiscard]] bool translate_selection(
        const std::vector<std::string>& point_ids,
        const std::vector<std::string>& geometry_ids,
        double translation_x, double translation_y);
    [[nodiscard]] std::string add_point(
        double x, double y, double snap_tolerance = 1.0e-6,
        bool construction = false);
    [[nodiscard]] std::string add_segment(
        double first_x, double first_y, double second_x, double second_y,
        double snap_tolerance = 1.0e-6, bool construction = false,
        bool reuse_first_point = true, bool reuse_second_point = true);
    [[nodiscard]] std::string add_segment_constraint(
        const std::string& segment_id, ConstraintKind kind);
    [[nodiscard]] std::string add_point_pair_constraint(
        const std::string& reference_point_id,
        const std::string& driven_point_id, ConstraintKind kind);
    // Point-point coincidence is a topology operation: every reference to
    // absorbed_point_id is rewired to reference_point_id and the absorbed
    // point is removed. No persistent Coincident equation or View marker is
    // created.
    [[nodiscard]] std::string merge_points(
        const std::string& reference_point_id,
        const std::string& absorbed_point_id);
    // A read-only external point (Sketch origin, projected point or generated
    // curve keypoint) cannot become a native graph vertex. Keep that special
    // anchor as an explicit reference equation, without presenting it as the
    // point-on-geometry C relation.
    [[nodiscard]] std::string add_point_reference_constraint(
        const std::string& native_point_id,
        const std::string& reference_point_id);
    [[nodiscard]] std::string add_segment_pair_constraint(
        const std::string& first_segment_id, const std::string& second_segment_id,
        ConstraintKind kind);
    [[nodiscard]] std::string add_equal_radius_constraint(
        const std::string& reference_geometry_id,
        const std::string& driven_geometry_id);
    [[nodiscard]] std::string add_point_on_circle_constraint(
        const std::string& point_id, const std::string& circle_id);
    [[nodiscard]] std::optional<std::array<double, 2>> project_point_to_curve(
        const std::string& geometry_id, double x, double y) const;
    [[nodiscard]] std::optional<std::array<double, 2>> curve_tangent_at_point(
        const std::string& geometry_id, double x, double y) const;
    [[nodiscard]] std::string add_common_tangent_segment(
        const std::string& first_curve_id,
        const std::array<double, 2>& first_hint,
        const std::string& second_curve_id,
        const std::array<double, 2>& second_hint);
    [[nodiscard]] std::vector<std::array<double, 2>> curve_line_intersections(
        const std::string& geometry_id,
        const std::array<double, 2>& line_origin,
        const std::array<double, 2>& line_direction,
        bool line_bounded) const;
    [[nodiscard]] std::string add_point_on_line_constraint(
        const std::string& point_id, const std::string& line_id);
    [[nodiscard]] std::string add_midpoint_on_line_constraint(
        const std::string& segment_id, const std::string& line_id);
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
        const std::string& driven_geometry_id,
        const std::string& contact_point_id = {});
    void remove_constraint(const std::string& constraint_id);
    void remove_dimension(const std::string& dimension_id);
    void remove_geometry(const std::string& geometry_id);
    void remove_point(const std::string& point_id);
    [[nodiscard]] std::vector<std::string> add_rectangle(
        double first_x, double first_y, double second_x, double second_y,
        double snap_tolerance = 1.0e-6);
    [[nodiscard]] std::vector<std::string> add_oriented_rectangle(
        double first_x, double first_y, double guide_x, double guide_y,
        const std::string& symmetry_axis_id,
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
        double snap_tolerance = 1.0e-6, bool clockwise = false);
    [[nodiscard]] std::string add_tangent_arc(
        const std::string& start_point_id,
        double end_x, double end_y,
        const std::string& tangent_geometry_id,
        bool reverse = false,
        bool construction = false,
        double snap_tolerance = 1.0e-6);
    [[nodiscard]] CornerFilletResult add_corner_fillet(
        const std::string& first_segment_id,
        const std::string& second_segment_id,
        double radius,
        double snap_tolerance = 1.0e-6);
    [[nodiscard]] Sketch evaluated_profile_sketch() const;
    [[nodiscard]] std::optional<std::pair<std::array<double, 2>,
        std::array<double, 2>>> visible_segment_endpoints(
            const std::string& segment_id) const;
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
        double snap_tolerance = 1.0e-6, bool interpolating = false);
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
    [[nodiscard]] std::string add_external_profile_geometry(
        const std::string& reference_id);
    [[nodiscard]] bool refresh_external_references(
        const std::string& source_document_id,
        const zima::kernel::ViewerReferenceGeometry& source_geometry);
    [[nodiscard]] std::optional<std::vector<std::array<double, 2>>>
        project_external_axis(const zima::kernel::ViewerAxis& axis) const;
    [[nodiscard]] std::optional<std::vector<std::array<double, 2>>>
        project_external_face_plane(
            const zima::kernel::ViewerReferenceGeometry& source_geometry,
            const zima::kernel::FaceReference& face) const;
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
    [[nodiscard]] SketchDimension create_three_point_angle_dimension(
        const std::string& first_point_id, const std::string& vertex_point_id,
        const std::string& second_point_id) const;
    [[nodiscard]] SketchDimension create_point_line_angle_dimension(
        const std::string& first_point_id, const std::string& second_point_id,
        const std::string& reference_line_id) const;
    [[nodiscard]] SketchDimension create_four_point_angle_dimension(
        const std::string& first_point_id, const std::string& second_point_id,
        const std::string& third_point_id,
        const std::string& fourth_point_id) const;
    [[nodiscard]] SketchDimension create_axis_dimension(
        const std::string& point_id,
        const std::string& sketch_axis_id) const;
    [[nodiscard]] SketchDimension create_point_line_dimension(
        const std::string& point_id,
        const std::string& line_id) const;
    [[nodiscard]] SketchDimension create_symmetric_dimension(
        const std::string& first_point_id,
        const std::string& second_point_id,
        const std::string& axis_id) const;
    [[nodiscard]] SketchDimension create_line_pair_dimension(
        const std::string& reference_line_id,
        const std::string& driven_line_id,
        DimensionKind kind) const;
    [[nodiscard]] SketchDimension create_line_symmetric_dimension(
        const std::string& axis_id,
        const std::string& first_line_id,
        const std::string& second_line_id,
        DimensionKind kind) const;
    void apply_dimension(SketchDimension dimension);
    [[nodiscard]] SketchDimension create_circle_radius_dimension(
        const std::string& circle_id) const;
    [[nodiscard]] SketchDimension create_circle_diameter_dimension(
        const std::string& circle_id) const;
    [[nodiscard]] SketchDimension create_arc_radius_dimension(
        const std::string& arc_id) const;
    [[nodiscard]] SketchDimension create_arc_diameter_dimension(
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
    // Returns a forward ray normal to the active resolved Sketch plane and
    // passing through the requested local point. This is the canonical ray
    // for visual snapping on arbitrarily referenced or placed Sketch planes.
    [[nodiscard]] std::pair<zima::kernel::Vec3, zima::kernel::Vec3>
        normal_ray(double x, double y) const;
    [[nodiscard]] std::array<double, 2> local_point(
        const zima::kernel::Vec3& point) const;
    [[nodiscard]] std::optional<std::array<double, 2>> intersect_ray(
        const zima::kernel::Vec3& origin,
        const zima::kernel::Vec3& direction) const;
    [[nodiscard]] std::string serialized() const;
    [[nodiscard]] static Sketch from_serialized(const std::string& value);
    void save(const std::filesystem::path& path) const;
    [[nodiscard]] static Sketch load(const std::filesystem::path& path);

private:
    [[nodiscard]] SolveResult solve_impl(
        std::size_t maximum_iterations, bool calculate_degrees_of_freedom,
        const std::vector<std::string>& preferred_point_ids = {});
    // Non-persisted exact-state cache. The key is the complete current ZIMA
    // Sketch serialization, so direct public-vector edits invalidate it too.
    std::string solved_rank_cache_key_;
    std::optional<SolveResult> solved_rank_cache_result_;
    mutable bool point_lookup_active_{};
    mutable std::unordered_map<std::string, std::size_t> point_lookup_indices_;
};

}  // namespace zima::sketcher
