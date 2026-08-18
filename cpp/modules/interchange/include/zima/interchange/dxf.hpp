#pragma once

#include <zima/sketcher/sketch.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace zima::interchange {

struct DxfImportResult {
    std::size_t source_entities{};
    std::size_t imported_entities{};
    std::string import_block_id;
    std::vector<std::string> warnings;
};

[[nodiscard]] DxfImportResult import_dxf(
    const std::filesystem::path& path, zima::sketcher::Sketch& target,
    double ambiguous_unit_scale_to_mm = 1.0,
    std::size_t maximum_entities = 100);
void export_dxf(
    const std::filesystem::path& path, const zima::sketcher::Sketch& sketch);

}  // namespace zima::interchange
