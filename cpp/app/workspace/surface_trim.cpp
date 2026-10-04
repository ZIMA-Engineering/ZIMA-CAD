#include "workspace_internal.hpp"
#include "../surface_trim_dialog.hpp"
#include "../confirmed_face_hit.hpp"
#include <zima/document/boundary_surface.hpp>
#include <zima/workspace/surface_trim_operations.hpp>
namespace zima::app {
void AssemblyWorkspaceWindow::show_surface_trim_properties(const std::string& id) {
    if(properties_dialog_)return;
    auto* part=workspace_.open_part(workspace_.active_document_id());if(!part)return;
    try {
        const auto document_id=workspace_.active_document_id();const auto occurrence=resolve_active_occurrence(document_id);
        if(!occurrence)throw std::invalid_argument("Activate the exact Part occurrence first.");
        const auto* stored=part->session.document().find_container(id);auto feature=stored?*stored:document::create_surface_trim();
        if(!stored)feature.name=tr("Oříznout plochu").toStdString();
        const auto rollback=stored?part->session.rollback_boundary(id):std::optional<document::HistoryRollbackBoundary>{};
        if(stored&&!rollback)throw std::invalid_argument("Regenerate the Part before editing this feature.");
        auto* dialog=new SurfaceTrimDialog(feature,[this,document_id](auto pending){static_cast<void>(workspace::commit_surface_trim(workspace_,kernel_,document_id,std::move(pending)));},this);
        properties_dialog_=dialog;properties_dialog_instance_path_=*occurrence;track_tree_edit(dialog);
        if(rollback)part_rollback_=PartRollbackContext{document_id,*occurrence,rollback->history_index,rollback->input_body};
        tree_->setProperty("commandSelectionActive",true);
        dialog->label=[this,document_id,path=*occurrence](auto tool) {
            const auto* owner=workspace_.open_part(document_id)->session.document().find_container(tool.reference.owner_id);
            auto reference=tool.reference;reference.instance_path=path;
            QString name=QString::fromStdString(owner?owner->name:reference.owner_id);
            if(tool.face)return name+" / "+reference_display_label(kernel::FaceReference{reference.owner_id,reference.semantic_key,path},viewer_->mesh().original_references).value_or(tr("Plocha"));
            return name+" / "+reference_display_label(reference,viewer_->mesh().original_references).value_or(tr("Hrana"));
        };
        auto offered=std::make_shared<std::set<std::pair<std::string,std::string>>>();
        const auto resolve=[this,dialog,document_id,offered,path=*occurrence](const viewer::ViewerCandidate& candidate)->std::optional<kernel::SurfaceTrimTool> {
            if(candidate.instance_path!=path||candidate.geometry!=viewer::CandidateGeometry::OriginalReference)return {};
            const auto& doc=workspace_.open_part(document_id)->session.document();
            const bool face=candidate.kind==viewer::CandidateKind::Face;
            if(!face&&candidate.kind!=viewer::CandidateKind::Edge)return {};
            kernel::SurfaceTrimTool tool{{candidate.owner_id,candidate.semantic_key,{}},face};
            if(dialog->active_row()==0||dialog->active_row()==-2) {
                if(!face||!offered->contains({candidate.owner_id,candidate.semantic_key}))return {};
                if(dialog->active_row()==-2&&(candidate.owner_id!=dialog->pending.surface_trim.target.owner_id||candidate.semantic_key!=dialog->pending.surface_trim.target.semantic_key))return {};
            }
            if(face){if(!document::boundary_surface_support_allowed(doc,dialog->pending.id,{candidate.owner_id,candidate.semantic_key,{}}))return {};}
            else{document::BoundaryCurveSource source{candidate.owner_id,candidate.semantic_key};source.kind=document::BoundaryCurveSource::Kind::Edge;
                if(!document::boundary_surface_source_allowed(doc,dialog->pending.id,source))return {};}
            return tool;
        };
        const auto update=[this,dialog,resolve,path=*occurrence] {
            viewer_->set_original_container_selection(true);viewer_->set_original_face_selection(true);
            viewer_->set_selection_contract({viewer::CandidateKind::Face,viewer::CandidateKind::Edge});
            viewer_->set_candidate_filter([dialog,resolve](const auto& candidate){return dialog->active_row()!=-1&&resolve(candidate).has_value();},false);
            std::vector<viewer::ViewerCandidate> faces;std::set<viewer::EdgeKey> edges;
            for(int row=0;row<=static_cast<int>(dialog->pending.surface_trim.tools.size());++row)if(dialog->inspected(row)) {
                kernel::SurfaceTrimTool tool;
                if(row==0){const auto& r=dialog->pending.surface_trim.target;tool={{r.owner_id,r.semantic_key,{}},true};}
                else tool=dialog->pending.surface_trim.tools[row-1];if(!tool.reference.valid())continue;
                viewer::ViewerCandidate candidate;candidate.kind=tool.face?viewer::CandidateKind::Face:viewer::CandidateKind::Edge;
                candidate.geometry=viewer::CandidateGeometry::OriginalReference;candidate.owner_id=tool.reference.owner_id;
                candidate.semantic_key=tool.reference.semantic_key;candidate.instance_path=path;
                if(tool.face)faces.push_back(std::move(candidate));else edges.insert({tool.reference.owner_id,tool.reference.semantic_key,path});
            }
            viewer_->set_inspected_faces(std::move(faces));viewer_->set_constraint_reference_highlights({},std::move(edges));
        };
        feature_reference_pick_=[this,dialog,resolve,document_id,path=*occurrence](const auto& candidate) {
            const auto tool=resolve(candidate);if(!tool)return;
            if(dialog->active_row()==-2) {
                const auto ray=viewer_->ray_at(viewer_->last_pointer_position());if(!ray)return;
                auto hit=confirmed_face_hit(viewer_->candidate_face_triangles(candidate),ray->first,ray->second);if(!hit)return;
                const auto instance=assembly::InstancePath::decode(path);
                if(!instance.occurrence_ids.empty())*hit=workspace_.occurrence_point_from_scene(workspace_.displayed_document_id(),instance,*hit);
                const auto& doc=workspace_.open_part(document_id)->session.document();const auto* body=doc.body_owner_for_object(dialog->pending.id);
                if(!body)body=doc.body_history.find(doc.body_history.active_body_id());
                if(body)*hit=workspace_detail::container_dimension_frame(body->scope.placement).inverse_point(*hit);
                dialog->set_region(*hit);
            }else if(dialog->active_row()==0)dialog->set_target({tool->reference.owner_id,tool->reference.semantic_key,{}});
            else dialog->set_tool(*tool);
            viewer_->clear_selection();
        };
        feature_reference_end_=[dialog]{dialog->end_entry();};dialog->changed=update;
        connect(dialog,&QDialog::finished,this,[this,dialog] {
            dialog->changed={};feature_reference_pick_={};feature_reference_end_={};properties_dialog_=nullptr;part_rollback_.reset();properties_dialog_instance_path_.clear();
            viewer_->set_original_container_selection(false);viewer_->set_original_face_selection(false);
            viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->set_inspected_faces({});viewer_->set_constraint_reference_highlights({},{});viewer_->clear_selection();
            tree_->setProperty("commandSelectionActive",false);preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
        });
        preserve_view_on_refresh_=true;refresh_scene();
        for(const auto& face:viewer_->mesh().triangle_references)if(face.instance_path==*occurrence&&face.surface_result)offered->emplace(face.owner_id,face.semantic_key);
        dialog->refresh();dialog->show();update();
    }catch(const std::exception& error){state_->setText(tr(error.what()));}
}
}
