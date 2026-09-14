#include <zima/assembly/assembly_document.hpp>
#include <algorithm>
#include <set>
#include <utility>
#include <stdexcept>

namespace zima::assembly {
const document::HistoryContainer* AssemblyDocument::find_sketch_container(const std::string& id) const {
    const auto found=std::ranges::find(sketch_containers,id,&document::HistoryContainer::id);
    return found==sketch_containers.end()?nullptr:&*found;
}
document::HistoryContainer* AssemblyDocument::find_sketch_container(const std::string& id) {
    return const_cast<document::HistoryContainer*>(std::as_const(*this).find_sketch_container(id));
}
void AssemblyDocument::insert_sketch(sketcher::Sketch sketch,
    std::optional<document::HistoryContainer> owner) {
    auto container=owner?std::move(*owner):document::PartDocument::create_sketch_container();
    if(container.feature_kind!=document::FeatureKind::Sketch||container.id.empty()||
        container.feature_id.empty()||container.feature_parent_id!=container.id||
        (!sketch.owner_container_id.empty()&&sketch.owner_container_id!=container.id))
        throw std::invalid_argument("A standalone Assembly Sketch requires its own Sketch container.");
    if(find_sketch_container(container.id)||find_cut(container.id)||
        std::ranges::any_of(sketches,[&](const auto& value){return value.id==sketch.id;}))
        throw std::invalid_argument("The Sketch identity already exists.");
    sketch.validate();sketch.owner_container_id=container.id;container.name=sketch.name;
    sketch_containers.push_back(std::move(container));sketches.push_back(std::move(sketch));
}
void AssemblyDocument::validate_sketch_containers() const {
    std::set<std::string> ids,owners;
    for(const auto& container:sketch_containers) {
        if(container.feature_kind!=document::FeatureKind::Sketch||container.id.empty()||
            container.feature_id.empty()||container.feature_parent_id!=container.id||
            container.container_origin!=document::create_container_origin(container.id)||
            find_cut(container.id)||!ids.insert(container.id).second||!ids.insert(container.feature_id).second)
            throw std::runtime_error("Invalid Assembly Sketch container identity.");
    }
    for(const auto& sketch:sketches) {
        if(sketch.id.empty()||!ids.insert(sketch.id).second||!owners.insert(sketch.owner_container_id).second)
            throw std::runtime_error("Invalid Assembly Sketch ownership.");
        if(find_sketch_container(sketch.owner_container_id))continue;
        const auto* cut=find_cut(sketch.owner_container_id);
        if(!cut)throw std::runtime_error("Assembly Sketch has no owning container.");
        const auto& feature=cut->definition;
        const auto& owned=feature.feature_kind==document::FeatureKind::Extrusion?feature.extrusion.sketch_id:feature.revolution.sketch_id;
        if(owned!=sketch.id)throw std::runtime_error("Assembly cut owns a different Sketch.");
    }
    for(const auto& container:sketch_containers)if(!owners.contains(container.id))
        throw std::runtime_error("Assembly Sketch container has no Sketch.");
}
}
