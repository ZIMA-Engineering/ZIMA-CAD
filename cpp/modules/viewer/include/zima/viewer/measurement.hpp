#pragma once
#include <zima/measurement/measurement.hpp>
#include <zima/viewer/picking.hpp>
namespace zima::viewer {
[[nodiscard]] std::optional<zima::kernel::MeasurementReference> measurement_reference(const ViewerCandidate&);
}
