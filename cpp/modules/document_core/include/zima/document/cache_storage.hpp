#pragma once
#include <nlohmann/json_fwd.hpp>
#include <memory>
#include <string>
#include <vector>
namespace zima::document {
using CacheStorageSharedStrings =
    std::vector<std::shared_ptr<const std::string>>;
// File-local, lossless sharing of identical JSON blocks, with adaptive lossless
// compression. No external files, topology changes or geometry calculation.
[[nodiscard]] nlohmann::json pack_cache_storage(const nlohmann::json& value);
// Consume an exclusively owned temporary with identical bytes/block order.
// Calculated model packets themselves are never moved into this function.
[[nodiscard]] nlohmann::json pack_cache_storage(nlohmann::json&& value);
[[nodiscard]] nlohmann::json unpack_cache_storage(const nlohmann::json& storage);
// Consume parsed storage, expanding shared blocks once and moving their final use.
[[nodiscard]] nlohmann::json unpack_cache_storage(nlohmann::json&& storage);
// Part loading can retain pooled large strings and resolve them directly into
// immutable native packets. Compound JSON is still expanded and validated;
// only exact pooled strings remain as their already-validated references.
[[nodiscard]] nlohmann::json unpack_cache_storage(
    nlohmann::json&& storage, CacheStorageSharedStrings* shared_strings);
}
