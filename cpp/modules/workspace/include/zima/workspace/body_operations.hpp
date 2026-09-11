#pragma once
#include <zima/workspace/model_calculation.hpp>

namespace zima::workspace {
class BodyOperationError : public std::runtime_error {
public:
    BodyOperationError(const char* code,const char* message):std::runtime_error(message),code(code) {}
    const char* code;
};
// A small transient graph draft, without calculated geometry. Creation defines
// the future object's identity before the properties dialog or calculation.
struct BodyGraphEdit {
    std::string document_id,object_id;
    document::BodyHistoryGraph original, pending;
};
[[nodiscard]] BodyGraphEdit prepare_body_edit(const document::PartDocument&,
    const std::string& body_id = {}, std::string new_name = "Body");
[[nodiscard]] BodyGraphEdit prepare_body_boolean_edit(const document::PartDocument&,
    const std::string& boolean_id = {}, std::string new_name = "Boolean",
    kernel::BodyCombination operation = kernel::BodyCombination::Subtract,
    std::string target = {}, std::string tool = {});
// Consume the existing placement solver unchanged. Reject stale drafts and commit
// only after calculation succeeds; a no-op does not create an Undo item.
[[nodiscard]] bool commit_body_edit(Workspace&,const kernel::OcctKernel&,
    const BodyGraphEdit&,document::BodyHistory,bool active);
[[nodiscard]] bool commit_body_boolean_edit(Workspace&,const kernel::OcctKernel&,
    const BodyGraphEdit&,document::BodyBoolean);
// Pure document transactions: reuse already calculated boundaries, never OCCT.
[[nodiscard]] bool activate_part_body(Workspace&,const std::string& document_id,
    const std::string& body_id);
[[nodiscard]] bool set_body_history_cursor(Workspace&,const std::string& document_id,
    std::size_t index,const std::string& body_id = {});
} // namespace zima::workspace
