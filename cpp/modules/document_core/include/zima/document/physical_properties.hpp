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
// Refresh only physical relations and their dependents; this uses cached
// measures, never a kernel calculation, dependency refresh or dimension edit.
template<class Document> void refresh_physical_relations(Document& doc,const std::map<std::string,double>& values) {
    std::set<std::string> physical{"model.mass","model.area","model.volume","material.density"};
    std::set<std::string> unavailable;
    for(const auto& key:physical)if(!values.contains(key))unavailable.insert(key);
    int precision=3;
    if(const auto it=doc.document_precision.find("decimal_places");it!=doc.document_precision.end())precision=std::stoi(it->second);
    std::vector<ModelRelation> selected;
    std::vector<std::string> targets;
    for(const auto& relation:doc.relations) {
        bool relevant=false,missing=false;
        for(std::size_t i=0;i<relation.expression.size();) {
            const auto start=i;
            while(i<relation.expression.size() && (std::isalnum(static_cast<unsigned char>(relation.expression[i])) || relation.expression[i]=='_' || relation.expression[i]=='.'))++i;
            if(i==start){++i;continue;}
            const auto key=relation.expression.substr(start,i-start);
            relevant|=physical.contains(key);missing|=unavailable.contains(key);
        }
        if(!relevant)continue;
        physical.insert(relation.target);
        targets.push_back(relation.target);
        if(missing)unavailable.insert(relation.target);
        else selected.push_back(relation);
    }
    // Evaluate in one pass so dependent expressions use unrounded values.
    const auto calculated=evaluate_relations(doc.user_parameters,selected,values,precision);
    for(const auto& target:targets) {
        const auto value=unavailable.contains(target)?std::string{}:calculated.at(target);
        doc.user_parameters[target]=value;
        doc.user_parameter_values[target][""]=value;
        if(std::ranges::find(doc.user_parameter_order,target)==doc.user_parameter_order.end())doc.user_parameter_order.push_back(target);
    }
}
} // namespace zima::document
