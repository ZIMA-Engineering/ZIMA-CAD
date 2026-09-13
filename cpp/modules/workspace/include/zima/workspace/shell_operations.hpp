#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <stdexcept>
namespace zima::workspace {
class ShellOperationError : public std::runtime_error {
public:
    ShellOperationError(const char* code,const char* message):std::runtime_error(message),code(code){}
    const char* code;
};
enum class ShellEditMode { Create, Replace };
// Unique persisted face identities of the real operation input, without OCCT.
[[nodiscard]] std::vector<kernel::FaceReference> shell_input_faces(
    const PartState&,const std::string& container_id={});
[[nodiscard]] bool commit_shell(Workspace&,const kernel::OcctKernel&,
    const std::string& document_id,document::HistoryContainer,ShellEditMode);
} // namespace zima::workspace
