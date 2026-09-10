#pragma once

#include <cstdint>
#include <cmath>
#include <bit>
#include <algorithm>
#include <array>
#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <memory>
#include <map>
#include <type_traits>
#include <utility>
#include <stdexcept>

namespace zima::kernel {

struct Vec3 {
    double x{};
    double y{};
    double z{};
    bool operator==(const Vec3&) const = default;
};

// Exact analytic data captured during explicit body calculation. These are
// measurements of the persisted source face, never topology identity.
struct SurfaceGeometry {
    enum class Kind { Plane, Cylinder, Cone };
    Kind kind{Kind::Plane};
    Vec3 origin;
    Vec3 axis{0,0,1};
    Vec3 radial{1,0,0};
    double radius{};
    double semi_angle{};
    double axial_min{};
    double axial_max{};
    bool reversed{};
    bool operator==(const SurfaceGeometry&) const = default;
};

struct FaceReference {
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;
    std::shared_ptr<const SurfaceGeometry> surface;

    [[nodiscard]] bool valid() const {
        return !owner_id.empty() && !semantic_key.empty();
    }
    // The persisted semantic role is also the surface classification used by
    // drawing sections. Only the full thread cylinder is threaded; its runout
    // and the underlying bore wall remain ordinary surfaces. No OCCT lookup.
    [[nodiscard]] bool is_thread_surface() const {
        return valid() && (semantic_key == "thread:surface:nominal" ||
            semantic_key == "thread:surface:root");
    }
    bool operator==(const FaceReference& other) const {
        return owner_id==other.owner_id && semantic_key==other.semantic_key &&
            instance_path==other.instance_path;
    }
};

struct EdgeReference {
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;
    [[nodiscard]] bool valid() const {
        return !owner_id.empty() && !semantic_key.empty();
    }
    bool operator==(const EdgeReference&) const = default;
};

struct VertexReference {
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;
    [[nodiscard]] bool valid() const {
        return !owner_id.empty() && !semantic_key.empty();
    }
    bool operator==(const VertexReference&) const = default;
};

struct AxisReference {
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;
    [[nodiscard]] bool valid() const {
        return !owner_id.empty() && !semantic_key.empty();
    }
    bool operator==(const AxisReference&) const = default;
};

struct ViewerEdge {
    std::vector<Vec3> points;
    EdgeReference reference;
    bool construction{};
    // Screen-space helpers (Sketcher and construction planes) are painted as
    // UI overlays. Calculated body topology stays in the depth-tested GL pass.
    bool overlay{};
    // Sketch centerlines are defined by two points but presented and picked
    // as an unbounded line in the active Sketch plane.
    bool infinite{};
    bool dash_dot{};
    // True only for an edge closing the UV parameterization of a periodic
    // surface. It remains persisted topology but is neither drawn nor picked.
    bool parameter_seam{};
    // Viewer-only ownership of an already calculated body wire. It never
    // forms a persistent topology reference; it only lets a Container hover
    // recolour the existing GL edge instead of drawing a second overlay.
    std::string display_owner_id;
    // Persisted viewer-only relation from this calculated Body edge to every
    // Fillet/Chamfer surface whose visible boundary it forms. The viewer uses
    // the exact display-edge occurrence carrying this relation: stable model
    // references are intentionally not used as draw identities because one
    // reference may have several visible descendants after a Boolean.
    std::vector<std::string> edge_treatment_owner_ids;
    // Two already-calculated inward directions, one for each face adjacent
    // to this body edge. Each direction row is sampled at the same positions
    // as points. Fillet/Chamfer previews consume this persisted viewer packet
    // directly; opening Properties or changing the size never asks OCCT to
    // recover face adjacency or material side.
    std::vector<std::vector<Vec3>> edge_treatment_side_directions;
    // Stable ZIMA owners of the two direction rows above.  The rows are
    // sorted by these references during explicit body calculation, so FLIP
    // selects a named side instead of depending on OCCT ancestor order.
    std::vector<FaceReference> edge_treatment_side_references;
    // Stable endpoint identities aligned with points.front()/points.back().
    // Variable-radius preview uses them to show the same R1/R2 direction as
    // the explicit OCCT calculation even when curve parameterization flips.
    std::vector<VertexReference> edge_treatment_endpoint_references;
    std::string color; // Optional presentation colour for template Sketch wires.
    bool filled_text{};
};

struct ViewerPoint {
    Vec3 position;
    VertexReference reference;
    std::string label;
    // False for an Axis/Plane container's own defining-point marker: it is
    // display/pick geometry only when hovered, confirmed, or referenced by
    // another container's placement, and stays invisible otherwise. A Point
    // container's own marker (and any other caller that does not set this)
    // defaults to true and always renders, since it IS the visible entity.
    bool always_visible{true};
    // Sketch geometry role. Ordinary/profile points render white; auxiliary
    // construction points render green, matching construction edges.
    bool construction{};
    // Sketch display association, derived from persisted SketchText.anchor_point_id.
    std::string sketch_text_key;
};

struct ViewerAxis {
    Vec3 point;
    Vec3 direction{0.0, 0.0, 1.0};
    double display_length{100.0};
    AxisReference reference;
    std::string label;
};

enum class ViewerDimensionKind { Linear, Angular, Radius, Diameter };

struct ViewerDimension {
    Vec3 witness_first;
    Vec3 witness_second;
    Vec3 line_first;
    Vec3 line_second;
    double value{};
    EdgeReference reference;
    std::string label_prefix;
    std::string unit_suffix{" mm"};
    std::vector<std::string> participant_semantic_keys;
    ViewerDimensionKind kind{ViewerDimensionKind::Linear};
    // Angular dimensions use witness_first as their vertex, line_first and
    // line_second as points on the two rays, and this stable modeling-plane
    // normal to define sweep orientation. Radius/Diameter use witness_first
    // as center and witness_second as the rim point.
    Vec3 plane_normal{0.0, 0.0, 1.0};
    double sweep_degrees{};
    // Presentation state comes from the persisted ZIMA dimension. The viewer
    // only colours it; it never infers editability from geometry.
    bool driving{true};
    bool locked{};
    // Explicit annotation anchor. Angular Sketch dimensions use the exact
    // user-confirmed placement point instead of forcing text to the arc
    // bisector.
    std::optional<Vec3> label_position;
    // Optional presentation text. The numeric value remains the driving
    // engineering value used by editing and solving; only its View label is
    // replaced (for example "M10" on a cosmetic thread diameter).
    std::string display_text_override;
    // Actual editable value represented by a generated parameter dimension.
    // A reference-driven RX/RY/RZ dimension edits its local correction.
    std::string value_lock_key;
    bool arrows_reversed{};
    bool radius_center_line_hidden{};
    bool operator==(const ViewerDimension&)const=default;
};

struct ViewerConstraintMarker {
    Vec3 position;
    std::string label;
    EdgeReference reference;
    // Stable Sketch semantic keys of every entity participating in the
    // relation. The viewer uses these persisted identities for exact
    // dependency highlighting; it never reconstructs participants from OCCT.
    std::vector<std::string> participant_semantic_keys;
};

// Hidden persisted geometry owned by original ZIMA entities. It participates
// in picking and exact highlight only; the calculated OCCT result remains the
// sole shaded/display body.
struct ViewerReferenceGeometry {
    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> triangles;
    std::vector<FaceReference> triangle_references;
    std::vector<ViewerEdge> edges;
    std::vector<ViewerPoint> points;
    std::vector<ViewerAxis> axes;
};

struct ViewerImage {
    std::array<Vec3,4> corners; // Pixel top-left, top-right, bottom-right, bottom-left.
    EdgeReference reference;
    std::string data_base64;
    std::string format{"png"};
};

// Tight geometric envelope. Annotations, origins and construction datums never
// contribute; presentation offsets cannot enlarge the model's spatial bounds.
struct ModelEnvelope {
    Vec3 minimum{}, maximum{};
    bool valid{};
    Vec3 origin{};
    std::array<Vec3,3> axes{{{1,0,0},{0,1,0},{0,0,1}}};
    Vec3 local(Vec3 p)const {p={p.x-origin.x,p.y-origin.y,p.z-origin.z};const auto dot=[&](Vec3 a){return p.x*a.x+p.y*a.y+p.z*a.z;};return {dot(axes[0]),dot(axes[1]),dot(axes[2])};}
    Vec3 world(Vec3 p)const {return {origin.x+axes[0].x*p.x+axes[1].x*p.y+axes[2].x*p.z,origin.y+axes[0].y*p.x+axes[1].y*p.y+axes[2].y*p.z,origin.z+axes[0].z*p.x+axes[1].z*p.y+axes[2].z*p.z};}
    bool operator==(const ModelEnvelope&) const = default;
    void include(Vec3 p) {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return;
        p=local(p);
        if(!valid){minimum=maximum=p;valid=true;return;}
        minimum={std::min(minimum.x,p.x),std::min(minimum.y,p.y),std::min(minimum.z,p.z)};
        maximum={std::max(maximum.x,p.x),std::max(maximum.y,p.y),std::max(maximum.z,p.z)};
    }
    std::array<Vec3,8> corners()const {
        std::array<Vec3,8> out;
        for(unsigned i=0;i<8;++i)out[i]=world({i&1?maximum.x:minimum.x,i&2?maximum.y:minimum.y,i&4?maximum.z:minimum.z});
        return out;
    }
};
using ObjectEnvelopeKey=std::pair<std::string,std::string>; // owner, exact occurrence

struct ViewerMesh {
    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> triangles;
    // One entry per triangle. Invalid entries are displayed but never offered
    // as persistent modeling references.
    std::vector<FaceReference> triangle_references;
    std::vector<ViewerEdge> edges;
    std::vector<ViewerPoint> points;
    std::vector<ViewerAxis> axes;
    std::vector<ViewerDimension> dimensions;
    std::vector<ViewerConstraintMarker> constraint_markers;
    ViewerReferenceGeometry original_references;
    std::vector<ViewerImage> images;
    // Presentation-independent, oriented geometric envelopes from resolved source data.
    std::map<ObjectEnvelopeKey,ModelEnvelope> annotation_frames;
};

struct BoxRequest {
    BoxRequest() = default;
    BoxRequest(double box_length, double box_width, double box_height)
        : length(box_length), width(box_width), height(box_height) {}

