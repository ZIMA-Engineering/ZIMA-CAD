#pragma once
#include <zima/interchange/dxf.hpp>
#include <zima/interchange/step_model.hpp>
namespace zima::interchange {
struct DxfPartImport {
    StepImportedPart part;
    DxfImportResult report;
    std::string sketch_id;
};
// Value transactions: failures leave the caller's document unchanged.
DxfPartImport import_dxf_part(document::PartDocument document,
    const std::vector<kernel::BodyResult>& previous, const std::filesystem::path& source,
    const std::string& active_sketch_id = {},
    double ambiguous_unit_scale_to_mm = 1.0, std::size_t maximum_entities = 100000);
StepImportedPart import_iges_part(document::PartDocument document,
    const std::vector<kernel::BodyResult>& previous, const std::filesystem::path& source, std::optional<double> mesh_deflection = {});
}
