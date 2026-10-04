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
#include <span>
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
    int role{}; // glyph, leader, arrow, shelf, triangle, dot, all-around circle
    bool perpendicular{true};
    bool short_shelf{};
    double shelf_length{3.};
    bool framed{}; // Shelf joins the midpoint of the nearest frame side.
    bool all_around{}; // Weld circle at the leader/reference-line junction.
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
    // Transient end-condition / omitted-wall preview only; not serialized.
    // Symbolism must not change roles consumed by sheet-cut footprint estimation.
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
    // Numeric annotation units are independent of geometry (always mm/degrees)
    // and of authored suffix text. Empty selects the canonical quantity unit.
    std::string value_unit;
    bool keep_trailing_zeros{};
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
    double draft_angle_degrees{};
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
    struct Twist {
        double length{100},angle_degrees{90};
        bool smooth{true};
        // Disconnected regions of one profile share its combined area centroid.
        std::optional<Vec3> axis_point;
        ProfileCenterlines centerlines;
    };
    // Authored constant profile, rotated about its exact area centroid during
    // explicit calculation. Approximation stations never define identity.
    std::optional<Twist> twist;
    // Datums prepared by explicit Twist expansion, including its straight state.
    std::optional<ViewerReferenceGeometry> profile_references;
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
    // Authored Helical rotation datum, independent of the swept spine.
    // The signed direction runs from the base centre to the guide end height.
    std::optional<ViewerAxis> rotation_axis;
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
    double bend_line_end_length{}; // Zero keeps the complete development axis.
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
    struct ReferencePlane { FaceReference reference; std::array<Vec3,4> corners; };
    // Authored planar datums; no material and no OCCT-derived identity.
    std::vector<ReferencePlane> reference_planes;
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
enum class BodyCombination { Separate, Add, Subtract, Intersect, Mirror, Pattern, Scale };
struct BodyResult;
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
    // Body suppression excludes its result, not the geometry needed by children.
    bool result_suppressed{};
    double scale_factor{1};
    Vec3 scale_center;
    std::shared_ptr<const BodyResult> linked_body;
    bool operator==(const BodyHistoryScope&) const = default;
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

struct SolidStateRequest {
    bool restore{};
    bool all{true};
    double coefficient{1};
    std::vector<std::string> owners;
    bool operator==(const SolidStateRequest&) const = default;
};

enum class SurfaceContinuity { G0, G1, G2 };
struct BoundarySurfaceConstraint {
    std::optional<EdgeReference> edge;
    SurfaceContinuity continuity{SurfaceContinuity::G0};
    std::optional<FaceReference> support;
    bool support_reversed{};
};
struct BoundarySurfaceRequest {
    // Authored open chains, in perimeter order. Exact curve data and
    // source point/curve identities use the same packet as Sketch profiles.
    std::vector<ExtrusionRequest> boundaries{4};
    std::vector<std::string> source_owners{4};
    // Empty means unconstrained authored curves. Otherwise one row per boundary.
    // Original source topology is resolved only during explicit calculation.
    std::vector<BoundarySurfaceConstraint> constraints;
    std::string region_id;
    double tolerance{0.001};
    static constexpr bool surface_result=true;
    double angular_tolerance{0.001*3.14159265358979323846/180}; // radians
    double curvature_tolerance{1e-5}; // inverse mm
};
struct SurfaceSewingRequest {
    // Original native face identities, resolved at the operation's input
    // boundary. Sewing creates a shell and never adds material or caps.
    std::vector<FaceReference> faces;
    double tolerance{0.001};
    static constexpr bool surface_result=true;
};
struct SurfaceIntersectionRequest {
    // Both bounded original faces are native parents of each derived curve.
    // This reference operation leaves the calculated body unchanged.
    std::array<FaceReference,2> faces;
    double tolerance{0.001};
};

struct SurfaceTrimTool {
    EdgeReference reference;
    bool face{};
    bool operator==(const SurfaceTrimTool&)const=default;
};
struct SurfaceTrimRequest {
    FaceReference target;
    std::vector<SurfaceTrimTool> tools;
    Vec3 seed;
    double tolerance{0.001};
    // Validation-only native intent, not another geometry input.
    std::string expected_region_key;
    static constexpr bool surface_result=true;
};

using PrimitiveRequest = std::variant<
    ExtrusionRequest, RevolutionRequest, FeatureGroupRequest,
    Sweep3DRequest, StepRequest, FilletRequest, ChamferRequest, ShellRequest,
    ThreadSurfaceRequest, DrillPointRequest, SheetStateRequest, BoundarySurfaceRequest,
    SolidStateRequest, SurfaceSewingRequest, SurfaceIntersectionRequest, SurfaceTrimRequest>;

// Transient material-frame transfer for a source end-section attachment.
// No container/reference definition or native document format changes.
struct SolidStateFaceTransfer {
    std::string owner_id;
    std::string source_owner_id;
};

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
    // Transient, document-resolved geometry at this solid-state boundary.
    // Earlier authored operations remain unchanged. Entries contain no states
    // or nested replay data; their actual geometry participates in the cache key.
    std::shared_ptr<const std::vector<HistoryOperation>> solid_state_placements;
    std::vector<SolidStateFaceTransfer> solid_state_face_transfers;
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
    // Original-source evaluation geometry at each solid-state boundary. These
    // immutable packets are not selectable topology and never enter mesh.
    // Native persistence retains them for reference resolution without OCCT.
    std::map<std::string,std::shared_ptr<const ViewerReferenceGeometry>> solid_state_reference_views;
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


[[nodiscard]] std::string history_fingerprint(
    const std::vector<HistoryOperation>& operations, std::size_t operation_count);
// All prefixes of the same immutable operation sequence, including the empty
// prefix. Each uses exactly the existing byte encoding and count-dependent hash.
[[nodiscard]] std::vector<std::string> history_fingerprints(
    const std::vector<HistoryOperation>& operations);

class GeometryKernel {
public:
    virtual ~GeometryKernel() = default;
    [[nodiscard]] virtual std::string name() const = 0;
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
