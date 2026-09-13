#include <zima/command_host/host.hpp>
#include <zima/workspace/value_lock_operations.hpp>
namespace zima::command_host {
namespace {
Json details(const workspace::Workspace& live,const std::string& id,const std::string& object) {
    auto values=Json::array();
    for(const auto& value:workspace::value_locks(live,id,object))
        values.push_back({{"key",value.key},{"locked",value.locked},{"editable",value.editable}});
    const auto revision=live.open_part(id)?live.open_part(id)->session.revision():live.open_assembly(id)->session.revision();
    return {{"document",id},{"object",object},{"items",std::move(values)},{"revision",revision}};
}
}
void Host::register_value_lock_commands() {
    dispatcher_.add({"value_lock.list",tr("List numeric property locks, including hidden and zero-valued dimensions."),
        {{"object",true},{"document",false}},false},[this](const Json& args) {
            try {return Result::success(details(workspace_,args.value("document",workspace_.active_document_id()),args.at("object")));}
            catch(const workspace::ValueLockError& error){return Result::failure(error.code,tr(error.what()));}
        });
    dispatcher_.add({"value_lock.set",tr("Lock or unlock a numeric property without calculating geometry."),
        {{"object",true},{"key",true},{"locked",true,commands::ArgumentType::Boolean},{"document",false}},true},[this](const Json& args) {
            const auto checked=target(args);if(!checked.ok)return checked;
            if(interaction().template_document)return Result::failure("unsupported_document",tr("Value locks require an open Part or Assembly."));
            try {
                const auto id=workspace_.active_document_id();const auto object=args.at("object").get<std::string>();
                const auto [owner,key]=workspace::value_lock_address(object,args.at("key"));
                const bool changed=workspace::set_value_lock(workspace_,id,owner,key,args.at("locked").get<bool>());
                auto data=details(workspace_,id,owner);data["changed"]=changed;
                if(changed)change_=Change{ChangeKind::Metadata,id};
                return Result::success(std::move(data));
            }catch(const workspace::ValueLockError& error){return Result::failure(error.code,tr(error.what()));}
             catch(const std::exception& error){return Result::failure("value_lock_rejected",tr(error.what()));}
        });
}
} // namespace zima::command_host
