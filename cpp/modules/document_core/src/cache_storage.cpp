#include <zima/document/cache_storage.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "cache_compression.inc"

namespace zima::document {
namespace {
using Json = nlohmann::json;
constexpr auto reference_key = "$zima_cache";
constexpr std::size_t minimum_block_size = 256;
constexpr std::size_t maximum_depth = 256;

// Cache block identity is the exact JSON text produced by nlohmann::json.
// Building that complete text for every parent block made packing a large
// history quadratic: each successively larger Body packet serialized all of
// its children again merely to use the string as a hash-table key.  Keep the
// identical block threshold and equality, but calculate a compact recursive
// fingerprint first and compare only the few blocks in its collision bucket.
// Leaf hashing mirrors nlohmann's serialized equality without allocating one
// tiny dump string per coordinate.  That distinction matters for native BRep
// packets containing millions of numeric leaves.  Finite doubles are emitted
// with a shortest round-trip representation, so their bit pattern (including
// signed zero) is the exact identity; non-finite values serialize as null.
constexpr std::uint64_t fingerprint_offset = 1469598103934665603ULL;
constexpr std::uint64_t fingerprint_prime = 1099511628211ULL;

void fingerprint_bytes(std::uint64_t& value, std::string_view bytes) {
    for (const unsigned char byte : bytes) {
        value ^= byte;
        value *= fingerprint_prime;
    }
}

void fingerprint_size(std::uint64_t& value, std::size_t size) {
    for (std::size_t index = 0; index < sizeof(size); ++index) {
        value ^= static_cast<unsigned char>(size >> (index * 8));
        value *= fingerprint_prime;
    }
}

void fingerprint_u64(std::uint64_t& value, std::uint64_t number) {
    for (std::size_t index = 0; index < sizeof(number); ++index) {
        value ^= static_cast<unsigned char>(number >> (index * 8));
        value *= fingerprint_prime;
    }
}

std::uint64_t large_string_fingerprint(std::string_view bytes) {
    // Large BRep/base64 packets dominate save time. Hash eight bytes per step;
    // exact string equality below remains authoritative for every collision.
    std::uint64_t result=0x9e3779b97f4a7c15ULL^static_cast<std::uint64_t>(bytes.size());
    const auto* data=reinterpret_cast<const unsigned char*>(bytes.data());
    std::size_t remaining=bytes.size();
    while(remaining>=sizeof(std::uint64_t)) {
        std::uint64_t word{};std::memcpy(&word,data,sizeof(word));
        word^=word>>23;word*=0x2127599bf4325c37ULL;word^=word>>47;
        result^=word;result=(result<<27|result>>37)*0x3c79ac492ba7b653ULL+0x1c69b3f74ac4ae35ULL;
        data+=sizeof(word);remaining-=sizeof(word);
    }
    std::uint64_t tail{};
    if(remaining)std::memcpy(&tail,data,remaining);
    result^=tail;result^=result>>33;result*=0xff51afd7ed558ccdULL;
    result^=result>>33;result*=0xc4ceb9fe1a85ec53ULL;result^=result>>33;
    return result;
}

bool serialized_null(const Json& value) {
    return value.is_null() || (value.is_number_float() &&
        !std::isfinite(value.get<Json::number_float_t>()));
}

void fingerprint_leaf(std::uint64_t& result, const Json& value) {
    const auto append = [&](std::string_view bytes) { fingerprint_bytes(result, bytes); };
    if (serialized_null(value)) {
        append("null");
    } else if (value.is_boolean()) {
        append(value.get<bool>() ? "true" : "false");
    } else if (value.is_string()) {
        append("string");
        const auto& text = value.get_ref<const Json::string_t&>();
        fingerprint_size(result, text.size());
        append(text);
    } else if (value.is_number_unsigned()) {
        append("uint");
        fingerprint_u64(result, value.get<Json::number_unsigned_t>());
    } else if (value.is_number_integer()) {
        const auto number = value.get<Json::number_integer_t>();
        if (number >= 0) {
            append("uint");
            fingerprint_u64(result, static_cast<std::uint64_t>(number));
        } else {
            append("int");
            fingerprint_u64(result, static_cast<std::uint64_t>(number));
        }
    } else if (value.is_number_float()) {
        append("float");
        const auto number = value.get<Json::number_float_t>();
        static_assert(sizeof(number) == sizeof(std::uint64_t));
        std::uint64_t bits{};
        std::memcpy(&bits, &number, sizeof(bits));
        fingerprint_u64(result, bits);
    } else {
        // Binary/discarded values are not part of the native document schema.
        // Retain the library's exact representation if one reaches this path.
        const auto text = value.dump();
        append("other"); fingerprint_size(result, text.size()); append(text);
    }
}

std::uint64_t exact_json_fingerprint(const Json& value) {
    std::uint64_t result = fingerprint_offset;
    const auto append = [&](std::string_view bytes) { fingerprint_bytes(result, bytes); };
    if (value.is_object()) {
        append("object"); fingerprint_size(result, value.size());
        for (auto item = value.begin(); item != value.end(); ++item) {
            fingerprint_size(result, item.key().size()); append(item.key());
            const auto child = exact_json_fingerprint(item.value());
            fingerprint_bytes(result, std::string_view(
                reinterpret_cast<const char*>(&child), sizeof(child)));
        }
    } else if (value.is_array()) {
        append("array"); fingerprint_size(result, value.size());
        for (const auto& child_value : value) {
            const auto child = exact_json_fingerprint(child_value);
            fingerprint_bytes(result, std::string_view(
                reinterpret_cast<const char*>(&child), sizeof(child)));
        }
    } else {
        append("leaf");
        fingerprint_leaf(result, value);
    }
    return result;
}

bool exact_json_text_equal(const Json& left, const Json& right) {
    if (left.is_object() != right.is_object() ||
        left.is_array() != right.is_array()) return false;
    if (left.is_object()) {
        if (left.size() != right.size()) return false;
        auto a = left.begin(), b = right.begin();
        for (; a != left.end(); ++a, ++b)
            if (a.key() != b.key() ||
                !exact_json_text_equal(a.value(), b.value())) return false;
        return true;
    }
    if (left.is_array()) {
        if (left.size() != right.size()) return false;
        for (std::size_t index = 0; index < left.size(); ++index)
            if (!exact_json_text_equal(left[index], right[index])) return false;
        return true;
    }
    if (serialized_null(left) || serialized_null(right))
        return serialized_null(left) && serialized_null(right);
    if (left.is_boolean() || right.is_boolean())
        return left.is_boolean() && right.is_boolean() && left.get<bool>() == right.get<bool>();
    if (left.is_string() || right.is_string())
        return left.is_string() && right.is_string() &&
            left.get_ref<const Json::string_t&>() == right.get_ref<const Json::string_t&>();
    const auto left_integral = left.is_number_integer() || left.is_number_unsigned();
    const auto right_integral = right.is_number_integer() || right.is_number_unsigned();
    if (left_integral || right_integral) {
        if (!left_integral || !right_integral) return false;
        const auto negative = [](const Json& value) {
            return value.is_number_integer() &&
                value.get<Json::number_integer_t>() < 0;
        };
        if (negative(left) != negative(right)) return false;
        if (negative(left))
            return left.get<Json::number_integer_t>() == right.get<Json::number_integer_t>();
        const auto nonnegative = [](const Json& value) {
            return value.is_number_unsigned()
                ? value.get<Json::number_unsigned_t>()
                : static_cast<Json::number_unsigned_t>(value.get<Json::number_integer_t>());
        };
        return nonnegative(left) == nonnegative(right);
    }
    if (left.is_number_float() || right.is_number_float()) {
        if (!left.is_number_float() || !right.is_number_float()) return false;
        const auto a = left.get<Json::number_float_t>();
        const auto b = right.get<Json::number_float_t>();
        std::uint64_t a_bits{}, b_bits{};
        std::memcpy(&a_bits, &a, sizeof(a_bits));
        std::memcpy(&b_bits, &b, sizeof(b_bits));
        return a_bits == b_bits;
    }
    return left.dump() == right.dump();
}

std::size_t capped_size_add(std::size_t value, std::size_t addition) {
    if (value >= minimum_block_size ||
        addition >= minimum_block_size - value) return minimum_block_size;
    return value + addition;
}

std::size_t decimal_digits(std::size_t value) {
    std::size_t result = 1;
    while (value >= 10) { value /= 10; ++result; }
    return result;
}

std::size_t serialized_string_size(std::string_view value) {
    std::size_t result = 2;
    for (const unsigned char byte : value) {
        const std::size_t count = byte < 0x20
            ? ((byte == '\b' || byte == '\t' || byte == '\n' ||
                byte == '\f' || byte == '\r') ? 2 : 6)
            : (byte == '"' || byte == '\\') ? 2 : 1;
        result = capped_size_add(result, count);
        if (result == minimum_block_size) break;
    }
    return result;
}

std::size_t serialized_leaf_size(const Json& value) {
    if (value.is_string())
        return serialized_string_size(value.get_ref<const Json::string_t&>());
    return std::min(minimum_block_size, value.dump().size());
}

class Writer {
public:
    struct Encoded {
        Json value;
        std::size_t serialized_size{};
    };
    struct Timings {
        double string_lookup_ms{};
        double fingerprint_ms{};
        double equality_ms{};
        std::size_t large_strings{};
        std::size_t compound_blocks{};
        std::size_t equality_checks{};
    } timings;
    Json values = Json::array();
    template<class Source>
    Json encode(Source&& source, std::size_t depth = 0) {
        return encode_node(std::forward<Source>(source), depth).value;
    }
private:
    Encoded reference(std::size_t index) const {
        // {"$zima_cache":N}: braces, quoted key, colon and decimal index.
        return {Json{{reference_key, index}}, 16 + decimal_digits(index)};
    }

