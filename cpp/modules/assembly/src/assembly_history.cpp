#include <zima/assembly/assembly_document.hpp>
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace zima::assembly {
namespace {
using Entry=AssemblyDocument::HistoryEntry;
using Kind=AssemblyDocument::HistoryKind;
auto authored_entries(const AssemblyDocument& document) {
    std::vector<Entry> result;
    for(const auto& value:document.components)result.push_back({Kind::Component,value.occurrence_id});
    for(const auto& value:document.constructions)result.push_back({Kind::Construction,value.id});
    for(const auto& value:document.sketch_containers)result.push_back({Kind::Sketch,value.id});
    for(const auto& value:document.cuts)result.push_back({Kind::Cut,value.definition.id});
    std::unordered_set<std::string> ids;
    for(const auto& entry:result)if(entry.id.empty()||!ids.insert(entry.id).second)
        throw std::runtime_error("Invalid Assembly Container data");
    return result;
}
template<class Values,class Identity> void order_values(Values& values,const std::vector<Entry>& order,Identity identity) {
    std::unordered_map<std::string,std::size_t> indices;
    for(std::size_t i=0;i<order.size();++i)indices.emplace(order[i].id,i);
    std::stable_sort(values.begin(),values.end(),[&](const auto& a,const auto& b){return indices.at(identity(a))<indices.at(identity(b));});
}
}
std::vector<AssemblyDocument::HistoryEntry> AssemblyDocument::complete_history_order() const {
    const auto authored=authored_entries(*this);
    std::unordered_map<std::string,HistoryKind> known;
    for(const auto& entry:authored)known.emplace(entry.id,entry.kind);
    std::unordered_set<std::string> retained;
    std::vector<HistoryEntry> result;
    std::size_t insertion=0;
    for(std::size_t i=0;i<history_order.size();++i) {
        const auto& entry=history_order[i];const auto found=known.find(entry.id);
        if(found==known.end())continue; // An explicit deletion also removes its history row.
        if(found->second!=entry.kind||!retained.insert(entry.id).second)
            throw std::runtime_error("Invalid Assembly Container data");
        result.push_back(entry);if(i<history_cursor)++insertion;
    }
    std::vector<HistoryEntry> inserted;
    for(const auto& entry:authored)if(!retained.contains(entry.id))inserted.push_back(entry);
    result.insert(result.begin()+insertion,inserted.begin(),inserted.end());
    return result;
}
std::size_t AssemblyDocument::effective_history_cursor() const {
    return std::min(history_cursor,complete_history_order().size());
}
void AssemblyDocument::synchronize_history() {
    const auto order=complete_history_order();
    if(history_cursor!=std::numeric_limits<std::size_t>::max()) {
        std::unordered_set<std::string> ids;for(const auto& entry:order)ids.insert(entry.id);
        std::size_t preceding=0,retained=0;
        for(std::size_t i=0;i<history_order.size();++i)if(ids.contains(history_order[i].id)) {
            ++retained;if(i<history_cursor)++preceding;
        }
        history_cursor=preceding+order.size()-retained;
    }
    history_order=order;
    order_values(components,history_order,[](const auto& value){return value.occurrence_id;});
    order_values(constructions,history_order,[](const auto& value){return value.id;});
    order_values(sketch_containers,history_order,[](const auto& value){return value.id;});
    order_values(cuts,history_order,[](const auto& value){return value.definition.id;});
}
void AssemblyDocument::validate_history() const {
    if(history_order!=complete_history_order()||
        (history_cursor!=std::numeric_limits<std::size_t>::max()&&history_cursor>history_order.size()))
        throw std::runtime_error("Invalid Assembly Container data");
}
void AssemblyDocument::insert_history_entry(HistoryKind kind,const std::string& id) {
    if(id.empty()||std::ranges::any_of(history_order,[&](const auto& entry){return entry.id==id;}))
        throw std::runtime_error("Invalid Assembly Container data");
    const auto cursor=std::min(history_cursor,history_order.size());
    history_order.insert(history_order.begin()+cursor,{kind,id});
    if(history_cursor!=std::numeric_limits<std::size_t>::max())history_cursor=cursor+1;
}
void AssemblyDocument::insert_history_entry(document::PartHistoryKind kind,const std::string& id) {
    insert_history_entry(kind==document::PartHistoryKind::Construction?HistoryKind::Construction:
        kind==document::PartHistoryKind::Sketch?HistoryKind::Sketch:HistoryKind::Cut,id);
}
bool AssemblyDocument::history_visible(const std::string& id) const {
    if(id==document_id||id==document_id+":origin")return true;
    auto owner=id;
    if(const auto* object=find_construction(id);object&&!object->parent_construction_id.empty())owner=object->parent_construction_id;
    if(const auto sketch=std::ranges::find(sketches,id,&sketcher::Sketch::id);sketch!=sketches.end())owner=sketch->owner_container_id;
    const auto order=complete_history_order();const auto cursor=std::min(history_cursor,order.size());
    return std::ranges::any_of(order.begin(),order.begin()+cursor,[&](const auto& entry){return entry.id==owner;});
}
AssemblyDocument AssemblyDocument::history_view_document(std::optional<std::size_t> cursor) const {
    auto result=*this;const auto order=complete_history_order();
    const auto count=cursor.value_or(std::min(history_cursor,order.size()));
    if(count>order.size())throw std::runtime_error("Invalid Assembly Container data");
    std::unordered_set<std::string> visible;
    for(std::size_t i=0;i<count;++i)visible.insert(order[i].id);
    // The first inactive cut retains the actual component boundary after all
    // preceding cuts. Selecting this snapshot does not calculate geometry.
    std::unordered_set<std::string> restored;
    for(const auto& cut:cuts)if(!visible.contains(cut.definition.id)&&!cut.definition.suppressed)
        for(const auto& [id,input]:cut.input_component_bodies)if(visible.contains(id)&&restored.insert(id).second)
            if(auto* component=result.find_occurrence(id))component->calculated_source=input;
    std::erase_if(result.components,[&](const auto& value){return !visible.contains(value.occurrence_id);});
    std::erase_if(result.constructions,[&](const auto& value){return !visible.contains(value.id);});
    std::erase_if(result.sketch_containers,[&](const auto& value){return !visible.contains(value.id);});
    std::erase_if(result.cuts,[&](const auto& value){return !visible.contains(value.definition.id);});
    std::erase_if(result.sketches,[&](const auto& value){return !visible.contains(value.owner_container_id);});
    std::erase_if(result.dependencies,[&](const auto& value){return !visible.contains(value.dependent_occurrence_id)||!visible.contains(value.prerequisite_occurrence_id);});
    result.history_order.assign(order.begin(),order.begin()+count);result.history_cursor=count;
    return result;
}
} // namespace zima::assembly