    double length{100.0};
    double width{80.0};
    double height{50.0};
    Vec3 translation;
    Vec3 rotation_degrees;
};

struct CylinderRequest {
    double radius{40.0};
    double height{50.0};
    Vec3 translation;
    Vec3 rotation_degrees;
};

struct FeatureGroupRequest;

// The technological thread itself is non-volumetric. An opening sequences
// ordinary profile cuts before and after that sheet in one history boundary;
// cuts_after trims the sheet as well as the solid. A disabled sheet makes a
// plain opening with the same bore/tip/chamfer contract.
struct ThreadSurfaceRequest {
    enum class Side { Automatic, Internal, External };

    bool enabled{true};
    // Standalone shaft threading references original faces; the solid stays unchanged.
    std::optional<FaceReference> shaft_face;
    FaceReference shaft_start;
    std::optional<FaceReference> shaft_chamfer;
    std::optional<FaceReference> shaft_end;
    bool shaft_through_all{};
    bool shaft_runout{true};
    std::shared_ptr<const FeatureGroupRequest> cuts_before;
    std::shared_ptr<const FeatureGroupRequest> cuts_after;

    double nominal_radius{5.0};
    double root_radius{4.1881};
    double start_offset{};
    double length{15.0};
    double runout_start{};
    double runout_end{3.0};
    std::optional<Vec3> end_plane_origin;
    Vec3 end_plane_normal{0,0,1};
    bool through_all_forward{};
    bool through_all_reverse{};
    Side side{Side::Automatic};
    Vec3 origin;
    Vec3 axis_direction{0.0, 0.0, 1.0};
    Vec3 radial_direction{1.0, 0.0, 0.0};
};

// Subtractive solids of revolution derived from persisted circular bottom
// faces. Every face supplies its own exact centre, radius and material-side
// normal only when the user explicitly calculates the history.
struct DrillPointRequest {
    std::vector<FaceReference> bottom_faces;
    double included_angle_degrees{118.0};
};

struct SphereRequest {
    double radius{40.0};
    Vec3 translation;
    Vec3 rotation_degrees;
};

struct ConeRequest {
    double bottom_radius{20.0};
    double top_radius{};
    double height{50.0};
    Vec3 translation;
    Vec3 rotation_degrees;
};

struct PyramidRequest {
    double length{40.0};
    double width{40.0};
    double height{50.0};
    Vec3 translation;
    Vec3 rotation_degrees;
};

struct WedgeRequest {
    double length{60.0};
    double width{40.0};
    double height{40.0};
    double top_offset{30.0};
    Vec3 translation;
    Vec3 rotation_degrees;
};

struct ExtrusionRequest {
    enum class Extent { Blind, UpToPlane, UpToSurface, ThroughAll };
    struct PolygonProfile {
        std::vector<Vec3> vertices;
    };
    struct CircleProfile {
        Vec3 center;
        double radius{};
    };
    struct EllipseProfile {
        Vec3 center;
        Vec3 major_axis_direction{1.0, 0.0, 0.0};
        double major_radius{};
        double minor_radius{};
    };
    struct LineCurve {
        Vec3 start;
        Vec3 end;
    };
    struct ArcCurve {
        Vec3 start;
        Vec3 middle;
        Vec3 end;
    };
    struct EllipticalArcCurve {
        Vec3 start;
        Vec3 end;
        Vec3 center;
        Vec3 major_axis_direction{1.0, 0.0, 0.0};
        double major_radius{};
        double minor_radius{};
        double start_parameter{};
        double end_parameter{};
        bool reversed{};
    };
    struct BSplineCurve {
        Vec3 start;
        Vec3 end;
        std::vector<Vec3> control_points;
        unsigned degree{3};
        bool interpolating{};
        bool periodic{};
    };
    struct CurvedProfile {
        std::vector<std::variant<
            LineCurve, ArcCurve, EllipticalArcCurve, BSplineCurve>> curves;
    };
    using ProfileLoop = std::variant<
        PolygonProfile, CircleProfile, EllipseProfile, CurvedProfile>;
    struct ProfileRegion {
        std::string region_id;
        std::string outer_boundary_id;
        std::vector<std::string> inner_boundary_ids;
        std::vector<std::string> outer_edge_source_ids;
        std::vector<std::vector<std::string>> inner_edge_source_ids;
        std::vector<std::string> outer_vertex_source_ids;
        std::vector<std::vector<std::string>> inner_vertex_source_ids;
        ProfileLoop outer_profile{PolygonProfile{}};
        std::vector<ProfileLoop> inner_profiles;
    };
    std::string profile_region_id;
    std::string outer_boundary_id;
    std::vector<std::string> inner_boundary_ids;
    std::vector<std::string> outer_edge_source_ids;
    std::vector<std::vector<std::string>> inner_edge_source_ids;
    std::vector<std::string> outer_vertex_source_ids;
    std::vector<std::vector<std::string>> inner_vertex_source_ids;
    ProfileLoop outer_profile{PolygonProfile{}};
    std::vector<ProfileLoop> inner_profiles;
    std::vector<ProfileRegion> additional_profile_regions;
    bool first_cap_is_start{true};
    Vec3 direction{0.0, 0.0, 10.0};
    double start_offset{};
    Extent extent{Extent::Blind};
    // Through-all is directional.  These flags distinguish a forward-only
    // extrusion from a genuinely two-sided one; the Sketch plane remains a
    // hard boundary on every side that is not marked Through-all.
    bool through_all_forward{true};
    bool through_all_reverse{};
    FaceReference target_face;
    bool target_is_datum{};
    Vec3 target_plane_origin;
    Vec3 target_plane_normal{0.0, 0.0, 1.0};
    std::vector<Vec3> target_surface_triangles;
};

struct RevolutionRequest {
    ExtrusionRequest::ProfileLoop outer_profile{
        ExtrusionRequest::PolygonProfile{}};
    std::string profile_region_id;
    std::string outer_boundary_id;
    std::vector<std::string> inner_boundary_ids;
    std::vector<std::string> outer_edge_source_ids;
    std::vector<std::vector<std::string>> inner_edge_source_ids;
    std::vector<std::string> outer_vertex_source_ids;
    std::vector<std::vector<std::string>> inner_vertex_source_ids;
    std::vector<ExtrusionRequest::ProfileLoop> inner_profiles;
    std::vector<ExtrusionRequest::ProfileRegion> additional_profile_regions;
    Vec3 profile_normal{0.0, 0.0, 1.0};
    Vec3 axis_point;
    Vec3 axis_direction{1.0, 0.0, 0.0};
    // Stable semantic cap ownership. Reversing the rotation axis changes
    // which OCCT boundary is geometrically first, but must not exchange the
    // persisted ZIMA start/end identities used by downstream references.
    bool first_cap_is_start{true};
    double start_angle_degrees{};
    double angle_degrees{360.0};
};

// A semantic feature may organize several ordinary modeling primitives while
// still committing one history boundary.  The children remain real ZIMA
// operations (currently Extrusion/Revolution); this is grouping, not a new
// kernel shortcut for any particular feature such as Hole.
struct FeatureGroupRequest {
    using Child = std::variant<ExtrusionRequest, RevolutionRequest>;
    std::vector<Child> children;
};

struct StepRequest {
    std::string source_path;
    std::string component_path;
    // Immutable B-Rep captured by the explicit STEP import.  Once present,
    // ordinary history evaluation must never reopen or translate the source
    // STEP file.  Shared ownership keeps Part/document preview copies cheap.
    std::shared_ptr<const std::string> frozen_brep;
    // Optional runtime boundary identity used by explicit import to hand the
    // already translated OCCT shape directly to later history evaluation.
    // It is deliberately excluded from persisted parameter identity.
    std::string live_cache_fingerprint;
    // Runtime owner supplied by the importing/history container. It does not
    // participate in parameter identity; it is the persisted ZIMA parent of
    // source STEP topology offered by the viewer.
    std::string reference_owner_id;
    // Source-defined identities plus deterministic geometry locators captured
    // by the explicit import. The STEP entity number defines identity; the
    // locator is used only to recover that already-defined identity from the
    // frozen B-Rep during a later explicit calculation.
    struct TopologyIdentity {
        enum class Kind { Face, Edge, Vertex };
        Kind kind{Kind::Face};
        std::string semantic_key;
        std::string shape_locator;
        bool operator==(const TopologyIdentity&) const = default;
    };
    std::vector<TopologyIdentity> topology;
};

struct Sweep3DRequest {
    struct PathSegment {
        std::string source_id;
        Vec3 start;
        Vec3 end;
        // Empty for a line.  A four-point array is an exact cubic Bezier
        // representation of the corresponding ZIMA Hermite Curve3D segment.
        std::vector<Vec3> bezier_control_points;
        std::optional<Vec3> arc_midpoint;
        std::vector<std::array<Vec3,4>> bezier_spans;
    };
    struct Section {
        std::string profile_id;
        std::string point_id;
        std::size_t point_index{};
        // Persisted ZIMA Sketch-plane normal. Polygon/curved profiles carry
        // their points in 3D, but exact circles and ellipses still need this
        // plane to construct their OCCT wire without falling back to world XY.
        Vec3 profile_normal{0.0, 0.0, 1.0};
        ExtrusionRequest::ProfileRegion profile;
        std::optional<Vec3> circle_radial_direction;
        std::string thin_end_point_id;
    };
    std::vector<Vec3> path_points;
    std::vector<std::string> path_point_ids;
    std::vector<PathSegment> path_segments;
    std::vector<Section> sections;
    bool make_solid{true};
    // A smooth, single-section sweep with a transported normal frame.
    // Sampling indices are transient approximation data, never topology IDs.
    bool transported{};
    // Unrounded polyline: each segment owns two endpoint stations and
    // perpendicular caps. No corner projection or transition joins segments.
    bool separate_segments{};
    bool thin{};
    double thin_first{}, thin_second{};
    double linear_tolerance{0.001};

};

struct FilletRequest {
    enum class Mode { Constant, Linear };
    FilletRequest() = default;
    FilletRequest(std::vector<EdgeReference> selected_edges, double radius)
        : edges(std::move(selected_edges)), radius_start(radius),
          radius_end(radius) {}
    FilletRequest(std::vector<EdgeReference> selected_edges, Mode selected_mode,
                  double start, double end, bool reversed,
                  std::vector<VertexReference> contour_starts = {})
        : edges(std::move(selected_edges)), mode(selected_mode),
          radius_start(start), radius_end(end), reverse(reversed),
          contour_start_vertices(std::move(contour_starts)) {}
    std::vector<EdgeReference> edges;
    Mode mode{Mode::Constant};
    double radius_start{1.0};
    double radius_end{1.0};
    bool reverse{};
    // Parallel to edges. Every member of one persisted tangent route carries
    // the same semantic R1 endpoint, allowing whichever member seeds OCCT to
    // recover the intended contour direction.
    std::vector<VertexReference> contour_start_vertices;
};

struct ChamferRequest {
    enum class Mode { EqualDistance, TwoDistances, DistanceAngle };
    ChamferRequest() = default;
    ChamferRequest(std::vector<EdgeReference> selected_edges, double distance)
        : edges(std::move(selected_edges)), distance_a(distance),
          distance_b(distance) {}
    ChamferRequest(std::vector<EdgeReference> selected_edges, Mode selected_mode,
                   double first, double second, double angle, bool flipped)
        : edges(std::move(selected_edges)), mode(selected_mode),
          distance_a(first), distance_b(second), angle_radians(angle),
          flip(flipped) {}
    std::vector<EdgeReference> edges;
    Mode mode{Mode::EqualDistance};
    double distance_a{1.0};
    double distance_b{1.0};
    double angle_radians{0.7853981633974483};
    bool flip{};
};

struct ShellRequest {
    std::vector<FaceReference> removed_faces;
    double thickness{1.0};
};

enum class BooleanOperation { Add, Subtract };

// Independent histories are combined only at an explicit body boundary.
struct MirrorPlane {
    Vec3 point;
    Vec3 normal{0,0,1};
    bool operator==(const MirrorPlane&) const = default;
};
enum class PatternDistribution { Forward, Reverse, Both, Symmetric };
struct LinearPatternDirection {
    int local_axis{-1}; // -1 is unused; X/Y/Z refer to the Pattern container's Origin.
    unsigned count{4}; // Includes the source; in Both this is the forward count.
    unsigned reverse_count{1}; // Additional copies behind the source in Both.
    PatternDistribution distribution{PatternDistribution::Forward};
    double spacing{20};
    Vec3 direction{1,0,0}; // Resolved at calculation/preview from the local Origin.
    bool operator==(const LinearPatternDirection&) const = default;
};
struct PatternRequest {
    bool circular{};
    unsigned count{4}; // Circular occurrences including source; computed total for validated linear requests.
    double angle_degrees{90};
    bool full_circle{true};
    Vec3 origin;
    Vec3 axis{0,0,1};
    std::array<LinearPatternDirection,3> linear{{{0}, {}, {}}};
    bool operator==(const PatternRequest&) const = default;
};
enum class BodyCombination { Separate, Add, Subtract, Intersect, Mirror, Pattern };
struct BodyHistoryScope {
    std::string id;
    BodyCombination combination{BodyCombination::Separate};
    std::string target_id;
    // Source of an explicit Boolean or Mirror step; primitive is unused there.
    std::string source_id;
    Vec3 translation;
    Vec3 rotation_degrees;
    MirrorPlane mirror_plane;
    PatternRequest pattern;
    bool operator==(const BodyHistoryScope&) const = default;
};

struct BoxOperation {
    std::string owner_id;
    BoxRequest box;
    BooleanOperation operation{BooleanOperation::Add};
};

using PrimitiveRequest = std::variant<
    BoxRequest, CylinderRequest, SphereRequest, ConeRequest, PyramidRequest, WedgeRequest,
    ExtrusionRequest, RevolutionRequest, FeatureGroupRequest,
    Sweep3DRequest, StepRequest, FilletRequest, ChamferRequest, ShellRequest,
    ThreadSurfaceRequest, DrillPointRequest>;

struct HistoryOperation {
    std::string owner_id;
    PrimitiveRequest primitive;
    BooleanOperation operation{BooleanOperation::Add};
    bool suppressed{};
    // Explicit model resolution for Boolean operations and body treatments.
    // Geometry coordinates remain unchanged binary64 values.
    double boolean_tolerance{1.0e-7};
    // Absolute display deviation in model millimetres, shared by faces and edges.
    double mesh_deflection{0.1};
    BodyHistoryScope body;
    // Document preparation failure, evaluated at this operation boundary.
    std::string input_error;
};

struct BodyResult {
    // Failed/blocked feature owners. Geometry is the last valid input, never a
    // successful result of these operations. Persist with calculation snapshots.
    std::map<std::string, std::string> calculation_errors;
    // Last resolved shaft references, isolated from selectable topology.
    std::string shaft_thread_owner;
    std::array<FaceReference,4> shaft_thread_references{};
    ViewerMesh mesh;
    double volume{};
    double surface_area{};
    std::string source_fingerprint;
    // Opaque calculation snapshot. Only the solid kernel may consume it
    // during an explicit body calculation; viewer/reference code uses mesh.
    std::string kernel_shape;
    // Returned only by explicit STEP import so the owning Part container can
    // persist the source topology map with its parameters.
    std::vector<StepRequest::TopologyIdentity> imported_step_topology;
    // Present on a document result only. Branch snapshots retain their own
    // fingerprints, so changing another body does not invalidate this cache.
    std::map<std::string, std::vector<BodyResult>> body_boundaries;
    std::map<std::string, BodyResult> body_inputs;
    std::map<std::string, BodyResult> body_outputs;
};

// A product definition and its positioned occurrences for explicit STEP export.
// Repeated definition IDs share one STEP product; geometry stays in local mm.
struct StepProduct {
    std::string definition_id;
    std::string name;
    BodyResult body;
    std::vector<StepProduct> children;
    Vec3 translation;
    Vec3 rotation_degrees;
};

struct PlacedBody {
    BodyResult body;
    Vec3 translation;
    Vec3 rotation_degrees;
};

[[nodiscard]] inline std::string box_history_fingerprint(
    const std::vector<BoxOperation>& operations,
    std::size_t operation_count) {
    operation_count = std::min(operation_count, operations.size());
    std::uint64_t hash = 1469598103934665603ULL;
    const auto append_byte = [&](std::uint8_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    const auto append_u64 = [&](std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            append_byte(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    };
    append_u64(operation_count);
    for (std::size_t index = 0; index < operation_count; ++index) {
        const auto& operation = operations[index];
        append_u64(operation.owner_id.size());
        for (const unsigned char value : operation.owner_id) append_byte(value);
        append_byte(static_cast<std::uint8_t>(operation.operation));
        for (const double value : {
                operation.box.length, operation.box.width, operation.box.height,
                operation.box.translation.x, operation.box.translation.y,
                operation.box.translation.z, operation.box.rotation_degrees.x,
                operation.box.rotation_degrees.y,
                operation.box.rotation_degrees.z}) {
            append_u64(std::bit_cast<std::uint64_t>(value));
        }
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index) {
        result[static_cast<std::size_t>(index)] = digits[hash & 0xfU];
        hash >>= 4;
    }
    return result;
}

[[nodiscard]] inline std::string history_fingerprint(
    const std::vector<HistoryOperation>& operations,
    std::size_t operation_count) {
    // A dedicated byte stream keeps primitive kind part of the identity while
    // preserving the established Box fingerprint for existing tests.
    std::uint64_t hash = 1469598103934665603ULL;
    const auto byte = [&](std::uint8_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    const auto u64 = [&](std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            byte(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    };
    operation_count = std::min(operation_count, operations.size());
    u64(operation_count);
    for (std::size_t index = 0; index < operation_count; ++index) {
        const auto& operation = operations[index];
        u64(operation.owner_id.size());
        for (const unsigned char value : operation.owner_id) byte(value);
        byte(static_cast<std::uint8_t>(operation.operation));
        byte(operation.suppressed ? 1U : 0U);
        if (!operation.input_error.empty()) {
            u64(operation.input_error.size());
            for (const unsigned char value : operation.input_error) byte(value);
        }
        u64(std::bit_cast<std::uint64_t>(operation.boolean_tolerance));
        u64(std::bit_cast<std::uint64_t>(operation.mesh_deflection));
        if (!operation.body.id.empty()) {
            for (const auto& text : {operation.body.id, operation.body.target_id, operation.body.source_id}) {
                u64(text.size());
                for (const unsigned char value : text) byte(value);
            }
            byte(static_cast<std::uint8_t>(operation.body.combination));
            for (const auto value : {operation.body.translation.x, operation.body.translation.y,
                    operation.body.translation.z, operation.body.rotation_degrees.x,
                    operation.body.rotation_degrees.y, operation.body.rotation_degrees.z})
                u64(std::bit_cast<std::uint64_t>(value));
        }
        if(operation.body.combination==BodyCombination::Mirror)
            for(double v:{operation.body.mirror_plane.point.x,operation.body.mirror_plane.point.y,operation.body.mirror_plane.point.z,
                    operation.body.mirror_plane.normal.x,operation.body.mirror_plane.normal.y,operation.body.mirror_plane.normal.z})
                u64(std::bit_cast<std::uint64_t>(v));
        if(operation.body.combination==BodyCombination::Pattern) {
            const auto& p=operation.body.pattern;u64(p.count);byte(p.circular);byte(p.full_circle);
            for(double v:{p.angle_degrees,p.origin.x,p.origin.y,p.origin.z,p.axis.x,p.axis.y,p.axis.z})
                u64(std::bit_cast<std::uint64_t>(v));
            for (const auto& d : p.linear) {
                u64(d.local_axis + 1);u64(d.count);u64(d.reverse_count);byte(static_cast<std::uint8_t>(d.distribution));
                for (double v : {d.spacing,d.direction.x,d.direction.y,d.direction.z}) u64(std::bit_cast<std::uint64_t>(v));
            }
        }
        byte(static_cast<std::uint8_t>(operation.primitive.index()));
        std::visit([&](const auto& primitive) {
            using Request = std::decay_t<decltype(primitive)>;
            if constexpr (std::is_same_v<Request, BoxRequest>) {
                for (const double value : {
                        primitive.length, primitive.width, primitive.height,
                        primitive.translation.x, primitive.translation.y,
                        primitive.translation.z, primitive.rotation_degrees.x,
                        primitive.rotation_degrees.y, primitive.rotation_degrees.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
            } else if constexpr (std::is_same_v<Request, CylinderRequest>) {
                for (const double value : {
                        primitive.radius, primitive.height,
                        primitive.translation.x, primitive.translation.y,
                        primitive.translation.z, primitive.rotation_degrees.x,
                        primitive.rotation_degrees.y, primitive.rotation_degrees.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
            } else if constexpr (std::is_same_v<Request, SphereRequest>) {
                for (const double value : {
                        primitive.radius, primitive.translation.x,
                        primitive.translation.y, primitive.translation.z,
                        primitive.rotation_degrees.x, primitive.rotation_degrees.y,
                        primitive.rotation_degrees.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
            } else if constexpr (std::is_same_v<Request, ConeRequest>) {
                for (const double value : {primitive.bottom_radius,
                        primitive.top_radius, primitive.height,
                        primitive.translation.x, primitive.translation.y,
                        primitive.translation.z, primitive.rotation_degrees.x,
                        primitive.rotation_degrees.y, primitive.rotation_degrees.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
            } else if constexpr (std::is_same_v<Request, PyramidRequest>) {
                for (const double value : {primitive.length, primitive.width,
                        primitive.height, primitive.translation.x,
                        primitive.translation.y, primitive.translation.z,
                        primitive.rotation_degrees.x, primitive.rotation_degrees.y,
                        primitive.rotation_degrees.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
            } else if constexpr (std::is_same_v<Request, WedgeRequest>) {
                for (const double value : {primitive.length, primitive.width,
                        primitive.height, primitive.top_offset,
                        primitive.translation.x, primitive.translation.y,
                        primitive.translation.z, primitive.rotation_degrees.x,
                        primitive.rotation_degrees.y, primitive.rotation_degrees.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
            } else if constexpr (std::is_same_v<Request, ExtrusionRequest>) {
                // Up-to-plane prisms cover the complete curved profile.
                if (primitive.extent == ExtrusionRequest::Extent::UpToPlane) byte(1);
                const auto append_profile = [&](const auto& profile_variant) {
                    byte(static_cast<std::uint8_t>(profile_variant.index()));
                    std::visit([&](const auto& profile) {
                        using Profile = std::decay_t<decltype(profile)>;
                        if constexpr (std::is_same_v<Profile,
                                          ExtrusionRequest::PolygonProfile>) {
                            u64(profile.vertices.size());
                            for (const auto& point : profile.vertices) {
                                for (const double value : {point.x, point.y, point.z}) {
                                    u64(std::bit_cast<std::uint64_t>(value));
                                }
                            }
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::CircleProfile>) {
                            for (const double value : {
                                    profile.center.x, profile.center.y,
                                    profile.center.z, profile.radius}) {
                                u64(std::bit_cast<std::uint64_t>(value));
                            }
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::EllipseProfile>) {
                            for (const double value : {
                                    profile.center.x, profile.center.y,
                                    profile.center.z, profile.major_axis_direction.x,
                                    profile.major_axis_direction.y,
                                    profile.major_axis_direction.z,
                                    profile.major_radius, profile.minor_radius}) {
                                u64(std::bit_cast<std::uint64_t>(value));
                            }
                        } else {
                            u64(profile.curves.size());
                            for (const auto& curve : profile.curves) {
                                byte(static_cast<std::uint8_t>(curve.index()));
                                std::visit([&](const auto& exact_curve) {
                                    const auto append_point = [&](const Vec3& point) {
                                        for (const double value : {
                                                point.x, point.y, point.z}) {
                                            u64(std::bit_cast<std::uint64_t>(value));
                                        }
                                    };
                                    append_point(exact_curve.start);
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::ArcCurve>) {
                                        append_point(exact_curve.middle);
                                    }
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::EllipticalArcCurve>) {
                                        append_point(exact_curve.center);
                                        append_point(exact_curve.major_axis_direction);
                                        for (const double value : {
                                                exact_curve.major_radius,
                                                exact_curve.minor_radius,
                                                exact_curve.start_parameter,
                                                exact_curve.end_parameter}) {
                                            u64(std::bit_cast<std::uint64_t>(value));
                                        }
                                        byte(exact_curve.reversed);
                                    }
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::BSplineCurve>) {
                                        u64(exact_curve.degree);
                                        byte(exact_curve.periodic);
                                        u64(exact_curve.control_points.size());
                                        for (const auto& point : exact_curve.control_points) {
                                            append_point(point);
                                        }
                                    }
                                    append_point(exact_curve.end);
                                }, curve);
                            }
                        }
                    }, profile_variant);
                };
                append_profile(primitive.outer_profile);
                u64(primitive.profile_region_id.size());
                for (const unsigned char value : primitive.profile_region_id) byte(value);
                u64(primitive.outer_boundary_id.size());
                for (const unsigned char value : primitive.outer_boundary_id) byte(value);
                u64(primitive.inner_boundary_ids.size());
                for (const auto& id : primitive.inner_boundary_ids) {
                    u64(id.size()); for (const unsigned char value : id) byte(value);
                }
                const auto append_source_ids = [&](const auto& groups) {
                    u64(groups.size());
                    for (const auto& group : groups) {
                        u64(group.size());
                        for (const auto& id : group) {
                            u64(id.size());
                            for (const unsigned char value : id) byte(value);
                        }
                    }
                };
                append_source_ids(std::vector<std::vector<std::string>>{
                    primitive.outer_edge_source_ids});
                append_source_ids(primitive.inner_edge_source_ids);
                append_source_ids(std::vector<std::vector<std::string>>{
                    primitive.outer_vertex_source_ids});
                append_source_ids(primitive.inner_vertex_source_ids);
                u64(primitive.inner_profiles.size());
                for (const auto& profile : primitive.inner_profiles) {
                    append_profile(profile);
                }
                u64(primitive.additional_profile_regions.size());
                for (const auto& region : primitive.additional_profile_regions) {
                    u64(region.region_id.size());
                    for (const unsigned char value : region.region_id) byte(value);
                    u64(region.outer_boundary_id.size());
                    for (const unsigned char value : region.outer_boundary_id) byte(value);
                    u64(region.inner_boundary_ids.size());
                    for (const auto& id : region.inner_boundary_ids) {
                        u64(id.size()); for (const unsigned char value : id) byte(value);
                    }
                    append_source_ids(std::vector<std::vector<std::string>>{
                        region.outer_edge_source_ids});
                    append_source_ids(region.inner_edge_source_ids);
                    append_source_ids(std::vector<std::vector<std::string>>{
                        region.outer_vertex_source_ids});
                    append_source_ids(region.inner_vertex_source_ids);
                    append_profile(region.outer_profile);
                    u64(region.inner_profiles.size());
                    for (const auto& profile : region.inner_profiles) append_profile(profile);
                }
                for (const double value : {
                        primitive.direction.x, primitive.direction.y,
                        primitive.direction.z, primitive.start_offset}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
                byte(static_cast<std::uint8_t>(primitive.extent));
                byte(primitive.first_cap_is_start);
                byte(primitive.through_all_forward);
                byte(primitive.through_all_reverse);
                u64(primitive.target_face.owner_id.size());
                for (const unsigned char value : primitive.target_face.owner_id) byte(value);
                u64(primitive.target_face.semantic_key.size());
                for (const unsigned char value : primitive.target_face.semantic_key) byte(value);
                byte(primitive.target_is_datum);
                for (const double value : {primitive.target_plane_origin.x,
                        primitive.target_plane_origin.y, primitive.target_plane_origin.z,
                        primitive.target_plane_normal.x, primitive.target_plane_normal.y,
                        primitive.target_plane_normal.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
                u64(primitive.target_surface_triangles.size());
                for (const auto& point : primitive.target_surface_triangles) {
                    for (const double value : {point.x, point.y, point.z}) {
                        u64(std::bit_cast<std::uint64_t>(value));
                    }
                }
            } else if constexpr (std::is_same_v<Request, FeatureGroupRequest>) {
                u64(primitive.children.size());
                std::vector<HistoryOperation> child_operations;
                child_operations.reserve(primitive.children.size());
                for (std::size_t child_index = 0;
                     child_index < primitive.children.size(); ++child_index) {
                    HistoryOperation child;
                    child.owner_id = operation.owner_id + ":child:" +
                        std::to_string(child_index);
                    std::visit([&](const auto& value) {
                        child.primitive = value;
                    }, primitive.children[child_index]);
                    child.operation = operation.operation;
                    child.boolean_tolerance = operation.boolean_tolerance;
                    child_operations.push_back(std::move(child));
                }
                const auto child_fingerprint = history_fingerprint(
                    child_operations, child_operations.size());
                u64(child_fingerprint.size());
                for (const unsigned char value : child_fingerprint) byte(value);
            } else if constexpr (std::is_same_v<Request, RevolutionRequest>) {
                const auto append_profile = [&](const auto& profile_variant) {
                    byte(static_cast<std::uint8_t>(profile_variant.index()));
                    std::visit([&](const auto& profile) {
                        using Profile = std::decay_t<decltype(profile)>;
                        if constexpr (std::is_same_v<Profile,
                                          ExtrusionRequest::PolygonProfile>) {
                            u64(profile.vertices.size());
                            for (const auto& point : profile.vertices) {
                                for (const double value : {point.x, point.y, point.z}) {
                                    u64(std::bit_cast<std::uint64_t>(value));
                                }
                            }
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::CircleProfile>) {
                            for (const double value : {
                                    profile.center.x, profile.center.y,
                                    profile.center.z, profile.radius}) {
                                u64(std::bit_cast<std::uint64_t>(value));
                            }
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::EllipseProfile>) {
                            for (const double value : {
                                    profile.center.x, profile.center.y,
                                    profile.center.z, profile.major_axis_direction.x,
                                    profile.major_axis_direction.y,
                                    profile.major_axis_direction.z,
                                    profile.major_radius, profile.minor_radius}) {
                                u64(std::bit_cast<std::uint64_t>(value));
                            }
                        } else {
                            u64(profile.curves.size());
                            for (const auto& curve : profile.curves) {
                                byte(static_cast<std::uint8_t>(curve.index()));
                                std::visit([&](const auto& exact_curve) {
                                    const auto point = [&](const Vec3& value) {
                                        for (const double coordinate : {
                                                value.x, value.y, value.z}) {
                                            u64(std::bit_cast<std::uint64_t>(coordinate));
                                        }
                                    };
                                    point(exact_curve.start);
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::ArcCurve>) {
                                        point(exact_curve.middle);
                                    }
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::EllipticalArcCurve>) {
                                        point(exact_curve.center);
                                        point(exact_curve.major_axis_direction);
                                        for (const double value : {
                                                exact_curve.major_radius,
                                                exact_curve.minor_radius,
                                                exact_curve.start_parameter,
                                                exact_curve.end_parameter}) {
                                            u64(std::bit_cast<std::uint64_t>(value));
                                        }
                                        byte(exact_curve.reversed);
                                    }
                                    if constexpr (std::is_same_v<
                                                      std::decay_t<decltype(exact_curve)>,
                                                      ExtrusionRequest::BSplineCurve>) {
                                        u64(exact_curve.degree);
                                        byte(exact_curve.periodic);
                                        u64(exact_curve.control_points.size());
                                        for (const auto& control : exact_curve.control_points) {
                                            point(control);
                                        }
                                    }
                                    point(exact_curve.end);
                                }, curve);
                            }
                        }
                    }, profile_variant);
                };
                append_profile(primitive.outer_profile);
                u64(primitive.profile_region_id.size());
                for (const unsigned char value : primitive.profile_region_id) byte(value);
                u64(primitive.outer_boundary_id.size());
                for (const unsigned char value : primitive.outer_boundary_id) byte(value);
                u64(primitive.inner_boundary_ids.size());
                for (const auto& id : primitive.inner_boundary_ids) {
                    u64(id.size()); for (const unsigned char value : id) byte(value);
                }
                const auto append_source_ids = [&](const auto& groups) {
                    u64(groups.size());
                    for (const auto& group : groups) {
                        u64(group.size());
                        for (const auto& id : group) {
                            u64(id.size());
                            for (const unsigned char value : id) byte(value);
                        }
                    }
                };
                append_source_ids(std::vector<std::vector<std::string>>{
                    primitive.outer_edge_source_ids});
                append_source_ids(primitive.inner_edge_source_ids);
                append_source_ids(std::vector<std::vector<std::string>>{
                    primitive.outer_vertex_source_ids});
                append_source_ids(primitive.inner_vertex_source_ids);
                u64(primitive.inner_profiles.size());
                for (const auto& profile : primitive.inner_profiles) {
                    append_profile(profile);
                }
                u64(primitive.additional_profile_regions.size());
                for (const auto& region : primitive.additional_profile_regions) {
                    u64(region.region_id.size());
                    for (const unsigned char value : region.region_id) byte(value);
                    u64(region.outer_boundary_id.size());
                    for (const unsigned char value : region.outer_boundary_id) byte(value);
                    u64(region.inner_boundary_ids.size());
                    for (const auto& id : region.inner_boundary_ids) {
                        u64(id.size()); for (const unsigned char value : id) byte(value);
                    }
                    append_source_ids(std::vector<std::vector<std::string>>{
                        region.outer_edge_source_ids});
                    append_source_ids(region.inner_edge_source_ids);
                    append_source_ids(std::vector<std::vector<std::string>>{
                        region.outer_vertex_source_ids});
                    append_source_ids(region.inner_vertex_source_ids);
                    append_profile(region.outer_profile);
                    u64(region.inner_profiles.size());
                    for (const auto& profile : region.inner_profiles) append_profile(profile);
                }
                for (const double value : {
                        primitive.profile_normal.x, primitive.profile_normal.y,
                        primitive.profile_normal.z,
                        primitive.axis_point.x, primitive.axis_point.y,
                        primitive.axis_point.z, primitive.axis_direction.x,
                        primitive.axis_direction.y, primitive.axis_direction.z,
                        primitive.start_angle_degrees,
                        primitive.angle_degrees}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
                byte(primitive.first_cap_is_start);
            } else if constexpr (std::is_same_v<Request, Sweep3DRequest>) {
                const auto append_string = [&](const std::string& value) {
                    u64(value.size());
                    for (const unsigned char character : value) byte(character);
                };
                const auto append_point = [&](const Vec3& point) {
                    for (const double value : {point.x, point.y, point.z}) {
                        u64(std::bit_cast<std::uint64_t>(value));
                    }
                };
                const auto append_profile = [&](
                        const ExtrusionRequest::ProfileLoop& profile_variant) {
                    byte(static_cast<std::uint8_t>(profile_variant.index()));
                    std::visit([&](const auto& profile) {
                        using Profile = std::decay_t<decltype(profile)>;
                        if constexpr (std::is_same_v<Profile,
                                          ExtrusionRequest::PolygonProfile>) {
                            u64(profile.vertices.size());
                            for (const auto& point : profile.vertices)
                                append_point(point);
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::CircleProfile>) {
                            append_point(profile.center);
                            u64(std::bit_cast<std::uint64_t>(profile.radius));
                        } else if constexpr (std::is_same_v<Profile,
                                                 ExtrusionRequest::EllipseProfile>) {
                            append_point(profile.center);
                            append_point(profile.major_axis_direction);
                            u64(std::bit_cast<std::uint64_t>(
                                profile.major_radius));
                            u64(std::bit_cast<std::uint64_t>(
                                profile.minor_radius));
                        } else {
                            u64(profile.curves.size());
                            for (const auto& curve : profile.curves) {
                                byte(static_cast<std::uint8_t>(curve.index()));
                                std::visit([&](const auto& exact_curve) {
                                    append_point(exact_curve.start);
                                    if constexpr (std::is_same_v<
                                            std::decay_t<decltype(exact_curve)>,
                                            ExtrusionRequest::ArcCurve>) {
                                        append_point(exact_curve.middle);
                                    }
                                    if constexpr (std::is_same_v<
                                            std::decay_t<decltype(exact_curve)>,
                                            ExtrusionRequest::EllipticalArcCurve>) {
                                        append_point(exact_curve.center);
                                        append_point(
                                            exact_curve.major_axis_direction);
                                        for (const double value : {
                                                exact_curve.major_radius,
                                                exact_curve.minor_radius,
                                                exact_curve.start_parameter,
                                                exact_curve.end_parameter}) {
                                            u64(std::bit_cast<std::uint64_t>(
                                                value));
                                        }
                                        byte(exact_curve.reversed);
                                    }
                                    if constexpr (std::is_same_v<
                                            std::decay_t<decltype(exact_curve)>,
                                            ExtrusionRequest::BSplineCurve>) {
                                        u64(exact_curve.degree);
                                        byte(exact_curve.interpolating);
                                        byte(exact_curve.periodic);
                                        u64(exact_curve.control_points.size());
                                        for (const auto& point :
                                             exact_curve.control_points) {
                                            append_point(point);
                                        }
                                    }
                                    append_point(exact_curve.end);
                                }, curve);
                            }
                        }
                    }, profile_variant);
                };
                u64(primitive.path_points.size());
                byte(primitive.separate_segments);
                for (const auto& point : primitive.path_points)
                    append_point(point);
                u64(primitive.path_point_ids.size());
                for (const auto& id : primitive.path_point_ids)
                    append_string(id);
                u64(primitive.path_segments.size());
                for (const auto& segment : primitive.path_segments) {
                    append_string(segment.source_id);
                    append_point(segment.start);
                    append_point(segment.end);
                    u64(segment.arc_midpoint.has_value());
                    if(segment.arc_midpoint)append_point(*segment.arc_midpoint);
                    u64(segment.bezier_control_points.size());
                    for (const auto& point : segment.bezier_control_points)
                        append_point(point);
                    u64(segment.bezier_spans.size());
                    for(const auto& span:segment.bezier_spans)for(const auto& point:span)append_point(point);
                }
                u64(primitive.sections.size());
                for (const auto& section : primitive.sections) {
                    append_string(section.profile_id);
                    append_string(section.point_id);
                    u64(section.point_index);
                    append_point(section.profile_normal);
                    u64(section.circle_radial_direction.has_value());
                    if(section.circle_radial_direction) append_point(*section.circle_radial_direction);
                    append_string(section.profile.region_id);
                    append_string(section.profile.outer_boundary_id);
                    u64(section.profile.outer_edge_source_ids.size());
                    for (const auto& id :
                         section.profile.outer_edge_source_ids) {
                        append_string(id);
                    }
                    u64(section.profile.outer_vertex_source_ids.size());
                    for (const auto& id :
                         section.profile.outer_vertex_source_ids) {
                        append_string(id);
                    }
                    append_profile(section.profile.outer_profile);
                    u64(section.profile.inner_profiles.size());
                    for(const auto& inner:section.profile.inner_profiles)append_profile(inner);
                    for(const auto& id:section.profile.inner_boundary_ids)append_string(id);
                    for(const auto& loop:section.profile.inner_edge_source_ids){u64(loop.size());for(const auto& id:loop)append_string(id);}
                    for(const auto& loop:section.profile.inner_vertex_source_ids){u64(loop.size());for(const auto& id:loop)append_string(id);}
                    append_string(section.thin_end_point_id);

                }
                byte(primitive.make_solid);
                byte(primitive.transported);
                u64(std::bit_cast<std::uint64_t>(primitive.linear_tolerance));
                byte(primitive.thin);
                u64(std::bit_cast<std::uint64_t>(primitive.thin_first));u64(std::bit_cast<std::uint64_t>(primitive.thin_second));
            } else if constexpr (std::is_same_v<Request, StepRequest>) {
                u64(primitive.source_path.size());
                for (const unsigned char value : primitive.source_path) byte(value);
                u64(primitive.component_path.size());
                for (const unsigned char value : primitive.component_path) byte(value);
            } else if constexpr (std::is_same_v<Request, ThreadSurfaceRequest>) {
                if (primitive.shaft_face) {
                    byte(255); // Standalone external thread reference contract.
                    for (const auto& face : {primitive.shaft_face, std::optional<FaceReference>{primitive.shaft_start},
                            primitive.shaft_chamfer,primitive.shaft_end}) {
                        byte(face.has_value());
                        if (face) for (const auto* text : {&face->owner_id,&face->semantic_key,&face->instance_path}) {
                            u64(text->size());for (const unsigned char ch : *text) byte(ch);
                        }
                    }
                    byte(primitive.shaft_through_all);byte(primitive.shaft_runout);
                }
                byte(primitive.end_plane_origin.has_value());
                if (primitive.end_plane_origin) {
                    for (double value : {primitive.end_plane_origin->x,primitive.end_plane_origin->y,
                            primitive.end_plane_origin->z,primitive.end_plane_normal.x,
                            primitive.end_plane_normal.y,primitive.end_plane_normal.z})
                        u64(std::bit_cast<std::uint64_t>(value));
                }
                // Opening result revision: unified source wire and persisted axis.
                byte(2);
                for (const double value : {primitive.nominal_radius,
                        primitive.root_radius, primitive.start_offset,
                        primitive.length, primitive.runout_start,
                        primitive.runout_end, primitive.origin.x,
                        primitive.origin.y, primitive.origin.z,
                        primitive.axis_direction.x, primitive.axis_direction.y,
                        primitive.axis_direction.z,
                        primitive.radial_direction.x,
                        primitive.radial_direction.y,
                        primitive.radial_direction.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
                byte(primitive.through_all_forward);
                byte(primitive.through_all_reverse);
                byte(static_cast<std::uint8_t>(primitive.side));
                byte(primitive.enabled);
                for (const auto& group : {primitive.cuts_before, primitive.cuts_after}) {
                    byte(static_cast<bool>(group));
                    if (!group) continue;
                    HistoryOperation cut;
                    cut.owner_id = operation.owner_id;
                    cut.primitive = *group;
                    cut.operation = BooleanOperation::Subtract;
                    cut.boolean_tolerance = operation.boolean_tolerance;
                    const auto fingerprint = history_fingerprint({cut}, 1);
                    u64(fingerprint.size());
                    for (const unsigned char value : fingerprint) byte(value);
                }
            } else if constexpr (std::is_same_v<Request, DrillPointRequest>) {
                u64(primitive.bottom_faces.size());
                for (const auto& face : primitive.bottom_faces) {
                    for (const auto* text : {&face.owner_id,
                            &face.semantic_key, &face.instance_path}) {
                        u64(text->size());
                        for (const unsigned char value : *text) byte(value);
                    }
                }
                u64(std::bit_cast<std::uint64_t>(
                    primitive.included_angle_degrees));
            } else if constexpr (std::is_same_v<Request, ShellRequest>) {
                u64(primitive.removed_faces.size());
                for (const auto& face : primitive.removed_faces) {
                    u64(face.owner_id.size());
                    for (const unsigned char value : face.owner_id) byte(value);
                    u64(face.semantic_key.size());
                    for (const unsigned char value : face.semantic_key) byte(value);
                }
                u64(std::bit_cast<std::uint64_t>(primitive.thickness));
            } else {
                u64(primitive.edges.size());
                for (const auto& edge : primitive.edges) {
                    u64(edge.owner_id.size());
                    for (const unsigned char value : edge.owner_id) byte(value);
                    u64(edge.semantic_key.size());
                    for (const unsigned char value : edge.semantic_key) byte(value);
                }
                if constexpr (std::is_same_v<Request, FilletRequest>) {
                    byte(static_cast<std::uint8_t>(primitive.mode));
                    u64(std::bit_cast<std::uint64_t>(primitive.radius_start));
                    u64(std::bit_cast<std::uint64_t>(primitive.radius_end));
                    byte(primitive.reverse ? 1U : 0U);
                    u64(primitive.contour_start_vertices.size());
                    for (const auto& vertex :
                         primitive.contour_start_vertices) {
                        u64(vertex.owner_id.size());
                        for (const unsigned char value : vertex.owner_id)
                            byte(value);
                        u64(vertex.semantic_key.size());
                        for (const unsigned char value : vertex.semantic_key)
                            byte(value);
                    }
                } else {
                    byte(static_cast<std::uint8_t>(primitive.mode));
                    u64(std::bit_cast<std::uint64_t>(primitive.distance_a));
                    u64(std::bit_cast<std::uint64_t>(primitive.distance_b));
                    u64(std::bit_cast<std::uint64_t>(primitive.angle_radians));
                    byte(primitive.flip ? 1U : 0U);
                }
            }
        }, operation.primitive);
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index) {
        result[static_cast<std::size_t>(index)] = digits[hash & 0xfU];
        hash >>= 4;
    }
    return result;
}

class GeometryKernel {
public:
    virtual ~GeometryKernel() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual BodyResult make_box(const BoxRequest& request) const = 0;
    [[nodiscard]] virtual BodyResult evaluate_boxes(
        const std::vector<BoxOperation>& operations) const = 0;
    [[nodiscard]] virtual std::vector<BodyResult> evaluate_box_boundaries(
        const std::vector<BoxOperation>& operations) const = 0;
    [[nodiscard]] virtual std::vector<BodyResult> evaluate_history(
        const std::vector<HistoryOperation>& operations) const = 0;
    // Reuses the longest valid persisted prefix when the remaining operations
    // do not require live topology ancestry from that prefix. Implementations
    // must conservatively fall back to a full calculation otherwise.
    [[nodiscard]] virtual std::vector<BodyResult> evaluate_history_incremental(
        const std::vector<HistoryOperation>& operations,
        const std::vector<BodyResult>& previous_boundaries) const = 0;
    [[nodiscard]] virtual BodyResult pattern_body(const BodyResult&,const PatternRequest&,
        const std::string& ={},Vec3 ={},Vec3 ={},bool =false) const {
        throw std::runtime_error("Kernel does not support body patterns");
    }
    [[nodiscard]] virtual BodyResult mirror_body(const BodyResult&,MirrorPlane,
        const std::string& ={},Vec3 ={},Vec3 ={}) const {
        throw std::runtime_error("Kernel does not support body reflection");
    }
    [[nodiscard]] virtual BodyResult compound_bodies(
        const std::vector<PlacedBody>& bodies) const = 0;
};

}  // namespace zima::kernel
