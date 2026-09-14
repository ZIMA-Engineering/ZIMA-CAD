#pragma once
#include <zima/workspace/primitive_operations.hpp>
#include <zima/workspace/placement_edit.hpp>
namespace zima::workspace {
// Existing original-reference rules and primitive OK transaction; one body calculation.
[[nodiscard]] bool set_primitive_reference(Workspace&,const kernel::OcctKernel&,
    const std::string& document,const std::string& container,std::size_t index,
    document::ConstructionReference,bool derive_orientation=true);
}
