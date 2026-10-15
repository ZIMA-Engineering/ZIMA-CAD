#pragma once
#include <nlohmann/json_fwd.hpp>
namespace zima::document {
// File-local, lossless sharing of identical JSON blocks. No compression,
// external dependencies, topology identity changes or geometry calculation.
[[nodiscard]] nlohmann::json pack_cache_storage(const nlohmann::json& value);
// Consume an exclusively owned temporary with identical bytes/block order.
// Calculated model packets themselves are never moved into this function.
[[nodiscard]] nlohmann::json pack_cache_storage(nlohmann::json&& value);
[[nodiscard]] nlohmann::json unpack_cache_storage(const nlohmann::json& storage);
// Consume parsed storage, expanding shared blocks once and moving their final use.
[[nodiscard]] nlohmann::json unpack_cache_storage(nlohmann::json&& storage);
}
