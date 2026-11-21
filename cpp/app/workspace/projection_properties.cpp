#include "workspace_internal.hpp"
#include "../projection_parameter_panel.hpp"
#include "../confirmed_face_hit.hpp"
#include <zima/document/boundary_surface.hpp>
#include <zima/assembly/assembly_document.hpp>

namespace zima::app {
void AssemblyWorkspaceWindow::install_projection_properties(PrimitivePropertiesDialog* dialog,const std::string& document_id) {
    auto* panel=dialog->projection_panel();if(!panel)return;
    const auto occurrence=resolve_active_occurrence(document_id);if(!occurrence)return;
    const auto path=*occurrence;
    panel->face_label=[this,document_id,path](auto reference) {
        if(reference.instance_path.empty())reference.instance_path=path;
        const auto* feature=workspace_.open_part(document_id)->session.document().find_container(reference.owner_id);
        return QString::fromStdString(feature?feature->name:reference.owner_id)+" / "+
            reference_display_label(reference,viewer_->mesh().original_references).value_or(tr("Plocha"));
    };
    const auto finish=[this,panel] {
        panel->finish_entry();feature_reference_pick_={};feature_reference_end_={};
        viewer_->set_original_container_selection(false);viewer_->set_original_face_selection(false);
        viewer_->set_candidate_filter({});viewer_->clear_selection();set_primitive_properties_dimension_selection();
    };
    const auto allowed=[this,dialog,panel,document_id,path](const viewer::ViewerCandidate& candidate) {
        using Pick=ProjectionParameterPanel::Pick;
        const auto& doc=workspace_.open_part(document_id)->session.document();const auto pending=dialog->pending_value();
        if(pending.feature.type!=document::FeatureType::Projection||candidate.owner_id==pending.id)return false;
        if(panel->active_pick()!=Pick::Target&&panel->active_pick()!=Pick::Region)return false;
        if(candidate.kind!=viewer::CandidateKind::Face||candidate.geometry!=viewer::CandidateGeometry::Display)return false;
        kernel::FaceReference reference{candidate.owner_id,candidate.semantic_key,candidate.instance_path==path?std::string{}:candidate.instance_path};
        if(panel->active_pick()==Pick::Region)return std::ranges::any_of(panel->parameters().targets,[&](const auto& value){return value.reference==reference;});
        if(candidate.instance_path==path)return document::boundary_surface_support_allowed(doc,pending.id,reference);
        if(panel->parameters().output==document::ProjectionOutput::Split)return false;
        const auto file=workspace_.occurrence_source_file(workspace_.displayed_document_id(),assembly::InstancePath::decode(candidate.instance_path));
        return file&&assembly::is_skeleton_file(*file);
    };
    const auto inspect=[this,panel,path] {
        std::vector<viewer::ViewerCandidate> faces;
        for(auto reference:panel->inspected_faces()) {
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Face;candidate.geometry=viewer::CandidateGeometry::Display;
            candidate.owner_id=reference.owner_id;candidate.semantic_key=reference.semantic_key;
            candidate.instance_path=reference.instance_path.empty()?path:reference.instance_path;faces.push_back(std::move(candidate));
        }
        viewer_->set_inspected_faces(std::move(faces));
        viewer_->set_constraint_reference_highlights({},{});
    };
    panel->inspection_changed=inspect;
    panel->pick_requested=[this,dialog,panel,document_id,path,allowed,finish](auto kind) {
        if(kind==ProjectionParameterPanel::Pick::None){finish();return;}
        if(extrusion_target_dialog_)finish_extrusion_target_selection(false);
        pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
        dialog->set_active_reference_index(std::nullopt);viewer_->clear_selection();tree_->setProperty("commandSelectionActive",true);
        viewer_->set_original_container_selection(false);viewer_->set_original_face_selection(false);
        viewer_->set_selection_contract({viewer::CandidateKind::Face});
        viewer_->set_candidate_filter(allowed,false);
        feature_reference_end_=finish;
        feature_reference_pick_=[this,dialog,panel,document_id,path,allowed,finish](const auto& candidate) {
            if(!allowed(candidate))return;
            try {
                const auto& doc=workspace_.open_part(document_id)->session.document();const auto pending=dialog->pending_value();
                const bool external=candidate.instance_path!=path;
                kernel::FaceReference reference{candidate.owner_id,candidate.semantic_key,external?candidate.instance_path:std::string{}};
                const auto* body=doc.body_owner_for_object(pending.id);if(!body)body=doc.body_history.find(doc.body_history.active_body_id());
                const auto to_local=[&](kernel::Vec3 point) {
                    const auto instance=assembly::InstancePath::decode(path);
                    if(!instance.occurrence_ids.empty())point=workspace_.occurrence_point_from_scene(workspace_.displayed_document_id(),instance,point);
                    return body?workspace_detail::container_dimension_frame(body->scope.placement).inverse_point(point):point;
                };
                if(panel->active_pick()==ProjectionParameterPanel::Pick::Region) {
                    const auto ray=viewer_->ray_at(viewer_->last_pointer_position());if(!ray)return;
                    const auto hit=confirmed_face_hit(viewer_->candidate_face_triangles(candidate),ray->first,ray->second);if(!hit)return;
                    auto seed=to_local(*hit);
                    if(!external)if(const auto* source=doc.find_container(reference.owner_id))seed=workspace_detail::container_dimension_frame(source->placement).inverse_point(seed);
                    if(panel->set_region(reference,seed))finish();return;
                }
                document::ExtrusionParameters::EndTarget target;target.reference=reference;target.kind=document::EndTargetKind::Face;
                target.label=panel->face_label(reference).toStdString();
                if(external)target=workspace::prepare_skeleton_end_target(workspace_,doc,body?body->scope.id:pending.id,target,nullptr,true);
                else {
                    // The selected current bounded triangles are preview data;
                    // OK resolves this exact native face at the input boundary.
                    for(const auto& point:viewer_->candidate_face_triangles(candidate))
                        target.fallback_triangles.push_back(to_local(point));
                }
                panel->set_target(std::move(target));viewer_->clear_selection();
            }catch(const std::exception& error){state_->setText(tr(error.what()));}
        };
    };
    const auto changed=panel->changed;
    panel->changed=[panel,changed,inspect] {
        if(changed)changed();inspect();
        if(panel->pick_requested)panel->pick_requested(panel->active_pick());
    };
    panel->refresh();
    connect(dialog->findChild<QComboBox*>("featureType"),&QComboBox::currentIndexChanged,dialog,
        [panel,finish](int type){if(type!=static_cast<int>(document::FeatureType::Projection)&&panel->active_pick()!=ProjectionParameterPanel::Pick::None)finish();});
    connect(dialog,&QObject::destroyed,this,[this] {
        feature_reference_pick_={};feature_reference_end_={};viewer_->set_inspected_faces({});viewer_->set_constraint_reference_highlights({},{});
    });
}
}
