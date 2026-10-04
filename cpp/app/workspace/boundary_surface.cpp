#include "workspace_internal.hpp"
#include "../boundary_surface_dialog.hpp"
#include <zima/workspace/boundary_surface_operations.hpp>
namespace zima::app {
void AssemblyWorkspaceWindow::show_boundary_surface_properties(const std::string& id) {
    if(properties_dialog_)return;
    auto* part=workspace_.open_part(workspace_.active_document_id());if(!part)return;
    try {
        const auto document_id=workspace_.active_document_id();
        const auto occurrence=resolve_active_occurrence(document_id);
        if(!occurrence)throw std::invalid_argument("Activate the exact Part occurrence first.");
        const auto* stored=part->session.document().find_container(id);
        auto feature=stored?*stored:document::create_boundary_surface();
        if(!stored)feature.name=tr("Zaplnit plochu").toStdString();
        const auto rollback=stored?part->session.rollback_boundary(id):std::optional<document::HistoryRollbackBoundary>{};
        if(stored&&!rollback)throw std::invalid_argument("Regenerate the Part before editing this feature.");
        auto* dialog=new BoundarySurfaceDialog(feature,[this,document_id](auto pending){
            static_cast<void>(workspace::commit_boundary_surface(workspace_,kernel_,document_id,std::move(pending)));
        },this);
        properties_dialog_=dialog;properties_dialog_instance_path_=*occurrence;track_tree_edit(dialog);
        if(rollback)part_rollback_=PartRollbackContext{document_id,*occurrence,rollback->history_index,rollback->input_body};
        tree_->setProperty("commandSelectionActive",true);
        dialog->label=[this,document_id,path=*occurrence](const auto& source){
            const auto& doc=workspace_.open_part(document_id)->session.document();
            const auto* container=doc.find_container(source.owner_id);
            const auto* curve=doc.find_construction(source.owner_id);
            auto name=QString::fromStdString(container?container->name:curve?curve->name:source.owner_id);
            if(source.kind==document::BoundaryCurveSource::Kind::Edge)
                return name+" / "+reference_display_label(kernel::EdgeReference{source.owner_id,source.curve_id,path},
                    viewer_->mesh().original_references).value_or(tr("Hrana"));
            if(!source.curve_id.empty())for(const auto& sketch:doc.sketches)if(sketch.owner_container_id==source.owner_id) {
                int number=0,selected=0;
                const auto visit=[&](const auto& curves){for(const auto& item:curves){++number;if(item.id==source.curve_id)selected=number;}};
                visit(sketch.segments);visit(sketch.arcs);visit(sketch.elliptical_arcs);visit(sketch.bsplines);
                if(selected)return name+" / "+tr("Hraniční křivka")+" "+QString::number(selected);
            }
            return source.curve_id.empty()?name:name+" / "+QString::fromStdString(source.curve_id);
        };
        dialog->support_label=[this,document_id,path=*occurrence](auto reference){
            const auto& doc=workspace_.open_part(document_id)->session.document();const auto* owner=doc.find_container(reference.owner_id);
            reference.instance_path=path;
            return QString::fromStdString(owner?owner->name:reference.owner_id)+" / "+
                reference_display_label(reference,viewer_->mesh().original_references).value_or(tr("Plocha"));
        };
        const auto resolve=[this,dialog,document_id,path=*occurrence](const viewer::ViewerCandidate& candidate)->document::BoundaryCurveSource {
            if(candidate.instance_path!=path)return {};
            const auto& doc=workspace_.open_part(document_id)->session.document();
            document::BoundaryCurveSource source;
            if(candidate.kind==viewer::CandidateKind::Container)source.owner_id=candidate.owner_id;
            else if(candidate.kind==viewer::CandidateKind::Edge&&candidate.geometry==viewer::CandidateGeometry::OriginalReference)
                source={candidate.owner_id,candidate.semantic_key,document::BoundaryCurveSource::Kind::Edge};
            else if(candidate.kind==viewer::CandidateKind::SketchSegment||candidate.kind==viewer::CandidateKind::SketchCurve) {
                const auto sketch=std::ranges::find(doc.sketches,candidate.owner_id,&sketcher::Sketch::id);
                if(sketch==doc.sketches.end())return {};
                const auto separator=candidate.semantic_key.find(':');if(separator==std::string::npos)return {};
                source={sketch->owner_container_id,candidate.semantic_key.substr(separator+1)};
            }
            if(!document::boundary_surface_source_allowed(doc,dialog->pending.id,source))return {};
            return source;
        };
        const auto resolve_support=[this,dialog,document_id,path=*occurrence](const viewer::ViewerCandidate& candidate)->std::optional<kernel::FaceReference> {
            if(candidate.instance_path!=path||candidate.kind!=viewer::CandidateKind::Face||candidate.geometry!=viewer::CandidateGeometry::OriginalReference)return {};
            kernel::FaceReference reference{candidate.owner_id,candidate.semantic_key,{}};
            if(!document::boundary_surface_support_allowed(workspace_.open_part(document_id)->session.document(),dialog->pending.id,reference))return {};
            return reference;
        };
        const auto update=[this,dialog,resolve,resolve_support,document_id,path=*occurrence]{
            viewer_->set_original_container_selection(true);
            viewer_->set_original_face_selection(dialog->support_entry());
            viewer_->set_selection_contract(dialog->support_entry()?std::vector<viewer::CandidateKind>{viewer::CandidateKind::Face}:
                std::vector<viewer::CandidateKind>{viewer::CandidateKind::Container,viewer::CandidateKind::SketchSegment,viewer::CandidateKind::SketchCurve,viewer::CandidateKind::Edge});
            viewer_->set_candidate_filter([dialog,resolve,resolve_support](const auto& candidate){return dialog->active_row()>=0&&
                (dialog->support_entry()?resolve_support(candidate).has_value():!resolve(candidate).owner_id.empty());},false);
            std::set<viewer::EdgeKey> edges;
            std::vector<viewer::ViewerCandidate> faces;
            const auto& doc=workspace_.open_part(document_id)->session.document();
            for(std::size_t row=0;row<dialog->pending.boundary_surface.boundaries.size();++row) {
                const auto& source=dialog->pending.boundary_surface.boundaries[row];
                if(source.continuity!=kernel::SurfaceContinuity::G0&&source.support&&dialog->support_inspected(static_cast<int>(row))) {
                    viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Face;candidate.owner_id=source.support->owner_id;
                    candidate.semantic_key=source.support->semantic_key;candidate.instance_path=path;candidate.geometry=viewer::CandidateGeometry::OriginalReference;
                    faces.push_back(std::move(candidate));
                }
                if(source.kind==document::BoundaryCurveSource::Kind::Edge&&dialog->inspected(static_cast<int>(row)))
                    edges.insert({source.owner_id,source.curve_id,path});
            }
            for(std::size_t row=0;row<dialog->pending.boundary_surface.boundaries.size();++row)if(dialog->inspected(static_cast<int>(row))) {
                const auto& source=dialog->pending.boundary_surface.boundaries[row];
                for(const auto& edge:viewer_->mesh().edges) {
                    if(edge.reference.instance_path!=path)continue;
                    bool matches=false;
                    if(const auto* curve=doc.find_construction(source.owner_id))
                        matches=edge.reference.owner_id==curve->entity_id&&viewer::is_curve3d_edge(edge.reference.semantic_key);
                    for(const auto& sketch:doc.sketches)if(sketch.owner_container_id==source.owner_id&&edge.reference.owner_id==sketch.id) {
                        const auto separator=edge.reference.semantic_key.find(':');
                        if(separator!=std::string::npos)matches=document::boundary_surface_source_allowed(doc,dialog->pending.id,
                            {source.owner_id,edge.reference.semantic_key.substr(separator+1)});
                    }
                    if(!matches)continue;
                    if(!source.curve_id.empty()&&!edge.reference.semantic_key.ends_with(":"+source.curve_id))continue;
                    edges.insert({edge.reference.owner_id,edge.reference.semantic_key,path});
                }
            }
            viewer_->set_inspected_faces(std::move(faces));viewer_->set_constraint_reference_highlights({},std::move(edges));
        };
        feature_reference_pick_=[this,dialog,resolve,resolve_support](const auto& candidate){
            if(dialog->active_row()<0)return;
            if(dialog->support_entry()){const auto support=resolve_support(candidate);if(!support)return;dialog->set_support(*support);}
            else {const auto source=resolve(candidate);if(source.owner_id.empty())return;dialog->set_boundary(source);}
            viewer_->clear_selection();
        };
        feature_reference_end_=[dialog]{dialog->end_entry();};dialog->changed=update;
        connect(dialog,&QDialog::finished,this,[this,dialog]{
            dialog->changed={};feature_reference_pick_={};feature_reference_end_={};properties_dialog_=nullptr;
            part_rollback_.reset();properties_dialog_instance_path_.clear();
            viewer_->set_original_container_selection(false);viewer_->set_candidate_filter({});viewer_->set_selection_contract({});
            viewer_->set_original_face_selection(false);viewer_->set_inspected_faces({});
            viewer_->set_constraint_reference_highlights({},{});viewer_->clear_selection();
            tree_->setProperty("commandSelectionActive",false);preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
        });
        preserve_view_on_refresh_=true;refresh_scene();dialog->refresh();dialog->show();update();
    }catch(const std::exception& error){state_->setText(tr(error.what()));}
}
}
