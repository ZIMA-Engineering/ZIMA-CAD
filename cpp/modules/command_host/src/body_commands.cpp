#include <zima/command_host/host.hpp>
#include <zima/workspace/body_operations.hpp>
#include <zima/document/placement_json.hpp>
#include <algorithm>
#include <cctype>
#include <limits>

namespace zima::command_host {
namespace {
workspace::PartState& part(workspace::Workspace& workspace,const Json& args) {
    auto* state=workspace.open_part(args.value("document",workspace.active_document_id()));
    if(!state)throw workspace::BodyOperationError("unsupported_document","Body operations require an open Part.");
    return *state;
}
std::string name(std::string text) {
    const auto space=[](unsigned char c){return std::isspace(c)!=0;};
    const auto first=std::find_if_not(text.begin(),text.end(),space);
    if(first==text.end())throw workspace::BodyOperationError("invalid_arguments","Specify a nonempty object name.");
    return {first,std::find_if_not(text.rbegin(),text.rend(),space).base()};
}
const char* mode_name(kernel::BodyCombination mode) {
    switch(mode) {
    case kernel::BodyCombination::Add:return "add";
    case kernel::BodyCombination::Subtract:return "subtract";
    case kernel::BodyCombination::Intersect:return "intersect";
    }
    throw workspace::BodyOperationError("invalid_arguments","Expected add, subtract or intersect.");
}
kernel::BodyCombination mode(const std::string& value) {
    if(value=="add")return kernel::BodyCombination::Add;
    if(value=="subtract")return kernel::BodyCombination::Subtract;
    if(value=="intersect")return kernel::BodyCombination::Intersect;
    throw workspace::BodyOperationError("invalid_arguments","Expected add, subtract or intersect.");
}
Json body_data(const workspace::PartState& state,const std::string& id) {
    const auto& document=state.session.document();const auto* body=document.body_history.find(id);
    if(!body)throw workspace::BodyOperationError("body_not_found","The requested Body does not exist.");
    Json entries=Json::array();for(const auto& entry:body->entries)entries.push_back(entry.id);
    return {{"document",document.document_id},{"body",id},{"kind","body"},{"name",body->name},
        {"origin",body->origin().id},{"active",document.body_history.active_body_id()==id},
        {"visible",body->visible},{"derived",body->derived_copy.has_value()},
        {"history",std::move(entries)},{"cursor",body->cursor},{"dependencies",body->dependencies},
        {"placement",body->scope.placement},{"revision",state.session.revision()}};
}
Json boolean_data(const workspace::PartState& state,const std::string& id) {
    const auto& document=state.session.document();const auto* value=document.body_history.find_boolean(id);
    if(!value)throw workspace::BodyOperationError("boolean_not_found","The requested Body Boolean does not exist.");
    return {{"document",document.document_id},{"boolean",id},{"kind","boolean"},{"name",value->name},
        {"visible",value->visible},{"operation",mode_name(value->operation)},
        {"target",value->target_id},{"tool",value->tool_id},{"revision",state.session.revision()}};
}
Json graph_data(const workspace::PartState& state) {
    const auto& graph=state.session.document().body_history;Json items=Json::array();
    for(const auto& id:graph.order())items.push_back(graph.find(id)?body_data(state,id):boolean_data(state,id));
    return {{"document",state.session.document().document_id},{"items",std::move(items)},
        {"active_body",graph.active_body_id()},{"insertion_cursor",graph.insertion_cursor()},
        {"available_results",graph.available_before(graph.insertion_cursor())},{"visible_context",graph.visible_context()},
        {"revision",state.session.revision()}};
}
}
void Host::register_body_commands() {
    const auto add=[this](commands::Command command,std::function<Json(const Json&)> operation) {
        const bool changes=command.changes_state;
        const bool clear_selection=command.name=="body.activate";
        dispatcher_.add(std::move(command),[this,changes,clear_selection,operation=std::move(operation)](const Json& args) {
            if(changes) {
                const auto check=target(args);if(!check.ok)return check;
                if(interaction().template_document)return Result::failure("unsupported_document",tr("Body operations require an open Part."));
            }
            try {
                auto result=operation(args);
                if(changes && result.value("changed",false))change_=Change{ChangeKind::Model,workspace_.active_document_id(),clear_selection};
                return Result::success(std::move(result));
            } catch(const workspace::BodyOperationError& error) {return Result::failure(error.code,tr(error.what()));}
        });
    };
    add({"body.list",tr("Read Bodies, Boolean results and their history order."),{{"document",false}},false},[this](const Json& args) {
        return graph_data(part(workspace_,args));
    });
    add({"body.get",tr("Read Body properties and persisted placement references."),{{"body",true},{"document",false}},false},[this](const Json& args) {
        return body_data(part(workspace_,args),args["body"].get<std::string>());
    });
    add({"body.create",tr("Create a Body attached to the Part origin."),
        {{"name",true},{"visible",false,commands::ArgumentType::Boolean},{"active",false,commands::ArgumentType::Boolean},{"document",false}},true},[this](const Json& args) {
        auto& state=part(workspace_,args);
        const auto edit=workspace::prepare_body_edit(state.session.document(),{},name(args["name"].get<std::string>()));
        auto value=*edit.pending.find(edit.object_id);value.visible=args.value("visible",true);
        const auto changed=workspace::commit_body_edit(workspace_,kernel_,edit,std::move(value),args.value("active",true));
        auto result=body_data(state,edit.object_id);result["changed"]=changed;return result;
    });
    add({"body.set",tr("Change Body name, visibility or activation."),
        {{"body",true},{"name",false},{"visible",false,commands::ArgumentType::Boolean},{"active",false,commands::ArgumentType::Boolean},{"document",false}},true},[this](const Json& args) {
        auto& state=part(workspace_,args);
        const auto edit=workspace::prepare_body_edit(state.session.document(),args["body"].get<std::string>());
        if(!args.contains("name") && !args.contains("visible") && !args.contains("active"))
            throw workspace::BodyOperationError("invalid_arguments","Specify at least one Body property.");
        auto value=*edit.pending.find(edit.object_id);
        if(args.contains("name"))value.name=name(args["name"].get<std::string>());
        if(args.contains("visible"))value.visible=args["visible"].get<bool>();
        const auto changed=workspace::commit_body_edit(workspace_,kernel_,edit,std::move(value),args.value("active",edit.original.active_body_id()==edit.object_id));
        auto result=body_data(state,edit.object_id);result["changed"]=changed;return result;
    });
    add({"body.activate",tr("Activate a Body; omit its ID to deactivate it."),{{"body",false},{"document",false}},true},[this](const Json& args) {
        auto& state=part(workspace_,args);
        const auto changed=workspace::activate_part_body(workspace_,state.session.document().document_id,args.value("body",std::string{}));
        auto result=graph_data(state);result["changed"]=changed;return result;
    });
    add({"body.cursor",tr("Set the insertion index in Body history or in the active Body."),
        {{"index",true,commands::ArgumentType::Integer},{"body",false},{"document",false}},true},[this](const Json& args) {
        auto& state=part(workspace_,args);
        if(args["index"]<0 || args["index"].get<std::uint64_t>()>std::numeric_limits<std::size_t>::max())
            throw workspace::BodyOperationError("invalid_arguments","The history index must be a nonnegative integer.");
        const auto changed=workspace::set_body_history_cursor(workspace_,state.session.document().document_id,
            args["index"].get<std::size_t>(),args.value("body",std::string{}));
        auto result=graph_data(state);result["changed"]=changed;return result;
    });
    add({"body.boolean.get",tr("Read a Body Boolean and its two source results."),{{"boolean",true},{"document",false}},false},[this](const Json& args) {
        return boolean_data(part(workspace_,args),args["boolean"].get<std::string>());
    });
    for(bool create:{true,false}) {
        std::vector<commands::Argument> fields;if(!create)fields.push_back({"boolean",true});
        fields.insert(fields.end(),{{"operation",create},{"target",create},{"tool",create},{"name",false},{"document",false}});
        add({create?"body.boolean.create":"body.boolean.set",create?tr("Calculate a Boolean from two available Body results."):
            tr("Edit a Body Boolean using the same operation as Properties."),std::move(fields),true},[this,create](const Json& args) {
            auto& state=part(workspace_,args);
            if(!create && !args.contains("operation") && !args.contains("target") && !args.contains("tool") && !args.contains("name"))
                throw workspace::BodyOperationError("invalid_arguments","Specify at least one Boolean property.");
            const auto edit=create?workspace::prepare_body_boolean_edit(state.session.document(),{},name(args.value("name",std::string("Boolean"))),
                mode(args["operation"].get<std::string>()),args["target"].get<std::string>(),args["tool"].get<std::string>()):
                workspace::prepare_body_boolean_edit(state.session.document(),args["boolean"].get<std::string>());
            auto value=*edit.pending.find_boolean(edit.object_id);
            if(args.contains("operation"))value.operation=mode(args["operation"].get<std::string>());
            if(args.contains("target"))value.target_id=args["target"].get<std::string>();
            if(args.contains("tool"))value.tool_id=args["tool"].get<std::string>();
            if(args.contains("name"))value.name=name(args["name"].get<std::string>());
            const auto changed=workspace::commit_body_boolean_edit(workspace_,kernel_,edit,std::move(value));
            auto result=boolean_data(state,edit.object_id);result["changed"]=changed;return result;
        });
    }
}
} // namespace zima::command_host
