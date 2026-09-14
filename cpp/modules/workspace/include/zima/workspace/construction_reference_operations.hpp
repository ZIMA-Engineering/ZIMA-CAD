#pragma once
#include <zima/workspace/placement_edit.hpp>
namespace zima::workspace {
// Assign a reference to an standalone construction or owned curve point using the same
// position and FRONT/TOP rows as Properties; no solid-body calculation.
[[nodiscard]] bool set_construction_reference(Workspace&,const std::string& document,
    const std::string& construction,std::size_t index,document::ConstructionReference,
    bool derive_orientation=true);
}
