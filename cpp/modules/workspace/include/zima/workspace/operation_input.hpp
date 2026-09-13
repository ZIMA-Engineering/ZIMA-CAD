#pragma once
#include <zima/document/document_session.hpp>
namespace zima::workspace {
// Read the already calculated body before editing a feature or at the current
// insertion cursor. Borrowed data: valid until the session changes. No OCCT,
// original-reference hydration, or copy of the full triangulation is needed.
[[nodiscard]] const kernel::BodyResult* calculated_operation_input(
    const document::DocumentSession&,const std::string& container_id={});
} // namespace zima::workspace
