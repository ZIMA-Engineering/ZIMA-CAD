#pragma once
#include <zima/sketcher/sketch.hpp>
#include <map>
#include <string>
#include <vector>
namespace zima::symbols {
struct TextField {
    std::string sketch_id, text_id;
    std::vector<std::string> choices;
    bool allow_custom{true};
};
struct Variant {
    std::vector<std::string> sketches;
    std::map<std::string, std::string> text_values;
    std::vector<std::string> hidden_texts;
};
// All sketches share local XY. Occurrence placement is annotation placement,
// independent of the shared history-container placement contract.
struct Definition {
    std::string id, name;
    std::vector<sketcher::Sketch> sketches;
    std::array<double, 2> insertion_point{};
    std::map<std::string, TextField> fields;
    std::map<std::string, Variant> variants;
    // Sketch ID -> persisted curve ID -> display/plot pen (white or yellow).
    std::map<std::string,std::map<std::string,std::string>> pens;
    std::string default_variant, variant_source;
    void validate() const;
    [[nodiscard]] std::vector<sketcher::Sketch> evaluate(const std::string& variant,
        const std::map<std::string,std::string>& overrides = {}) const;
    [[nodiscard]] std::string serialized() const;
    [[nodiscard]] static Definition from_serialized(const std::string& data);
    [[nodiscard]] static Definition load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
};
[[nodiscard]] Definition projection_method();
[[nodiscard]] kernel::ViewerMesh instance_mesh(const sketcher::SymbolInstance&,
    const std::string& cad_variant = {});
}
