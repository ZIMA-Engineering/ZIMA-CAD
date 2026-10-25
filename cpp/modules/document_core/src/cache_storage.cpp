#include <zima/document/cache_storage.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <algorithm>
#include <vector>
#include "cache_compression.inc"

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
        if(encoded.is_string() && encoded.get_ref<const Json::string_t&>().size()>=4096) {
            // Large archives/binary packets are already owned by the pool.
            // Index their immutable bytes there, rather than retaining another
            // full escaped copy as a hash-table key. A raw string this large
            // always exceeds the pooling threshold. Exact duplicates have
            // already passed UTF-8 validation; validate each distinct value.
            const auto& bytes=encoded.get_ref<const Json::string_t&>();
            if(const auto found=string_indices.find(bytes);found!=string_indices.end())
                return Json{{reference_key,found->second}};
            // Native BReps and base64 buffers are ASCII. Their bytes already
            // prove UTF-8 validity; avoid allocating an escaped copy merely
            // to validate them. Non-ASCII input keeps the established check.
            if(std::any_of(bytes.begin(),bytes.end(),[](unsigned char byte){return byte>=128;}))
                static_cast<void>(encoded.dump());
            const auto index=values.size();
            values.push_back(std::move(encoded));
            // Moving a json (including array growth) retains its separately
            // allocated string. These views live only as long as this Writer.
            string_indices.emplace(values.back().get_ref<const Json::string_t&>(),index);
            return Json{{reference_key,index}};
        }
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
    std::unordered_map<std::string_view, std::size_t> string_indices;
};

class Reader {
public:
    explicit Reader(Json& values) : values_(values), uses_(values.size()),
        heights_(values.size(), unknown_height), expanded_(values.size()) {}

    Json read(Json& root) {
        // Validate the reachable DAG before consuming it. Track subtree height,
        // so reuse cannot bypass the original expanded-tree nesting limit.
        inspect(root, values_.size(), 0);
        return consume(root);
    }
private:
    static constexpr std::size_t unknown_height=maximum_depth+1;
    Json& values_;
    std::vector<std::size_t> uses_, heights_;
    std::vector<bool> expanded_;

    std::size_t inspect(const Json& source, std::size_t limit, std::size_t depth) {
        if(depth>maximum_depth)throw std::runtime_error("Cache nesting is too deep");
        std::size_t height=0;
        if(source.is_object()&&source.contains(reference_key)) {
            const auto& id=source.at(reference_key);
            if(source.size()!=1||!id.is_number_unsigned())throw std::runtime_error("Invalid cache reference");
            const auto index=id.get<std::size_t>();
            if(index>=limit)throw std::runtime_error("Invalid cache reference order");
            ++uses_[index];
            if(heights_[index]==unknown_height)
                heights_[index]=inspect(values_.at(index),index,depth+1);
            height=1+heights_[index];
        } else if(source.is_structured()) {
            for(const auto& child:source)
                height=std::max(height,1+inspect(child,limit,depth+1));
        }
        if(height>maximum_depth-depth)throw std::runtime_error("Cache nesting is too deep");
        return height;
    }

    Json consume(Json& source) {
        if(source.is_object()&&source.contains(reference_key)) {
            const auto index=source.at(reference_key).get<std::size_t>();
            auto& value=values_.at(index);
            if(!expanded_[index]) {
                value=consume(value);
                expanded_[index]=true;
            }
            // Decode a pooled block once. Retain it while another block needs
            // it, then transfer its allocations to its final consumer.
            if(--uses_[index]==0)return std::move(value);
            return value;
        }
        if(source.is_structured())for(auto& child:source)child=consume(child);
        return std::move(source);
    }
};
}

nlohmann::json pack_cache_storage(const nlohmann::json& value) {
    Writer writer;
    auto root = writer.encode(value);
    Json storage=Json::object();
    storage["format"]="zima-shared-cache-v1";
    storage["root"]=std::move(root);
    storage["values"]=std::move(writer.values);
    return compress_cache_storage(std::move(storage));
}

nlohmann::json pack_cache_storage(nlohmann::json&& value) {
    Writer writer;
    auto root=writer.encode(std::move(value));
    Json storage=Json::object();
    storage["format"]="zima-shared-cache-v1";
    storage["root"]=std::move(root);
    storage["values"]=std::move(writer.values);
    return compress_cache_storage(std::move(storage));
}

nlohmann::json unpack_cache_storage(const nlohmann::json& storage) {
    auto owned=storage;
    return unpack_cache_storage(std::move(owned));
}

nlohmann::json unpack_cache_storage(nlohmann::json&& storage) {
    storage=decompress_cache_storage(std::move(storage));
    if (!storage.is_object() || storage.value("format", "") != "zima-shared-cache-v1" ||
        !storage.contains("values") || !storage.at("values").is_array())
        throw std::runtime_error("Unsupported shared cache format");
    Reader reader(storage.at("values"));
    return reader.read(storage.at("root"));
}
}
