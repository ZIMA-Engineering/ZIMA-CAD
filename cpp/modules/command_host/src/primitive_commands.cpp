#include <zima/command_host/host.hpp>
#include <zima/workspace/primitive_operations.hpp>
#include <charconv>

namespace zima::command_host {
namespace {
double dimension(const Json& args, const std::string& name) {
    const auto& text = args.at(name).get_ref<const std::string&>();
    double value{};
    const auto parsed = std::from_chars(text.data(), text.data()+text.size(), value);
    if(parsed.ec != std::errc{} || parsed.ptr != text.data()+text.size())
        throw workspace::PrimitiveOperationError("invalid_arguments", "Use a decimal number in mm with a decimal point.");
    return value;
}
const document::HistoryContainer& primitive(const workspace::PartState* state,
    const std::string& id, document::FeatureKind kind) {
    if(!state) throw workspace::PrimitiveOperationError("unsupported_document", "Primitive operations require an open Part.");
    const auto* container = state->session.document().find_container(id);
    if(!container) throw workspace::PrimitiveOperationError("container_not_found", "The requested container does not exist.");
    if(container->feature_kind != kind)
        throw workspace::PrimitiveOperationError("wrong_feature", "The requested primitive type does not match the container.");
    return *container;
}
Json parameters(const workspace::PartState& state, const document::HistoryContainer& container) {
    const auto& doc = state.session.document();
    const auto* owner = doc.body_owner_for_object(container.id);
    Json result = {{"document",doc.document_id},{"container",container.id},{"feature",container.feature_id},
        {"body",owner?owner->scope.id:std::string{}},{"name",container.name},
        {"value_locks",container.value_locks},{"revision",state.session.revision()}};
    for(const auto& [name,value] : workspace::primitive_dimensions(container)) result[name+"_mm"] = value;
    return result;
}
}
void Host::register_primitive_commands() {
    for(const auto& definition : workspace::primitive_definitions()) {
        const std::string prefix = definition.command_name;
        dispatcher_.add({prefix+".get",tr("Read primitive dimensions without calculating geometry."),
            {{"container",true},{"document",false}},false}, [this,kind=definition.kind](const Json& args) {
            try {
                const auto id = args.value("document",workspace_.active_document_id());
                const auto* state = workspace_.open_part(id);
                const auto& container = primitive(state,args.at("container").get<std::string>(),kind);
                return Result::success(parameters(*state,container));
            } catch(const workspace::PrimitiveOperationError& error) {return Result::failure(error.code,tr(error.what()));}
        });
        document::HistoryContainer parameter_sample;parameter_sample.feature_kind=definition.kind;
        std::vector<std::string> names;
        for(const auto& [name,value] : workspace::primitive_dimensions(parameter_sample)) names.push_back(name);
        for(const bool create : {true,false}) {
            std::vector<commands::Argument> arguments;
            if(!create) arguments.push_back({"container",true});
            for(const auto& name : names) arguments.push_back({name+"_mm",create});
            arguments.push_back({"document",false});
            dispatcher_.add({prefix+(create?".create":".set"),create
                ?tr("Create a primitive in the active Body; dimensions are in mm.")
                :tr("Change the specified primitive dimensions in mm."),std::move(arguments),true},
                [this,create,definition,names](const Json& args) {
                    const auto check = target(args);if(!check.ok) return check;
                    const auto id = workspace_.active_document_id();
                    try {
                        auto* state = workspace_.open_part(id);
                        if(!state || interaction().template_document)
                            throw workspace::PrimitiveOperationError("unsupported_document", "Primitive operations require an open Part.");
                        workspace::PrimitiveDimensionPatch patch;
                        for(const auto& name : names) if(args.contains(name+"_mm")) patch[name]=dimension(args,name+"_mm");
                        std::string container_id;
                        bool changed;
                        if(create) {
                            auto container = definition.create();container.name=tr(definition.display_name);
                            workspace::assign_primitive_dimensions(container,patch);container_id=container.id;
                            changed=workspace::commit_primitive(workspace_,kernel_,id,std::move(container),workspace::PrimitiveEditMode::Create);
                        } else {
                            container_id=args.at("container").get<std::string>();
                            changed=workspace::set_primitive_dimensions(workspace_,kernel_,id,container_id,definition.kind,patch);
                        }
                        auto data=parameters(*state,primitive(state,container_id,definition.kind));data["changed"]=changed;
                        if(changed) change_=Change{ChangeKind::Model,id};
                        return Result::success(std::move(data));
                    } catch(const workspace::PrimitiveOperationError& error) {return Result::failure(error.code,tr(error.what()));}
                });
        }
    }
}
} // namespace zima::command_host
