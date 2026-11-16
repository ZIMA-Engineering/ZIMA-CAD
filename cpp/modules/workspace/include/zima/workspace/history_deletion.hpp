#pragma once
#include <zima/document/part_document.hpp>
#include <set>
namespace zima::workspace {
struct HistoryDeletionPlan {
    std::set<std::string> removed;
    std::set<std::string> affected;
    // Affected edge treatments which still own at least one exact persisted
    // route member after references to the deleted source are removed.  They
    // remain ordinary calculated history instead of becoming broken merely
    // because another selected route member disappeared.
    std::set<std::string> auto_repaired;
    // Removed objects and retained edit-in-place operations which cannot
    // publish geometry after pruning.  Loss propagates through this set only;
    // an auto-repaired Fillet/Chamfer remains a valid source for its consumers.
    std::set<std::string> unavailable;
};
// Inspection uses persisted identities only, without calculating geometry.
[[nodiscard]] HistoryDeletionPlan plan_history_deletion(const document::PartDocument&,const std::string&);
void detach_deleted_history_references(document::PartDocument&,const HistoryDeletionPlan&);
// Properties editing turns a retained, self-contained container into a normal
// authored definition again. Operations which intrinsically edit their input
// still require a real surviving or replacement reference; deletion may have
// already pruned only missing Fillet/Chamfer route members automatically.
bool acknowledge_retained_history_edit(document::PartDocument&,const std::string& owner);
// Outside an explicit Properties edit, only a changed reference definition
// acknowledges repair.
bool refresh_removed_reference_states(document::PartDocument&);
inline constexpr const char* removed_reference_message="A source reference was deleted. Select replacement references in Properties.";
}
