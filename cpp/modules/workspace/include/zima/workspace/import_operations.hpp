#pragma once
#include <zima/workspace/workspace.hpp>
#include <zima/interchange/model_import.hpp>
#include <functional>
#include <stdexcept>

namespace zima::workspace {
class ImportOperationError : public std::runtime_error {
public:
    ImportOperationError(const char* code, const char* message) : std::runtime_error(message), code(code) {}
    const char* code;
};
struct PartImportOptions {
    std::optional<double> mesh_deflection;
    std::string sketch_id;
    double unitless_scale_mm{1.0};
    std::size_t maximum_entities{100000};
};
struct PartImportReport {
    std::vector<std::string> body_ids, container_ids;
    std::string sketch_id;
    interchange::DxfImportResult dxf;
    bool body_calculated{};
};
struct PreparedSketchImport {sketcher::Sketch sketch;interchange::DxfImportResult report;};
// A private draft for both GUI Sketcher and stored/embedded CLI Sketch targets.
[[nodiscard]] PreparedSketchImport prepare_sketch_dxf(const sketcher::Sketch&,const std::filesystem::path&,
    double unitless_scale_mm=1.0,std::size_t maximum_entities=100000);
[[nodiscard]] PartImportReport import_sketch(Workspace&,const std::string& document_id,
    const std::filesystem::path&,const PartImportOptions&,
    const std::function<void(std::function<void()>)>& runner = {});
// The runner must finish the supplied task before returning. It receives only
// a private input snapshot; all Workspace access/commit stays on the caller.
// Failure or a changed/reopened target discards the calculated import result.
[[nodiscard]] PartImportReport import_part(Workspace&, const std::string& document_id,
    const std::filesystem::path& source, const PartImportOptions& = {},
    const std::function<void(std::function<void()>)>& runner = {});
}
