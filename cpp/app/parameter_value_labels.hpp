#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <vector>
#include <string>

namespace zima::app {
// Presentation only: values, units, reference identity and editability stay canonical.
// Both complete scenes and transient Properties overlays must use this path.
template<class Translate> void localize_parameter_value_labels(
    std::vector<kernel::ViewerDimension>& dimensions,Translate translate) {
    for(auto& dimension:dimensions)if(dimension.label_only) {
        const auto& key=dimension.reference.semantic_key;
        const char* label=nullptr;
        if(key=="parameter:direction")label="Směr tažení";
        else if(key=="parameter:thin_thickness")label="Tloušťka";
        else if(key=="parameter:thickness")label="Tloušťka skořepiny";
        else if(key=="parameter:pitch")label="Stoupání";
        else if(key=="parameter:pattern:angle")label="Úhel pole";
        else if(key.starts_with("parameter:pattern:spacing:"))label="Rozteč pole";
        else if(key.starts_with("parameter:pattern:reverse_count:"))label="Počet instancí v opačném směru";
        else if(key.starts_with("parameter:pattern:count"))label="Počet instancí";
        else if(key.ends_with("_up_to_length_correction"))label="Korekce délky";
        else if(key.ends_with("_up_to_angle_correction"))label="Korekce úhlu";
        if(label) {
            dimension.label_prefix=translate(label).toStdString()+": ";
            if(key=="parameter:direction"&&dimension.value>0)dimension.label_prefix+="+";
            if(key.starts_with("parameter:pattern:")&&key.back()>='0'&&key.back()<='2')
                dimension.label_prefix=translate(label).toStdString()+" "+std::to_string(key.back()-'0'+1)+": ";
        }
    }
}
} // namespace zima::app
