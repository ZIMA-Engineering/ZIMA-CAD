#include <zima/command_host/host.hpp>
#include <zima/workspace/assembly_cut_operations.hpp>
namespace zima::command_host {
void Host::register_assembly_cut_commands() {
    using Type = commands::ArgumentType;
    const auto add = [this](commands::Command command, auto operation) {
        const bool mutates = command.changes_state;
        dispatcher_.add(std::move(command), [this, mutates, operation](const Json& args) {
            if (mutates) { const auto check = target(args); if (!check.ok) return check; }
            const auto id = args.value("document", workspace_.active_document_id());
            auto* state = workspace_.open_assembly(id);
            if (!state || (mutates && interaction().template_document))
                return Result::failure("unsupported_document", tr("Cut operations require an open Assembly."));
            try {
                const bool changed = operation(id, args);
                if (changed && mutates) change_ = Change{ChangeKind::Model, id, true};
                Json result = {{"document", id}, {"container", args.at("container")}, {"revision", state->session.revision()}};
                if (!mutates) { result["allowed"] = true; result["would_change"] = changed; }
                else result["changed"] = changed;
                auto order = Json::array();
                for (const auto& cut : state->session.document().cuts) order.push_back(cut.definition.id);
                result["order"] = std::move(order);
                return Result::success(std::move(result));
            } catch (const workspace::HistoryOperationError& error) { return Result::failure(error.code, tr(error.what())); }
              catch (const std::exception& error) { return Result::failure("history_rejected", tr(error.what())); }
        });
    };
    add({"assembly.cut.suppress",tr("Set Assembly cut suppression and calculate its owning Assembly."),
        {{"container",true},{"suppressed",true,Type::Boolean},{"document",false}},true},
        [this](const std::string& id,const Json& args) {
            return workspace::set_assembly_cut_suppressed(workspace_,kernel_,id,args.at("container").get<std::string>(),args.at("suppressed").get<bool>());
        });
    add({"assembly.cut.remove",tr("Remove an Assembly cut and its owned Sketch in one transaction."),
        {{"container",true},{"document",false}},true},[this](const std::string& id,const Json& args) {
            workspace::remove_assembly_cut(workspace_,kernel_,id,args.at("container").get<std::string>());return true;
        });
    for (const bool commit : {false,true})
        add({commit?"assembly.cut.move":"assembly.cut.can_move",commit?
            tr("Move an Assembly cut before another; omit before for the end."):
            tr("Check Assembly cut order and dependencies without calculation."),
            {{"container",true},{"before",false},{"document",false}},commit},
            [this,commit](const std::string& id,const Json& args) {
                return workspace::move_assembly_cut(workspace_,kernel_,id,args.at("container").get<std::string>(),args.value("before",std::string{}),commit);
            });
}
}
