#pragma once
#include <zima/assembly/assembly_document.hpp>
namespace zima::app {
inline std::optional<kernel::FaceReference> assembly_curve_local_reference(kernel::FaceReference reference,const std::string& active_path) {
    const auto active=assembly::InstancePath::decode(active_path);
    auto selected=assembly::InstancePath::decode(reference.instance_path);
    if(selected.occurrence_ids.size()<=active.occurrence_ids.size()||
        !std::equal(active.occurrence_ids.begin(),active.occurrence_ids.end(),selected.occurrence_ids.begin()))return std::nullopt;
    selected.occurrence_ids.erase(selected.occurrence_ids.begin(),selected.occurrence_ids.begin()+active.occurrence_ids.size());
    reference.instance_path=selected.encoded();return reference;
}
inline kernel::FaceReference assembly_curve_scene_reference(kernel::FaceReference reference,const std::string& active_path) {
    auto active=assembly::InstancePath::decode(active_path);
    for(const auto& id:assembly::InstancePath::decode(reference.instance_path).occurrence_ids)active=active.child(id);
    reference.instance_path=active.encoded();return reference;
}
}
