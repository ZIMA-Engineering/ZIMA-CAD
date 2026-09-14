#pragma once
#include <zima/workspace/feature_reference_input.hpp>
namespace zima::workspace {
// Prepare through the approved reference assignment and confirm through the
// existing Hole/Opening Properties transaction, including its profile identities.
[[nodiscard]] bool set_drill_placement_reference(Workspace&,const kernel::OcctKernel&,
    const std::string& document,const std::string& container,std::size_t index,
    document::ConstructionReference,bool derive_orientation=true);
}
