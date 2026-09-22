#pragma once
#include <map>
#include <string>
#include <nlohmann/json_fwd.hpp>
namespace zima::sketcher {
// Embedded native annotation, separate from profile geometry and solver entities.
struct SymbolInstance {
    std::string id, definition, variant;
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
