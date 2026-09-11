#pragma once
#include <zima/document/part_document.hpp>
#include <zima/assembly/assembly_document.hpp>
#include <zima/kernel/geometry_kernel.hpp>
namespace zima::interchange {
struct StepImportedPart {
    document::PartDocument document;
    std::vector<kernel::BodyResult> calculated;
    std::filesystem::path path;
};
struct StepImportedAssembly {
    assembly::AssemblyDocument document;
    std::filesystem::path path;
};
struct StepAssemblyImport {
    std::vector<StepImportedPart> parts;
    std::vector<StepImportedAssembly> assemblies; // dependencies before owners
    std::size_t root_index{};
    assembly::PartOccurrence root_occurrence;
};
// Explicit import transactions. No files or open documents are mutated here.
StepImportedPart import_step_part(document::PartDocument document,
    const std::vector<kernel::BodyResult>& previous, const std::filesystem::path& source, std::optional<double> mesh_deflection = {});
StepAssemblyImport import_step_assembly(const std::filesystem::path& source,
    const std::filesystem::path& directory, const std::map<std::string,std::string>& precision, std::optional<double> mesh_deflection = {});
kernel::StepProduct step_product(const document::PartDocument& document,
    const std::vector<kernel::BodyResult>& calculated);
kernel::StepProduct step_product(const assembly::AssemblyDocument& document);
}
