#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace zima::document {
struct ThreadCatalogSize {
    std::string designation;
    double nominal_diameter{}, pitch{}, internal_root_diameter{}, external_root_diameter{};
    bool preferred{};
};
// The same bundled TSV data used by the CAD's thread selection controls.
// All lengths are millimetres, including Whitworth and pipe catalog entries.
// No GUI, installed files, or active document are required. Records are immutable.
[[nodiscard]] const std::vector<ThreadCatalogSize>& thread_catalog(std::string_view standard);
} // namespace zima::document
