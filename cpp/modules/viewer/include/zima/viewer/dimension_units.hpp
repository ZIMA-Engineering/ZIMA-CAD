#pragma once
#include <zima/kernel/dimension_layout.hpp>
#include <zima/document/dimension_unit_conversion.hpp>
#include <charconv>

namespace zima::viewer {
struct DimensionDisplayUnits {
    double millimetres_per_unit{1};
    std::string length_suffix{"mm"};
    double degrees_per_unit{1};
    std::string angle_suffix{"°"};
    bool operator==(const DimensionDisplayUnits&) const = default;
};

struct DimensionUnitLabel {
    std::string primary;
    std::optional<kernel::DimensionTextStyle> style;
    std::string secondary;
    std::string text() const {return primary+(secondary.empty()?std::string{}:" "+secondary);}
};

// Presentation only. Geometry, angular sweep, source identities and values stay
// canonical. Drawing renderers do not consume this model-View unit policy.
inline DimensionUnitLabel dimension_unit_presentation(const kernel::ViewerDimension& d,int decimals,
                                       const DimensionDisplayUnits& units) {
    const auto original=[&]() -> DimensionUnitLabel {
        return {!d.display_text_override.empty()?d.display_text_override:
            d.label_prefix+kernel::dimension_number(d.value,decimals)+kernel::dimension_unit_text(d.unit_suffix),d.source_text_style,{}};
    };
    // Count labels and literal labels (including catalog thread names) are not
    // lengths. A layout-generated override still has its source style.
    if(d.label_only&&d.unit_suffix.empty())return original();
    if(!d.display_text_override.empty()&&(!d.source_text_style||!d.source_text_style->text_override.empty()))return original();
    const bool angular=d.kind==kernel::ViewerDimensionKind::Angular;
    const double scale=angular?units.degrees_per_unit:units.millimetres_per_unit;
    const auto& suffix=angular?units.angle_suffix:units.length_suffix;
    const std::string native_suffix=angular?"°":"mm";
    if(d.source_text_style) {
        auto style=*d.source_text_style;
        if(document::dimension_has_specification(style)) {
            if(!style.text_override.empty())return {style.text_override,style,{}};
            // The persisted annotation precision is part of its specification.
            // Global display precision affects only the approximate indication.
            const auto source_unit=style.value_unit.empty()?(angular?"deg":"mm"):style.value_unit;
            const auto target_unit=suffix=="°"?"deg":suffix;
            const auto exact=document::convert_dimension_annotation_units(d,style,target_unit);
            if(exact.style)return {kernel::dimension_text(d,*exact.style),exact.style,{}};
            if(exact.field=="suffix")return {kernel::dimension_text(d,style),style,{}};
            // The original specification remains authoritative. A secondary
            // nominal is explicitly approximate; never print rounded limits.
            const auto source_suffix=source_unit=="deg"?"°":source_unit;
            const auto first=style.suffix.find_first_not_of(" \t"),last=style.suffix.find_last_not_of(" \t");
            if(first==std::string::npos||style.suffix.substr(first,last-first+1)!=source_suffix)
                style.suffix+=" ["+source_suffix+"]";
            DimensionUnitLabel label{kernel::dimension_text(d,style),style,{}};
            const double source_scale=kernel::dimension_annotation_scale(source_unit,d.kind);
            auto nominal=kernel::dimension_number(d.value/source_scale,style.decimals,style.keep_trailing_zeros);
            std::replace(nominal.begin(),nominal.end(),',','.');double value{};
            const auto parsed=std::from_chars(nominal.data(),nominal.data()+nominal.size(),value);
            if(parsed.ec==std::errc{}&&parsed.ptr==nominal.data()+nominal.size()) {
                const double converted=value*(source_scale/scale);
                if(std::isfinite(converted))label.secondary="(≈"+kernel::dimension_number(converted,decimals)+suffix+")";
            }
            return label;
        }
    }
    if(scale==1&&suffix==native_suffix)return original();
    const auto automatic=[&](const std::string& text) {
        const auto first=text.find_first_not_of(" \t"),last=text.find_last_not_of(" \t");
        return first==std::string::npos||text.substr(first,last-first+1)==native_suffix;
    };
    if(d.source_text_style) {
        auto style=*d.source_text_style;
        if(!style.text_override.empty()||!automatic(style.suffix))return original();
        if(!style.suffix.empty())style.suffix=suffix;
        if(d.display_text_override.empty())style.decimals=decimals;
        kernel::ViewerDimension shown;shown.kind=d.kind;shown.value=d.value/scale;
        return {kernel::dimension_text(shown,style),style,{}};
    }
    if(!automatic(d.unit_suffix))return original();
    return {d.label_prefix+kernel::dimension_number(d.value/scale,decimals)+
        (d.unit_suffix.empty()?std::string{}:suffix),{}, {}};
}
inline std::string dimension_unit_label(const kernel::ViewerDimension& d,int decimals,
                                       const DimensionDisplayUnits& units) {
    return dimension_unit_presentation(d,decimals,units).text();
}
} // namespace zima::viewer
