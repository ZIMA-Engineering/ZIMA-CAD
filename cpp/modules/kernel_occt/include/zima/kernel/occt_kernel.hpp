#pragma once

#include <zima/kernel/geometry_kernel.hpp>

namespace zima::kernel {

class OcctKernel final : public GeometryKernel {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] BodyResult make_box(const BoxRequest& request) const override;
    [[nodiscard]] BodyResult evaluate_boxes(
        const std::vector<BoxOperation>& operations) const override;
    [[nodiscard]] std::vector<BodyResult> evaluate_box_boundaries(
        const std::vector<BoxOperation>& operations) const override;
    [[nodiscard]] std::vector<BodyResult> evaluate_history(
        const std::vector<HistoryOperation>& operations) const override;
    [[nodiscard]] BodyResult compound_bodies(
        const std::vector<PlacedBody>& bodies) const override;
    [[nodiscard]] std::vector<BodyResult> import_step_components(
        const std::vector<StepRequest>& requests) const;
    [[nodiscard]] BodyResult subtract_bodies(
        const BodyResult& target,
        const BodyResult& cutter,
        Vec3 target_translation,
        Vec3 target_rotation_degrees) const;
    void export_step(
        const std::vector<PlacedBody>& bodies, const std::string& path) const;
    void export_stl(
        const std::vector<PlacedBody>& bodies, const std::string& path) const;
};

}  // namespace zima::kernel
