#pragma once
#include <zima/workspace/history_operations.hpp>

namespace zima::workspace {
[[nodiscard]] bool set_assembly_history_cursor(Workspace&, const std::string& document,
    std::size_t cursor);
void validate_assembly_history_order(const assembly::AssemblyDocument&,
    const std::vector<assembly::AssemblyDocument::HistoryEntry>* order = nullptr);
[[nodiscard]] std::vector<assembly::AssemblyDocument::HistoryEntry> reordered_assembly_history(
    const assembly::AssemblyDocument&,const std::string& object,const std::string& before);
// Component, construction and standalone Sketch order changes reuse calculated
// data. Cuts delegate to the existing explicit cut transaction. Every object and
// destination belongs to one immediate owning Assembly and one native list.
[[nodiscard]] bool move_assembly_history(Workspace&, const std::string& document,
    const kernel::OcctKernel&, const std::string& object,
    const std::string& before = {}, bool commit = true);
}
