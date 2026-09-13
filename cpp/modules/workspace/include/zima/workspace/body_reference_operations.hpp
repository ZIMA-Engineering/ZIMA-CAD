#pragma once
#include <zima/workspace/body_operations.hpp>
namespace zima::workspace {
// Single pending reference-field replacement, followed by the existing Body
// Properties transaction. Indices 0..2 are position, 3 FRONT, 4 TOP.
[[nodiscard]] bool set_body_placement_reference(Workspace&,const kernel::OcctKernel&,
    const std::string& document,const std::string& body,std::size_t index,
    document::ConstructionReference source,bool derive_orientation=true);
}
