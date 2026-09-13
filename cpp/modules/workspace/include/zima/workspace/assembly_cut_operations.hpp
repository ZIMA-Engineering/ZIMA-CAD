#pragma once
#include <zima/workspace/history_operations.hpp>
namespace zima::workspace {
// A cut belongs to this exact Assembly; all mutations publish one calculated state.
bool set_assembly_cut_suppressed(Workspace&, const kernel::OcctKernel&, const std::string& document,
    const std::string& cut, bool suppressed);
void remove_assembly_cut(Workspace&, const kernel::OcctKernel&, const std::string& document,
    const std::string& cut);
bool move_assembly_cut(Workspace&, const kernel::OcctKernel&, const std::string& document,
    const std::string& cut, const std::string& before = {}, bool commit = true);
}
