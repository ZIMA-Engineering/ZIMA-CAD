#include <zima/workspace/derived_copy_operations.hpp>
#include <algorithm>
namespace zima::workspace {
namespace {
bool solid_source(const document::HistoryContainer& feature) {
    using Kind=document::FeatureKind;
    switch(feature.feature_kind) {
    case Kind::Sketch:case Kind::Fillet:case Kind::Chamfer:case Kind::Shell:
    case Kind::ShaftThread:case Kind::DrillPoint:return false;
    default:return !feature.suppressed;
    }
}
}
DerivedCopyDefinition derived_copy_definition(const Workspace& live,const std::string& id,const std::string& object) {
    if(const auto* part=live.open_part(id)) {
        if(const auto* feature=part->session.document().find_container(object);
            feature&&feature->feature_kind==document::FeatureKind::DerivedCopy)
            return {feature->id,feature->name,!feature->suppressed,feature->placement,feature->derived_copy};
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
        const auto& document=part->session.document();const auto& graph=document.body_history;
        result.boundary=graph.insertion_cursor();
        const auto* feature_owner=object.empty()?graph.find(graph.active_body_id()):graph.owner(object);
        if(feature_owner) {
            result.body_id=feature_owner->scope.id;
            result.body_cursor=object.empty()?feature_owner->cursor:static_cast<std::size_t>(
                std::ranges::find(feature_owner->entries,object,&document::PartHistoryEntry::id)-feature_owner->entries.begin());
            const auto position=std::ranges::find(graph.order(),result.body_id);
            result.boundary=static_cast<std::size_t>(position-graph.order().begin())+1;
            result.context_bodies=graph.available_before(result.boundary);
            for(std::size_t index=0;index<result.body_cursor;++index) {
                const auto* feature=document.find_container(feature_owner->entries[index].id);
                if(feature&&solid_source(*feature))result.items.push_back({feature->id,feature->name,CopySourceKind::Solid,feature_owner->visible});
            }
            return result;
        }
        if(!object.empty()||!graph.active_body_id().empty()) {
            const auto& anchor=object.empty()?graph.active_body_id():object;
            const auto found=std::ranges::find(graph.order(),anchor);
            if(found==graph.order().end())throw DerivedCopyError("invalid_history","The copy boundary is missing from the document history.");
            result.boundary=static_cast<std::size_t>(found-graph.order().begin())+(object.empty()?1:0);
        }
        result.context_bodies=graph.available_before(result.boundary);
        const auto active=object.empty()?graph.active_body_id():std::string{};
        for(std::size_t position=0;position<result.boundary;++position) {
            const auto& source=graph.order()[position];
            const bool available=std::ranges::find(result.context_bodies,source)!=result.context_bodies.end();
            if(!active.empty()&&source!=active)continue;
            if(const auto* body=graph.find(source)) {
                if(active.empty()&&available)result.items.push_back({source,body->name,CopySourceKind::Body,body->visible});
                const auto count=active.empty()?body->entries.size():body->cursor;
                for(std::size_t index=0;index<count;++index) {
                    const auto* feature=document.find_container(body->entries[index].id);
                    if(feature&&solid_source(*feature)) {
                        if(feature->combine_mode==document::CombineMode::Subtract&&
                            std::ranges::find(result.context_bodies,graph.copy_target_before(feature->id,result.boundary))==result.context_bodies.end())continue;
                        result.items.push_back({feature->id,feature->name,CopySourceKind::Solid,body->visible});
                    }
                }
            } else if(const auto* operation=graph.find_boolean(source);operation&&available)result.items.push_back({source,operation->name,CopySourceKind::Boolean,operation->visible});
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
kernel::ViewerMesh derived_copy_source_mesh(const document::DocumentSession& session,const CopySource& source) {
    if(session.calculated_boundaries().empty())return {};
    const auto& result=session.calculated_boundaries().back();
    if(source.kind!=CopySourceKind::Solid) {
        const auto found=result.body_outputs.find(source.id);
        return found==result.body_outputs.end()?kernel::ViewerMesh{}:found->second->mesh;
    }
    kernel::ViewerMesh mesh;
    const auto& geometry=result.mesh.original_references;
    for(std::size_t i=0;i<geometry.triangle_references.size();++i) {
        const auto& ref=geometry.triangle_references[i];if(ref.owner_id!=source.id)continue;
        for(std::size_t j=0;j<3;++j) {
            mesh.triangles.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
            mesh.vertices.push_back(geometry.vertices.at(geometry.triangles.at(3*i+j)));
        }
        mesh.triangle_references.push_back(ref);
    }
    for(const auto& edge:geometry.edges)if(edge.reference.owner_id==source.id)mesh.edges.push_back(edge);
    for(const auto& point:geometry.points)if(point.reference.owner_id==source.id)mesh.points.push_back(point);
    for(const auto& axis:geometry.axes)if(axis.reference.owner_id==source.id)mesh.axes.push_back(axis);
    mesh.original_references={mesh.vertices,mesh.triangles,mesh.triangle_references,mesh.edges,mesh.points,mesh.axes};
    return mesh;
}
} // namespace zima::workspace
