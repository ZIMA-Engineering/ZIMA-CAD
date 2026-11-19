#include <zima/workspace/modeling_confirmation.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>

namespace zima::workspace {
namespace {
thread_local ModelingConfirmationAttempt* active_attempt{};

nlohmann::json source_generations(const Workspace& workspace) {
    auto result=nlohmann::json::array();
    for(const auto& state:workspace.documents())std::visit([&](const auto& current) {
        if constexpr(requires { current.session; })
            result.push_back({current.session.document().document_id,
                current.session.revision(),current.session.data_generation(),
                reinterpret_cast<std::uintptr_t>(current.runtime_identity.get())});
    },state);
    result.push_back(workspace.active_occurrence_path());
    return result;
}

std::set<std::string> changed_owners(const document::PartDocument& before,
        const document::PartDocument& candidate) {
    std::set<std::string> owners;
    for(const auto& feature:candidate.history) {
        const auto* old=before.find_container(feature.id);
        if(!old||*old!=feature)owners.insert(feature.id);
    }
    for(const auto& sketch:candidate.sketches) {
        const auto old=std::ranges::find(before.sketches,sketch.id,&sketcher::Sketch::id);
        if(old==before.sketches.end()||old->serialized()!=sketch.serialized())
            owners.insert(sketch.owner_container_id);
    }
    return owners;
}
}

struct ModelingConfirmation::Pending {
    nlohmann::json input;
    nlohmann::json sources;
    document::PartDocument result;
    std::vector<kernel::BodyResult> calculated;
    std::set<std::string> failed_owners;
    std::string message;
};
ModelingConfirmation::ModelingConfirmation()=default;
ModelingConfirmation::~ModelingConfirmation()=default;
void ModelingConfirmation::clear() { pending_.reset(); }
const std::string& ModelingConfirmation::failure() const {
    static const std::string empty;
    return pending_?pending_->message:empty;
}
ModelingConfirmationAttempt::ModelingConfirmationAttempt(ModelingConfirmation& confirmation,
        const Workspace& workspace,const std::string& id)
    :confirmation_(confirmation),workspace_(workspace),document_id_(id),previous_(active_attempt) {
    active_attempt=this;
}
ModelingConfirmationAttempt::~ModelingConfirmationAttempt() { active_attempt=previous_; }
ModelingConfirmationAttempt* ModelingConfirmationAttempt::current() { return active_attempt; }
bool ModelingConfirmationAttempt::applies(const document::PartDocument& part) const {
    return part.document_id==document_id_&&workspace_.open_part(document_id_);
}
bool ModelingConfirmationAttempt::accepted(const std::string& owner) const {
    return owner.empty()?!accepted_owners_.empty():accepted_owners_.contains(owner);
}
bool confirmed_unresolved_modeling_definition(const std::string& owner) {
    return active_attempt&&active_attempt->accepted(owner);
}
std::vector<kernel::BodyResult> ModelingConfirmationAttempt::calculate(
        document::PartDocument& candidate,const PartCalculationPolicy& policy,
        const Calculation& calculate) {
    // Nested settling and worker-thread calculations must not acknowledge an
    // intermediate result. The owner-thread outer call owns the attempt.
    struct Restore {
        ModelingConfirmationAttempt* previous=active_attempt;
        Restore(){active_attempt=nullptr;}
        ~Restore(){active_attempt=previous;}
    } restore;
    auto input=candidate.serialized();
    auto sources=source_generations(workspace_);
    auto& pending=confirmation_.pending_;
    if(pending&&pending->input==input&&pending->sources==sources) {
        candidate=pending->result;
        accepted_owners_=pending->failed_owners;
        return pending->calculated;
    }
    confirmation_.clear();
    const auto* state=workspace_.open_part(document_id_);
    const auto changed=changed_owners(state->session.document(),candidate);
    auto recover=policy;recover.reject_errors=false;
    auto calculated=calculate(candidate,recover);
    if(calculated.empty())return calculated;
    std::set<std::string> failures;std::string message;
    for(const auto& [owner,error]:calculated.back().calculation_errors) {
        if(!changed.contains(owner))continue;
        failures.insert(owner);if(message.empty())message=error;
    }
    if(failures.empty())return calculated;
    pending=std::make_unique<ModelingConfirmation::Pending>(ModelingConfirmation::Pending{
        std::move(input),std::move(sources),candidate,std::move(calculated),std::move(failures),message});
    failed_=true;
    throw std::runtime_error(message);
}
} // namespace zima::workspace
