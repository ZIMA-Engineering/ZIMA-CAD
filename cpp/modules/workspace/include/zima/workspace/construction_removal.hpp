#pragma once
#include <zima/workspace/workspace.hpp>
namespace zima::kernel { class OcctKernel; }
namespace zima::workspace {
class ConstructionRemovalError : public std::runtime_error {
public:
    const char* code;
    ConstructionRemovalError(const char* code,const char* message):std::runtime_error(message),code(code){}
};
// Delete an independent root using the existing Part history or Assembly datum
// transaction. Owned points and embedded paths are edited through their parent.
void delete_construction(Workspace&,const kernel::OcctKernel&,const std::string& document,
    const std::string& construction);
}
