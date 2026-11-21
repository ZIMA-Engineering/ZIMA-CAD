#pragma once
#include <zima/document/fem_condition.hpp>
#include <zima/kernel/geometry_kernel.hpp>

namespace zima::document {
// Presentation and reference availability only. These sampled locations are
// never mesher bindings, integration weights or replacements for native curves.
[[nodiscard]] bool fem_region_reference_available(const FemRegionReference&,FemRegionKind,const kernel::ViewerMesh&);
[[nodiscard]] kernel::ViewerMesh fem_condition_presentation(const FemCondition&,const kernel::ViewerMesh& source,double symbol_size=10);
} // namespace zima::document
