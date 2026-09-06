#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace zima::document {

// Identity is the existing ZIMA owner and semantic dimension key. Numbers are
// document metadata, never geometry identity or an index into visible geometry.
struct DimensionParameter {
    std::string owner_id;
    std::string semantic_key;
    std::string owner_name;
};

class DimensionIdentifiers {
public:
    using Key = std::pair<std::string, std::string>;
    void synchronize(const std::vector<DimensionParameter>& parameters);
    // Keep retired allocations across undo/redo and deletion.
    void retain(const DimensionIdentifiers& previous);
    [[nodiscard]] std::string identifier(const std::string& owner,
                                         const std::string& key) const;
    [[nodiscard]] std::string serialized() const;
    [[nodiscard]] static DimensionIdentifiers from_serialized(const std::string& data);
    [[nodiscard]] std::uint64_t allocation_count() const { return numbers_.size(); }
    bool operator==(const DimensionIdentifiers&) const = default;
private:
    std::map<Key, std::uint64_t> numbers_;
    std::uint64_t next_{1};
};

struct HistoryContainer;
struct ConstructionObject;
void append_dimension_parameters(std::vector<DimensionParameter>& result,
                                 const HistoryContainer& feature);
void append_dimension_parameters(std::vector<DimensionParameter>& result,
                                 const ConstructionObject& object);
} // namespace zima::document
