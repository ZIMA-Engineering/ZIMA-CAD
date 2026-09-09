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
    const std::string& active_sketch_id = {});
StepImportedPart import_iges_part(document::PartDocument document,
    const std::vector<kernel::BodyResult>& previous, const std::filesystem::path& source);
}
