#pragma once
#include <zima/workspace/workspace.hpp>

namespace zima::workspace {
// Private save candidates only. Integrate existing native solids and publish
// physical-dependent parameters; never replay history, solve mates or remesh.
bool refresh_save_properties(document::PartDocument&, std::vector<kernel::BodyResult>&);
bool refresh_save_properties(assembly::AssemblyDocument&, const Workspace&,
    const std::filesystem::path& owner_file,
    std::map<std::string,assembly::AssemblyDocument>* family_analysis = nullptr);
// No OCCT or mutation. A Family row with an uncut source unavailable in its
// owner may hydrate that source from its current native document.
bool save_properties_pending(const PartState&);
bool save_properties_pending(const AssemblyState&,const Workspace&);
// Family rows retain component identity/source ownership. Reuse the owner's
// current source analysis while preserving a row's own calculated operations.
bool share_family_source_analysis(assembly::AssemblyDocument& member,
    const assembly::AssemblyDocument& owner);
} // namespace zima::workspace
