#pragma once
#include <atomic>
#include <cstdint>

namespace zima::kernel {
// Process-local instrumentation, including rejected calculations. Tests take
// differences around interaction/confirmation; this is never persisted state.
inline std::atomic<std::uint64_t> body_calculation_count{};
// Complete physical integrations, excluding reuse of current exact inputs.
inline std::atomic<std::uint64_t> body_property_integration_count{};
// Counts branch jobs actually entered on async workers, not merely scheduled.
inline std::atomic<std::uint64_t> parallel_sheet_branch_jobs{};
inline std::atomic<std::uint64_t> direct_sheet_tool_calculation_count{};
inline std::atomic<std::uint64_t> direct_sheet_tool_reuse_count{};
inline std::atomic<std::uint64_t> direct_sheet_material_reuse_count{};
}
