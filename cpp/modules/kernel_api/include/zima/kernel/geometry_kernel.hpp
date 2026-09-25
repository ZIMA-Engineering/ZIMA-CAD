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
#include <set>
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

enum class SheetFaceRole { Unknown, SideA, SideB, ThicknessFace };
enum class SheetEdgeRole { Unknown, Boundary, Thickness, Junction };
enum class SheetOperation { None, Flat, Bend, Revolution };

struct FaceReference {
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;
    std::shared_ptr<const SurfaceGeometry> surface;
    std::optional<double> measured_area; // Captured only during explicit calculation.
    bool surface_result{}; // Non-volumetric modeling surface; not part of identity.
    SheetFaceRole sheet_role{SheetFaceRole::Unknown};
    double sheet_thickness{};
    // Source material region, independent of the later feature owning this face.
    std::string sheet_owner;
    // Ordinary View selection may present a calculated derivative as the
    // authored Container that produced it.  This is presentation metadata;
    // owner_id/semantic_key remain the persisted topology identity used by
    // references and commands.
    std::string display_owner_id;

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
    // See FaceReference::display_owner_id.  It is deliberately excluded from
    // topology identity comparisons.
    std::string display_owner_id;
    [[nodiscard]] bool valid() const {
        return !owner_id.empty() && !semantic_key.empty();
    }
    bool operator==(const EdgeReference& other) const {
        return owner_id==other.owner_id && semantic_key==other.semantic_key &&
            instance_path==other.instance_path;
    }
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

// Non-periodic clamped rational B-spline, captured during body calculation.
// Knots include multiplicities. End poles are the actual trimmed endpoints.
struct BSplineGeometry {
    unsigned degree{3};
    std::vector<Vec3> poles;
    std::vector<double> knots;
    std::vector<double> weights;
    bool operator==(const BSplineGeometry&) const = default;
    void validate() const {
        const auto n = poles.size();
        if (degree < 1 || n <= degree || knots.size() != n + degree + 1 ||
            weights.size() != n || !std::is_sorted(knots.begin(), knots.end()))
            throw std::runtime_error("Invalid rational spline arrays");
        for (const auto& p : poles)
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                throw std::runtime_error("Invalid rational spline pole");
        for (double w : weights) if (!std::isfinite(w) || w <= 0)
            throw std::runtime_error("Invalid rational spline weight");
        for (double k : knots) if (!std::isfinite(k))
            throw std::runtime_error("Invalid rational spline knot");
        for (std::size_t i=degree+1; i<n; ) {
            auto end=i+1;
            while(end<n && knots[end]==knots[i]) ++end;
            if (end-i>degree || knots[i]<=knots.front() || knots[i]>=knots.back())
                throw std::runtime_error("Invalid interior spline multiplicity");
            i=end;
        }
        if (!(knots[degree] < knots[n]))
            throw std::runtime_error("Empty rational spline domain");
        for (unsigned i = 0; i <= degree; ++i)
            if (knots[i] != knots.front() || knots[n+i] != knots.back())
                throw std::runtime_error("Rational spline must be clamped");
    }
};

inline Vec3 bspline_value(const BSplineGeometry& curve, double fraction) {
    const auto n = curve.poles.size();
    const auto degree = curve.degree;
    const double u = curve.knots[degree] + std::clamp(fraction, 0.0, 1.0) *
        (curve.knots[n] - curve.knots[degree]);
    const auto span = fraction >= 1.0 ? n - 1 : static_cast<std::size_t>(
        std::upper_bound(curve.knots.begin() + degree, curve.knots.begin() + n + 1, u)
        - curve.knots.begin() - 1);
    std::vector<std::array<double, 4>> d(degree + 1);
    for (unsigned j = 0; j <= degree; ++j) {
        const auto i = span - degree + j;
        const auto& p = curve.poles[i]; const double w = curve.weights[i];
        d[j] = {p.x*w, p.y*w, p.z*w, w};
    }
    for (unsigned r = 1; r <= degree; ++r)
        for (unsigned j = degree; j >= r; --j) {
            const auto i = span - degree + j;
            const double den = curve.knots[i+degree-r+1] - curve.knots[i];
            const double a = den > 0 ? (u-curve.knots[i])/den : 0;
            for (unsigned c = 0; c < 4; ++c) d[j][c] = (1-a)*d[j-1][c] + a*d[j][c];
        }
    return {d[degree][0]/d[degree][3], d[degree][1]/d[degree][3], d[degree][2]/d[degree][3]};
}

inline void reverse_bspline_parameters(std::vector<double>& knots, std::vector<double>& weights) {
    if (!knots.empty()) {
        const double sum = knots.front() + knots.back();
        std::reverse(knots.begin(), knots.end());
        for (auto& k : knots) k = sum - k;
    }
    std::reverse(weights.begin(), weights.end());
}

// Disposable annotation display data. Native ownership remains in Symbol Placement.
struct AnnotationStroke {
    Vec3 contact, grip, direction_tip;
    std::vector<Vec3> local_points;
    double left{}, right{}, bottom{}, arrow_length{2.5};
    int kind{2}; // face normal, edge tangent, free point
    int role{}; // glyph, leader, arrow, shelf
    bool perpendicular{true};
    bool short_shelf{};
    double shelf_length{3.};
};
template<class Transform> inline void transform_annotation(AnnotationStroke& a,Transform transform) {
    a.contact=transform(a.contact);a.grip=transform(a.grip);a.direction_tip=transform(a.direction_tip);
}

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
    std::optional<double> measured_length; // Exact source curve length in mm.
    std::optional<BSplineGeometry> exact_spline;
    bool surface_result{};
    // Transient preview only; not serialized. End-condition symbolism must not
    // change the semantic roles consumed by sheet-cut footprint estimation.
    bool preview_terminal_dashed{};
    std::optional<AnnotationStroke> annotation;
};

