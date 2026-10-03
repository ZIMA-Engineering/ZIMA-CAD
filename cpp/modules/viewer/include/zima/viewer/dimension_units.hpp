#pragma once
#include <zima/kernel/dimension_layout.hpp>

namespace zima::viewer {
struct DimensionDisplayUnits {
    double millimetres_per_unit{1};
    std::string length_suffix{"mm"};
    double degrees_per_unit{1};
    std::string angle_suffix{"°"};
    bool operator==(const DimensionDisplayUnits&) const = default;
};

// Presentation only. Geometry, angular sweep, source identities and values stay
// canonical. Drawing renderers do not consume this model-View unit policy.
inline std::string dimension_unit_label(const kernel::ViewerDimension& d,int decimals,
                                       const DimensionDisplayUnits& units) {
    const auto original=[&] {
        return !d.display_text_override.empty()?d.display_text_override:
            d.label_prefix+kernel::dimension_number(d.value,decimals)+kernel::dimension_unit_text(d.unit_suffix);
    };
    // Count labels and literal labels (including catalog thread names) are not
    // lengths. A layout-generated override still has its source style.
    if(d.label_only&&d.unit_suffix.empty())return original();
    if(!d.display_text_override.empty()&&(!d.source_text_style||!d.source_text_style->text_override.empty()))return original();
    // An explicit annotation basis owns both the nominal and its deviations.
    // A different active document must not reinterpret that specification.
    if(d.source_text_style&&(!d.source_text_style->value_unit.empty()||d.source_text_style->keep_trailing_zeros))
        return kernel::dimension_text(d,*d.source_text_style);
    const bool angular=d.kind==kernel::ViewerDimensionKind::Angular;
    const double scale=angular?units.degrees_per_unit:units.millimetres_per_unit;
    const auto& suffix=angular?units.angle_suffix:units.length_suffix;
    const std::string native_suffix=angular?"°":"mm";
    if(scale==1&&suffix==native_suffix)return original();
    const auto automatic=[&](const std::string& text) {
        const auto first=text.find_first_not_of(" \t"),last=text.find_last_not_of(" \t");
        return first==std::string::npos||text.substr(first,last-first+1)==native_suffix;
    };
    if(d.source_text_style) {
        auto style=*d.source_text_style;
        // Tolerances are currently authored strings. Their authoritative
        // specification and custom suffixes must not be silently reinterpreted
        // by a repaint; explicit tolerance conversion owns that separate work.
        if(!style.text_override.empty()||(!style.tolerance_mode.empty()&&style.tolerance_mode!="basic")||!automatic(style.suffix))return original();
        if(!style.suffix.empty())style.suffix=suffix;
        if(d.display_text_override.empty())style.decimals=decimals;
        kernel::ViewerDimension shown;shown.kind=d.kind;shown.value=d.value/scale;
        return kernel::dimension_text(shown,style);
    }
    if(!automatic(d.unit_suffix))return original();
    return d.label_prefix+kernel::dimension_number(d.value/scale,decimals)+
        (d.unit_suffix.empty()?std::string{}:suffix);
}
} // namespace zima::viewer
