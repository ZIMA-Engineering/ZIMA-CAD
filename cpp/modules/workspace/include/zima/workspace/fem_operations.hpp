#pragma once

#include <zima/workspace/body_operations.hpp>

namespace zima::workspace {

[[nodiscard]] BodyGraphEdit prepare_fem_edit(const document::PartDocument&,
    const std::string& body_id = {});

// The returned snapshot shares the current calculated source. These read paths
// neither evaluate CAD nor invoke a mesher/solver, including for consumed Bodies.
[[nodiscard]] std::vector<std::string> fem_source_candidates(
    const document::DocumentSession&, const BodyGraphEdit&);
[[nodiscard]] kernel::BodySnapshot read_fem_source(
    const document::DocumentSession&, const std::string& fem_body_id);

// A definition-only transaction: no kernel parameter is deliberately accepted.
// Unchanged OK returns false; callers discard the draft for Cancel.
[[nodiscard]] bool commit_fem(Workspace&, const BodyGraphEdit&, document::BodyHistory);

} // namespace zima::workspace
