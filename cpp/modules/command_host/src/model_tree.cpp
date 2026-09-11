#include <zima/command_host/host.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace zima::command_host {
namespace {
struct Tree {
    Json rows=Json::array();std::size_t limit;bool truncated{};
    bool add(const std::string& id,const std::string& parent,const std::string& doc,
        const std::string& path,int depth,const std::string& type,const std::string& label,
        const std::string& semantic={}) {
        if(id.empty())return false;
        if(rows.size()>=limit){truncated=true;return false;}
        rows.push_back({{"id",id},{"parent_id",parent},{"parent_instance_path",path},
            {"document_id",doc},{"instance_path",path},{"depth",depth},{"type",type},
            {"label",label},{"semantic_key",semantic}});return true;
    }
    void origin(const document::ContainerOrigin& value,const std::string& parent,const std::string& doc,const std::string& path,int depth){
        if(!add(value.id,parent,doc,path,depth,"origin",value.name))return;
        for(const auto& child:value.children){
            if(!add(child.id,value.id,doc,path,depth+1,"origin-child",child.name,child.key))break;
            rows.back()["owner_id"]=value.id;
        }
    }
    void sketch(const sketcher::Sketch& value,const std::string& parent,const std::string& doc,const std::string& path,int depth){
        if(!add(value.id,parent,doc,path,depth,"sketch",value.name))return;
        rows.back()["suppressed"]=value.suppressed;
        const auto geometry=[&](const auto& values,const char* kind){for(const auto& item:values)
            if(!add(item.id,value.id,doc,path,depth+1,kind,kind))break;};
        geometry(value.points,"sketch-point");geometry(value.segments,"sketch-segment");
        geometry(value.circles,"sketch-circle");geometry(value.arcs,"sketch-arc");
        geometry(value.ellipses,"sketch-ellipse");geometry(value.elliptical_arcs,"sketch-elliptical-arc");
        geometry(value.bsplines,"sketch-bspline");geometry(value.texts,"sketch-text");
        geometry(value.constraints,"sketch-constraint");geometry(value.dimensions,"sketch-dimension");
        for(const auto& reference:value.external_references){
            if(!add(reference.id,value.id,doc,path,depth+1,"sketch-external-reference","reference"))break;
            rows.back()["source_document_id"]=reference.source_document_id;
            rows.back()["source_owner_id"]=reference.source_owner_id;
            rows.back()["source_semantic_key"]=reference.source_semantic_key;
            rows.back()["source_instance_path"]=reference.source_instance_path;
            rows.back()["broken"]=reference.broken;
        }
    }
    void construction(const document::ConstructionObject& value,const std::string& parent,const std::string& doc,const std::string& path,int depth){
        // Explicit stack avoids recursive traversal of imported/deep definitions.
        struct Item {const document::ConstructionObject* object;std::string parent;int depth;};
        std::vector<Item> pending{{&value,parent,depth}};
        while(!pending.empty()&&!truncated){
            auto item=std::move(pending.back());pending.pop_back();const auto& object=*item.object;
            if(!add(object.id,item.parent,doc,path,item.depth,"construction",object.name))break;
            origin(object.container_origin,object.id,doc,path,item.depth+1);
            if(!object.entity_id.empty())add(object.entity_id,object.id,doc,path,item.depth+1,"construction-entity",object.name);
            for(auto it=object.curve_points.rbegin();it!=object.curve_points.rend();++it)pending.push_back({&*it,object.id,item.depth+1});
        }
    }
    void feature(const document::HistoryContainer& value,const std::vector<sketcher::Sketch>& sketches,
        const std::string& parent,const std::string& doc,const std::string& path,int depth){
        if(!add(value.id,parent,doc,path,depth,"history-container",value.name))return;
        rows.back()["suppressed"]=value.suppressed;
        origin(value.container_origin,value.id,doc,path,depth+1);
        if(!value.feature_id.empty())add(value.feature_id,value.id,doc,path,depth+1,"feature-entity",value.name);
        for(const auto& owned:sketches)if(owned.owner_container_id==value.id)sketch(owned,value.id,doc,path,depth+1);
        if(value.feature_kind==document::FeatureKind::Sweep3D)construction(value.sweep3d.path,value.id,doc,path,depth+1);
        if(value.feature_kind==document::FeatureKind::Sweep2D)
            for(const auto& data:value.sweep2d.sketches())if(!truncated)sketch(sketcher::Sketch::from_serialized(data),value.id,doc,path,depth+1);
    }
    void sections(const std::vector<document::SectionDefinition>& values,const std::string& parent,const std::string& doc,int depth){
        for(const auto& value:values){
            if(!add(value.id,parent,doc,{},depth,"section",value.name))break;
            origin(value.container_origin,value.id,doc,{},depth+1);sketch(value.sketch,value.id,doc,{},depth+1);
        }
    }
    void part(const document::PartDocument& value){
        const auto& id=value.document_id;if(!add(id,{},id,{},0,"part",value.name))return;
        origin(document::create_container_origin(id),id,id,{},1);
        std::map<std::string,const document::HistoryContainer*> features;
        std::map<std::string,const document::ConstructionObject*> objects;
        std::map<std::string,const sketcher::Sketch*> sketches;
        std::set<std::string> emitted;
        for(const auto& item:value.history)features.emplace(item.id,&item);
        for(const auto& item:value.constructions)objects.emplace(item.id,&item);
        for(const auto& item:value.sketches)sketches.emplace(item.id,&item);
        const auto entry=[&](const std::string& entry_id,const std::string& parent,int depth){
            if(!emitted.insert(entry_id).second||truncated)return;
            if(const auto found=features.find(entry_id);found!=features.end())feature(*found->second,value.sketches,parent,id,{},depth);
            else if(const auto found=objects.find(entry_id);found!=objects.end())construction(*found->second,parent,id,{},depth);
            else if(const auto found=sketches.find(entry_id);found!=sketches.end()&&found->second->owner_container_id.empty())sketch(*found->second,parent,id,{},depth);
        };
        for(const auto& step:value.body_history.order()){
            if(truncated)break;
            if(const auto* body=value.body_history.find(step)){
                if(!add(step,id,id,{},1,"body",body->name))break;
                rows.back()["visible"]=body->visible;origin(body->origin(),step,id,{},2);
                for(const auto& item:body->entries)entry(item.id,step,2);
            }else if(const auto* operation=value.body_history.find_boolean(step))
                add(step,id,id,{},1,"body-boolean",operation->name);
        }
        // Include all current objects, including unowned construction/sketch items.
        for(const auto& item:value.history_order)entry(item.id,id,1);
        for(const auto& item:value.history)entry(item.id,id,1);
        for(const auto& item:value.constructions)entry(item.id,id,1);
        for(const auto& item:value.sketches)entry(item.id,id,1);
        sections(value.sections,id,id,1);
    }
    void assembly(const assembly::AssemblyDocument& value){
        const auto& id=value.document_id;if(!add(id,{},id,{},0,"assembly",value.name))return;
        origin(document::create_container_origin(id),id,id,{},1);
        for(const auto& item:value.constructions)construction(item,id,id,{},1);
        for(const auto& item:value.cuts)feature(item.definition,value.sketches,id,id,{},1);
        for(const auto& item:value.sketches)if(item.owner_container_id.empty())sketch(item,id,id,{},1);
        sections(value.sections,id,id,1);
        struct Item {const assembly::OccurrenceSnapshot* snapshot;const assembly::PartOccurrence* component;
            std::string parent,owner;assembly::InstancePath path;int depth;bool suppressed,visible;};
        std::vector<Item> pending;
        for(auto it=value.components.rbegin();it!=value.components.rend();++it)pending.push_back({nullptr,&*it,id,id,{},1,false,true});
        while(!pending.empty()&&!truncated){
            auto item=std::move(pending.back());pending.pop_back();
            const auto visit=[&](const auto& component,const auto& children,bool suppressed){
                const auto path=item.path.child(component.occurrence_id);
                if(!add(component.occurrence_id,item.parent,item.owner,path.encoded(),item.depth,
                    component.source_kind==assembly::ComponentSourceKind::Part?"part-occurrence":
                    component.source_kind==assembly::ComponentSourceKind::Pattern?"pattern-occurrence":"assembly-occurrence",component.name))return;
                rows.back()["parent_instance_path"]=item.path.encoded();
                rows.back()["source_document_id"]=component.source_document_id;
                rows.back()["suppressed"]=item.suppressed||suppressed;
                rows.back()["visible"]=item.visible&&component.visible;
                for(auto it=children.rbegin();it!=children.rend();++it)pending.push_back({&*it,nullptr,component.occurrence_id,
                    component.source_kind==assembly::ComponentSourceKind::Pattern?item.owner:component.source_document_id,
                    path,item.depth+1,item.suppressed||suppressed,item.visible&&component.visible});
            };
            if(item.component)visit(*item.component,item.component->nested_snapshot,item.component->suppressed);
            else visit(*item.snapshot,item.snapshot->children,item.snapshot->manually_suppressed||item.snapshot->dependency_suppressed);
        }
    }
    void drawing(const drawing::DrawingDocument& value){
        const auto& id=value.document_id;if(!add(id,{},id,{},0,"drawing",value.name))return;
        for(const auto& sheet:value.sheets){
            if(!add(sheet.id,id,id,{},1,"drawing-sheet",sheet.name))break;
            for(const auto& view:sheet.views){
                if(!add(view.id,sheet.id,id,{},2,"drawing-view",view.name))break;
                rows.back()["source_document_id"]=view.source_document_id;
            }
            for(const auto& dimension:sheet.dimensions)if(!add(dimension.id,sheet.id,id,{},2,"drawing-dimension","dimension"))break;
        }
    }
};
}
Json model_tree(const workspace::Workspace& workspace,const std::string& id,std::size_t limit){
    Tree tree;tree.limit=std::min<std::size_t>(limit,2000);
    const auto* state=workspace.find(id);
    if(!id.empty()&&!state)throw std::invalid_argument("Tree requires an open document");
    if(state)std::visit([&](const auto& value){
        using State=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<State,workspace::PartState>)tree.part(value.session.document());
        else if constexpr(std::is_same_v<State,workspace::AssemblyState>)tree.assembly(value.session.document());
        else tree.drawing(value.document());
    },*state);
    return {{"document",id},{"projection","model"},{"items",std::move(tree.rows)},{"truncated",tree.truncated}};
}
} // namespace zima::command_host
