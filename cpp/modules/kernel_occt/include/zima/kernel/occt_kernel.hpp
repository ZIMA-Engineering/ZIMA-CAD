#pragma once

#include <zima/kernel/geometry_kernel.hpp>

#include <memory>
#include <span>
#include <set>
#include <string>
#include <vector>

namespace zima::kernel {

struct SolidStraighteningPlan;
class StepSourceDocument;
struct FacePlaneIntersectionRequest;

// Controls which calculated history rows must publish a complete BodyResult.
// The native Boolean/topology chain is always evaluated in full; this only
// defers large viewer/persistence packets that no current dependency consumes.
struct HistoryOutputPolicy {
    bool final_boundary_first{};
    // Owners whose input body is queried after calculation (for example a
    // Sketch body-edge projection or exact face intersection). Their immediate
    // preceding boundary must remain complete.
    std::set<std::string> input_boundary_consumers;
};

enum class AggregatePropertyDemand {
    All,
    Final,
    Deferred,
    // Preserve the established immediate path for cheap analytic shapes while
    // leaving adaptive surface integration to background analysis.
    DeferredAdaptive
};
// Explicit calculation policy only; no viewer/selection callbacks install it.
class AggregatePropertyScope final {
public:
    explicit AggregatePropertyScope(AggregatePropertyDemand demand);
    ~AggregatePropertyScope();
    AggregatePropertyScope(const AggregatePropertyScope&) = delete;
    AggregatePropertyScope& operator=(const AggregatePropertyScope&) = delete;
private:
    AggregatePropertyDemand previous_;
};

struct FrozenStepComponent {
    std::shared_ptr<const std::string> brep;
    ImmutableVector<StepRequest::TopologyIdentity> topology;
};

// Derived native support data for an explicitly selected Fillet/Chamfer edge.
// It is calculation-only inspection state: none of it is persisted or used as
// topology identity. Stable references remain the sole parent contract.
enum class OcctEdgeTreatmentCurveKind {
    Line,
    Circle,
    Ellipse,
    Hyperbola,
    Parabola,
    Bezier,
    BSpline,
    Offset,
    Other
};

enum class OcctEdgeTreatmentSurfaceKind {
    Plane,
    Cylinder,
    Cone,
    Sphere,
    Torus,
    Bezier,
    BSpline,
    Revolution,
    Extrusion,
    Offset,
    Other
};

struct OcctEdgeTreatmentSupportFace {
    // One runtime face can retain several valid ancestry aliases. Keeping all
    // of them prevents inspection from choosing a newer parent arbitrarily.
    std::vector<FaceReference> references;
    OcctEdgeTreatmentSurfaceKind kind{
        OcctEdgeTreatmentSurfaceKind::Other};
    bool reversed{};
};

struct OcctEdgeTreatmentSupportSegment {
    EdgeReference edge;
    OcctEdgeTreatmentCurveKind curve_kind{
        OcctEdgeTreatmentCurveKind::Other};
    std::vector<OcctEdgeTreatmentSupportFace> adjacent_faces;
    // OCCT continuity rank: 0=C0, 1=G1/C1, 2=G2/C2, 3=C3, 4=CN.
    unsigned continuity_order{};
    // Angle between oriented outward face normals at the edge midpoint.
    // Empty for a non-manifold edge or a degenerate support derivative.
    std::optional<double> outward_normal_angle_degrees;
};

struct OcctEdgeTreatmentSupportInspection {
    std::vector<OcctEdgeTreatmentSupportSegment> segments;
    double native_decode_ms{};
    double adjacency_ms{};
};

// Ordinary modeling retains exact native face locators instead of integrating
// every face for a later inspector. Explicit analysis can request eager values.
class FaceMeasurementScope final {
public:
    explicit FaceMeasurementScope(bool eager);
    ~FaceMeasurementScope();
    FaceMeasurementScope(const FaceMeasurementScope&) = delete;
    FaceMeasurementScope& operator=(const FaceMeasurementScope&) = delete;
private:
    bool previous_;
};

class OcctKernel final : public GeometryKernel {
public:
    OcctKernel();
    ~OcctKernel() override;
    OcctKernel(const OcctKernel&) = delete;
    OcctKernel& operator=(const OcctKernel&) = delete;
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] bool has_current_calculation(const std::vector<BodyResult>& boundaries) const;
    // Integrate the already calculated solid; never rebuild geometry or references.
    void ensure_body_properties(BodyResult& body) const;
    [[nodiscard]] std::vector<std::optional<double>> measure_face_areas(
        const BodyResult& body, std::span<const FaceReference> references) const;
    // Decode an already calculated Body once and classify the exact native
    // supports of the requested persisted edges. This never recalculates the
    // Body, creates geometry, changes identity or creates an undo transaction.
    [[nodiscard]] OcctEdgeTreatmentSupportInspection
        inspect_edge_treatment_supports(
            const BodyResult&,std::span<const EdgeReference>) const;
    // Explicit reference calculation, never an ordinary picking/hover query.
    // Returns exact curves/points with native ancestry; source stays read-only.
    [[nodiscard]] ViewerReferenceGeometry intersect_face_plane(
        const BodyResult&,const FacePlaneIntersectionRequest&) const;
    // Explicit dependency calculation only. Retain the selected native face
    // and its existing identity without copying a source Part or tessellating.
    [[nodiscard]] BodyResult capture_reference_face(const BodyResult&,const FaceReference&) const;
    [[nodiscard]] BodyResult capture_reference_face_history(
        const std::vector<BodyResult>&,const FaceReference&) const;
    // Explicit, private Feature calculation for an editing preview. Reuses
    // ordinary primitive/limit algorithms without applying the Body Boolean
    // or evaluating another history row. Sources remain immutable.
    [[nodiscard]] std::vector<ViewerEdge> calculate_feature_preview(
        HistoryOperation,const BodyResult& references,
        KernelRigidPlacement frame_to_document={},const BodyResult& local_input={},
        const std::vector<OrderedUpToPatch>& external_sources={}) const;
    // The accepted Assembly cutter uses the same source/bounds preparation as
    // the exact draft, and additionally retains its complete native solid.
    [[nodiscard]] BodyResult calculate_feature_tool(
        HistoryOperation,const BodyResult& references,KernelRigidPlacement frame_to_document,
        const BodyResult& local_input,const std::vector<OrderedUpToPatch>& external_sources) const;
    // Completes one independently prepared contiguous Add block with one
    // exterior-boundary calculation. This experimental entry point does not
    // alter ordinary history evaluation or native persistence.
    [[nodiscard]] BodyResult calculate_add_block(
        const BodyResult& input,std::span<const BodyResult> tools,
        std::span<const std::size_t> tool_history_indices,
        std::span<const std::string> history_owners,
        double boolean_tolerance,double mesh_deflection,
        const std::string& source_fingerprint = {},
        std::span<const std::string> history_fingerprints = {},
        bool publish_result = true) const;
    // Completes one contiguous set of independent subtractive tools with one
    // exact Cut while retaining authored topology ancestry. Experimental: it
    // does not alter ordinary history evaluation or native persistence.
    [[nodiscard]] BodyResult calculate_subtract_block(
        const BodyResult& input,std::span<const BodyResult> tools,
        std::span<const std::size_t> tool_history_indices,
        std::span<const std::string> history_owners,
        double boolean_tolerance,double mesh_deflection,
        const std::string& source_fingerprint = {},
        bool publish_result = true) const;
    [[nodiscard]] SolidStraighteningPlan prepare_straightening(
        const RevolutionRequest& source, double coefficient = 1.0) const;
    [[nodiscard]] SolidStraighteningPlan prepare_straightening(
        const Sweep3DRequest& source, double coefficient = 1.0) const;
    [[nodiscard]] SolidStraighteningPlan prepare_straightening(
        const FeatureGroupRequest& source, double coefficient = 1.0) const;
    [[nodiscard]] std::vector<BodyResult> evaluate_history(
        const std::vector<HistoryOperation>& operations) const override;
    [[nodiscard]] std::vector<BodyResult> evaluate_history_incremental(
        const std::vector<HistoryOperation>& operations,
        const std::vector<BodyResult>& previous_boundaries) const override;
    // Explicit document calculation preserves valid prefixes and independent bodies.
    [[nodiscard]] std::vector<BodyResult> evaluate_history_recovering(
        const std::vector<HistoryOperation>& operations,
        const std::vector<BodyResult>& previous_boundaries = {},
        const HistoryOutputPolicy& output_policy = {}) const;
    [[nodiscard]] BodyResult compound_bodies(
        const std::vector<PlacedBody>& bodies) const override;
    [[nodiscard]] BodyResult import_iges(const std::string& path,
        const std::string& owner_id, double mesh_deflection = 0.1) const;
    [[nodiscard]] std::vector<BodyResult> import_step_components(
        const std::vector<StepRequest>& requests, double mesh_deflection = 0.1) const;
    // Capture native source geometry and identity bindings for the ordinary
    // history evaluation, which prepares the final viewer and properties once.
    [[nodiscard]] std::vector<FrozenStepComponent> freeze_step_components(
        const std::vector<StepRequest>& requests,
        const StepSourceDocument* source = nullptr) const;
    [[nodiscard]] BodyResult subtract_bodies(
        const BodyResult& target,
        const BodyResult& cutter,
        Vec3 target_translation,
        Vec3 target_rotation_degrees,
        double linear_tolerance = 0.001, double mesh_deflection = 0.1) const;
    [[nodiscard]] BodyResult scale_body(const BodyResult& source,double factor,Vec3 center,const std::string& owner) const;
    [[nodiscard]] BodyResult mirror_body(const BodyResult& source,MirrorPlane plane,
        const std::string& owner_id={},Vec3 source_translation={},Vec3 source_rotation={}) const override;
    [[nodiscard]] BodyResult pattern_body(const BodyResult& source,const PatternRequest& pattern,
        const std::string& owner_id={},Vec3 source_translation={},Vec3 source_rotation={},bool occurrences=false) const override;
    void export_step(
        const std::vector<PlacedBody>& bodies, const std::string& path) const;
    void export_step(const StepProduct& root, const std::string& path) const;
    void export_iges(const StepProduct& root, const std::string& path) const;
    void export_iges(const std::vector<PlacedBody>& bodies, const std::string& path) const;
    void export_stl(
        const std::vector<PlacedBody>& bodies, const std::string& path) const;

private:
    [[nodiscard]] BodyResult calculate_feature_geometry(HistoryOperation,const BodyResult&,
        KernelRigidPlacement,const BodyResult&,const std::vector<OrderedUpToPatch>&,bool wire_only) const;
    struct NativeFaceSource;
    [[nodiscard]] NativeFaceSource native_face_source(const BodyResult&,const FaceReference&) const;
    struct HistoryContext;
    [[nodiscard]] std::vector<BodyResult> evaluate_flat_history(
        const std::vector<HistoryOperation>& operations,
        const std::vector<BodyResult>& previous_boundaries, HistoryContext& context) const;
    [[nodiscard]] std::optional<std::vector<BodyResult>>
        evaluate_flat_history_batched(
            const std::vector<HistoryOperation>& operations,
            HistoryContext& context) const;
    [[nodiscard]] std::vector<BodyResult> evaluate_flat_history_recovering(
        const std::vector<HistoryOperation>& operations,
        const std::vector<BodyResult>& previous_boundaries, HistoryContext& context) const;
    [[nodiscard]] std::vector<BodyResult> evaluate_body_histories(
        const std::vector<HistoryOperation>& operations,
        const std::vector<BodyResult>& previous_boundaries, bool recover_errors = false,
        const HistoryOutputPolicy& output_policy = {}) const;
    struct LiveCache;
    std::unique_ptr<LiveCache> live_cache_;
};

}  // namespace zima::kernel
