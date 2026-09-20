#pragma once
#include <zima/kernel/dimension_layout.hpp>

namespace zima::drawing {
// Millimetres are implicit on the sheet. Keep explicit non-length suffixes,
// angular units, tolerances and overrides, without changing the source model.
inline kernel::DimensionTextStyle sheet_dimension_style(kernel::DimensionTextStyle style) {
    const auto first=style.suffix.find_first_not_of(" \t");
    const auto last=style.suffix.find_last_not_of(" \t");
    if(first!=std::string::npos && style.suffix.substr(first,last-first+1)=="mm")
        style.suffix.clear();
    return style;
}
}