[[nodiscard]] inline SheetEdgeRole sheet_edge_role(const ViewerEdge& edge) {
    const auto& sides=edge.edge_treatment_side_references;
    if(sides.size()!=2)return SheetEdgeRole::Unknown;
    const auto a=sides[0].sheet_role,b=sides[1].sheet_role;
    if(a==SheetFaceRole::Unknown||b==SheetFaceRole::Unknown)return SheetEdgeRole::Unknown;
    const bool x=a==SheetFaceRole::ThicknessFace,y=b==SheetFaceRole::ThicknessFace;
    return x&&y?SheetEdgeRole::Thickness:x!=y?SheetEdgeRole::Boundary:SheetEdgeRole::Junction;
}

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
    bool surface_result{};
    // Transient construction presentation, rebuilt from the owning Curve.
    // The reference retains the child Point's persisted identity.
    std::string display_owner_id;
};

struct ViewerAxis {
    Vec3 point;
    Vec3 direction{0.0, 0.0, 1.0};
    double display_length{100.0};
    AxisReference reference;
    std::string label;
};

enum class ViewerDimensionKind { Linear, Angular, Radius, Diameter };

struct DimensionTextStyle {
    std::string prefix, suffix{"mm"}, text_override;
    int decimals{3};
    std::string tolerance_mode, symmetric_tolerance, single_tolerance, upper_tolerance, lower_tolerance;
    bool operator==(const DimensionTextStyle&) const = default;
};
struct ViewerDimension {
    Vec3 witness_first;
    Vec3 witness_second;
    Vec3 line_first;
    Vec3 line_second;
    double value{};
    EdgeReference reference;
    std::string label_prefix;
    std::string unit_suffix{"mm"};
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
    std::optional<DimensionTextStyle> source_text_style;
    // Derived Assembly hinge control; edits the same persisted angle value.
    // Presentation only, with a screen-sized radial arm and endpoint grip.
    bool rotation_handle{};
    // Selectable parameter annotation without measuring lines or arrows.
    bool label_only{};
    // Definition-derived linear measurement axis, independent of value or
    // coincident witnesses. Transform as a vector with the annotation plane.
    std::optional<Vec3> measurement_direction;
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
    // Present only for an original solid face, resolved during body calculation.
    std::optional<FaceReference> end_plane_reference;
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

// Derived from the persisted feature's thickness and side, never from tessellation.
struct ProfileWall {
    double first_offset{};
    double second_offset{};
    std::string end_point_id; // Native final point for an open profile; empty for a closed loop.
};

// One explicitly resolved end reference; numerical geometry is in profile coordinates.
struct ExtrusionLimit {
    bool planar{true};
    FaceReference reference;
    bool datum{};
    Vec3 origin;
    Vec3 normal{0.0, 0.0, 1.0};
    std::vector<Vec3> triangles;
};

struct ProfileCenterlines {
    bool origin_enabled{}, centroid_enabled{};
    Vec3 origin, normal{0,0,1};
    std::string origin_id, profile_id;
};

struct ExtrusionRequest {
    ProfileCenterlines centerlines;
    bool sheet_cut{};
    // Both Sheet Cut methods retain normal walls. Clearance encloses the
    // profile passage through the full thickness instead of its surface trim.
    bool sheet_cut_clearance{};
    // Persisted document calculation tolerance, in millimetres; never the
    // current machine's application default or a Boolean sewing tolerance.
    double sheet_cut_tolerance{0.05};
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
        std::vector<double> knots;
        std::vector<double> weights;
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
    std::optional<ProfileWall> wall;
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
    std::optional<ExtrusionLimit> reverse_limit;
    bool symmetric_limit{};
    // One independently identified side may use the reflection of the authored
    // forward target across its source profile (unified Feature symmetry).
    bool mirror_forward_limit{};
    bool surface_result{};
    std::string open_profile_end_id;
};

struct RevolutionRequest {
    ProfileCenterlines centerlines;
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
    std::optional<ProfileWall> wall;
    bool first_cap_is_start{true};
    double start_angle_degrees{};
    double angle_degrees{360.0};
    bool surface_result{};
    std::string open_profile_end_id;
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
    // Already resolved feature placement in the owning Body's coordinates.
    // The immutable source B-Rep and its identity locators stay unplaced.
    Vec3 translation{};
    Vec3 rotation_degrees{};
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
    // Station points shared by separate primitives of one semantic feature
    // use one identity without a local start/end role.
    std::set<std::string> canonical_station_ids;
    std::vector<PathSegment> path_segments;
    std::vector<Section> sections;
    bool make_solid{true};
    // A smooth, single-section sweep with a transported normal frame.
    // Sampling indices are transient approximation data, never topology IDs.
    bool transported{};
    // Interpolate every authored section as one smooth loft. Longitudinal
    // boundaries are BSplines instead of one edge per sampling interval.
    bool smooth_loft{};
    // Explicitly calculated sections already have their final spatial frames.
    // Only valid for a smooth loft with one section per path point. Ordinary
    // Sweep/Loft callers retain the existing path-normal transport behavior.
    bool fixed_section_frames{};
    // Authored Sweep tools expose their path ends as attachment points.
    // Other users of the sweep kernel (e.g. sheet bends) do not opt in.
    bool attachment_endpoints{};
    // Unrounded polyline: each segment owns two endpoint stations and
    // perpendicular caps. No corner projection or transition joins segments.
    bool separate_segments{};
    bool thin{};
    double thin_first{}, thin_second{};
    double linear_tolerance{0.001};

};

// One semantic feature may own ordinary modeling primitives at one history
// boundary. All child identities are authored before kernel calculation.
struct FeatureGroupRequest {
    using Child = std::variant<ExtrusionRequest, RevolutionRequest, Sweep3DRequest>;
    std::vector<Child> children;
    std::vector<ViewerAxis> axes;
    // Inactive authored sides retain endpoint references at their source
    // profiles. They do not add faces or material to the result body.
    using ReferenceProfile = std::variant<ExtrusionRequest, RevolutionRequest>;
    std::vector<ReferenceProfile> reference_profiles;
    // An explicitly authored sketch-only Feature may have no profile yet.
    // Ordinary empty groups remain invalid calculation requests.
    bool allow_empty{};
    std::vector<ViewerPoint> reference_points;
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
    // Optional solid feature within source_id. Copies its original operand,
    // never the accumulated Body result at that feature's history boundary.
    std::string source_feature_id;
    bool operator==(const BodyHistoryScope&) const = default;
};

struct BoxOperation {
    std::string owner_id;
    BoxRequest box;
    BooleanOperation operation{BooleanOperation::Add};
};

// Authored material coordinates of a sheet creator. They are independent of
// OCCT face enumeration and survive cuts and later state operations.
struct SheetMaterialDefinition {
    enum class Kind { Plane, Cylinder, Cone, Twist };
    Kind kind{Kind::Plane};
    std::string owner_id, parent_owner_id;
    // Optional owning history feature for an authored subregion of a compound sheet.
    std::string feature_owner_id;
    Vec3 origin, along{1,0,0}, tangent{0,1,0}, radial{0,0,1};
    double radius{}, neutral_radius{}, angle{}, thickness{}, continuation{}, cone_half_angle{};
    // Twist uses an authored axial length and the corrected flat development.
    double formed_length{}, developed_length{}, signed_twist_angle{};
    double thickness_sign{-1};
    bool unfolded{};
    std::string curved_source_id, continuation_source_id;
    bool operator==(const SheetMaterialDefinition&) const = default;
};

struct SheetStateRequest {
    bool unfold{true};
    bool all{true};
    std::vector<std::string> owners;
    double tolerance{0.05};
    bool operator==(const SheetStateRequest&) const = default;
};

using PrimitiveRequest = std::variant<
    BoxRequest, CylinderRequest, SphereRequest, ConeRequest, PyramidRequest, WedgeRequest,
    ExtrusionRequest, RevolutionRequest, FeatureGroupRequest,
    Sweep3DRequest, StepRequest, FilletRequest, ChamferRequest, ShellRequest,
    ThreadSurfaceRequest, DrillPointRequest, SheetStateRequest>;

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
    SheetOperation sheet_operation{SheetOperation::None};
    double sheet_thickness{};
    std::optional<SheetMaterialDefinition> sheet_material;
    // One authored material region per FeatureGroup child, in attachment order.
    std::vector<SheetMaterialDefinition> sheet_regions;
    // A copy operand within the same Body, applied by the ordinary Boolean chain.
    std::optional<BodyHistoryScope> feature_copy;
};

