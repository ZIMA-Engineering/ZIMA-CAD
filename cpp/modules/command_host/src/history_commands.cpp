#include <zima/workspace/assembly_history_operations.hpp>
#include <zima/command_host/host.hpp>
#include <zima/workspace/history_operations.hpp>
#include <limits>

namespace zima::command_host {
namespace {
workspace::PartState& part(workspace::Workspace& live,const Json& args) {
    auto* state=live.open_part(args.value("document",live.active_document_id()));
    if(!state)throw workspace::HistoryOperationError("unsupported_document","History operations require an open Part.");
    return *state;
}
Json assembly_history_data(const workspace::AssemblyState& state) {
    const auto& document=state.session.document();
    Json rows=Json::array();
    Json orders={{"components",Json::array()},{"constructions",Json::array()},
        {"sketches",Json::array()},{"cuts",Json::array()}};
    const auto append=[&](const char* group,const char* kind,const std::string& id,
        const std::string& name,bool suppressed) {
        orders[group].push_back(id);
        rows.push_back({{"object",id},{"kind",kind},{"name",name},{"suppressed",suppressed}});
    };
    for(const auto& value:document.components)
        append("components","component",value.occurrence_id,value.name,value.suppressed);
    for(const auto& value:document.constructions)
        append("constructions","construction",value.id,value.name,value.suppressed);
    for(const auto& value:document.sketch_containers)
        append("sketches","sketch",value.id,value.name,value.suppressed);
    for(const auto& value:document.cuts)
        append("cuts","cut",value.definition.id,value.definition.name,value.definition.suppressed);
    return {{"document",document.document_id},{"type","assembly"},{"items",std::move(rows)},
        {"orders",std::move(orders)},{"revision",state.session.revision()}};
}
Json history_data(const workspace::PartState& state) {
    const auto& document=state.session.document();Json rows=Json::array();
    for(const auto& entry:document.history_order) {
        Json row={{"object",entry.id},{"suppressed",workspace::part_history_suppressed(document,entry.id)}};
        if(const auto* feature=document.find_container(entry.id)) {row["kind"]="feature";row["name"]=feature->name;}
        else if(const auto* object=document.find_construction(entry.id)) {row["kind"]="construction";row["name"]=object->name;}
        else {row["kind"]="sketch";for(const auto& sketch:document.sketches)if(sketch.id==entry.id)row["name"]=sketch.name;}
        const auto* owner=document.body_history.owner(entry.id);row["body"]=owner?owner->scope.id:std::string{};
        rows.push_back(std::move(row));
    }
    const auto& boundaries=state.session.calculated_boundaries();
    return {{"document",document.document_id},{"items",std::move(rows)},
        {"body_order",document.body_history.order()},{"active_body",document.body_history.active_body_id()},
        {"cursor",document.effective_history_cursor()},{"revision",state.session.revision()},
        {"calculation_errors",boundaries.empty()?Json::object():Json(boundaries.back().calculation_errors)}};
}
}
void Host::register_history_commands() {
    const auto add=[this](commands::Command command,std::function<Json(const Json&)> operation) {
        const bool changes=command.changes_state;
        dispatcher_.add(std::move(command),[this,changes,operation=std::move(operation)](const Json& args) {
            if(changes) {
                const auto check=target(args);if(!check.ok)return check;
                if(interaction().template_document)return Result::failure("unsupported_document",tr("History operations require an open Part."));
            }
            try {
                auto result=operation(args);
                if(changes && result.value("changed",false)) {
                    change_=Change{ChangeKind::Model,workspace_.active_document_id(),result.value("body_calculated",true)};
                    if(result.contains("calculation_errors")&&!result.at("calculation_errors").empty())
                        return Result{false,"calculation_errors",tr("History changed; some dependent features could not be calculated."),std::move(result)};
                }
                return Result::success(std::move(result));
            } catch(const workspace::HistoryOperationError& error) {return Result::failure(error.code,tr(error.what()));}
              catch(const std::exception& error) {return Result::failure("history_rejected",tr(error.what()));}
        });
    };
    add({"history.list",tr("Read Part history or the separate Assembly history lists without calculation."),{{"document",false}},false},[this](const Json& args) {
        if(const auto* state=workspace_.open_assembly(args.value("document",workspace_.active_document_id())))
            return assembly_history_data(*state);
        return history_data(part(workspace_,args));
    });
    add({"history.suppress",tr("Set suppression of a history object and calculate the Part."),
        {{"object",true},{"suppressed",true,commands::ArgumentType::Boolean},{"document",false}},true},[this](const Json& args) {
        const auto document_id=part(workspace_,args).session.document().document_id;
        const bool changed=workspace::set_part_history_suppressed(workspace_,document_id,kernel_,args["object"].get<std::string>(),args["suppressed"].get<bool>());
        auto data=history_data(part(workspace_,args));data["changed"]=changed;return data;
    });
    add({"history.delete",tr("Delete a history object, Body or Boolean and calculate the Part."),{{"object",true},{"document",false}},true},[this](const Json& args) {
        const auto document_id=part(workspace_,args).session.document().document_id;workspace::delete_part_history(workspace_,document_id,kernel_,args["object"].get<std::string>());
        auto data=history_data(part(workspace_,args));data["changed"]=true;return data;
    });
    for(bool commit:{false,true}) {
        add({commit?"history.move":"history.can_move",commit?tr("Move an object before another in the same history; omit before for the end."):
            tr("Check history order and dependencies without calculating geometry."),{{"object",true},{"before",false},{"document",false}},commit},[this,commit](const Json& args) {
            const auto id=args.value("document",workspace_.active_document_id());
            if(auto* state=workspace_.open_assembly(id)) {
                const auto object=args.at("object").get<std::string>();
                const bool cut=state->session.document().find_cut(object)!=nullptr;
                const bool changed=workspace::move_assembly_history(workspace_,id,kernel_,object,
                    args.value("before",std::string{}),commit);
                if(!commit)return Json{{"allowed",true},{"would_change",changed},{"document",id},
                    {"revision",state->session.revision()},{"body_calculated",false}};
                auto data=assembly_history_data(*state);data["changed"]=changed;
                data["body_calculated"]=cut&&changed;return data;
            }
            const auto document_id=part(workspace_,args).session.document().document_id;
            const bool changed=workspace::move_part_history(workspace_,document_id,kernel_,args["object"].get<std::string>(),args.value("before",std::string{}),commit);
            if(!commit)return Json{{"allowed",true},{"would_change",changed},{"document",document_id},{"revision",workspace_.open_part(document_id)->session.revision()}};
            auto data=history_data(part(workspace_,args));data["changed"]=changed;return data;
        });
    }
    add({"history.cursor",tr("Set the insertion index in the flattened Part history."),{{"index",true,commands::ArgumentType::Integer},{"document",false}},true},[this](const Json& args) {
        const auto document_id=part(workspace_,args).session.document().document_id;
        if(args["index"]<0 || args["index"].get<std::uint64_t>()>std::numeric_limits<std::size_t>::max())
            throw workspace::HistoryOperationError("invalid_arguments","The history index must be a nonnegative integer.");
        const bool changed=workspace::set_part_history_cursor(workspace_,document_id,args["index"].get<std::size_t>());
        auto data=history_data(part(workspace_,args));data["changed"]=changed;return data;
    });
}
}
