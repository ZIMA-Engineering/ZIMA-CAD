#include "../assembly_curve_reference.hpp"
#include "workspace_internal.hpp"
#include "../wrapping_parameter_panel.hpp"
#include <zima/document/boundary_surface.hpp>
#include <zima/assembly/assembly_document.hpp>
#include <zima/kernel/wrapped_image.hpp>

namespace zima::app {
void AssemblyWorkspaceWindow::install_wrapping_properties(PrimitivePropertiesDialog* dialog,const std::string& document_id) {
    auto* panel=dialog->wrapping_panel();if(!panel)return;
    const auto occurrence=resolve_active_occurrence(document_id);if(!occurrence)return;const auto path=*occurrence;
    using FaceKey=std::tuple<std::string,std::string,std::string>;
    auto analytic_faces=std::make_shared<std::set<FaceKey>>();
    panel->face_label=[this,document_id,path](auto reference) {
        const auto* part=workspace_.open_part(document_id);
        if(!part)reference=assembly_curve_scene_reference(std::move(reference),path);
        else if(reference.instance_path.empty())reference.instance_path=path;
        const auto* feature=part?part->session.document().find_container(reference.owner_id):nullptr;
        return QString::fromStdString(feature?feature->name:reference.owner_id)+" / "+
            reference_display_label(reference,viewer_->mesh().original_references).value_or(tr("Plocha"));
    };
    const auto finish=[this,panel] {
        panel->finish_entry();feature_reference_pick_={};feature_reference_end_={};
        viewer_->set_original_container_selection(false);viewer_->set_original_face_selection(false);
        viewer_->set_candidate_filter({});viewer_->clear_selection();set_primitive_properties_dimension_selection();
    };
    const auto allowed=[this,dialog,panel,document_id,path,analytic_faces](const viewer::ViewerCandidate& candidate) {
        const auto* part=workspace_.open_part(document_id);const auto pending=dialog->pending_value();
        if(pending.feature.type!=document::FeatureType::Wrapping||!panel->active_pick()||candidate.owner_id==pending.id||
                candidate.kind!=viewer::CandidateKind::Face||candidate.geometry!=viewer::CandidateGeometry::Display)return false;
        if(!analytic_faces->contains({candidate.owner_id,candidate.semantic_key,candidate.instance_path}))return false;
        if(!part)return panel->parameters().output==document::WrappingOutput::Curves&&
            assembly_curve_local_reference({candidate.owner_id,candidate.semantic_key,candidate.instance_path},path).has_value();
        const auto& doc=part->session.document();
        kernel::FaceReference reference{candidate.owner_id,candidate.semantic_key,candidate.instance_path==path?std::string{}:candidate.instance_path};
        if(candidate.instance_path==path)return document::boundary_surface_support_allowed(doc,pending.id,reference);
        if(panel->parameters().output==document::WrappingOutput::Relief||panel->parameters().output==document::WrappingOutput::Split)return false;
        const auto file=workspace_.occurrence_source_file(workspace_.displayed_document_id(),assembly::InstancePath::decode(candidate.instance_path));
        return file&&assembly::is_skeleton_file(*file);
    };
    const auto inspect=[this,panel,path,document_id] {
        std::vector<viewer::ViewerCandidate> faces;
        for(auto reference:panel->inspected_faces()) {
            if(workspace_.open_assembly(document_id))reference=assembly_curve_scene_reference(std::move(reference),path);
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Face;candidate.geometry=viewer::CandidateGeometry::Display;
            candidate.owner_id=reference.owner_id;candidate.semantic_key=reference.semantic_key;
            candidate.instance_path=reference.instance_path.empty()?path:reference.instance_path;faces.push_back(std::move(candidate));
        }
        viewer_->set_inspected_faces(std::move(faces));viewer_->set_constraint_reference_highlights({},{});
    };
    panel->inspection_changed=inspect;
    panel->pick_requested=[this,dialog,panel,document_id,path,allowed,finish,analytic_faces](bool active) {
        if(!active){finish();return;}
        if(extrusion_target_dialog_)finish_extrusion_target_selection(false);
        pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
        dialog->set_active_reference_index(std::nullopt);viewer_->clear_selection();tree_->setProperty("commandSelectionActive",true);
        // Classify the immutable input packet once per reference-entry session.
        // Mouse hover then performs only an identity lookup, with no OCCT work.
        analytic_faces->clear();
        for(const auto* faces:{&viewer_->mesh().triangle_references,&viewer_->mesh().original_references.triangle_references})
            for(const auto& face:*faces) {
                const auto support=face.surface?face.surface:face.contour_surface;
                if(support&&(support->kind==kernel::SurfaceGeometry::Kind::Cylinder||support->kind==kernel::SurfaceGeometry::Kind::Cone||support->kind==kernel::SurfaceGeometry::Kind::Sphere||support->kind==kernel::SurfaceGeometry::Kind::Plane))
                    analytic_faces->emplace(face.owner_id,face.semantic_key,face.instance_path);
            }
        std::set<FaceKey> examined;
        const auto supports=*analytic_faces;
        for(const auto& face:viewer_->mesh().triangle_references) {
            const FaceKey key{face.owner_id,face.semantic_key,face.instance_path};
            if(analytic_faces->contains(key)||!examined.insert(key).second)continue;
            for(const auto& [owner,semantic,path]:supports)
                if(kernel::wrapped_image_face_matches(face,kernel::FaceReference{owner,semantic,path})) {
                    analytic_faces->insert(key);break;
                }
        }
        viewer_->set_original_container_selection(false);viewer_->set_original_face_selection(false);
        viewer_->set_selection_contract({viewer::CandidateKind::Face});viewer_->set_candidate_filter(allowed,false);feature_reference_end_=finish;
        feature_reference_pick_=[this,dialog,panel,document_id,path,allowed,finish](const auto& candidate) {
            if(!allowed(candidate))return;
            try {
                if(workspace_.open_assembly(document_id)) {
                    const auto reference=assembly_curve_local_reference({candidate.owner_id,candidate.semantic_key,candidate.instance_path},path);
                    if(!reference)return;
                    document::ExtrusionParameters::EndTarget target;target.reference=*reference;target.kind=document::EndTargetKind::Face;
                    target.label=panel->face_label(*reference).toStdString();
                    const auto instance=assembly::InstancePath::decode(path);
                    for(auto point:viewer_->candidate_face_triangles(candidate)) {
                        if(!instance.occurrence_ids.empty())point=workspace_.occurrence_point_from_scene(workspace_.displayed_document_id(),instance,point);
                        target.fallback_triangles.push_back(point);
                    }
                    if(panel->set_target(std::move(target)))finish();return;
                }
                const auto& doc=workspace_.open_part(document_id)->session.document();const auto pending=dialog->pending_value();
                const bool external=candidate.instance_path!=path;
                document::ExtrusionParameters::EndTarget target;
                target.reference={candidate.owner_id,candidate.semantic_key,external?candidate.instance_path:std::string{}};
                target.kind=document::EndTargetKind::Face;target.label=panel->face_label(target.reference).toStdString();
                const auto* body=doc.body_owner_for_object(pending.id);if(!body)body=doc.body_history.find(doc.body_history.active_body_id());
                if(external)target=workspace::prepare_skeleton_end_target(workspace_,doc,body?body->scope.id:pending.id,target,nullptr,true);
                else for(auto point:viewer_->candidate_face_triangles(candidate)) {
                    const auto instance=assembly::InstancePath::decode(path);
                    if(!instance.occurrence_ids.empty())point=workspace_.occurrence_point_from_scene(workspace_.displayed_document_id(),instance,point);
                    if(body)point=workspace_detail::container_dimension_frame(body->scope.placement).inverse_point(point);
                    target.fallback_triangles.push_back(point);
                }
                if(panel->set_target(std::move(target)))finish();
            }catch(const std::exception& error){state_->setText(tr(error.what()));}
        };
    };
    const auto changed=panel->changed;
    panel->changed=[changed,inspect]{if(changed)changed();inspect();};panel->refresh();
    connect(dialog->findChild<QComboBox*>("featureType"),&QComboBox::currentIndexChanged,dialog,
        [dialog,panel,finish](int){if(dialog->pending_value().feature.type!=document::FeatureType::Wrapping&&panel->active_pick())finish();});
    connect(dialog,&QObject::destroyed,this,[this]{feature_reference_pick_={};feature_reference_end_={};viewer_->set_inspected_faces({});viewer_->set_constraint_reference_highlights({},{});});
}
}
