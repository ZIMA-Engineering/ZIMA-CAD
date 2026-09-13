#pragma once
#include <zima/commands/dispatcher.hpp>
#include <zima/document/part_document.hpp>
namespace zima::command_host {
// Shared wire input for original bore/thread targets. Derived coordinates are
// resolved from persisted source geometry, never accepted from the caller.
template<class Error> std::vector<document::ExtrusionParameters::EndTarget> opening_target_input(const commands::Json& input) {
    if(!input.is_array()||input.size()>1)throw Error("invalid_arguments","Each opening end accepts one target reference.");
    std::vector<document::ExtrusionParameters::EndTarget> values;
    for(const auto& item:input) {
        if(!item.is_object()||!item.contains("owner")||!item.contains("key"))throw Error("invalid_arguments","A target reference requires owner and key.");
        for(const auto& [name,field]:item.items())if((name!="owner"&&name!="key"&&name!="instance_path"&&name!="kind"&&name!="label")||!field.is_string())
            throw Error("invalid_arguments","Target reference fields must be supported text fields.");
        const auto kind=item.value("kind",std::string("face"));
        if(kind!="face"&&kind!="plane")throw Error("invalid_reference","An opening end requires an original face or plane of this Part.");
        document::ExtrusionParameters::EndTarget target;
        target.reference={item.at("owner"),item.at("key"),item.value("instance_path",std::string{})};
        target.label=item.value("label",std::string{});target.kind=kind=="plane"?document::EndTargetKind::Plane:document::EndTargetKind::Face;
        values.push_back(std::move(target));
    }
    return values;
}
}
