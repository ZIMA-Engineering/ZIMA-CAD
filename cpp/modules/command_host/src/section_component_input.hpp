#pragma once
#include <zima/command_host/host.hpp>
#include <zima/document/section.hpp>
#include <cmath>
#include <set>
namespace zima::command_host {
// Shared syntax for source Section and Drawing component patches.
template<class Error> void patch_section_components(document::SectionDefinition& section,const commands::Json& patches) {
    if(patches.size()>10000)throw Error("invalid_arguments","A Section component patch is too large.");
    std::set<std::string> touched;
    for(const auto& patch:patches) {
        if(!patch.is_object()||!patch.contains("component")||!patch.at("component").is_string())
            throw Error("invalid_arguments","A Section component patch requires an exact component ID.");
        const auto key=patch.at("component").get<std::string>();
        if(!touched.insert(key).second)throw Error("invalid_arguments","A Section component may be changed only once per patch.");
        if(!section.component_names.contains(key)&&!section.components.contains(key))
            throw Error("component_not_found","The requested component does not belong to this Section.");
        for(const auto& [name,value]:patch.items())if(name!="component"&&name!="mode"&&name!="custom_hatch"&&name!="hatch")
            throw Error("invalid_arguments","Unknown Section component property.");
        auto& component=section.components[key];
        if(patch.contains("mode")) {
            if(!patch.at("mode").is_string())throw Error("invalid_arguments","Neplatný režim komponenty řezu.");
            const auto mode=patch.at("mode").get<std::string>();
            if(mode!="cut_hatch"&&mode!="cut_only"&&mode!="uncut")throw Error("invalid_arguments","Neplatný režim komponenty řezu.");
            component.mode=mode=="cut_hatch"?0:mode=="cut_only"?1:2;
        }
        if(patch.contains("custom_hatch")&&!patch.at("custom_hatch").is_boolean())
            throw Error("invalid_arguments","custom_hatch must be a boolean.");
        const bool custom=patch.value("custom_hatch",patch.contains("hatch")||component.custom_hatch);
        if(patch.contains("hatch")&&!custom)throw Error("invalid_arguments","Custom hatch values require custom_hatch enabled.");
        if(custom&&!component.custom_hatch)component.hatch=document::section_component_hatch(section,key);
        component.custom_hatch=custom;
        if(patch.contains("hatch")) {
            if(!patch.at("hatch").is_object()||patch.at("hatch").empty())throw Error("invalid_arguments","Specify at least one hatch property.");
            for(const auto& [name,value]:patch.at("hatch").items()) {
                if(name=="pattern") {
                    if(!value.is_string())throw Error("invalid_arguments","Unknown Section hatch pattern.");
                    const auto pattern=value.get<std::string>();
                    if(pattern!="parallel"&&pattern!="cross"&&pattern!="dashed")throw Error("invalid_arguments","Unknown Section hatch pattern.");
                    component.hatch.pattern=pattern=="parallel"?0:pattern=="cross"?1:2;
                }else {
                    if(!value.is_number()||!std::isfinite(value.get<double>()))throw Error("invalid_arguments","Hatch parameters must be finite numbers.");
                    if(name=="angle_degrees")component.hatch.angle=value.get<double>();
                    else if(name=="spacing_mm")component.hatch.spacing_mm=value.get<double>();
                    else if(name=="offset_mm")component.hatch.offset_mm=value.get<double>();
                    else throw Error("invalid_arguments","Unknown Section hatch property.");
                }
            }
        }
    }
}
}
