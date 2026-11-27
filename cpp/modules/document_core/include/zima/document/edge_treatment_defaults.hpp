#pragma once
#include <string_view>

namespace zima::document {
// Creation defaults only; changing display units never resizes authored values.
[[nodiscard]] inline double default_edge_treatment_size_mm(std::string_view length_unit) {
    return length_unit == "in" ? 0.040 * 25.4 : 1.0;
}
}
