#pragma once
#include <zima/workspace/workspace.hpp>
namespace zima::workspace {
// Prepare dependent Assembly summaries before committing the Part. Publication
// does not calculate geometry or create independent Assembly Undo items.
void commit_part_document(Workspace&,const std::string& document,
    document::PartDocument,std::vector<kernel::BodyResult> calculated_boundaries);
[[nodiscard]] bool step_part_document_history(Workspace&,const std::string& document,bool redo);
// Reconcile open owners using open authoritative and closed native source Parts.
// A missing source preserves existing summary edges; it never deletes by guess.
void reconcile_external_sketch_dependencies(Workspace&);
}
