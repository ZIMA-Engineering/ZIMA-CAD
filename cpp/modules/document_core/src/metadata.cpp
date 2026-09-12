#include <zima/document/metadata.hpp>
#include <zima/document/precision.hpp>
#include <algorithm>
#include <cctype>
#include <set>
namespace zima::document {
namespace {
void key(const std::string& value) {
    if(value.empty() || value.size()>256 || std::isspace(static_cast<unsigned char>(value.front())) ||
        std::isspace(static_cast<unsigned char>(value.back())) || std::ranges::any_of(value,[](unsigned char c){return c<32 || c==127;}))
        throw std::invalid_argument("Parameter keys must be nonempty, unique and contain no surrounding whitespace or control characters.");
}
}
void normalize_user_parameters(UserParameterData& data) {
    if(data.order.size()>4096)throw std::invalid_argument("A document supports at most 4096 user parameters.");
    std::set<std::string> seen;
    for(const auto& name:data.order) {
        key(name);if(!seen.insert(name).second)throw std::invalid_argument("Parameter keys must be nonempty, unique and contain no surrounding whitespace or control characters.");
    }
    for(const auto* localized:{&data.labels,&data.values})for(const auto& [name,values]:*localized) {
        if(!seen.contains(name))throw std::invalid_argument("Parameter labels and values must belong to an ordered parameter.");
        if(values.size()>128)throw std::invalid_argument("A parameter supports at most 128 language variants.");
        for(const auto& [language,text]:values) {
            if(!language.empty())key(language);
            if(text.size()>65536 || text.find('\0')!=std::string::npos)throw std::invalid_argument("A parameter value is too long or contains a null character.");
        }
    }
    data.flat.clear();
    for(const auto& name:data.order) {
        const auto found=data.values.find(name);
        if(found!=data.values.end())if(const auto shared=found->second.find("");shared!=found->second.end())data.flat[name]=shared->second;
    }
    std::erase_if(data.labels,[](const auto& entry){return entry.second.empty();});
    std::erase_if(data.values,[](const auto& entry){return entry.second.empty();});
}
const std::map<std::string,std::vector<std::string>>& file_unit_choices() {
    static const std::map<std::string,std::vector<std::string>> choices{
        {"Length",{"mm","cm","m","in"}},{"Angle",{"deg","rad"}},{"Mass",{"kg","g","t","lb"}},
        {"Time",{"s","min"}},{"Temperature",{"C","K","F"}},{"Stress",{"Pa","kPa","MPa","GPa","psi"}}};
    return choices;
}
void validate_file_settings(const FileSettingsData& data) {
    for(const auto& [name,choices]:file_unit_choices()) {
        const auto unit=data.units.find(name);
        if(unit==data.units.end() || std::ranges::find(choices,unit->second)==choices.end())
            throw std::invalid_argument("Choose a supported unit for every document quantity.");
    }
    for(const auto* name:{"linear_tolerance","angular_tolerance","mesh_deflection","decimal_places"}) {
        if(!data.precision.contains(name))throw std::invalid_argument("All document precision values are required.");
        const auto value=precision_value(data.precision,name,0);
        if(std::string_view(name)=="decimal_places") {
            if(value<0 || value>12 || value!=std::floor(value))throw std::invalid_argument("Decimal places must be an integer from 0 to 12.");
        } else if(value<0 || value>1000000 || (std::string_view(name)=="mesh_deflection" && value<1e-9))
            throw std::invalid_argument("Tolerances must be from 0 to 1000000; mesh deflection must be at least 0.000000001.");
    }
}
}
