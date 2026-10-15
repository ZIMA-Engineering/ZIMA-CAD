#pragma once
#include <zima/document/engineering_metadata.hpp>
#include <filesystem>
namespace zima::document {
// Read the native UTF-8 material library into self-contained document data.
// No dependency on the library file survives confirmation in the document.
[[nodiscard]] MaterialData load_material_library(const std::filesystem::path&);
// Serialize validated material data in the existing native UTF-8 format.
[[nodiscard]] std::string serialize_material_library(const MaterialData&);
}
