#include <zima/command_host/host.hpp>
#include <zima/workspace/component_operations.hpp>
#include <zima/document/file_path.hpp>
#include <algorithm>
namespace zima::command_host {
namespace {
const char* kind_name(assembly::ComponentSourceKind kind) {
    switch(kind){case assembly::ComponentSourceKind::Part:return "part";case assembly::ComponentSourceKind::Assembly:return "assembly";case assembly::ComponentSourceKind::Pattern:return "pattern";}
    throw std::invalid_argument("Unknown component source kind.");
}
Json placement(const assembly::ComponentPlacement& p){return {{"x_mm",p.x},{"y_mm",p.y},{"z_mm",p.z},{"rotation_x_deg",p.rotation_x},{"rotation_y_deg",p.rotation_y},{"rotation_z_deg",p.rotation_z}};}
Json reference(const assembly::MateReference& ref){return {{"kind",ref.kind==assembly::MateReferenceKind::Face?"face":ref.kind==assembly::MateReferenceKind::Axis?"axis":"point"},{"instance_path",ref.instance_path.encoded()},{"owner",ref.owner_id},{"key",ref.semantic_key}};}
Json direct_details(const assembly::PartOccurrence& item) {
    Json rows=Json::array();for(const auto& row:item.placement_references) {
        const auto* kind=row.mate_type==assembly::MateKind::PlaneCoincident?"plane_coincident":row.mate_type==assembly::MateKind::AxisCoincident?"axis_coincident":row.mate_type==assembly::MateKind::PointCoincident?"point_coincident":"plane_angle";
        rows.push_back({{"kind",kind},{"component",reference(row.component_reference)},{"target",reference(row.target_reference)},{"offset",row.offset},{"flip",row.flip},{"locked",row.offset_locked},
            {"lower_limit",row.lower_limit?Json(*row.lower_limit):Json(nullptr)},{"upper_limit",row.upper_limit?Json(*row.upper_limit):Json(nullptr)}});
    }
    return {{"source_path",document::path_to_utf8(item.source_path)},{"placement_references",std::move(rows)},{"cached_volume_mm3",item.calculated_source->volume},{"cached_area_mm2",item.calculated_source->surface_area},{"value_locks",item.value_locks}};
}
// Traverse the calculated/persisted hierarchy, not newly edited open sources.
// Selection and query paths therefore describe the same snapshot on screen.
Json component_rows(const assembly::AssemblyDocument& doc,bool recursive,std::size_t limit,const std::string& selected={}) {
    Json items=Json::array();std::size_t total=0;const auto suppressed=doc.effectively_suppressed_occurrences();
    const auto row=[&](const std::string& id,const std::string& name,const std::string& source,auto kind,const auto& local,bool visible,bool manual,bool effective,bool grounded,bool derived,
        const assembly::InstancePath& path,const std::string& owner,bool parent_visible,std::size_t children,const assembly::PartOccurrence* direct) {
        ++total;const auto encoded=path.encoded();if((!selected.empty() && encoded!=selected) || items.size()>=limit)return;
        Json value={{"occurrence",id},{"instance_path",encoded},{"parent_path",path.parent()?path.parent()->encoded():""},{"owning_document",owner},{"source_document",source},{"kind",kind_name(kind)},{"name",name},
            {"visible",visible},{"effective_visible",parent_visible && visible && !effective},{"suppressed",manual},{"effective_suppressed",effective},{"grounded",grounded},{"derived",derived},{"direct",direct!=nullptr},{"child_count",children},{"placement",placement(local)}};
        if(direct)value.update(direct_details(*direct));items.push_back(std::move(value));
    };
    const auto children=[&](const auto& self,const auto& nodes,const assembly::InstancePath& parent,const std::string& owner,bool ancestor_visible,bool ancestor_suppressed,std::size_t depth)->void {
        if(depth>256)throw workspace::ComponentOperationError("dependency_limit","The component dependency graph is too large or too deep.");
        for(const auto& node:nodes) {
            const auto path=parent.child(node.occurrence_id);const auto effective=ancestor_suppressed || node.manually_suppressed || node.dependency_suppressed;
            row(node.occurrence_id,node.name,node.source_document_id,node.source_kind,node.placement,node.visible,node.manually_suppressed,effective,node.grounded,!node.derived_source_id.empty(),path,owner,ancestor_visible,node.children.size(),nullptr);
            self(self,node.children,path,node.source_kind==assembly::ComponentSourceKind::Pattern?owner:node.source_document_id,ancestor_visible && node.visible, effective,depth+1);
        }
    };
    for(const auto& item:doc.components) {
        const auto path=assembly::InstancePath{}.child(item.occurrence_id);const auto effective=suppressed.contains(item.occurrence_id);
        row(item.occurrence_id,item.name,item.source_document_id,item.source_kind,item.placement,item.visible,item.suppressed,effective,item.grounded,item.derived_copy.has_value(),path,doc.document_id,true,item.nested_snapshot.size(),&item);
        if(recursive)children(children,item.nested_snapshot,path,item.source_kind==assembly::ComponentSourceKind::Pattern?doc.document_id:item.source_document_id,item.visible,effective,1);
    }
    return {{"items",std::move(items)},{"total",total}};
}
}
void Host::register_component_commands() {
    using Type=commands::ArgumentType;
    const auto add=[this](commands::Command command,std::function<Json(const workspace::AssemblyState&,const Json&)> action) {
        const bool changes=command.changes_state;
        dispatcher_.add(std::move(command),[this,changes,action](const Json& args) {
            if(changes){const auto check=target(args);if(!check.ok)return check;if(interaction().template_document)return Result::failure("unsupported_document",tr("Component insertion requires an open owning Assembly."));}
            const auto id=args.value("document",workspace_.active_document_id());const auto* state=workspace_.open_assembly(id);
            if(!state)return Result::failure("unsupported_document",tr("Component commands require an open Assembly."));
            try {auto result=action(*state,args);result["document"]=id;result["revision"]=workspace_.open_assembly(id)->session.revision();if(changes)change_=Change{ChangeKind::Model,id,true};return Result::success(std::move(result));}
            catch(const workspace::ComponentOperationError& e){return Result::failure(e.code,tr(e.what()));}
            catch(const std::invalid_argument& e){return Result::failure("invalid_arguments",tr(e.what()));}
            catch(const std::exception& e){return Result::failure("component_failed",tr(e.what()));}
        });
    };
    add({"component.list",tr("List component occurrences from the stored Assembly hierarchy."),{{"recursive",false,Type::Boolean},{"limit",false,Type::Integer},{"document",false}},false},[](const auto& state,const Json& args){
        const auto limit=args.value("limit",2000.0);if(limit<1 || limit>10000)throw std::invalid_argument("Component query limit must be from 1 to 10000.");
        return component_rows(state.session.document(),args.value("recursive",false),static_cast<std::size_t>(limit));
    });
    add({"component.get",tr("Read one exact persisted occurrence and its available component data."),{{"instance_path",true},{"document",false}},false},[](const auto& state,const Json& args){
        const auto path=assembly::InstancePath::decode(args["instance_path"].get<std::string>());
        if(path.occurrence_ids.empty())throw workspace::ComponentOperationError("occurrence_not_found","The requested component occurrence does not exist.");
        auto result=component_rows(state.session.document(),true,1,path.encoded());
        if(result["items"].empty())throw workspace::ComponentOperationError("occurrence_not_found","The requested component occurrence does not exist.");
        return result["items"][0];
    });
    add({"component.insert",tr("Insert an open Part or Assembly through the shared native insertion transaction."),{{"source",true},{"name",false},{"document",false}},true},[this](const auto& state,const Json& args){
        const auto id=state.session.document().document_id;
        const auto occurrence=workspace::insert_component(workspace_,id,args["source"].get<std::string>(),args.contains("name")?std::optional<std::string>(args["name"].get<std::string>()):std::nullopt);
        return Json{{"occurrence",occurrence},{"instance_path",assembly::InstancePath{}.child(occurrence).encoded()},{"source_document",args["source"]},{"changed",true}};
    });
}
}
