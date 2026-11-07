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
// Properties editing turns a retained, self-contained container into a normal
// authored definition again. Operations which intrinsically edit their input
// (Fillet, Chamfer and Shell) still require a real replacement reference.
bool acknowledge_retained_history_edit(document::PartDocument&,const std::string& owner);
// Outside an explicit Properties edit, only a changed reference definition
// acknowledges repair.
bool refresh_removed_reference_states(document::PartDocument&);
inline constexpr const char* removed_reference_message="A source reference was deleted. Select replacement references in Properties.";
}
