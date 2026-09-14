#pragma once
#include <zima/workspace/sweep_operations.hpp>
namespace zima::workspace {
// An owned Point stays inside the Sweep path; one successful edit is one Sweep OK.
[[nodiscard]] bool set_sweep_point_reference(Workspace&,const kernel::OcctKernel&,
    const std::string& document,const std::string& point,std::size_t index,
    document::ConstructionReference,bool derive_orientation=true);
}
