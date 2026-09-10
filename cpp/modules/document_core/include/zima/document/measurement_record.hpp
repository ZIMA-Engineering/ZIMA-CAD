#pragma once
#include <zima/kernel/measurement.hpp>
namespace zima::document {
[[nodiscard]] std::string serialize_measurements(const std::vector<zima::kernel::SavedMeasurement>&);
[[nodiscard]] std::vector<zima::kernel::SavedMeasurement> parse_measurements(const std::string&);
}