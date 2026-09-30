#pragma once
#include <zima/document/part_document.hpp>
#include <set>
namespace zima::workspace {
struct HistoryDeletionPlan {
    std::set<std::string> removed;
    std::set<std::string> affected;
};
// Inspection uses persisted identities only, without calculating geometry.
[[nodiscard]] HistoryDeletionPlan plan_history_deletion(const document::PartDocument&,const std::string&);
void detach_deleted_history_references(document::PartDocument&,const HistoryDeletionPlan&);
// A changed reference definition acknowledges repair; numeric edits do not.
bool refresh_removed_reference_states(document::PartDocument&);
inline constexpr const char* removed_reference_message="A source reference was deleted. Select replacement references in Properties.";
}
