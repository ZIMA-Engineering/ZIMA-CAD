#pragma once
#include <zima/document/container_origin_display.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <optional>

namespace zima::workspace {
// The caller must first ensure that any separately owned Sketch is unchanged.
inline std::optional<bool> commit_origin_display_only(Workspace& live,const std::string& id,
    const document::HistoryContainer& value) {
    auto* state=live.open_part(id);
    if(!state || !document::has_origin_display_controls(value.feature_kind))return {};
    const auto& before=state->session.document();
    const auto* old=before.find_container(value.id);
    if(!old || !document::same_except_origin_display(*old,value))return {};
    const auto* body=before.body_owner_for_object(value.id);
    if(body && (body->derived_copy || body->scope.id!=before.body_history.active_body_id()))return {};
    if(*old==value)return false;
    auto next=before;*next.find_container(value.id)=value;
    commit_part_document(live,id,std::move(next),state->session.calculated_boundaries());
    return true;
}
}
