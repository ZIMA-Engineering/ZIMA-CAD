#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <TopoDS_Shape.hxx>

namespace zima::kernel {
// Kernel-only materialization, for explicit geometry calculation/export.
[[nodiscard]] TopoDS_Shape read_calculated_body_shape(const BodyResult& body);
}
