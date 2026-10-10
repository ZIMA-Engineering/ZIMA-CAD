#include <zima/workspace/body_operations.hpp>
#include <zima/document/body_origin_attachment.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>

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
    auto phase_start=std::chrono::steady_clock::now();
    const auto phase=[&](const char* name) {
        const auto now=std::chrono::steady_clock::now();
        if(std::getenv("ZIMA_CPP_PROFILE_COMMAND"))std::fprintf(stderr,"Body commit %s: %.3f ms\n",name,
            std::chrono::duration<double,std::milli>(now-phase_start).count());
        phase_start=now;
    };
    auto calculated=calculate_part_with_resolved_references(kernel,next,&state.session.calculated_boundaries(),policy);
    phase("resolved calculation");
    // Matches Body Properties: preserve exact resolved inputs (including signed
    // zero) so the next Save can reuse the calculation's fingerprints.
    if(exact_final_pass) {
        const auto operations=next.kernel_operations();
        // Reference settling already proves the final calculated fingerprint.
        // Avoid copying the complete cached packet through another kernel call
        // when the exact resolved inputs match. Group solid-state calculations
        // retain their established explicit final preparation path.
        const bool prepare_group=std::ranges::any_of(operations,[](const auto& operation) {
            return std::holds_alternative<kernel::FeatureGroupRequest>(operation.primitive);
        });
        if(prepare_group||calculated.empty()||calculated.back().source_fingerprint!=
                kernel::history_fingerprint(operations,operations.size()))
            calculated=calculate_part(kernel,next,&calculated,policy);
    }
    phase("exact fingerprint calculation");
    state.session.commit(std::move(next),std::move(calculated));phase("transaction");return true;
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
        value.cursor!=initial->cursor || value.dependencies!=initial->dependencies || value.derived_copy!=initial->derived_copy || value.scale!=initial->scale || value.link!=initial->link)
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
bool set_part_body_visibility(Workspace& workspace,const std::string& id,const std::string& body_id,bool visible) {
    auto& state=part(workspace,id);
    const auto* body=state.session.document().body_history.find(body_id);
    if(!body)throw BodyOperationError("body_not_found","The requested Body does not exist.");
    if(body->visible==visible)return false;
    auto next=state.session.document();auto value=*body;value.visible=visible;
    next.body_history.update_body(std::move(value));
    state.session.commit(std::move(next),state.session.calculated_boundaries());return true;
}
bool activate_part_body(Workspace& workspace,const std::string& id,const std::string& body_id) {
    auto& state=part(workspace,id);
    if(state.session.document().body_history.active_body_id()==body_id)return false;
    auto next=state.session.document();next.body_history.activate(body_id);
    state.session.commit(std::move(next),state.session.calculated_boundaries());return true;
}
bool set_part_body_suppressed(Workspace& workspace,const kernel::OcctKernel& kernel,
    const std::string& id,const std::string& body_id,bool suppressed) {
    auto& state=part(workspace,id);auto graph=state.session.document().body_history;
    if(suppressed&&graph.active_body_id()==body_id)
        throw BodyOperationError("active_body","An active Body cannot be suppressed.");
    if(const auto* body=graph.find(body_id)) {
        if(body->suppressed==suppressed)return false;
        auto value=*body;value.suppressed=suppressed;graph.update_body(std::move(value));
    } else if(const auto* operation=graph.find_boolean(body_id)) {
        if(operation->suppressed==suppressed)return false;
        auto value=*operation;value.suppressed=suppressed;graph.update_boolean(std::move(value));
    } else throw BodyOperationError("body_not_found","The requested Body does not exist.");
    return calculate_and_commit(state,kernel,std::move(graph),false);
}
bool set_body_history_cursor(Workspace& workspace,const std::string& id,std::size_t index,const std::string& body_id) {
    auto& state=part(workspace,id);auto next=state.session.document();
    if(body_id.empty()) {
        if(next.body_history.insertion_cursor()==index && next.body_history.active_body_id().empty())return false;
        next.body_history.set_insertion_cursor(index);
        next.body_history.activate({});
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
