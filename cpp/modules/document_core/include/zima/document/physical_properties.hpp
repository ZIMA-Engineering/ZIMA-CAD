#pragma once
#include <zima/document/part_document.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <optional>
#include <set>
#include <stdexcept>

namespace zima::document {
inline double length_unit_mm(const std::string& unit) {
    if(unit=="mm")return 1; if(unit=="cm")return 10;
    if(unit=="m")return 1000; if(unit=="in")return 25.4;
    throw std::invalid_argument("Unsupported length unit: "+unit);
}
inline double mass_unit_kg(const std::string& unit) {
    if(unit=="kg")return 1; if(unit=="g")return .001;
    if(unit=="t")return 1000; if(unit=="lb")return .45359237;
    throw std::invalid_argument("Unsupported mass unit: "+unit);
}
template<class Document> void validate_physical_units(const Document& doc) {
    static_cast<void>(length_unit_mm(doc.document_units.at("Length")));
    static_cast<void>(mass_unit_kg(doc.document_units.at("Mass")));
}
template<class Document> std::optional<double> material_density_kg_mm3(const Document& doc) {
    const auto density=doc.physical_parameters.find("MASS_DENSITY");
    const auto unit=doc.physical_parameter_units.find("MASS_DENSITY");
    if(density==doc.physical_parameters.end() || unit==doc.physical_parameter_units.end())return {};
    std::size_t count{};double value{};
    try {value=std::stod(density->second,&count);} catch(const std::exception&) {return {};}
    if(count!=density->second.size() || !std::isfinite(value) || value<=0)return {};
    const auto& u=unit->second;
    if(u=="kg/mm^3")return value;
    if(u=="kg/m^3")return value*1e-9;
    if(u=="g/cm^3")return value*1e-6;
    if(u=="lb/in^3")return value*.45359237/std::pow(25.4,3);
    return {};
}
template<class Document> std::map<std::string,double> physical_values_from_totals(
    const Document& doc,double volume_mm3,double area_mm2,std::optional<double> mass_kg,
    std::optional<double> density={}) {
    const double length=length_unit_mm(doc.document_units.at("Length"));
    const double mass=mass_unit_kg(doc.document_units.at("Mass"));
    std::map<std::string,double> result{{"model.volume",volume_mm3/std::pow(length,3)},
        {"model.area",area_mm2/(length*length)}};
    if(mass_kg)result["model.mass"]=*mass_kg/mass;
    if(density)result["material.density"]=*density*std::pow(length,3)/mass;
    return result;
}
inline std::map<std::string,double> physical_values(const PartDocument& doc,
    const std::vector<zima::kernel::BodyResult>& boundaries) {
    const double volume=boundaries.empty()?0:std::abs(boundaries.back().volume);
    const double area=boundaries.empty()?0:std::abs(boundaries.back().surface_area);
    const auto density=material_density_kg_mm3(doc);
    if(boundaries.empty() && !doc.history.empty()) {
        auto result=physical_values_from_totals(doc,0,0,std::nullopt,density);
        result.erase("model.volume");result.erase("model.area");return result;
    }
    const auto mass=volume==0?std::optional<double>{0}:density?std::optional<double>{volume * *density}:std::nullopt;
    return physical_values_from_totals(doc,volume,area,mass,density);
}
} // namespace zima::document
