#pragma once
#include <zima/workspace/history_operations.hpp>

namespace zima::workspace {
// Component, construction and standalone Sketch order changes reuse calculated
// data. Cuts delegate to the existing explicit cut transaction. Every object and
// destination belongs to one immediate owning Assembly and one native list.
[[nodiscard]] bool move_assembly_history(Workspace&, const std::string& document,
    const kernel::OcctKernel&, const std::string& object,
    const std::string& before = {}, bool commit = true);
}
