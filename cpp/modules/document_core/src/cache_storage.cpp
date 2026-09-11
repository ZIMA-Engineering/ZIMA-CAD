#include <zima/document/cache_storage.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>
#include <string>

namespace zima::document {
namespace {
using Json = nlohmann::json;
constexpr auto reference_key = "$zima_cache";
constexpr std::size_t minimum_block_size = 256;
constexpr std::size_t maximum_depth = 256;

class Writer {
public:
    Json values = Json::array();
    Json encode(const Json& source, std::size_t depth = 0) {
        if (depth > maximum_depth) throw std::runtime_error("Cache nesting is too deep");
        if (source.is_object() && source.contains(reference_key))
            throw std::runtime_error("Reserved cache reference key in source data");
        Json encoded = source;
        if (source.is_structured()) {
            for (auto it = encoded.begin(); it != encoded.end(); ++it)
                *it = encode(*it, depth + 1);
        }
        // Pool large leaves and repeated compound blocks after their children.
        // Full serialized equality resolves hash collisions; a hash alone never
        // decides that two geometries or occurrence records are identical.
        if (!encoded.is_structured() && !encoded.is_string()) return encoded;
        auto key = encoded.dump();
        if (key.size() < minimum_block_size) return encoded;
        const auto found = indices.find(key);
        if (found != indices.end()) return Json{{reference_key, found->second}};
        const auto index = values.size();
        indices.emplace(std::move(key), index);
        values.push_back(std::move(encoded));
        return Json{{reference_key, index}};
    }
private:
    std::unordered_map<std::string, std::size_t> indices;
};

Json decode(const Json& source, const Json& values, std::size_t limit,
            std::size_t depth = 0) {
    if (depth > maximum_depth) throw std::runtime_error("Cache nesting is too deep");
    if (source.is_object() && source.contains(reference_key)) {
        const auto& id = source.at(reference_key);
        if (source.size() != 1 || !id.is_number_unsigned())
            throw std::runtime_error("Invalid cache reference");
        const auto index = id.get<std::size_t>();
        // Only backward references are legal. Reject dangling references and
        // cycles before recursively expanding any geometry packet.
        if (index >= limit) throw std::runtime_error("Invalid cache reference order");
        return decode(values.at(index), values, index, depth + 1);
    }
    Json result = source;
    if (result.is_structured()) for (auto& child : result)
        child = decode(child, values, limit, depth + 1);
    return result;
}
}

nlohmann::json pack_cache_storage(const nlohmann::json& value) {
    Writer writer;
    auto root = writer.encode(value);
    return {{"format", "zima-shared-cache-v1"}, {"root", std::move(root)},
            {"values", std::move(writer.values)}};
}

nlohmann::json unpack_cache_storage(const nlohmann::json& storage) {
    if (!storage.is_object() || storage.value("format", "") != "zima-shared-cache-v1" ||
        !storage.contains("values") || !storage.at("values").is_array())
        throw std::runtime_error("Unsupported shared cache format");
    const auto& values = storage.at("values");
    return decode(storage.at("root"), values, values.size());
}
}
