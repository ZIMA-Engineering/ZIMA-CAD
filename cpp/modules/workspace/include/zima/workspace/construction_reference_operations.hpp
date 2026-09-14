#pragma once
#include <zima/workspace/placement_edit.hpp>
namespace zima::workspace {
// Prepare a private construction draft with the established Properties rules.
// The caller validates ownership and commits the draft through its owning operation.
[[nodiscard]] document::ConstructionObject prepare_construction_reference(
    document::ConstructionObject,const kernel::ViewerReferenceGeometry&,std::size_t,
    document::ConstructionReference,bool derive_orientation=true);
// Assign a reference to a standalone construction or owned curve point using the same
// position and FRONT/TOP rows as Properties; no solid-body calculation.
[[nodiscard]] bool set_construction_reference(Workspace&,const std::string& document,
    const std::string& construction,std::size_t index,document::ConstructionReference,
    bool derive_orientation=true);
}
