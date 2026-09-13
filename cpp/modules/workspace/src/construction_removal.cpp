#include <zima/workspace/construction_removal.hpp>
#include <zima/workspace/history_operations.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <algorithm>
namespace zima::workspace {
namespace {
bool contains(const document::ConstructionObject& root,const std::string& id) {
    return root.id==id||std::ranges::any_of(root.curve_points,[&](const auto& child){return contains(child,id);});
}
const document::ConstructionObject& root(const Workspace& live,const std::string& id,const std::string& object) {
    const auto* part=live.open_part(id);const auto* assembly=live.open_assembly(id);
    if(!part&&!assembly)throw ConstructionRemovalError("unsupported_document","Construction operations require an open Part or Assembly.");
    const auto& objects=part?part->session.document().constructions:assembly->session.document().constructions;
    for(const auto& value:objects)if(value.id==object)return value;
    for(const auto& value:objects)if(contains(value,object))
        throw ConstructionRemovalError("owned_construction","Edit an owned Point through its parent 3D curve.");
    if(part)for(const auto& feature:part->session.document().history)
        if(feature.feature_kind==document::FeatureKind::Sweep3D&&contains(feature.sweep3d.path,object))
            throw ConstructionRemovalError("embedded_construction","Edit an embedded path through its owning Sweep command.");
    throw ConstructionRemovalError("construction_not_found","The requested construction object does not exist in this document.");
}
void require_unused(const Workspace& live,const assembly::AssemblyDocument& doc,const document::ConstructionObject& object) {
    HistoryDependencyCollector aliases;aliases.register_construction(object,object.id);
    const auto owner=[&](const auto& id){return aliases.owners.contains(id);};
    const auto local=[&](const auto& ref){return ref.instance_path.empty()&&owner(ref.owner_id);};
    bool used=false;
    const auto construction=[&](const auto& self,const auto& value)->void {
        used=used||std::ranges::any_of(value.references,local);
        for(const auto& point:value.curve_points)self(self,point);
    };
    for(const auto& value:doc.constructions)if(value.id!=object.id)construction(construction,value);
    for(const auto& component:doc.components)for(const auto& row:component.placement_references)
        used=used||(row.target_reference.instance_path.occurrence_ids.empty()&&owner(row.target_reference.owner_id))||
            (row.component_reference.instance_path.occurrence_ids.empty()&&owner(row.component_reference.owner_id));
    for(const auto& cut:doc.cuts) {
        HistoryDependencyCollector references;references.feature(cut.definition);
        for(const auto& [consumer,path,id,key]:references.references)used=used||(path.empty()&&owner(id));
    }
    for(const auto& section:doc.sections)used=used||std::ranges::any_of(section.placement.references,local);
    const auto sketches=[&](const auto& source,bool own) {
        visit_document_sketches(source,[&](const auto& sketch) {
            if(own&&owner(sketch.plane_reference_owner_id))used=true;
            for(const auto& ref:sketch.external_references)
                if((ref.source_document_id==doc.document_id||(own&&ref.source_document_id.empty()&&ref.source_instance_path.empty()))&&owner(ref.source_owner_id))used=true;
            return !used;
        });
    };
    sketches(doc,true);
    // Borrow the existing Workspace states. A dependency check must not deep
    // copy every open Part's geometry and Undo history.
    for(const auto& state:live.documents())if(const auto* part=std::get_if<PartState>(&state))sketches(part->session.document(),false);
    if(used)throw ConstructionRemovalError("construction_in_use","The construction is still used by a placement, Sketch, section or another construction.");
}
}
void delete_construction(Workspace& live,const kernel::OcctKernel& kernel,const std::string& document_id,const std::string& construction_id) {
    const auto id=document_id,object_id=construction_id;
    const auto& object=root(live,id,object_id);
    if(live.open_part(id)){delete_part_history(live,id,kernel,object_id);return;}
    auto* assembly=live.open_assembly(id);
    require_unused(live,assembly->session.document(),object);
    auto next=assembly->session.document();
    std::erase_if(next.constructions,[&](const auto& value){return value.id==object_id;});
    next.resolve_constructions();next.calculate_placement_references();
    assembly->session.commit(std::move(next));
}
}
