#pragma once
#include <zima/workspace/workspace.hpp>
#include <stdexcept>
namespace zima::workspace {
class ComponentOperationError : public std::runtime_error {
public:
    ComponentOperationError(const char* code,const char* message):std::runtime_error(message),code(code){}
    const char* code;
};
// Explicit insertion consumes the existing native Part/Assembly insertion contract.
// Dependency validation reads native data only and never changes the open documents.
[[nodiscard]] std::string insert_component(Workspace&,const std::string& owner,
    const std::string& source,const std::optional<std::string>& name={});
}