// Calculated material-space trim, owned by the later cut, never by rewriting
// the source feature. Curves are ordered rational B-splines in surface UV.
struct SheetTrimCurve {
    std::string parent_owner, parent_key;
    int degree{};
    std::vector<std::array<double,2>> poles;
    std::vector<double> knots, weights;
    std::vector<int> multiplicities;
    bool reversed{};
};
struct SheetCutRegion {
    std::string cut_owner;
    FaceReference source;
    std::string surface_type;
    // Persist both in-surface directions: analytic frames may be left-handed.
    // The surface UV mapping cannot recover Y from axis cross X in that case.
    Vec3 origin, axis, x_axis, y_axis;
    double radius{}, semi_angle{}, thickness{};
    std::vector<std::vector<SheetTrimCurve>> loops;
};
struct BodyResult;
// An immutable calculated revision. Copying a document/occurrence shares its
// snapshot; a calculation or source-data update publishes a replacement value.
class BodySnapshot {
public:
    BodySnapshot();
    BodySnapshot(BodyResult value);
    [[nodiscard]] const BodyResult& get() const { return *value_; }
    [[nodiscard]] const BodyResult* operator->() const { return value_.get(); }
    operator const BodyResult&() const { return *value_; }
    [[nodiscard]] bool shares_with(const BodySnapshot& other) const { return value_ == other.value_; }
private:
    std::shared_ptr<const BodyResult> value_;
};

