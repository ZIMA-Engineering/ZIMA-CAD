#pragma once
#include <nlohmann/json_fwd.hpp>
namespace zima::document {
// File-local, lossless sharing of identical JSON blocks. No compression,
// external dependencies, topology identity changes or geometry calculation.
[[nodiscard]] nlohmann::json pack_cache_storage(const nlohmann::json& value);
[[nodiscard]] nlohmann::json unpack_cache_storage(const nlohmann::json& storage);
}
