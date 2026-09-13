#pragma once
#include <zima/document/part_document.hpp>
#include <nlohmann/json_fwd.hpp>
namespace zima::document {
// One current parameter representation for Part features and Assembly cutters.
void load_profile_parameters(HistoryContainer&, const nlohmann::json&);
void save_profile_parameters(const HistoryContainer&, nlohmann::json&);
}
