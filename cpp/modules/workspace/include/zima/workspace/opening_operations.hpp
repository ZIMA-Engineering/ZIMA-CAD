#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <zima/document/thread_catalog.hpp>
#include <stdexcept>

namespace zima::workspace {
class OpeningOperationError : public std::runtime_error {
public:
    OpeningOperationError(const char* code,const char* message):std::runtime_error(message),code(code){}
    const char* code;
};
enum class OpeningEditMode { Create, Replace };
// Current Otvor feature (native Thread): blind bore, optional thread sheet,
// entrance chamfer and drill point. Full properties edits may change locks.
[[nodiscard]] bool commit_opening(Workspace&,const kernel::OcctKernel&,
    const std::string& document_id,document::HistoryContainer,OpeningEditMode);
[[nodiscard]] std::vector<std::pair<std::string,double>> opening_dimensions(const document::HistoryContainer&);
} // namespace zima::workspace
