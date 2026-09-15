#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <zima/document/engineering_metadata.hpp>

namespace zima::workspace {
struct FamilyReference {
    document::FamilyColumn binding;
    std::string name;
    std::string owner_name;
    std::string value;
};
// Persisted model data only. No geometry calculation or activation.
std::vector<FamilyReference> family_references(const Workspace&, const std::string& document);
void validate_family_references(const Workspace&, const std::string&, const document::FamilyTable&);
// Explicit generation creates a fully independent native model in a new tab.
// Its stable document identity belongs to the generic's persisted instance row.
// An unchanged open instance is reused. Changed source data regenerates an
// unmodified generated tab; independent edits are never overwritten.
std::string open_family_instance(Workspace&, const kernel::OcctKernel&,
    const std::string& generic, const std::string& instance_name);
// Explicit Drawing variant change; project the draft and commit only on success.
void select_family_drawing_source(drawing::DrawingDocument&, const Workspace&,
    const std::string& source, const std::filesystem::path& drawing_path);
}