// Exact volume integrals calculated with the solid, never from display facets.
// Central inertia is row-major, about centroid, in model axes and mm^5.
struct VolumeIntegrals {
    Vec3 centroid;
    std::array<double,9> inertia{};
    bool operator==(const VolumeIntegrals&) const = default;
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
    std::optional<VolumeIntegrals> volume_integrals;
    // Exact area-weighted centre, calculated with surface area at the kernel boundary.
    std::optional<Vec3> surface_centroid;
    std::string source_fingerprint;
    // Opaque calculation snapshot. Only the solid kernel may consume it
    // during an explicit body calculation; viewer/reference code uses mesh.
    std::string kernel_shape;
    // Returned only by explicit STEP import so the owning Part container can
    // persist the source topology map with its parameters.
    std::vector<StepRequest::TopologyIdentity> imported_step_topology;
    std::vector<SheetCutRegion> sheet_cuts;
    // Present on a document result only. Branch snapshots retain their own
    // fingerprints, so changing another body does not invalidate this cache.
    std::map<std::string, std::vector<BodyResult>> body_boundaries;
    std::map<std::string, BodySnapshot> body_inputs;
    std::map<std::string, BodySnapshot> body_outputs;
};

inline BodySnapshot::BodySnapshot() {
    static const auto empty = std::make_shared<const BodyResult>();
    value_ = empty;
}
inline BodySnapshot::BodySnapshot(BodyResult value)
    : value_(std::make_shared<const BodyResult>(std::move(value))) {}

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
    // Only rigid-frame coordinates/angles normalize signed zero. Do not use
    // this for authored dimensional parameters: a signed zero can encode a side.
    const auto number_bits=[](double value) {
        return std::bit_cast<std::uint64_t>(value==0.0?0.0:value);
    };
    operation_count = std::min(operation_count, operations.size());
    u64(operation_count);
    for (std::size_t index = 0; index < operation_count; ++index) {
        const auto& operation = operations[index];
        if(operation.feature_copy) {
            HistoryOperation key;key.body=*operation.feature_copy;
            const auto copy_key=history_fingerprint({key},1);
            byte(0xc7);u64(copy_key.size());for(unsigned char c:copy_key)byte(c);
        }
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
        u64(static_cast<std::uint64_t>(operation.sheet_operation));
        u64(std::bit_cast<std::uint64_t>(operation.sheet_thickness));
        if(operation.sheet_material) {
            const auto& material=*operation.sheet_material;u64(1);u64(static_cast<unsigned>(material.kind));
            for(const auto* text:{&material.owner_id,&material.parent_owner_id,&material.curved_source_id,&material.continuation_source_id}) {
                u64(text->size());for(unsigned char c:*text)byte(c);
            }
            for(const auto vector:{material.origin,material.along,material.tangent,material.radial})
                for(double v:{vector.x,vector.y,vector.z})u64(std::bit_cast<std::uint64_t>(v));
            for(double v:{material.radius,material.neutral_radius,material.angle,material.thickness,material.continuation,material.cone_half_angle,material.thickness_sign})
                u64(std::bit_cast<std::uint64_t>(v));
            byte(material.unfolded);
        }
        if(!operation.sheet_regions.empty()) {
            byte(0xeb);u64(operation.sheet_regions.size());
            for(const auto& material:operation.sheet_regions) {
                HistoryOperation child;child.owner_id=material.owner_id;child.sheet_material=material;
                const auto digest=history_fingerprint({child},1);u64(digest.size());for(unsigned char c:digest)byte(c);
                u64(material.feature_owner_id.size());for(unsigned char c:material.feature_owner_id)byte(c);
            }
        }
        if (!operation.body.id.empty()) {
            for (const auto& text : {operation.body.id, operation.body.target_id, operation.body.source_id}) {
                u64(text.size());
                for (const unsigned char value : text) byte(value);
            }
            byte(static_cast<std::uint8_t>(operation.body.combination));
            for (const auto value : {operation.body.translation.x, operation.body.translation.y,
                    operation.body.translation.z, operation.body.rotation_degrees.x,
                    operation.body.rotation_degrees.y, operation.body.rotation_degrees.z})
                u64(number_bits(value));
        }
        if(operation.body.combination==BodyCombination::Mirror)
            for(double v:{operation.body.mirror_plane.point.x,operation.body.mirror_plane.point.y,operation.body.mirror_plane.point.z,
                    operation.body.mirror_plane.normal.x,operation.body.mirror_plane.normal.y,operation.body.mirror_plane.normal.z})
                u64(number_bits(v));
        if(!operation.body.source_feature_id.empty()) {
            u64(operation.body.source_feature_id.size());
            for(const unsigned char value:operation.body.source_feature_id)byte(value);
        }
        if(operation.body.combination==BodyCombination::Pattern) {
            const auto& p=operation.body.pattern;u64(p.count);byte(p.circular);byte(p.full_circle);
            u64(std::bit_cast<std::uint64_t>(p.angle_degrees));
            for(double v:{p.origin.x,p.origin.y,p.origin.z,p.axis.x,p.axis.y,p.axis.z})
                u64(number_bits(v));
            for (const auto& d : p.linear) {
                u64(d.local_axis + 1);u64(d.count);u64(d.reverse_count);byte(static_cast<std::uint8_t>(d.distribution));
                u64(std::bit_cast<std::uint64_t>(d.spacing));
                for (double v : {d.direction.x,d.direction.y,d.direction.z}) u64(number_bits(v));
            }
        }
        byte(static_cast<std::uint8_t>(operation.primitive.index()));
        std::visit([&](const auto& primitive) {
            using Request = std::decay_t<decltype(primitive)>;
            if constexpr (std::is_same_v<Request, BoxRequest>) {
                for (const double value : {
                        primitive.length, primitive.width, primitive.height})
                    u64(std::bit_cast<std::uint64_t>(value));
                for (const double value : {
                        primitive.translation.x, primitive.translation.y,
                        primitive.translation.z, primitive.rotation_degrees.x,
                        primitive.rotation_degrees.y, primitive.rotation_degrees.z}) {
                    u64(number_bits(value));
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
                byte(primitive.centerlines.origin_enabled);byte(primitive.centerlines.centroid_enabled);
                for(const auto& text:{primitive.centerlines.origin_id,primitive.centerlines.profile_id}){u64(text.size());for(unsigned char c:text)byte(c);}
                for(double value:{primitive.centerlines.origin.x,primitive.centerlines.origin.y,primitive.centerlines.origin.z,primitive.centerlines.normal.x,primitive.centerlines.normal.y,primitive.centerlines.normal.z})u64(std::bit_cast<std::uint64_t>(value));

                byte(primitive.sheet_cut);
                byte(primitive.sheet_cut_clearance);
                if(primitive.sheet_cut)u64(std::bit_cast<std::uint64_t>(primitive.sheet_cut_tolerance));
                if(primitive.surface_result) {byte(0xf1);for(const auto c:primitive.open_profile_end_id)byte(c);}
                // Exact profile bounds reject an inclined plane crossing away from seam vertices.
                if (primitive.extent == ExtrusionRequest::Extent::UpToPlane) byte(2);
                if (primitive.extent == ExtrusionRequest::Extent::UpToPlane ||
                    primitive.extent == ExtrusionRequest::Extent::UpToSurface || primitive.reverse_limit)
                    for (const unsigned char value : std::string_view("original-extrusion-limits-v1")) byte(value);
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
                                        u64(exact_curve.knots.size());
                                        for (double v : exact_curve.knots) u64(std::bit_cast<std::uint64_t>(v));
                                        u64(exact_curve.weights.size());
                                        for (double v : exact_curve.weights) u64(std::bit_cast<std::uint64_t>(v));
                                        u64(exact_curve.degree);
                                        byte(exact_curve.interpolating);
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
                if (primitive.symmetric_limit) for(const unsigned char c:std::string_view("symmetric-extrusion-limit-v1"))byte(c);
                if (primitive.mirror_forward_limit) for(const unsigned char c:std::string_view("mirrored-forward-limit-v1"))byte(c);
                if (primitive.reverse_limit) {
                    for (const unsigned char c : std::string_view("extrusion-reverse-limit-v1")) byte(c);
                    const auto& limit=*primitive.reverse_limit;
                    byte(limit.planar);byte(limit.datum);
                    for(const auto& text:{limit.reference.owner_id,limit.reference.semantic_key,limit.reference.instance_path}) {
                        u64(text.size());for(const unsigned char c:text)byte(c);
                    }
                    for(const auto p:{limit.origin,limit.normal})for(const double v:{p.x,p.y,p.z})u64(std::bit_cast<std::uint64_t>(v));
                    u64(limit.triangles.size());for(const auto p:limit.triangles)for(const double v:{p.x,p.y,p.z})u64(std::bit_cast<std::uint64_t>(v));
                }
                if (primitive.wall) {
                    for (const unsigned char c : std::string_view("profile-wall-v1")) byte(c);
                    u64(std::bit_cast<std::uint64_t>(primitive.wall->first_offset));
                    u64(std::bit_cast<std::uint64_t>(primitive.wall->second_offset));
                    u64(primitive.wall->end_point_id.size());
                    for (const unsigned char c : primitive.wall->end_point_id) byte(c);
                }
            } else if constexpr (std::is_same_v<Request, FeatureGroupRequest>) {
                byte(primitive.allow_empty);
                u64(primitive.reference_points.size());
                for(const auto& point:primitive.reference_points) {
                    for(const auto* text:{&point.reference.owner_id,&point.reference.semantic_key,&point.reference.instance_path,&point.display_owner_id}) {
                        u64(text->size());for(const unsigned char value:*text)byte(value);
                    }
                    for(double value:{point.position.x,point.position.y,point.position.z})u64(std::bit_cast<std::uint64_t>(value));
                }
                u64(primitive.children.size());
                u64(primitive.axes.size());
                for (const auto& axis : primitive.axes) {
                    for (const auto* text : {&axis.reference.owner_id,&axis.reference.semantic_key,&axis.reference.instance_path,&axis.label}) {
                        u64(text->size()); for (const unsigned char value : *text) byte(value);
                    }
                    for (const double value : {axis.point.x,axis.point.y,axis.point.z,
                            axis.direction.x,axis.direction.y,axis.direction.z,axis.display_length})
                        u64(std::bit_cast<std::uint64_t>(value));
                }
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
                u64(primitive.reference_profiles.size());
                for(const auto& profile:primitive.reference_profiles) {
                    HistoryOperation reference;reference.owner_id=operation.owner_id;
                    std::visit([&](const auto& value){reference.primitive=value;},profile);
                    const auto key=history_fingerprint({reference},1);
                    u64(key.size());for(const unsigned char value:key)byte(value);
                }
            } else if constexpr (std::is_same_v<Request, RevolutionRequest>) {
                byte(primitive.centerlines.origin_enabled);byte(primitive.centerlines.centroid_enabled);
                for(const auto& text:{primitive.centerlines.origin_id,primitive.centerlines.profile_id}){u64(text.size());for(unsigned char c:text)byte(c);}
                for(double value:{primitive.centerlines.origin.x,primitive.centerlines.origin.y,primitive.centerlines.origin.z,primitive.centerlines.normal.x,primitive.centerlines.normal.y,primitive.centerlines.normal.z})u64(std::bit_cast<std::uint64_t>(value));

                if(primitive.surface_result) {byte(0xf1);for(const auto c:primitive.open_profile_end_id)byte(c);}
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
                                        u64(exact_curve.knots.size());
                                        for (double v : exact_curve.knots) u64(std::bit_cast<std::uint64_t>(v));
                                        u64(exact_curve.weights.size());
                                        for (double v : exact_curve.weights) u64(std::bit_cast<std::uint64_t>(v));
                                        u64(exact_curve.degree);
                                        byte(exact_curve.interpolating);
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
                if (primitive.wall) {
                    for (const unsigned char c : std::string_view("profile-wall-v1")) byte(c);
                    u64(std::bit_cast<std::uint64_t>(primitive.wall->first_offset));
                    u64(std::bit_cast<std::uint64_t>(primitive.wall->second_offset));
                    u64(primitive.wall->end_point_id.size());
                    for (const unsigned char c : primitive.wall->end_point_id) byte(c);
                }
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
                                        u64(exact_curve.knots.size());
                                        for (double v : exact_curve.knots) u64(std::bit_cast<std::uint64_t>(v));
                                        u64(exact_curve.weights.size());
                                        for (double v : exact_curve.weights) u64(std::bit_cast<std::uint64_t>(v));
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
                if(primitive.smooth_loft) {byte(0xe9);byte(1);}
                if(primitive.fixed_section_frames) {byte(0xea);byte(1);}
                if(primitive.attachment_endpoints) {byte(0xeb);byte(1);}
                u64(std::bit_cast<std::uint64_t>(primitive.linear_tolerance));
                byte(primitive.thin);
                u64(std::bit_cast<std::uint64_t>(primitive.thin_first));u64(std::bit_cast<std::uint64_t>(primitive.thin_second));
            } else if constexpr (std::is_same_v<Request, StepRequest>) {
                u64(primitive.source_path.size());
                for (const unsigned char value : primitive.source_path) byte(value);
                u64(primitive.component_path.size());
                for (const unsigned char value : primitive.component_path) byte(value);
                for (const double value : {primitive.translation.x, primitive.translation.y, primitive.translation.z,
                        primitive.rotation_degrees.x, primitive.rotation_degrees.y, primitive.rotation_degrees.z})
                    u64(std::bit_cast<std::uint64_t>(value));
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
                byte(primitive.end_plane_reference.has_value());
                if (primitive.end_plane_reference)
                    for (const auto* text : {&primitive.end_plane_reference->owner_id,
                            &primitive.end_plane_reference->semantic_key,&primitive.end_plane_reference->instance_path}) {
                        u64(text->size());for (const unsigned char ch : *text) byte(ch);
                    }
                // Opening result revision: validate and resolve original end faces.
                byte(3);
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
            } else if constexpr (std::is_same_v<Request, SheetStateRequest>) {
                u64(2);byte(primitive.unfold);byte(primitive.all);
                u64(std::bit_cast<std::uint64_t>(primitive.tolerance));
                u64(primitive.owners.size());for(const auto& owner:primitive.owners){u64(owner.size());for(unsigned char c:owner)byte(c);}
            } else if constexpr (std::is_same_v<Request, DrillPointRequest>) {
                // Source-parent topology replaces selection-order identities.
                // Explicit calculation must not reuse the former derived cache.
                u64(1); // Drill-point topology schema.
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
                // Do not reuse derived Shell topology which treated unowned
                // spherical seams/poles as persistent reference entities.
                u64(1); // Shell topology schema.
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
                    // R1 is mapped from each persisted request to all matching
                    // runtime edge uses. Earlier multi-route caches are unsafe.
                    u64(1); // Fillet calculation schema.
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
