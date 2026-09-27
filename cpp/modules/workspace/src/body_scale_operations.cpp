#include <zima/workspace/body_scale_operations.hpp>
#include <zima/kernel/scale_geometry.hpp>
#include <zima/kernel/stable_id.hpp>

namespace zima::workspace {
BodyGraphEdit prepare_body_scale_edit(const document::PartDocument& document,const std::string& id) {
    BodyGraphEdit edit{document.document_id,id,document.body_history,document.body_history};
    if(id.empty()) {
        if(!document.body_history.active_body_id().empty())
            throw BodyOperationError("active_body","Finish Body editing before creating a scaled Body.");
        edit.object_id=kernel::make_stable_id();
    } else if(const auto* body=document.body_history.find(id);!body||!body->scale)
        throw BodyOperationError("body_not_found","The requested scaled Body does not exist.");
    return edit;
}
bool commit_body_scale(Workspace& workspace,const kernel::OcctKernel& kernel,
    const BodyGraphEdit& edit,document::BodyHistory value) {
    auto* state=workspace.open_part(edit.document_id);
    if(!state)throw BodyOperationError("unsupported_document","Body operations require an open Part.");
    const auto& document=state->session.document();
    if(document.body_history!=edit.original)
        throw BodyOperationError("body_history_changed","Body history changed while properties were open.");
    if(value.scope.id!=edit.object_id||!value.scale||!value.entries.empty()||value.derived_copy||
        value.cursor||value.scope.placement!=document::Placement{})
        throw BodyOperationError("body_identity_changed","A scaled Body contains only its source result and scale parameters.");
    kernel::validate_body_scale(value.scale->factor,value.scale->center);
    auto graph=document.body_history;
    if(const auto* original=graph.find(edit.object_id)) {
        if(!original->scale)throw BodyOperationError("body_not_found","The requested scaled Body does not exist.");
        // Properties never alter presence or visibility as a side effect.
        value.visible=original->visible;value.suppressed=original->suppressed;
        graph.update_body(std::move(value));
    } else static_cast<void>(graph.create_scale(std::move(value)));
    if(graph==document.body_history)return false;
    auto next=document;next.set_body_history(std::move(graph));
    PartCalculationPolicy policy;policy.reject_errors=true;
    auto calculated=calculate_part_with_resolved_references(kernel,next,&state->session.calculated_boundaries(),policy);
    calculated=calculate_part(kernel,next,&calculated,policy);
    state->session.commit(std::move(next),std::move(calculated));return true;
}
} // namespace zima::workspace
