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
struct HistoryContainer;
enum class ThreadStandard;
// Shared catalog-selection rule for a caller-owned opening draft, without
// calculation: preserve custom bore diameter and accommodate blind runout.
void select_opening_thread_size(HistoryContainer&,ThreadStandard,const ThreadCatalogSize&);
void select_shaft_thread_size(HistoryContainer&,ThreadStandard,const ThreadCatalogSize&);
} // namespace zima::document
