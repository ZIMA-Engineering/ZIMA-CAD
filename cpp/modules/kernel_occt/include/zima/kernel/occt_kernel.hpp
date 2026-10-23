#pragma once

#include <zima/kernel/geometry_kernel.hpp>

#include <memory>

namespace zima::kernel {

struct SolidStraighteningPlan;
class StepSourceDocument;
struct FacePlaneIntersectionRequest;

enum class AggregatePropertyDemand { All, Final, Deferred };
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
    // Explicit reference calculation, never an ordinary picking/hover query.
    // Returns exact curves/points with native ancestry; source stays read-only.
    [[nodiscard]] ViewerReferenceGeometry intersect_face_plane(
        const BodyResult&,const FacePlaneIntersectionRequest&) const;
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
        const std::vector<BodyResult>& previous_boundaries = {}) const;
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
    struct NativeFaceSource;
    [[nodiscard]] NativeFaceSource native_face_source(const BodyResult&,const FaceReference&) const;
    struct HistoryContext;
    [[nodiscard]] std::vector<BodyResult> evaluate_flat_history(
        const std::vector<HistoryOperation>& operations,
        const std::vector<BodyResult>& previous_boundaries, HistoryContext& context) const;
    [[nodiscard]] std::vector<BodyResult> evaluate_flat_history_recovering(
        const std::vector<HistoryOperation>& operations,
        const std::vector<BodyResult>& previous_boundaries, HistoryContext& context) const;
    [[nodiscard]] std::vector<BodyResult> evaluate_body_histories(
        const std::vector<HistoryOperation>& operations,
        const std::vector<BodyResult>& previous_boundaries, bool recover_errors = false) const;
    struct LiveCache;
    std::unique_ptr<LiveCache> live_cache_;
};

}  // namespace zima::kernel
