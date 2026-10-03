#pragma once
#include <zima/document/exact_unit_conversion.hpp>
#include <zima/kernel/dimension_layout.hpp>

namespace zima::document {
struct DimensionUnitConversion {
    std::optional<kernel::DimensionTextStyle> style;
    // Stable field identifier for a caller's localized, item-specific error.
    std::string field;
};
namespace dimension_unit_detail {
inline bool same_decimal(std::string_view a,std::string_view b) {
    auto first=exact_unit_detail::parse(a),second=exact_unit_detail::parse(b);
    if(!first||!second)return false;
    const auto normalize=[](auto& v) {
        while(v.digits.size()>1&&v.digits.back()=='0'){v.digits.pop_back();++v.power;}
        if(v.digits=="0")v.power=0;
        if(v.sign=='+')v.sign=0;
    };
    normalize(*first);normalize(*second);
    return first->digits==second->digits&&first->power==second->power&&first->sign==second->sign;
}
}
// Geometry stays canonical. The returned style changes the complete printed
// numerical specification, including inactive deviation fields, atomically.
// Arbitrary notes/fit designations are never guessed to be numeric quantities.
inline DimensionUnitConversion convert_dimension_annotation_units(
        const kernel::ViewerDimension& dimension,const kernel::DimensionTextStyle& source,
        const std::string& target_unit) {
    const bool angular=dimension.kind==kernel::ViewerDimensionKind::Angular;
    const auto source_unit=source.value_unit.empty()?(angular?"deg":"mm"):source.value_unit;
    double old_scale{},new_scale{};
    try {
        old_scale=kernel::dimension_annotation_scale(source_unit,dimension.kind);
        new_scale=kernel::dimension_annotation_scale(target_unit,dimension.kind);
    } catch(const std::invalid_argument&) {return {{},"units"};}
    if(target_unit.empty())return {{},"units"};
    if(source.decimals<0||source.decimals>12||!std::isfinite(dimension.value))return {{},"nominal"};
    if(source_unit==target_unit)return {source,{}};
    if(!source.text_override.empty())return {{},"text_override"};
    const auto first=source.suffix.find_first_not_of(" \t"),last=source.suffix.find_last_not_of(" \t");
    const auto suffix=first==std::string::npos?std::string{}:source.suffix.substr(first,last-first+1);
    const auto expected_suffix=source_unit=="deg"?"°":source_unit;
    if(!suffix.empty()&&suffix!=expected_suffix&&suffix!=source_unit)return {{},"suffix"};
    const auto nominal=kernel::dimension_number(dimension.value/old_scale,source.decimals,source.keep_trailing_zeros);
    const auto converted=exact_decimal_unit_conversion(nominal,source_unit,target_unit);
    if(!converted)return {{},"nominal"};
    auto result=source;result.value_unit=target_unit;
    const auto point=converted->find('.');
    const auto places=point==std::string::npos?0:converted->size()-point-1;
    if(places>12)return {{},"nominal"};
    result.decimals=int(places);
    // The rounded nominal, not merely the unrounded geometry, defines the
    // printed interval. Refuse a new formatter precision that changes it.
    if(!dimension_unit_detail::same_decimal(*converted,kernel::dimension_number(
        dimension.value/new_scale,result.decimals,result.keep_trailing_zeros)))return {{},"nominal"};
    for(const auto& item:{std::pair{&kernel::DimensionTextStyle::symmetric_tolerance,"symmetric_tolerance"},
            {&kernel::DimensionTextStyle::single_tolerance,"single_tolerance"},
            {&kernel::DimensionTextStyle::upper_tolerance,"upper_tolerance"},
            {&kernel::DimensionTextStyle::lower_tolerance,"lower_tolerance"}}) {
        auto& value=result.*item.first;if(value.empty())continue;
        const auto exact=exact_decimal_unit_conversion(value,source_unit,target_unit);
        if(!exact)return {{},item.second};
        value=*exact;
    }
    // An explicit unit is needed after changing an implicit canonical unit.
    result.suffix=target_unit=="deg"?"°":target_unit;
    return {std::move(result),{}};
}
} // namespace zima::document
