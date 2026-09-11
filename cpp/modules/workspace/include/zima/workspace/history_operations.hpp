#pragma once
#include <zima/workspace/model_calculation.hpp>
#include <stdexcept>

namespace zima::workspace {
class HistoryOperationError : public std::runtime_error {
public:
    HistoryOperationError(const char* code,const char* message) : std::runtime_error(message),code(code) {}
    const char* code;
};
// Queries and cursor changes use persisted data only. Mutations calculate a
// temporary document and commit once; a thrown error never changes the session.
[[nodiscard]] bool part_history_suppressed(const document::PartDocument&,const std::string& object);
[[nodiscard]] bool set_part_history_suppressed(PartState&,const kernel::OcctKernel&,const std::string& object,bool suppressed);
[[nodiscard]] bool move_part_history(PartState&,const kernel::OcctKernel&,const std::string& object,const std::string& before={},bool commit=true);
void delete_part_history(PartState&,const kernel::OcctKernel&,const std::string& object);
[[nodiscard]] bool set_part_history_cursor(PartState&,std::size_t index);
}
