#pragma once
#include <atomic>
#include <cstdint>

namespace zima::kernel {
// Process-local instrumentation, including rejected calculations. Tests take
// differences around interaction/confirmation; this is never persisted state.
inline std::atomic<std::uint64_t> body_calculation_count{};
}
