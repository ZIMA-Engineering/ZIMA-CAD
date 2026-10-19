#pragma once
#include <zima/workspace/opening_operations.hpp>

namespace zima::workspace {
// A settings snapshot creates a new independent Opening, not a history clone.
// Source placement, generated profiles, relations and end-face identities never
// become dependencies of the new feature. Up-to modes require new target input.
[[nodiscard]] inline document::HistoryContainer copy_opening_settings(
    const document::HistoryContainer& source) {
    if(source.feature_kind!=document::FeatureKind::Thread)
        throw OpeningOperationError("wrong_feature","This container is not an opening.");
    auto result=document::PartDocument::create_thread_container();
    result.thread=source.thread;
    result.thread.end_targets_forward.clear();
    result.thread.length_end_targets.clear();
    result.hole.drill_point_enabled=source.hole.drill_point_enabled;
    result.hole.drill_point_angle_degrees=source.hole.drill_point_angle_degrees;
    result.value_locks=source.value_locks;
    return result;
}
}
