#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <stdexcept>
namespace zima::workspace {
class HoleOperationError : public std::runtime_error {
public:
    HoleOperationError(const char* code,const char* message):std::runtime_error(message),code(code){}
    const char* code;
};
enum class HoleEditMode { Create, Replace };
[[nodiscard]] bool commit_hole(Workspace&,const kernel::OcctKernel&,const std::string& document_id,
    document::HistoryContainer,HoleEditMode);
[[nodiscard]] std::vector<std::pair<std::string,double>> hole_dimensions(const document::HistoryContainer&);
}
