#include <zima/symbols/definition.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
namespace zima::sketcher {
void SymbolInstance::validate() const {
    if(id.empty()||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(angle_degrees)||!std::isfinite(scale)||scale<=0)throw std::invalid_argument("Invalid symbol placement");
    const auto d=symbols::Definition::from_serialized(definition);
    if(use_cad_variant&&d.variant_source.empty())throw std::invalid_argument("Symbol has no CAD variant source");
    static_cast<void>(d.evaluate(variant,text_values));
}
void to_json(nlohmann::json& j,const SymbolInstance& v) {
    v.validate();j={{"id",v.id},{"definition",nlohmann::json::parse(v.definition)},{"variant",v.variant},{"use_cad_variant",v.use_cad_variant},
        {"text_values",v.text_values},{"x",v.x},{"y",v.y},{"angle",v.angle_degrees},{"scale",v.scale},{"visible",v.visible}};
}
void from_json(const nlohmann::json& j,SymbolInstance& v) {
    v.id=j.at("id");v.definition=symbols::Definition::from_serialized(j.at("definition").dump()).serialized();v.variant=j.at("variant");
    v.use_cad_variant=j.at("use_cad_variant");v.text_values=j.at("text_values").get<decltype(v.text_values)>();
    v.x=j.at("x");v.y=j.at("y");v.angle_degrees=j.at("angle");v.scale=j.at("scale");v.visible=j.at("visible");v.validate();
}
}
