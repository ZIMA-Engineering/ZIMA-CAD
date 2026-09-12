#include <zima/document/engineering_metadata.hpp>
#include <zima/document/metadata.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <set>
#include <stdexcept>
namespace zima::document {
namespace {
void nonempty(const std::string& name) {
    UserParameterData value;value.order.push_back(name);normalize_user_parameters(value);
}
void text(const std::string& value) {
    if(value.size()>65536 || value.find('\0')!=std::string::npos)
        throw std::invalid_argument("Metadata text is too long or contains a null character.");
}
}
std::vector<std::string> material_unit_choices(const std::string& property) {
    static const std::map<std::string,std::vector<std::string>> known{
        {"YOUNG_MODULUS",{"MPa","GPa","kPa","Pa","psi"}},{"SHEAR_MODULUS",{"MPa","GPa","kPa","Pa","psi"}},
        {"STRESS_LIMIT_FOR_TENSION",{"MPa","GPa","kPa","Pa","psi"}},{"STRESS_LIMIT_FOR_COMPRESSION",{"MPa","GPa","kPa","Pa","psi"}},
        {"STRESS_LIMIT_FOR_SHEAR",{"MPa","GPa","kPa","Pa","psi"}},{"MASS_DENSITY",{"kg/mm^3","kg/m^3","g/cm^3","lb/in^3"}},
        {"THERMAL_EXPANSION_COEFFICIENT",{"1/C","1/K","1/F"}},{"THERM_EXPANSION_REF_TEMPERATURE",{"C","K","F"}},
        {"THERMAL_CONDUCTIVITY",{"mm*kg/(s^3*C)","W/(m*K)"}},{"SPECIFIC_HEAT",{"mm^2/(s^2*C)","J/(kg*K)"}},
        {"POISSON_RATIO",{"1"}},{"STRUCTURAL_DAMPING_COEFFICIENT",{"1"}},{"EMISSIVITY",{"1"}},{"SHEETMETAL_K_FACTOR",{"1"}},
        {"MATERIAL_NAME",{""}},{"HARDNESS",{""}},{"CONDITION",{""}}};
    if(const auto found=known.find(property);found!=known.end())return found->second;
    return {"","1","mm","cm","m","in","deg","rad","kg","g","t","lb","s","min","C","K","F","Pa","kPa","MPa","GPa","psi","kg/mm^3","g/cm^3","kg/m^3","lb/in^3","1/C","1/K","1/F","mm*kg/(s^3*C)","W/(m*K)","mm^2/(s^2*C)","J/(kg*K)"};
}
void validate_material(const MaterialData& data) {
    if(data.properties.size()>4096)throw std::invalid_argument("A material supports at most 4096 properties.");
    for(const auto& [name,value]:data.properties) {
        nonempty(name);validate_native_metadata_text(value);
        const auto found=data.units.find(name);const auto unit=found==data.units.end()?std::string{}:found->second;
        const auto choices=material_unit_choices(name);
        if(std::ranges::find(choices,unit)==choices.end())throw std::invalid_argument("The material property unit is not supported.");
        if(name=="MASS_DENSITY" && !value.empty()) {
            double density{};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),density);
            if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || !std::isfinite(density) || density<=0)
                throw std::invalid_argument("Material density must be finite and positive.");
        }
    }
    for(const auto& [name,unit]:data.units)if(!data.properties.contains(name))throw std::invalid_argument("Material units must belong to an existing property.");
    for(const auto& [name,languages]:data.descriptions) {
        if(!data.properties.contains(name))throw std::invalid_argument("Material descriptions must belong to an existing property.");
        if(languages.size()>128)throw std::invalid_argument("A material property supports at most 128 descriptions.");
        for(const auto& [language,value]:languages){if(!language.empty())nonempty(language);validate_native_metadata_text(value);}
    }
}
void validate_family_table(const FamilyTable& table,const std::string& generic_name) {
    if(table.columns.size()>512 || table.instances.size()>4096)throw std::invalid_argument("A family table supports at most 512 columns and 4096 instances.");
    std::set<std::string> columns,names;
    for(const auto& column:table.columns){nonempty(column);if(!columns.insert(column).second)throw std::invalid_argument("Family table columns must be unique.");}
    for(const auto& instance:table.instances) {
        nonempty(instance.name);
        if(instance.name==generic_name || !names.insert(instance.name).second)throw std::invalid_argument("Family instance names must be unique and different from the generic document name.");
        for(const auto& [column,value]:instance.values){if(!columns.contains(column))throw std::invalid_argument("A family value refers to an unknown column.");text(value);}
    }
}
FamilyTable parse_family_table(const std::string& text) {
    const auto data=nlohmann::json::parse(text);FamilyTable result;
    if(!data.is_object() || !data.contains("columns") || !data["columns"].is_array() || !data.contains("instances") || !data["instances"].is_array())
        throw std::invalid_argument("Invalid native family table structure.");
    for(auto it=data.begin();it!=data.end();++it)if(it.key()!="columns" && it.key()!="instances")throw std::invalid_argument("Invalid native family table structure.");
    result.columns=data["columns"].get<std::vector<std::string>>();
    for(const auto& row:data["instances"]) {
        if(!row.is_object() || !row.contains("name") || !row.contains("values") || row.size()!=2 || !row["values"].is_object())
            throw std::invalid_argument("Invalid native family instance structure.");
        result.instances.push_back({row["name"].get<std::string>(),row["values"].get<std::map<std::string,std::string>>()});
    }
    return result;
}
std::string serialize_family_table(const FamilyTable& table) {
    nlohmann::json rows=nlohmann::json::array();for(const auto& instance:table.instances)rows.push_back({{"name",instance.name},{"values",instance.values}});
    return nlohmann::json{{"columns",table.columns},{"instances",std::move(rows)}}.dump();
}
}
