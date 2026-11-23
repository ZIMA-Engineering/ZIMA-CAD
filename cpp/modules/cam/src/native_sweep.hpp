#pragma once
#include <zima/cam/program.hpp>
#include <TopoDS_Shape.hxx>

namespace zima::cam::native {
// Calculation-only native shapes. No OCCT topology becomes persistent identity.
[[nodiscard]] TopoDS_Shape tool_shape(const Tool&,Point tip,bool cutting);
[[nodiscard]] TopoDS_Shape swept_tool(const Tool&,const Motion&,bool cutting,double tolerance_mm=1e-7);
[[nodiscard]] double volume(const TopoDS_Shape&);
}
