#pragma once
#include <zima/workspace/workspace.hpp>
#include <stdexcept>
namespace zima::kernel { class OcctKernel; }
namespace zima::workspace {
class ComponentOperationError : public std::runtime_error {
public:
    ComponentOperationError(const char* code,const char* message):std::runtime_error(message),code(code){}
    const char* code;
};
// Read the existing removal blockers of one immediate occurrence. This is a
// dependency query only: it never solves placement or calculates body geometry.
struct ComponentRemovalDependencies {
    std::set<std::string> placement_components, dependency_components, sketches;
    [[nodiscard]] bool blocked() const {return !placement_components.empty()||!dependency_components.empty()||!sketches.empty();}
};
[[nodiscard]] ComponentRemovalDependencies component_removal_dependencies(
    const assembly::AssemblyDocument&,const std::string& occurrence);
// Explicit removal preserves the GUI dependency/cut policy and publishes one
// final candidate. Source documents and their files remain untouched.
void remove_component(Workspace&,const kernel::OcctKernel&,const std::string& owner,
    const std::string& occurrence);
// Explicit insertion consumes the existing native Part/Assembly insertion contract.
// Dependency validation reads native data only and never changes the open documents.
[[nodiscard]] std::string insert_component(Workspace&,const std::string& owner,
    const std::string& source,const std::optional<std::string>& name={});
// Replace one owned occurrence by another native Part/Assembly or family member.
// Keeps occurrence identity and stored reference keys; validates/calculates a
// private Assembly before publishing one Undo step.
bool replace_component(Workspace&,const kernel::OcctKernel&,const std::string& owner,
    const std::string& occurrence,const std::string& source);
}
