#pragma once
#include <zima/workspace/workspace.hpp>
#include <stdexcept>
namespace zima::workspace {
class DerivedCopyError : public std::runtime_error {
public:
    DerivedCopyError(const char* code,const char* message):std::runtime_error(message),code(code){}
    const char* code;
};
enum class CopySourceKind { Body, Boolean, Component };
struct CopySource {
    std::string id,name;
    CopySourceKind kind{};
    bool visible{true};
};
struct CopySources {
    std::size_t boundary{};
    std::vector<CopySource> items;
};
struct DerivedCopyDefinition {
    std::string id,name;
    bool visible{true};
    document::Placement placement;
    document::DerivedCopyParameters parameters;
};
// Read persisted data only. The boundary matches Properties: immediately after
// the active Body, otherwise the root cursor; immediately before an edited copy.
// Assembly sources are preceding, unsuppressed immediate occurrences.
[[nodiscard]] CopySources derived_copy_sources(const Workspace&,const std::string& document,const std::string& object={});
[[nodiscard]] DerivedCopyDefinition derived_copy_definition(const Workspace&,const std::string& document,const std::string& object);
} // namespace zima::workspace
