#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <stdexcept>
namespace zima::workspace {
class DerivedCopyError : public std::runtime_error {
public:
    DerivedCopyError(const char* code,const char* message):std::runtime_error(message),code(code){}
    const char* code;
};
enum class CopySourceKind { Body, Boolean, Solid, Component };
struct CopySource {
    std::string id,name;
    CopySourceKind kind{};
    bool visible{true};
};
struct CopySources {
    std::size_t boundary{};
    std::vector<CopySource> items;
    std::vector<std::string> context_bodies;
};
struct DerivedCopyDefinition {
    std::string id,name;
    bool visible{true};
    document::Placement placement;
    document::DerivedCopyParameters parameters;
    bool operator==(const DerivedCopyDefinition&) const = default;
};
// Read persisted data only. The boundary matches Properties: immediately after
// the active Body, otherwise the root cursor; immediately before an edited copy.
// Assembly sources are preceding, unsuppressed immediate occurrences.
// An active Part Body offers only its own solid features before its cursor.
// At Part level both available Bodies and their solid features are offered.
[[nodiscard]] CopySources derived_copy_sources(const Workspace&,const std::string& document,const std::string& object={});
[[nodiscard]] kernel::ViewerMesh derived_copy_source_mesh(const document::DocumentSession&,const CopySource&);
[[nodiscard]] DerivedCopyDefinition derived_copy_definition(const Workspace&,const std::string& document,const std::string& object);
struct DerivedCopyEdit {
    std::string document_id;
    std::uint64_t revision{};
    bool creating{};
    DerivedCopyDefinition initial;
    CopySources sources;
    kernel::ViewerReferenceGeometry references;
};
// Geometry is prepared once from stored viewer references for explicit editing.
// Assembly preparation shares current source packets, like GUI scene refresh;
// it does not calculate sources, mates or existing derived operations.
// Query commands above must not call this function. Existing-object type comes
// from its data; the pattern argument selects only the type of a new copy.
[[nodiscard]] DerivedCopyEdit prepare_derived_copy_edit(Workspace&,const std::string& document,
    const std::string& object={},bool pattern=false);
[[nodiscard]] bool commit_derived_copy(Workspace&,const kernel::OcctKernel&,const DerivedCopyEdit&,
    DerivedCopyDefinition);
} // namespace zima::workspace
