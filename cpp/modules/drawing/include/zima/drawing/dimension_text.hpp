#pragma once
#include <zima/kernel/dimension_layout.hpp>

namespace zima::drawing {
// Millimetres are implicit on the sheet. Keep explicit non-length suffixes,
// angular units, tolerances and overrides, without changing the source model.
inline kernel::DimensionTextStyle sheet_dimension_style(kernel::DimensionTextStyle style) {
    const auto first=style.suffix.find_first_not_of(" \t");
    const auto last=style.suffix.find_last_not_of(" \t");
    if((style.value_unit.empty()||style.value_unit=="mm")&&first!=std::string::npos && style.suffix.substr(first,last-first+1)=="mm")
        style.suffix.clear();
    return style;
}

inline std::string sheet_dimension_text(const kernel::ViewerDimension& dimension,
        const kernel::DimensionTextStyle& style) {
    // A sheet callout shows magnitude. The signed source value and the
    // witness geometry still drive editing and preserve the authored side.
    kernel::ViewerDimension nominal;
    nominal.kind=dimension.kind;
    nominal.value=std::abs(dimension.value);
    return kernel::dimension_text(nominal,style,true);
}
}
