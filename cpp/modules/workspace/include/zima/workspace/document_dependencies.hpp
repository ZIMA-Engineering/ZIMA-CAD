#pragma once
#include <zima/workspace/workspace.hpp>
namespace zima::workspace {
class DocumentDependencyError : public std::runtime_error {
public:
    DocumentDependencyError(const char* code,const char* message):std::runtime_error(message),code(code){}
    const char* code;
};
// Verify the proposed owner -> source edge through insertion and every Sketch
// dependency, including embedded feature profiles. Open documents override
// native sources. This query never mutates the live Workspace or calculates.
void require_acyclic_document_dependency(const Workspace&,const std::string& owner,const std::string& source);
}
