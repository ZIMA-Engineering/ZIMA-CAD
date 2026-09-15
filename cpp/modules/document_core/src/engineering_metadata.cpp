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
        if(name=="SHEETMETAL_K_FACTOR" && !value.empty()) {
            double factor{};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),factor);
            if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size())
                throw std::invalid_argument("K factor must be a number from 0 to 1.");
            validate_sheet_metal_defaults({{},factor});
        }
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
    std::set<std::string> columns,names,ids;
    for(const auto& column:table.columns){nonempty(column);if(!columns.insert(column).second)throw std::invalid_argument("Family table columns must be unique.");}
    if(table.bindings.size()!=table.columns.size())throw std::invalid_argument("Every family column needs a model reference.");
    std::set<std::pair<std::string,std::string>> references;
    for(const auto& [name,binding]:table.bindings) {
        if(!columns.contains(name) || binding.owner_id.empty() ||
            (binding.kind!="dimension" && binding.kind!="feature" && binding.kind!="body" && binding.kind!="component") ||
            (binding.kind=="dimension")!=!binding.semantic_key.empty())
            throw std::invalid_argument("Invalid family column reference.");
        if(!references.emplace(binding.owner_id,binding.semantic_key).second)
            throw std::invalid_argument("A family reference may only occur once.");
    }
    for(const auto& instance:table.instances) {
        if(!instance.id.empty()&&!ids.insert(instance.id).second)throw std::invalid_argument("Family instance identities must be unique.");
        nonempty(instance.name);
        if(instance.name==generic_name || !names.insert(instance.name).second)throw std::invalid_argument("Family instance names must be unique and different from the generic document name.");
        for(const auto& [column,value]:instance.values){if(!columns.contains(column))throw std::invalid_argument("A family value refers to an unknown column.");text(value);
            if(value.empty())continue;
            if(table.bindings.at(column).kind=="dimension") {
                double number{};auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),number);
                if(error!=std::errc{} || end!=value.data()+value.size() || !std::isfinite(number))throw std::invalid_argument("Family dimensions must be finite numbers.");
            } else if(value!="yes" && value!="no")throw std::invalid_argument("Family presence must be yes or no.");
        }
    }
}
FamilyTable parse_family_table(const std::string& text) {
    const auto data=nlohmann::json::parse(text);FamilyTable result;
    if(!data.is_object() || !data.contains("columns") || !data["columns"].is_array() || !data.contains("instances") || !data["instances"].is_array())
        throw std::invalid_argument("Invalid native family table structure.");
    for(auto it=data.begin();it!=data.end();++it)if(it.key()!="columns" && it.key()!="instances" && it.key()!="bindings")throw std::invalid_argument("Invalid native family table structure.");
    result.columns=data["columns"].get<std::vector<std::string>>();
    if(!data.contains("bindings")||!data["bindings"].is_object())throw std::invalid_argument("Invalid family column reference.");
    for(auto it=data.at("bindings").begin();it!=data.at("bindings").end();++it) {
        const auto& b=it.value();if(!b.is_object()||b.size()!=3)throw std::invalid_argument("Invalid family column reference.");
        result.bindings.emplace(it.key(),FamilyColumn{b.at("kind").get<std::string>(),b.at("owner").get<std::string>(),b.at("key").get<std::string>()});
    }
    for(const auto& row:data["instances"]) {
        if(!row.is_object() || !row.contains("name") || !row.contains("values") || row.size()!=3 || !row["values"].is_object())
            throw std::invalid_argument("Invalid native family instance structure.");
        result.instances.push_back({row["name"].get<std::string>(),row["values"].get<std::map<std::string,std::string>>(),row.at("id").get<std::string>()});
    }
    return result;
}
std::string serialize_family_table(const FamilyTable& table) {
    nlohmann::json rows=nlohmann::json::array();for(const auto& instance:table.instances)rows.push_back({{"name",instance.name},{"values",instance.values},{"id",instance.id}});
    auto bindings=nlohmann::json::object();for(const auto& [name,b]:table.bindings)bindings[name]={{"kind",b.kind},{"owner",b.owner_id},{"key",b.semantic_key}};
    return nlohmann::json{{"columns",table.columns},{"instances",std::move(rows)},{"bindings",std::move(bindings)}}.dump();
}
}
