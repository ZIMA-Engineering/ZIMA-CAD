#pragma once
#include <filesystem>

namespace zima::distribution {
// Retarget removed factory settings only when no custom file occupies that path.
inline std::filesystem::path updated_factory_template(const std::filesystem::path& directory,
        const std::filesystem::path& requested,bool inches) {
    const auto resolved=requested.is_absolute()?requested:directory/requested;
    if(std::filesystem::exists(resolved))return requested;
    const auto name=requested.filename().string();
    if(name!="START_PART.prtz"&&name!="START_ASSEMBLY.asmz"&&name!="START_SKELETON.prtz")return requested;
    const auto replacement=requested.parent_path()/
        (requested.stem().string()+(inches?"_in":"_mm")+requested.extension().string());
    if(std::filesystem::is_regular_file(replacement.is_absolute()?replacement:directory/replacement))return replacement;
    return requested;
}
}
