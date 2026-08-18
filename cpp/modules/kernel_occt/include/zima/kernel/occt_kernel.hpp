#pragma once

#include <zima/kernel/geometry_kernel.hpp>

namespace zima::kernel {

class OcctKernel final : public GeometryKernel {
public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] BodyResult make_box(const BoxRequest& request) const override;
    [[nodiscard]] BodyResult evaluate_boxes(
        const std::vector<BoxOperation>& operations) const override;
};

}  // namespace zima::kernel