    template<class Source>
    Encoded encode_node(Source&& source, std::size_t depth) {
        if (depth > maximum_depth) throw std::runtime_error("Cache nesting is too deep");
        if (source.is_object() && source.contains(reference_key))
            throw std::runtime_error("Reserved cache reference key in source data");
        // Build children directly: copying a whole subtree here would copy it
        // again at every nested level before replacing each copied child.
        Json encoded;
        std::size_t serialized_size{};
        if constexpr(!std::is_lvalue_reference_v<Source>) {
            // A save owns this transient JSON tree. Reuse its allocations and
            // move pooled blocks out of it as we traverse, instead of building
            // a second complete tree and later destroying the first one.
            encoded=std::move(source);
            if(encoded.is_array()) {
                serialized_size=2;
                for(std::size_t index=0;index<encoded.size();++index) {
                    auto child=encode_node(std::move(encoded[index]),depth+1);
                    encoded[index]=std::move(child.value);
                    if(index)serialized_size=capped_size_add(serialized_size,1);
                    serialized_size=capped_size_add(serialized_size,child.serialized_size);
                }
            } else if(encoded.is_object()) {
                serialized_size=2;bool first=true;
                for(auto item=encoded.begin();item!=encoded.end();++item) {
                    auto child=encode_node(std::move(item.value()),depth+1);
                    item.value()=std::move(child.value);
                    if(!first)serialized_size=capped_size_add(serialized_size,1);
                    first=false;
                    serialized_size=capped_size_add(serialized_size,serialized_string_size(item.key()));
                    serialized_size=capped_size_add(serialized_size,1);
                    serialized_size=capped_size_add(serialized_size,child.serialized_size);
                }
            } else serialized_size=serialized_leaf_size(encoded);
        } else if (source.is_object()) {
            encoded = Json::object();
            serialized_size=2;bool first=true;
            for (auto it = source.begin(); it != source.end(); ++it) {
                auto child=encode_node(it.value(),depth+1);
                encoded[it.key()]=std::move(child.value);
                if(!first)serialized_size=capped_size_add(serialized_size,1);
                first=false;
                serialized_size=capped_size_add(serialized_size,serialized_string_size(it.key()));
                serialized_size=capped_size_add(serialized_size,1);
                serialized_size=capped_size_add(serialized_size,child.serialized_size);
            }
        } else if (source.is_array()) {
            encoded = Json::array();
            encoded.get_ref<Json::array_t&>().reserve(source.size());
            serialized_size=2;
            for (const auto& value : source) {
                auto child=encode_node(value,depth+1);
                if(!encoded.empty())serialized_size=capped_size_add(serialized_size,1);
                serialized_size=capped_size_add(serialized_size,child.serialized_size);
                encoded.push_back(std::move(child.value));
            }
        } else { encoded = source; serialized_size=serialized_leaf_size(encoded); }
        // Pool large leaves and repeated compound blocks after their children.
        // Full serialized equality resolves hash collisions; a hash alone never
        // decides that two geometries or occurrence records are identical.
        if (!encoded.is_structured() && !encoded.is_string()) return {std::move(encoded),serialized_size};
        if(encoded.is_string() && encoded.get_ref<const Json::string_t&>().size()>=4096) {
            // Large archives/binary packets are already owned by the pool.
            // Index their immutable bytes there, rather than retaining another
            // full escaped copy as a hash-table key. A raw string this large
            // always exceeds the pooling threshold. Exact duplicates have
            // already passed UTF-8 validation; validate each distinct value.
            const auto& bytes=encoded.get_ref<const Json::string_t&>();
            const auto lookup_started=profile_?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
            const auto fingerprint=large_string_fingerprint(bytes);
            const auto found=string_indices.find(fingerprint);
            if(profile_) {
                timings.string_lookup_ms+=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-lookup_started).count();
                ++timings.large_strings;
            }
            if(found!=string_indices.end())
                for(const auto index:found->second)
                    if(values.at(index).get_ref<const Json::string_t&>()==bytes)
                        return reference(index);
            // Native BReps and base64 buffers are ASCII. Their bytes already
            // prove UTF-8 validity; avoid allocating an escaped copy merely
            // to validate them. Non-ASCII input keeps the established check.
            if(std::any_of(bytes.begin(),bytes.end(),[](unsigned char byte){return byte>=128;}))
                static_cast<void>(encoded.dump());
            const auto index=values.size();
            values.push_back(std::move(encoded));
            // Moving a json (including array growth) retains its separately
            // allocated string. These views live only as long as this Writer.
            string_indices[fingerprint].push_back(index);
            return reference(index);
        }
        if (serialized_size < minimum_block_size)
            return {std::move(encoded),serialized_size};
        if(profile_)++timings.compound_blocks;
        const auto fingerprint_started=profile_?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        const auto fingerprint = exact_json_fingerprint(encoded);
        if(profile_)timings.fingerprint_ms+=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-fingerprint_started).count();
        if (const auto found = indices.find(fingerprint);
            found != indices.end()) {
            for (const auto index : found->second) {
                const auto equality_started=profile_?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
                const auto equal=exact_json_text_equal(encoded,values.at(index));
                if(profile_) {
                    timings.equality_ms+=std::chrono::duration<double,std::milli>(
                        std::chrono::steady_clock::now()-equality_started).count();
                    ++timings.equality_checks;
                }
                if (equal)
                    return reference(index);
            }
        }
        const auto index = values.size();
        indices[fingerprint].push_back(index);
        values.push_back(std::move(encoded));
        return reference(index);
    }
    bool profile_=std::getenv("ZIMA_CPP_PROFILE_NATIVE_IO")!=nullptr;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> indices;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> string_indices;
};

