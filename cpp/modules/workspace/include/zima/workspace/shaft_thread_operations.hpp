#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <stdexcept>

namespace zima::workspace {
class ShaftThreadOperationError : public std::runtime_error {
public:
    ShaftThreadOperationError(const char* code,const char* message):std::runtime_error(message),code(code){}
    const char* code;
};
enum class ShaftThreadEditMode { Create, Replace };
// The same persisted input packet and native history boundary used by Properties.
[[nodiscard]] kernel::ViewerReferenceGeometry shaft_thread_input_references(const PartState&,const std::string& container_id);
// Same analytic validation and explicit OK transaction; no independent solver.
[[nodiscard]] bool commit_shaft_thread(Workspace&,const kernel::OcctKernel&,
    const std::string& document_id,document::HistoryContainer,ShaftThreadEditMode);
} // namespace zima::workspace
