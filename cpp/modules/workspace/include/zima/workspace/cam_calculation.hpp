#pragma once
#include <zima/workspace/cam_operations.hpp>
#include <functional>

namespace zima::workspace {
// Explicit private calculation. No document mutation or live viewer publication.
[[nodiscard]] document::CamCalculated calculate_cam(const CamCalculationInput&,
    const std::function<void(std::size_t,std::size_t)>& progress={});
}