class Reader {
public:
    explicit Reader(Json& values) : values_(values), uses_(values.size()),
        heights_(values.size(), unknown_height), expanded_(values.size()) {}

    Json read(Json& root) {
        // Validate the reachable DAG before consuming it. Track subtree height,
        // so reuse cannot bypass the original expanded-tree nesting limit.
        const bool profile=std::getenv("ZIMA_CPP_PROFILE_NATIVE_IO")!=nullptr;
        const auto started=profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        inspect(root, values_.size(), 0);
        const auto inspected=profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        consume(root);
        auto result=std::move(root);
        if(profile) {
            const auto finished=std::chrono::steady_clock::now();
            std::fprintf(stderr,"Native IO cache reader inspect: %.3f ms; consume: %.3f ms; values=%zu\n",
                std::chrono::duration<double,std::milli>(inspected-started).count(),
                std::chrono::duration<double,std::milli>(finished-inspected).count(),values_.size());
        }
        return result;
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

    void consume(Json& source) {
        if(source.is_object()&&source.contains(reference_key)) {
            const auto index=source.at(reference_key).get<std::size_t>();
            auto& value=values_.at(index);
            if(!expanded_[index]) {
                consume(value);
                expanded_[index]=true;
            }
            // Decode a pooled block once. Retain it while another block needs
            // it, then transfer its allocations to its final consumer.
            if(--uses_[index]==0)source=std::move(value);
            else source=value;
            return;
        }
        // Non-reference nodes already own their final allocation. Expand their
        // children in place instead of self-moving every one of the millions
        // of scalar/array nodes in a large native cache.
        if(source.is_structured())for(auto& child:source)consume(child);
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
    const bool profile=std::getenv("ZIMA_CPP_PROFILE_NATIVE_IO")!=nullptr;
    const auto started=profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    Writer writer;
    auto root=writer.encode(std::move(value));
    const auto encoded=profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    Json storage=Json::object();
    storage["format"]="zima-shared-cache-v1";
    storage["root"]=std::move(root);
    storage["values"]=std::move(writer.values);
    auto compressed = compress_cache_storage(std::move(storage));
    if (profile) {
        const auto finished = std::chrono::steady_clock::now();
        const auto milliseconds = [](auto from, auto to) {
            return std::chrono::duration<double, std::milli>(to - from).count();
        };
        std::fprintf(stderr,
            "Native IO cache writer encode: %.3f ms; compression: %.3f ms; strings=%zu/%.3f ms; blocks=%zu fingerprint=%.3f ms; equality=%zu/%.3f ms\n",
            milliseconds(started, encoded), milliseconds(encoded, finished),
            writer.timings.large_strings,writer.timings.string_lookup_ms,
            writer.timings.compound_blocks,writer.timings.fingerprint_ms,writer.timings.equality_checks,
            writer.timings.equality_ms);
    }
    return compressed;
}

nlohmann::json unpack_cache_storage(const nlohmann::json& storage) {
    auto owned=storage;
    return unpack_cache_storage(std::move(owned));
}

nlohmann::json unpack_cache_storage(nlohmann::json&& storage) {
    const bool profile=std::getenv("ZIMA_CPP_PROFILE_NATIVE_IO")!=nullptr;
    const auto started=profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    storage=decompress_cache_storage(std::move(storage));
    const auto decompressed=profile?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    if (!storage.is_object() || storage.value("format", "") != "zima-shared-cache-v1" ||
        !storage.contains("values") || !storage.at("values").is_array())
        throw std::runtime_error("Unsupported shared cache format");
    Reader reader(storage.at("values"));
    auto result=reader.read(storage.at("root"));
    if(profile)
        std::fprintf(stderr,"Native IO cache reader decompression: %.3f ms; expansion: %.3f ms\n",
            std::chrono::duration<double,std::milli>(decompressed-started).count(),
            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-decompressed).count());
    return result;
}
}
