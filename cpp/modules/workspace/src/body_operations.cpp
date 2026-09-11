#include <zima/workspace/body_operations.hpp>
#include <zima/document/body_origin_attachment.hpp>
#include <algorithm>

namespace zima::workspace {
namespace {
PartState& part(Workspace& workspace,const std::string& id) {
    auto* state=workspace.open_part(id);
    if(!state)throw BodyOperationError("unsupported_document","Body operations require an open Part.");
    return *state;
}
PartState& checked(Workspace& workspace,const BodyGraphEdit& edit) {
    auto& state=part(workspace,edit.document_id);
    if(state.session.document().body_history!=edit.original)
        throw BodyOperationError("body_history_changed","Body history changed while properties were open.");
    return state;
}
bool calculate_and_commit(PartState& state,const kernel::OcctKernel& kernel,
    document::BodyHistoryGraph graph,bool exact_final_pass) {
    if(graph==state.session.document().body_history)return false;
    auto next=state.session.document();next.set_body_history(std::move(graph));
    PartCalculationPolicy policy;policy.reject_errors=true;
    auto calculated=calculate_part_with_resolved_references(kernel,next,&state.session.calculated_boundaries(),policy);
    // Matches Body Properties: preserve exact resolved inputs (including signed
    // zero) so the next Save can reuse the calculation's fingerprints.
    if(exact_final_pass)calculated=calculate_part(kernel,next,&calculated,policy);
    state.session.commit(std::move(next),std::move(calculated));return true;
}
}
BodyGraphEdit prepare_body_edit(const document::PartDocument& document,const std::string& id,std::string name) {
    BodyGraphEdit edit{document.document_id,id,document.body_history,document.body_history};
    if(id.empty()) {
        const bool first=edit.pending.bodies().empty();
        edit.object_id=document::create_origin_bound_body(edit.pending,document.document_id,std::move(name));
        if(first)for(const auto& entry:document.history_order)edit.pending.insert(entry);
    } else if(!edit.pending.find(id))throw BodyOperationError("body_not_found","The requested Body does not exist.");
    return edit;
}
BodyGraphEdit prepare_body_boolean_edit(const document::PartDocument& document,const std::string& id,
    std::string name,kernel::BodyCombination operation,std::string target,std::string tool) {
    BodyGraphEdit edit{document.document_id,id,document.body_history,document.body_history};
    if(id.empty())edit.object_id=edit.pending.create_boolean(std::move(name),operation,std::move(target),std::move(tool));
    else if(!edit.pending.find_boolean(id))throw BodyOperationError("boolean_not_found","The requested Body Boolean does not exist.");
    return edit;
}
bool commit_body_edit(Workspace& workspace,const kernel::OcctKernel& kernel,
    const BodyGraphEdit& edit,document::BodyHistory value,bool active) {
    auto& state=checked(workspace,edit);
    const auto* initial=edit.pending.find(edit.object_id);
    if(!initial || value.scope.id!=edit.object_id || value.entries!=initial->entries ||
        value.cursor!=initial->cursor || value.dependencies!=initial->dependencies || value.derived_copy!=initial->derived_copy)
        throw BodyOperationError("body_identity_changed","Body properties cannot replace identity, contents or dependencies.");
    auto graph=edit.pending;graph.update_body(std::move(value));
    if(active)graph.activate(edit.object_id);
    else if(graph.active_body_id()==edit.object_id)graph.activate({});
    return calculate_and_commit(state,kernel,std::move(graph),true);
}
bool commit_body_boolean_edit(Workspace& workspace,const kernel::OcctKernel& kernel,
    const BodyGraphEdit& edit,document::BodyBoolean value) {
    auto& state=checked(workspace,edit);
    if(!edit.pending.find_boolean(edit.object_id) || value.id!=edit.object_id)
        throw BodyOperationError("boolean_identity_changed","Boolean properties cannot replace its identity.");
    auto graph=edit.pending;graph.update_boolean(std::move(value));graph.activate({});
    return calculate_and_commit(state,kernel,std::move(graph),false);
}
bool activate_part_body(Workspace& workspace,const std::string& id,const std::string& body_id) {
    auto& state=part(workspace,id);
    if(state.session.document().body_history.active_body_id()==body_id)return false;
    auto next=state.session.document();next.body_history.activate(body_id);
    state.session.commit(std::move(next),state.session.calculated_boundaries());return true;
}
bool set_body_history_cursor(Workspace& workspace,const std::string& id,std::size_t index,const std::string& body_id) {
    auto& state=part(workspace,id);auto next=state.session.document();
    if(body_id.empty()) {
        if(next.body_history.insertion_cursor()==index)return false;
        next.body_history.set_insertion_cursor(index);
    } else {
        const auto* body=next.body_history.find(body_id);
        if(!body)throw BodyOperationError("body_not_found","The requested Body does not exist.");
        if(next.body_history.active_body_id()!=body_id)
            throw BodyOperationError("inactive_body","Activate the Body before changing its history cursor.");
        if(body->cursor==index)return false;
        next.body_history.set_history_cursor(body_id,index);
    }
    state.session.commit(std::move(next),state.session.calculated_boundaries());return true;
}
} // namespace zima::workspace
