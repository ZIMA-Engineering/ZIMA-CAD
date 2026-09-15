#include <zima/document/metadata.hpp>
#include <zima/document/part_document.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace zima::document {
namespace {
constexpr const char* thickness_key="SHEETMETAL_THICKNESS";
constexpr const char* factor_key="SHEETMETAL_K_FACTOR";
std::optional<double> number(const std::map<std::string,std::string>& values,const char* key) {
    const auto found=values.find(key);
    if(found==values.end()||found->second.empty())return {};
    const auto& text=found->second;double value{};
    const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size()||!std::isfinite(value))
        throw std::invalid_argument("Sheet metal defaults must contain finite numeric values.");
    return value;
}
std::string text(double value) {
    char buffer[64];const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value);
    if(result.ec!=std::errc{})throw std::invalid_argument("Cannot encode sheet metal defaults.");
    return {buffer,result.ptr};
}
}
void validate_sheet_metal_defaults(const SheetMetalDefaults& values) {
    if(values.thickness_mm&&(!std::isfinite(*values.thickness_mm)||*values.thickness_mm<=0||*values.thickness_mm>1000000))
        throw std::invalid_argument("Sheet thickness must be a positive number in millimeters or unset.");
    if(!std::isfinite(values.k_factor)||values.k_factor<0||values.k_factor>1)
        throw std::invalid_argument("K factor must be a number from 0 to 1.");
}
SheetMetalDefaults sheet_metal_defaults(const PartDocument& part) {
    SheetMetalDefaults result{number(part.user_parameters,thickness_key),number(part.physical_parameters,factor_key).value_or(0.5)};
    validate_sheet_metal_defaults(result);return result;
}
void set_sheet_metal_defaults(PartDocument& part,const SheetMetalDefaults& values) {
    validate_sheet_metal_defaults(values);
    if(sheet_metal_defaults(part)==values)return;
    // Use the native document parameter and existing material property stores.
    // Loading another material changes its K factor without replacing thickness.
    if(values.thickness_mm) {
        const auto value=text(*values.thickness_mm);
        part.user_parameters[thickness_key]=value;
        part.user_parameter_values[thickness_key][""]=value;
        if(std::ranges::find(part.user_parameter_order,thickness_key)==part.user_parameter_order.end())
            part.user_parameter_order.push_back(thickness_key);
    } else {
        part.user_parameters.erase(thickness_key);part.user_parameter_values.erase(thickness_key);
        part.user_parameter_labels.erase(thickness_key);std::erase(part.user_parameter_order,std::string(thickness_key));
    }
    part.physical_parameters[factor_key]=text(values.k_factor);
    part.physical_parameter_units[factor_key]="1";
}
}
