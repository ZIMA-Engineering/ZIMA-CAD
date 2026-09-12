#pragma once
#include <zima/workspace/workspace.hpp>
#include <functional>
#include <stdexcept>

namespace zima::workspace {
class ExportOperationError : public std::runtime_error {
public:
    ExportOperationError(const char* code, const char* message) : std::runtime_error(message), code(code) {}
    const char* code;
};
struct ExportOptions { std::string sketch_id; bool overwrite{}; };
struct ExportReport { std::string document_id; std::uint64_t revision{}, bytes{}; std::filesystem::path path; };
// Export an owned snapshot of the last calculated/persisted state. Never
// regenerate dependencies or modify the document/Undo history. The runner
// must finish before returning. Publish a completed file from a sibling stage.
[[nodiscard]] ExportReport export_file(const Workspace&, const std::string& document_id,
    const std::filesystem::path& destination, const ExportOptions& = {},
    const std::function<void(std::function<void()>)>& runner = {});
}
