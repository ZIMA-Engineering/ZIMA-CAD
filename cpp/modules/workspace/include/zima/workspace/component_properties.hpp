#pragma once
#include <zima/workspace/component_operations.hpp>
namespace zima::workspace {
// Only occurrence-owned editable data. Source packets, paths, identities and
// appearance are deliberately not replaced by a stale Properties dialog.
struct ComponentProperties {
    std::string name;
    assembly::ComponentPlacement placement;
    std::vector<assembly::ComponentPlacementReference> references;
    std::set<std::string> value_locks;
    bool visible{true},suppressed{},grounded{};
    bool operator==(const ComponentProperties&) const = default;
};
struct ComponentEdit {
    std::string document_id,occurrence_id;
    std::uint64_t revision{};
    ComponentProperties initial;
    bool derived{};
};
[[nodiscard]] ComponentProperties component_properties(const assembly::PartOccurrence&);
[[nodiscard]] ComponentEdit prepare_component_edit(const Workspace&,const std::string& document,const std::string& occurrence);
// Explicit OK/CLI transaction: the existing placement solver runs only for
// placement/reference/grounding/suppression changes. No OCCT or cut/copy regen.
[[nodiscard]] bool commit_component_properties(Workspace&,const ComponentEdit&,const ComponentProperties&);
// Manual numeric entry uses the same free-coordinate mask and locks as GUI.
void assign_component_coordinates(const Workspace&,const ComponentEdit&,ComponentProperties&,const std::map<std::string,double>&);
} // namespace zima::workspace
