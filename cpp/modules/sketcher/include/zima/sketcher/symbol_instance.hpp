#pragma once
#include <map>
#include <string>
#include <nlohmann/json_fwd.hpp>
namespace zima::sketcher {
// Embedded annotation. In a Sketch its insertion point is an ordinary native
// solver point; standalone sheet/model placements do not own a Sketch point.
struct SymbolInstance {
    std::string id, definition, variant;
    std::string anchor_point_id;
    bool use_cad_variant{};
    std::map<std::string, std::string> text_values;
    double x{}, y{}, angle_degrees{}, scale{1.0};
    bool visible{true};
    void validate() const;
    bool operator==(const SymbolInstance&) const = default;
};
void to_json(nlohmann::json&, const SymbolInstance&);
void from_json(const nlohmann::json&, SymbolInstance&);
}
