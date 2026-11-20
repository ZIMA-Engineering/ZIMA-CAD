#include <zima/workspace/fem_operations.hpp>
#include <zima/kernel/stable_id.hpp>
#include <algorithm>

namespace zima::workspace {
namespace {
bool material_source(const document::BodyHistoryGraph& graph,const std::string& id) {
    if(const auto* body=graph.find(id))return !body->fem&&!body->suppressed;
    if(const auto* operation=graph.find_boolean(id))return !operation->suppressed;
    return false;
}
const kernel::BodySnapshot* source_snapshot(const document::DocumentSession& session,const std::string& id) {
    if(!material_source(session.document().body_history,id)||session.calculated_boundaries().empty())return nullptr;
    const auto& outputs=session.calculated_boundaries().back().body_outputs;
    const auto found=outputs.find(id);
    if(found==outputs.end())return nullptr;
    const auto& result=found->second.get();
    if(!result.solid_present||result.kernel_shape.empty()||result.source_fingerprint.empty()||
            !result.calculation_errors.empty())return nullptr;
    return &found->second;
}
void validate_edit(const document::DocumentSession& session,const BodyGraphEdit& edit) {
    if(session.document().document_id!=edit.document_id||session.document().body_history!=edit.original)
        throw BodyOperationError("body_history_changed","Body history changed while properties were open.");
}
}

BodyGraphEdit prepare_fem_edit(const document::PartDocument& document,const std::string& id) {
    if(!id.empty()&&(!document.body_history.find(id)||!document.body_history.find(id)->fem))
        throw std::invalid_argument("The requested FEM Body does not exist.");
    return {document.document_id,id.empty()?kernel::make_stable_id():id,document.body_history,document.body_history};
}

std::vector<std::string> fem_source_candidates(const document::DocumentSession& session,const BodyGraphEdit& edit) {
    validate_edit(session,edit);
    const auto& graph=session.document().body_history;
    const auto own=std::ranges::find(graph.order(),edit.object_id);
    const auto boundary=own==graph.order().end()?graph.insertion_cursor():
        static_cast<std::size_t>(own-graph.order().begin());
    std::vector<std::string> result;
    for(std::size_t i=0;i<boundary;++i)
        if(source_snapshot(session,graph.order()[i]))result.push_back(graph.order()[i]);
    return result;
}

kernel::BodySnapshot read_fem_source(const document::DocumentSession& session,const std::string& id) {
    const auto& graph=session.document().body_history;
    if(graph.fem_source_state(id)!=document::FemSourceState::Available)
        throw std::invalid_argument("The FEM source is missing or suppressed.");
    const auto* result=source_snapshot(session,graph.find(id)->fem->source_id);
    if(!result)throw std::invalid_argument("FEM requires a valid calculated solid. Regenerate the source first.");
    return *result;
}

bool commit_fem(Workspace& live,const BodyGraphEdit& edit,document::BodyHistory value) {
    auto* state=live.open_part(edit.document_id);
    if(!state)throw BodyOperationError("body_history_changed","Body history changed while properties were open.");
    auto& session=state->session;validate_edit(session,edit);
    if(value.scope.id!=edit.object_id||!value.fem)
        throw std::invalid_argument("The requested FEM Body does not exist.");
    const auto* initial=session.document().body_history.find(edit.object_id);
    if(initial&&!initial->fem)throw std::invalid_argument("Cannot change the kind of an existing Body.");
    if(!initial||initial->fem->source_id!=value.fem->source_id) {
        const auto candidates=fem_source_candidates(session,edit);
        if(std::ranges::find(candidates,value.fem->source_id)==candidates.end())
            throw std::invalid_argument("FEM requires a valid calculated solid. Regenerate the source first.");
    }
    auto next=session.document();auto graph=next.body_history;
    if(initial)graph.update_body(std::move(value));
    else static_cast<void>(graph.create_fem(std::move(value)));
    if(graph==next.body_history)return false;
    next.set_body_history(std::move(graph));
    session.commit_metadata(std::move(next));
    return true;
}
} // namespace zima::workspace
