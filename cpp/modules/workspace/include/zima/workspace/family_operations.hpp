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
// Mutate a private model draft using the same parameter bindings as family tables.
bool assign_driving_dimension(document::PartDocument&, const document::FamilyColumn&, double);
bool assign_driving_dimension(assembly::AssemblyDocument&, const document::FamilyColumn&, double);
// Explicit calculation opens a linked view of a row in the owning native file.
std::string open_family_instance(Workspace&, const kernel::OcctKernel&,
    const std::string& generic, const std::string& instance_name, bool activate = true);
std::string family_owner(const Workspace&, const std::string&);
bool commit_family_part(Workspace&, const std::string&, document::PartDocument&,
    std::vector<kernel::BodyResult>&);
bool commit_family_assembly(Workspace&, const std::string&, assembly::AssemblyDocument&);
// Republish persisted evaluation packets after Undo/Redo; never calculates.
void restore_family_tabs(Workspace&, const std::string& generic);
// Load a saved row packet from its parent's single native file; no OCCT.
document::PartDocument family_part_source(document::PartDocument,
    std::vector<kernel::BodyResult>&, const std::string& expected);
assembly::AssemblyDocument family_assembly_source(assembly::AssemblyDocument,
    const std::string& expected,bool resolve_sources=true);
document::PartDocument read_family_part(const Workspace*,const std::filesystem::path&,
    const std::string&,std::vector<kernel::BodyResult>&);
assembly::AssemblyDocument read_family_assembly(const Workspace*,const std::filesystem::path&,
    const std::string&,bool resolve_sources=true);
// Explicit per-sheet Drawing variant change; project the draft and commit only on success.
void select_family_drawing_source(drawing::DrawingDocument&, const Workspace&,
    const std::string& sheet, const std::string& source);
}
