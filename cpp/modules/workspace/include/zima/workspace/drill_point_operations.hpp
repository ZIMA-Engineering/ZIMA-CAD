#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <stdexcept>
namespace zima::workspace {
class DrillPointOperationError : public std::runtime_error {
public:
    DrillPointOperationError(const char* code,const char* message):std::runtime_error(message),code(code){}
    const char* code;
};
enum class DrillPointEditMode { Create, Replace };
[[nodiscard]] bool commit_drill_point(Workspace&,const kernel::OcctKernel&,
    const std::string& document_id,document::HistoryContainer,DrillPointEditMode);
} // namespace zima::workspace
