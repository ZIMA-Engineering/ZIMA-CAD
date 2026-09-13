#include <zima/workspace/derived_copy_operations.hpp>
#include <algorithm>
namespace zima::workspace {
DerivedCopyDefinition derived_copy_definition(const Workspace& live,const std::string& id,const std::string& object) {
    if(const auto* part=live.open_part(id)) {
        const auto* body=part->session.document().body_history.find(object);
        if(!body)throw DerivedCopyError("object_not_found","The requested derived copy does not exist in this document.");
        if(!body->derived_copy)throw DerivedCopyError("wrong_feature","This object is not a Mirror or Pattern.");
        return {body->scope.id,body->name,body->visible,body->scope.placement,*body->derived_copy};
    }
    if(const auto* assembly=live.open_assembly(id)) {
        const auto* component=assembly->session.document().find_occurrence(object);
        if(!component)throw DerivedCopyError("object_not_found","The requested derived copy does not exist in this document.");
        if(!component->derived_copy)throw DerivedCopyError("wrong_feature","This object is not a Mirror or Pattern.");
        return {component->occurrence_id,component->name,component->visible,component->copy_placement,*component->derived_copy};
    }
    throw DerivedCopyError("unsupported_document","Derived copies require an open Part or Assembly.");
}
CopySources derived_copy_sources(const Workspace& live,const std::string& id,const std::string& object) {
    if(!object.empty())static_cast<void>(derived_copy_definition(live,id,object));
    CopySources result;
    if(const auto* part=live.open_part(id)) {
        const auto& graph=part->session.document().body_history;
        result.boundary=graph.insertion_cursor();
        if(!object.empty()||!graph.active_body_id().empty()) {
            const auto& anchor=object.empty()?graph.active_body_id():object;
            const auto found=std::ranges::find(graph.order(),anchor);
            if(found==graph.order().end())throw DerivedCopyError("invalid_history","The copy boundary is missing from the document history.");
            result.boundary=static_cast<std::size_t>(found-graph.order().begin())+(object.empty()?1:0);
        }
        for(const auto& source:graph.available_before(result.boundary)) {
            if(const auto* body=graph.find(source))result.items.push_back({source,body->name,CopySourceKind::Body,body->visible});
            else if(const auto* operation=graph.find_boolean(source))result.items.push_back({source,operation->name,CopySourceKind::Boolean,operation->visible});
        }
        return result;
    }
    if(const auto* assembly=live.open_assembly(id)) {
        for(const auto& component:assembly->session.document().components) {
            if(component.occurrence_id==object)break;
            ++result.boundary;
            if(!component.suppressed)result.items.push_back({component.occurrence_id,component.name,CopySourceKind::Component,component.visible});
        }
        return result;
    }
    throw DerivedCopyError("unsupported_document","Derived copies require an open Part or Assembly.");
}
} // namespace zima::workspace
