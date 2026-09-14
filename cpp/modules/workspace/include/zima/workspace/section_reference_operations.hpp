#pragma once
#include <zima/workspace/section_operations.hpp>
namespace zima::workspace {
// Command input adapter over the approved shared row assignment and the same
// Section Properties transaction. Uses calculated original viewer references.
bool set_section_placement_reference(Workspace&,const std::string& document,
    const std::string& section,std::size_t index,document::ConstructionReference,
    bool derive_orientation=true);
}
