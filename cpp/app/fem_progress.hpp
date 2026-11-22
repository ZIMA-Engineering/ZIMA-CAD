#pragma once

#include <cstddef>
#include <functional>

namespace zima::app {
enum class FemProgressPhase {
    PreparingGeometry, GeneratingMesh, CheckingMesh, OptimizingMesh, ReadingMesh,
    AssemblingSystem, ApplyingConditions, SolvingSystem, ComputingResults, PreparingDisplay
};

struct FemProgress {
    FemProgressPhase phase;
    std::size_t completed{}, total{};
};

// Called on the calculation thread. Observers must not access UI objects.
// A zero total denotes a phase without a known amount of remaining work.
using FemProgressCallback = std::function<void(const FemProgress&)>;

inline void report_fem_progress(const FemProgressCallback& callback,
        FemProgressPhase phase, std::size_t completed = 0, std::size_t total = 0) {
    if (callback && (!total || !completed || completed == total || completed % 128 == 0))
        callback({phase, completed, total});
}
}
