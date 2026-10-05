#include "workspace_internal.hpp"
#include "../surface_thicken_dialog.hpp"
#include <zima/document/boundary_surface.hpp>
#include <zima/workspace/surface_thicken_operations.hpp>
namespace zima::app {
void AssemblyWorkspaceWindow::show_surface_thicken_properties(const std::string& id) {
    if(properties_dialog_)return;
    auto* part=workspace_.open_part(workspace_.active_document_id());if(!part)return;
    try {
        const auto document_id=workspace_.active_document_id();const auto occurrence=resolve_active_occurrence(document_id);
        if(!occurrence)throw std::invalid_argument("Activate the exact Part occurrence first.");
        const auto* stored=part->session.document().find_container(id);auto feature=stored?*stored:document::create_surface_thicken();
        if(!stored)feature.name=tr("Zesílit plochu").toStdString();
        const auto rollback=stored?part->session.rollback_boundary(id):std::optional<document::HistoryRollbackBoundary>{};
        if(stored&&!rollback)throw std::invalid_argument("Regenerate the Part before editing this feature.");
        auto* dialog=new SurfaceThickenDialog(feature,[this,document_id](auto pending){
            static_cast<void>(workspace::commit_surface_thicken(workspace_,kernel_,document_id,std::move(pending)));
        },this);
        properties_dialog_=dialog;properties_dialog_instance_path_=*occurrence;track_tree_edit(dialog);
        if(rollback)part_rollback_=PartRollbackContext{document_id,*occurrence,rollback->history_index,rollback->input_body};
        tree_->setProperty("commandSelectionActive",true);
        dialog->label=[this,document_id,path=*occurrence](auto reference) {
            const auto* owner=workspace_.open_part(document_id)->session.document().find_container(reference.owner_id);
            reference.instance_path=path;
            return QString::fromStdString(owner?owner->name:reference.owner_id)+" / "+
                reference_display_label(reference,viewer_->mesh().original_references).value_or(tr("Plocha"));
        };
        auto offered=std::make_shared<std::set<std::pair<std::string,std::string>>>();
        const auto resolve=[this,dialog,document_id,offered,path=*occurrence](const viewer::ViewerCandidate& candidate)->std::optional<kernel::FaceReference> {
            if(candidate.instance_path!=path||candidate.kind!=viewer::CandidateKind::Face||candidate.geometry!=viewer::CandidateGeometry::OriginalReference)return {};
            kernel::FaceReference reference{candidate.owner_id,candidate.semantic_key,{}};
            const auto& doc=workspace_.open_part(document_id)->session.document();
            if(!document::boundary_surface_support_allowed(doc,dialog->pending.id,reference))return {};
            if(!offered->contains({reference.owner_id,reference.semantic_key}))return {};
            return reference;
        };
        const auto update=[this,dialog,resolve,path=*occurrence] {
            viewer_->set_original_container_selection(true);viewer_->set_original_face_selection(true);
            viewer_->set_selection_contract({viewer::CandidateKind::Face});
            viewer_->set_candidate_filter([dialog,resolve](const auto& candidate){return dialog->active_row()>=0&&resolve(candidate).has_value();},false);
            std::vector<viewer::ViewerCandidate> faces;
            if(dialog->inspected()) {
                const auto& reference=dialog->pending.surface_thicken.face;viewer::ViewerCandidate candidate;
                candidate.kind=viewer::CandidateKind::Face;candidate.geometry=viewer::CandidateGeometry::OriginalReference;
                candidate.owner_id=reference.owner_id;candidate.semantic_key=reference.semantic_key;candidate.instance_path=path;faces.push_back(std::move(candidate));
            }
            viewer_->set_inspected_faces(std::move(faces));
        };
        feature_reference_pick_=[this,dialog,resolve](const auto& candidate) {const auto reference=resolve(candidate);if(!reference)return;dialog->set_face(*reference);viewer_->clear_selection();};
        feature_reference_end_=[dialog]{dialog->end_entry();};dialog->changed=update;
        connect(dialog,&QDialog::finished,this,[this,dialog] {
            dialog->changed={};feature_reference_pick_={};feature_reference_end_={};properties_dialog_=nullptr;
            part_rollback_.reset();properties_dialog_instance_path_.clear();viewer_->set_original_container_selection(false);
            viewer_->set_original_face_selection(false);viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->set_inspected_faces({});viewer_->clear_selection();
            tree_->setProperty("commandSelectionActive",false);preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
        });
        preserve_view_on_refresh_=true;refresh_scene();
        for(const auto& face:viewer_->mesh().triangle_references)if(face.instance_path==*occurrence&&face.surface_result)
            offered->emplace(face.owner_id,face.semantic_key);
        dialog->refresh();dialog->show();update();
    }catch(const std::exception& error){state_->setText(tr(error.what()));}
}
}
