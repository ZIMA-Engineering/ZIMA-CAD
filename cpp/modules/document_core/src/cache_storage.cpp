#include <zima/document/cache_storage.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace zima::document {
namespace {
using Json = nlohmann::json;
constexpr auto reference_key = "$zima_cache";
constexpr std::size_t minimum_block_size = 256;
constexpr std::size_t maximum_depth = 256;

class Writer {
public:
    Json values = Json::array();
    template<class Source>
    Json encode(Source&& source, std::size_t depth = 0) {
        if (depth > maximum_depth) throw std::runtime_error("Cache nesting is too deep");
        if (source.is_object() && source.contains(reference_key))
            throw std::runtime_error("Reserved cache reference key in source data");
        // Build children directly: copying a whole subtree here would copy it
        // again at every nested level before replacing each copied child.
        Json encoded;
        if constexpr(!std::is_lvalue_reference_v<Source>) {
            // A save owns this transient JSON tree. Reuse its allocations and
            // move pooled blocks out of it as we traverse, instead of building
            // a second complete tree and later destroying the first one.
            encoded=std::move(source);
            if(encoded.is_structured())for(auto& child:encoded)
                child=encode(std::move(child),depth+1);
        } else if (source.is_object()) {
            encoded = Json::object();
            for (auto it = source.begin(); it != source.end(); ++it)
                encoded[it.key()] = encode(it.value(), depth + 1);
        } else if (source.is_array()) {
            encoded = Json::array();
            encoded.get_ref<Json::array_t&>().reserve(source.size());
            for (const auto& child : source) encoded.push_back(encode(child, depth + 1));
        } else encoded = source;
        // Pool large leaves and repeated compound blocks after their children.
        // Full serialized equality resolves hash collisions; a hash alone never
        // decides that two geometries or occurrence records are identical.
        if (!encoded.is_structured() && !encoded.is_string()) return encoded;
        auto key = encoded.dump();
        if (key.size() < minimum_block_size) return encoded;
        if(encoded.is_string() && encoded.get_ref<const Json::string_t&>().size()>=4096) {
            // Large archives/binary packets are already owned by the pool.
            // Index their immutable bytes there, rather than retaining another
            // full escaped copy as a hash-table key. dump() above still checks
            // encoding and applies the same pooling threshold as before.
            const auto& bytes=encoded.get_ref<const Json::string_t&>();
            if(const auto found=string_indices.find(bytes);found!=string_indices.end())
                return Json{{reference_key,found->second}};
            const auto index=values.size();
            values.push_back(std::move(encoded));
            // Moving a json (including array growth) retains its separately
            // allocated string. These views live only as long as this Writer.
            string_indices.emplace(values.back().get_ref<const Json::string_t&>(),index);
            return Json{{reference_key,index}};
        }
        const auto found = indices.find(key);
        if (found != indices.end()) return Json{{reference_key, found->second}};
        const auto index = values.size();
        indices.emplace(std::move(key), index);
        values.push_back(std::move(encoded));
        return Json{{reference_key, index}};
    }
private:
    std::unordered_map<std::string, std::size_t> indices;
    std::unordered_map<std::string_view, std::size_t> string_indices;
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
    Json result;
    if (source.is_object()) {
        result = Json::object();
        for (auto it = source.begin(); it != source.end(); ++it)
            result[it.key()] = decode(it.value(), values, limit, depth + 1);
    } else if (source.is_array()) {
        result = Json::array();
        result.get_ref<Json::array_t&>().reserve(source.size());
        for (const auto& child : source) result.push_back(decode(child, values, limit, depth + 1));
    } else result = source;
    return result;
}
}

nlohmann::json pack_cache_storage(const nlohmann::json& value) {
    Writer writer;
    auto root = writer.encode(value);
    return {{"format", "zima-shared-cache-v1"}, {"root", std::move(root)},
            {"values", std::move(writer.values)}};
}

nlohmann::json pack_cache_storage(nlohmann::json&& value) {
    Writer writer;
    auto root=writer.encode(std::move(value));
    return {{"format","zima-shared-cache-v1"},{"root",std::move(root)},
            {"values",std::move(writer.values)}};
}

nlohmann::json unpack_cache_storage(const nlohmann::json& storage) {
    if (!storage.is_object() || storage.value("format", "") != "zima-shared-cache-v1" ||
        !storage.contains("values") || !storage.at("values").is_array())
        throw std::runtime_error("Unsupported shared cache format");
    const auto& values = storage.at("values");
    return decode(storage.at("root"), values, values.size());
}
}
