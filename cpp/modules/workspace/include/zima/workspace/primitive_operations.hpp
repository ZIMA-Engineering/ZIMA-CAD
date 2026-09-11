#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <stdexcept>

namespace zima::workspace {
enum class PrimitiveEditMode { Create, Replace };
class PrimitiveOperationError : public std::runtime_error {
public:
    PrimitiveOperationError(const char* code, const char* message)
        : std::runtime_error(message), code(code) {}
    const char* code;
};
struct PrimitiveDefinition {
    document::FeatureKind kind;
    const char* command_name;
    const char* display_name;
    document::HistoryContainer (*create)();
};
[[nodiscard]] const std::vector<PrimitiveDefinition>& primitive_definitions();
[[nodiscard]] const PrimitiveDefinition* primitive_definition(document::FeatureKind);
// Ordered semantic parameter names and persisted values, all in mm. No calculation.
[[nodiscard]] std::vector<std::pair<std::string,double>> primitive_dimensions(const document::HistoryContainer&);
// Complete properties edits can change locks. Command patches cannot override them.
[[nodiscard]] bool commit_primitive(Workspace&, const kernel::OcctKernel&,
    const std::string& document_id, document::HistoryContainer, PrimitiveEditMode);
using PrimitiveDimensionPatch = std::map<std::string,double>;
// Applied only to a caller-owned draft. Unknown names and invalid dimensions fail.
void assign_primitive_dimensions(document::HistoryContainer&, const PrimitiveDimensionPatch&);
[[nodiscard]] bool set_primitive_dimensions(Workspace&, const kernel::OcctKernel&,
    const std::string& document_id, const std::string& container_id,
    document::FeatureKind expected_kind, const PrimitiveDimensionPatch&);
} // namespace zima::workspace
