#pragma once

#include "workspace/workspace_internal.hpp"

namespace zima::app::workspace_detail {

// Broken references remain selectable for removal. Geometry-support entry
// deliberately keeps using the stricter parser, which rejects broken data.
inline std::optional<std::string> sketch_external_reference_selection_id_from_key(
        const std::string& key) {
    constexpr std::string_view suffix{":broken"};
    return sketch_external_reference_id_from_key(key.ends_with(suffix)
        ? key.substr(0, key.size() - suffix.size()) : key);
}

} // namespace zima::app::workspace_detail
