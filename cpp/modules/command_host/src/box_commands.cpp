#include <zima/command_host/host.hpp>
#include <zima/workspace/box_operations.hpp>
#include <charconv>

namespace zima::command_host {
namespace {
double dimension(const Json& args, const char* name) {
    const auto& text = args.at(name).get_ref<const std::string&>();
    double value{};
    const auto parsed = std::from_chars(text.data(), text.data()+text.size(), value);
    if(parsed.ec != std::errc{} || parsed.ptr != text.data()+text.size())
        throw workspace::BoxOperationError("invalid_arguments", "Use a decimal number in mm with a decimal point.");
    return value;
}
const document::HistoryContainer& box(const workspace::PartState* state, const std::string& id) {
    if(!state) throw workspace::BoxOperationError("unsupported_document", "Box operations require an open Part.");
    const auto* container = state->session.document().find_container(id);
    if(!container) throw workspace::BoxOperationError("container_not_found", "The requested container does not exist.");
    if(container->feature_kind != document::FeatureKind::Box)
        throw workspace::BoxOperationError("wrong_feature", "The selected container is not a Box.");
    return *container;
}
Json parameters(const workspace::PartState& state, const document::HistoryContainer& container) {
    const auto& doc = state.session.document();
    const auto* owner = doc.body_owner_for_object(container.id);
    return {{"document",doc.document_id},{"container",container.id},{"feature",container.feature_id},
        {"body",owner?owner->scope.id:std::string{}},{"name",container.name},
        {"length_mm",container.box.length},{"width_mm",container.box.width},{"height_mm",container.box.height},
        {"value_locks",container.value_locks},{"revision",state.session.revision()}};
}
}
void Host::register_box_commands() {
    dispatcher_.add({"box.get",tr("Read Box dimensions without calculating geometry."),
        {{"container",true},{"document",false}},false}, [this](const Json& args) {
        try {
            const auto id = args.value("document",workspace_.active_document_id());
            const auto* state = workspace_.open_part(id);
            const auto& container = box(state,args.at("container").get<std::string>());
            return Result::success(parameters(*state,container));
        } catch(const workspace::BoxOperationError& error) {return Result::failure(error.code,tr(error.what()));}
    });
    for(const bool create : {true,false}) {
        std::vector<commands::Argument> arguments;
        if(!create) arguments.push_back({"container",true});
        for(const auto* key : {"length_mm","width_mm","height_mm"}) arguments.push_back({key,create});
        arguments.push_back({"document",false});
        dispatcher_.add({create?"box.create":"box.set",create
            ?tr("Create a Box in the active Body: length, width, height in mm.")
            :tr("Change the specified Box dimensions in mm."),std::move(arguments),true},
            [this,create](const Json& args) {
                const auto check = target(args); if(!check.ok) return check;
                const auto id = workspace_.active_document_id();
                try {
                    auto* state = workspace_.open_part(id);
                    if(!state || interaction().template_document)
                        throw workspace::BoxOperationError("unsupported_document", "Box operations require an open Part.");
                    workspace::BoxDimensionPatch patch;
                    if(args.contains("length_mm")) patch.length = dimension(args,"length_mm");
                    if(args.contains("width_mm")) patch.width = dimension(args,"width_mm");
                    if(args.contains("height_mm")) patch.height = dimension(args,"height_mm");
                    std::string container_id;
                    bool changed;
                    if(create) {
                        auto container = document::PartDocument::create_box_container();
                        container.name = tr("Box");
                        container.box = {*patch.length,*patch.width,*patch.height};
                        container_id = container.id;
                        changed = workspace::commit_box(workspace_,kernel_,id,std::move(container),workspace::BoxEditMode::Create);
                    } else {
                        container_id = args.at("container").get<std::string>();
                        changed = workspace::set_box_dimensions(workspace_,kernel_,id,container_id,patch);
                    }
                    auto data = parameters(*state,box(state,container_id));
                    data["changed"] = changed;
                    if(changed) change_ = Change{ChangeKind::Model,id};
                    return Result::success(std::move(data));
                } catch(const workspace::BoxOperationError& error) {return Result::failure(error.code,tr(error.what()));}
            });
    }
}
} // namespace zima::command_host
