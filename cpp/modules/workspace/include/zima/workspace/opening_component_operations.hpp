#pragma once
#include <zima/workspace/opening_operations.hpp>
namespace zima::workspace {
struct OpeningComponent { std::string role; bool removable{}; };
[[nodiscard]] std::vector<OpeningComponent> opening_components(const document::HistoryContainer&);
// Same optional-component deletion as the Tree. Bore and owned profiles stay.
// Native Hole threads are derived wires; their removal needs no body calculation.
[[nodiscard]] bool remove_opening_component(Workspace&,const kernel::OcctKernel&,
    const std::string& document,const std::string& container,const std::string& role);
}
