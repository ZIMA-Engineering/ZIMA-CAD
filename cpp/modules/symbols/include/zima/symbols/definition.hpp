#pragma once
#include <zima/sketcher/sketch.hpp>
#include <map>
#include <string>
#include <vector>

namespace zima::symbols {
// Library definition only. Placement and document references belong to instances.
struct Group {
    std::vector<std::string> geometry;
    bool axis{};
};
struct Definition {
    std::string id, name;
    sketcher::Sketch sketch;
    std::array<double, 2> insertion_point{};
    std::map<std::string, Group> groups;
    std::map<std::string, std::vector<std::string>> variants;
    std::string default_variant;
    // Semantic input declaration, not an executable expression.
    std::string variant_source;
    void validate() const;
    [[nodiscard]] std::vector<std::string> visible_geometry(const std::string& variant) const;
    [[nodiscard]] std::string serialized() const;
    [[nodiscard]] static Definition from_serialized(const std::string& data);
    [[nodiscard]] static Definition load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
};
[[nodiscard]] Definition projection_method();
}
