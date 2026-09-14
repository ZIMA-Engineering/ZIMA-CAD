#pragma once
#include <zima/workspace/profile_operations.hpp>
namespace zima::workspace {
[[nodiscard]] bool set_profile_reference(Workspace&,const kernel::OcctKernel&,
    const std::string& document,const std::string& container,std::size_t index,
    document::ConstructionReference,bool derive_orientation=true);
}
