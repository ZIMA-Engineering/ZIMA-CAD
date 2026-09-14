#pragma once
#include <zima/workspace/sketch_operations.hpp>
namespace zima::workspace {
// Shared Part Sketch Properties OK; pending geometry and placement commit together.
[[nodiscard]] bool commit_part_sketch_properties(Workspace&,const kernel::OcctKernel&,
    const std::string& document,sketcher::Sketch,document::Placement,
    const std::optional<document::HistoryContainer>& new_container = {});
[[nodiscard]] bool set_part_sketch_reference(Workspace&,const kernel::OcctKernel&,
    const std::string& document,const std::string& sketch,std::size_t index,document::ConstructionReference);
}
