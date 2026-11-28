#include <zima/workspace/mech_groove_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/workspace/modeling_confirmation.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/document/boundary_surface.hpp>
#include <algorithm>

namespace zima::workspace {
namespace {
kernel::ViewerReferenceGeometry with_datums(const document::PartDocument& doc,
        const std::string& feature,kernel::ViewerReferenceGeometry input,
        bool input_is_body_local=true) {
    auto datums=doc.origin_viewer_mesh().original_references;
    document::history_reference_detail::append(datums,doc.history_origin_reference_geometry_before(feature));
    document::history_reference_detail::append(datums,doc.construction_viewer_mesh().original_references);
    if(!input_is_body_local) {
        // Regeneration receives the document reference universe, whereas a
        // creation/edit rollback boundary is already in Body coordinates.
        document::history_reference_detail::append(input,datums);
        return doc.construction_reference_geometry_for(feature,std::move(input));
    }
    datums=doc.construction_reference_geometry_for(feature,std::move(datums));
    document::history_reference_detail::append(input,datums);return input;
}
void validate_sources(const document::PartDocument& doc,const document::HistoryContainer& value) {
    auto origins=doc.origin_viewer_mesh().original_references;
    document::history_reference_detail::append(origins,doc.body_origin_reference_geometry());
    document::history_reference_detail::append(origins,doc.history_origin_reference_geometry_before(value.id));
    for(const auto& ref:value.placement.references)if(!ref.owner_id.empty()) {
        // Material/construction references obey history order; document, Body
        // and preceding container Origins are ordinary permitted datum inputs.
        const auto same=[&](const auto& r){return r.owner_id==ref.owner_id&&r.semantic_key==ref.semantic_key&&r.instance_path==ref.instance_path;};
        const bool datum=std::ranges::any_of(origins.triangle_references,same)||
            std::ranges::any_of(origins.axes,[&](const auto& a){return same(a.reference);})||
            std::ranges::any_of(origins.points,[&](const auto& p){return same(p.reference);});
        if(!ref.instance_path.empty()||(!datum&&!document::boundary_surface_owner_allowed(doc,value.id,ref.owner_id)))
            throw std::invalid_argument("Groove references must precede the groove in the active Body");
    }
}
}

bool refresh_mech_grooves(document::PartDocument& doc,
        const kernel::ViewerReferenceGeometry& refs,const document::HistoryReferenceViews& views) {
    bool changed=false;
    const document::HistoryReferenceResolver resolver(refs,views);
    for(auto& feature:doc.history) {
        if(feature.feature_kind!=document::FeatureKind::MechGroove||feature.suppressed)continue;
        const auto* body=doc.body_owner_for_object(feature.id);if(!body||body->suppressed)continue;
        const auto row=std::ranges::find(body->entries,feature.id,&document::PartHistoryEntry::id);
        if(row==body->entries.end()||static_cast<std::size_t>(row-body->entries.begin())>=body->cursor)continue;
        try {
            auto local=refs;resolver.enter(feature.id,local);
            local=with_datums(doc,feature.id,std::move(local),false);
            validate_sources(doc,feature);
            auto resolved=document::resolve_mech_groove_draft(feature,local);
            if(!document::boundary_surface_support_allowed(doc,feature.id,resolved.mech_groove.support))
                throw std::invalid_argument("Groove references must precede the groove in the active Body");
            if(resolved!=feature){feature=std::move(resolved);changed=true;}
            changed|=doc.reference_errors.erase(feature.id)>0;
        } catch(const std::exception& error) {
            // Retain the exact authored section and its last valid frame.
            // The ordinary failed-input path prevents new material calculation.
            auto& message=doc.reference_errors[feature.id];
            if(message!=error.what()){message=error.what();changed=true;}
        }
    }
    return changed;
}

bool commit_mech_groove(Workspace& live,const kernel::OcctKernel& kernel,
        const std::string& id,document::HistoryContainer value) {
    auto* state=live.open_part(id);if(!state)throw std::invalid_argument("Groove requires an editable Part Body");
    const auto& before=state->session.document();const auto* existing=before.find_container(value.id);
    if(existing&&*existing==value)return false;
    const auto* body=existing?before.body_owner_for_object(value.id):before.body_history.find(before.body_history.active_body_id());
    if(!body||body->derived_copy||body->suppressed||body->scope.id!=before.body_history.active_body_id())
        throw std::invalid_argument("Groove requires an editable Part Body");
    const auto rollback=existing?state->session.rollback_boundary(value.id):std::nullopt;
    const auto* input=existing?(rollback&&rollback->input_body?&*rollback->input_body:nullptr):
        calculated_operation_input(state->session,{});
    if(!input)throw std::invalid_argument("Groove requires calculated input material");
    auto next=before;
    value.combine_mode=document::CombineMode::Subtract;
    // Install the pending identity in the private document before checking
    // history ownership. An unresolved definition may have no section yet.
    if(existing)*next.find_container(value.id)=value;
    else {next.history.push_back(value);next.insert_history_entry(document::PartHistoryKind::Feature,value.id);}
    try {
        validate_sources(next,value);
        auto resolved=document::resolve_mech_groove_draft(value,
            with_datums(next,value.id,input->mesh.original_references));
        if(!document::boundary_surface_support_allowed(next,value.id,resolved.mech_groove.support))
            throw std::invalid_argument("Groove references must precede the groove in the active Body");
        *next.find_container(value.id)=std::move(resolved);
        next.reference_errors.erase(value.id);
    } catch(const std::invalid_argument& error) {
        const auto* attempt=ModelingConfirmationAttempt::current();
        if(!attempt||!attempt->applies(before))throw;
        document::validate_mech_groove(value.mech_groove);
        next.reference_errors[value.id]=error.what();
    }
    PartCalculationPolicy policy;policy.reject_errors=true;
    auto calculated=calculate_part_with_resolved_references(kernel,next,&state->session.calculated_boundaries(),policy);
    commit_part_document(live,id,std::move(next),std::move(calculated));return true;
}
}
