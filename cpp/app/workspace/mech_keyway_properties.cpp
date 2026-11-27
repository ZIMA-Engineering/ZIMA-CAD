#include "workspace_internal.hpp"
#include "../mech_keyway_parameter_panel.hpp"
#include "../symbol_properties_dialog.hpp"
#include <QDir>
#include <QPointer>
#include <zima/document/boundary_surface.hpp>
#include <zima/document/profile_targets.hpp>
#include <zima/kernel/wrapped_image.hpp>
#include <zima/workspace/mech_keyway_operations.hpp>

namespace zima::app {
void AssemblyWorkspaceWindow::install_mech_keyway_properties(PrimitivePropertiesDialog* dialog,const std::string& document_id){
    auto* panel=dialog->mech_keyway_panel();if(!panel)return;
    const auto occurrence=resolve_active_occurrence(document_id);if(!occurrence)return;
    const auto path=*occurrence;
    std::vector<symbols::Definition> definitions;
    const QDir folder(QDir(application_settings_.resolved_paths.value("Symbols")).filePath("surface-texture"));
    for(const auto& file:folder.entryInfoList({"*.symz"},QDir::Files,QDir::Name)){
        try{auto value=symbols::Definition::load(std::filesystem::path(file.absoluteFilePath().toStdWString()));
            if(value.fields.contains("Specification"))definitions.push_back(std::move(value));
        }catch(const std::exception&){ /* Other files do not define a surface-texture symbol. */ }
    }
    std::stable_sort(definitions.begin(),definitions.end(),[](const auto& a,const auto& b){return a.id=="ze:surface-texture:iso21920"&&b.id!=a.id;});
    panel->initialize_roughness(std::move(definitions),workspace_.open_part(document_id)->session.document().symbol_annotations);
    panel->annotations_changed=[dialog]{dialog->set_commit_required(true);};
    panel->roughness_edit_requested=[this,dialog,panel](int index){
        auto* child=new SymbolDialog(panel->roughness_instance(index),{},[panel,index](auto value){panel->set_roughness_instance(index,std::move(value));},this);
        const QPointer<PrimitivePropertiesDialog> parent(dialog);dialog->setEnabled(false);
        connect(child,&QObject::destroyed,this,[parent]{if(parent){parent->setEnabled(true);parent->raise();}});
        connect(dialog,&QObject::destroyed,child,&QWidget::close);child->show();
    };
    using Key=std::tuple<std::string,std::string>;
    auto supports=std::make_shared<std::map<Key,kernel::FaceReference>>();
    panel->face_label=[this,document_id,path](auto face){
        const auto* state=workspace_.open_part(document_id);
        const auto* owner=state?state->session.document().find_container(face.owner_id):nullptr;
        if(face.instance_path.empty())face.instance_path=path;
        return QString::fromStdString(owner?owner->name:face.owner_id)+" / "+
            reference_display_label(face,viewer_->mesh().original_references).value_or(tr("Plocha"));
    };
    const auto finish=[this,panel]{
        panel->finish_entry();feature_reference_pick_={};feature_reference_end_={};
        viewer_->set_original_container_selection(false);viewer_->set_original_face_selection(false);
        viewer_->set_candidate_filter({});viewer_->set_inspected_faces({});viewer_->clear_selection();
        set_primitive_properties_dimension_selection();
    };
    const auto resolve=[this,dialog,panel,path,document_id,supports](const viewer::ViewerCandidate& candidate)->std::optional<kernel::FaceReference>{
        if(panel->active_pick()<0||candidate.instance_path!=path||candidate.owner_id==dialog->container_id())return {};
        const auto* state=workspace_.open_part(document_id);if(!state)return {};
        kernel::FaceReference face{candidate.owner_id,candidate.semantic_key,{}};
        if(panel->active_pick()==0){
            if(candidate.kind!=viewer::CandidateKind::Face||candidate.geometry!=viewer::CandidateGeometry::Display)return {};
            const auto found=supports->find({candidate.owner_id,candidate.semantic_key});if(found==supports->end())return {};
            face=found->second;
        }else{
            if(candidate.kind!=viewer::CandidateKind::Face&&candidate.kind!=viewer::CandidateKind::Plane)return {};
            if(candidate.kind==viewer::CandidateKind::Face&&candidate.geometry!=viewer::CandidateGeometry::Display)return {};
            document::ExtrusionParameters::EndTarget target;target.reference=face;
            target.kind=candidate.kind==viewer::CandidateKind::Plane?document::EndTargetKind::Plane:document::EndTargetKind::Face;
            const auto plane=document::resolve_profile_target(target,primitive_reference_geometry_);
            const auto support=std::ranges::find(primitive_reference_geometry_.triangle_references,panel->parameters().support);
            if(!plane||support==primitive_reference_geometry_.triangle_references.end()||!support->surface)return {};
            const auto a=support->surface->axis,n=plane->fallback_normal;
            if(std::abs(a.x*n.x+a.y*n.y+a.z*n.z)<1.-1e-8)return {};
            if(plane->reference.surface&&plane->reference.surface->kind!=kernel::SurfaceGeometry::Kind::Plane)return {};
        }
        if(!document::boundary_surface_support_allowed(state->session.document(),dialog->container_id(),face))return {};
        return face;
    };
    panel->inspection_changed=[this,panel,path]{
        std::vector<viewer::ViewerCandidate> faces;
        for(auto ref:panel->inspected_faces()){
            viewer::ViewerCandidate candidate;candidate.owner_id=ref.owner_id;candidate.semantic_key=ref.semantic_key;candidate.instance_path=path;
            candidate.kind=document::profile_target_is_datum(ref)?viewer::CandidateKind::Plane:viewer::CandidateKind::Face;
            candidate.geometry=candidate.kind==viewer::CandidateKind::Face?viewer::CandidateGeometry::Display:viewer::CandidateGeometry::OriginalReference;
            faces.push_back(std::move(candidate));
        }
        viewer_->set_inspected_faces(std::move(faces));
    };
    panel->pick_requested=[this,dialog,panel,path,supports,resolve,finish](int){
        if(extrusion_target_dialog_)finish_extrusion_target_selection(false);
        pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
        dialog->set_active_reference_index(std::nullopt);viewer_->clear_selection();tree_->setProperty("commandSelectionActive",true);
        supports->clear();
        std::map<Key,kernel::FaceReference> analytic;
        for(const auto& face:primitive_reference_geometry_.triangle_references)
            if(face.surface&&!face.surface_result&&(face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder||face.surface->kind==kernel::SurfaceGeometry::Kind::Cone))
                analytic.try_emplace({face.owner_id,face.semantic_key},face);
        std::set<Key> examined;
        // Native ancestry maps displayed fragments to the authored analytic
        // face once per entry. Hover and confirmation consume this same map.
        for(const auto& displayed:viewer_->mesh().triangle_references){
            if(displayed.instance_path!=path)continue;
            const Key key{displayed.owner_id,displayed.semantic_key};if(!examined.insert(key).second)continue;
            if(const auto direct=analytic.find(key);direct!=analytic.end()){supports->emplace(key,direct->second);continue;}
            auto local=displayed;local.instance_path.clear();
            std::optional<kernel::FaceReference> source;bool ambiguous=false;
            for(const auto& [identity,face]:analytic){
                if(face==local){source=face;ambiguous=false;break;}
                if(!kernel::wrapped_image_face_matches(local,face))continue;
                if(source&&*source!=face){ambiguous=true;break;}source=face;
            }
            if(source&&!ambiguous)supports->emplace(key,*source);
        }
        viewer_->set_original_container_selection(false);viewer_->set_original_face_selection(false);
        viewer_->set_selection_contract({viewer::CandidateKind::Face,viewer::CandidateKind::Plane});
        viewer_->set_candidate_filter([resolve](const auto& candidate){return resolve(candidate).has_value();},false);
        feature_reference_end_=finish;
        feature_reference_pick_=[this,dialog,panel,resolve,finish](const auto& candidate){
            const auto reference=resolve(candidate);if(!reference)return;
            try {
                const int row=panel->active_pick();
                bool enter_placement=false;
                if(row==0){
                    auto pending=dialog->pending_value();const auto old=pending.mech_keyway.support;
                    enter_placement=!old.valid();
                    pending.mech_keyway.support=*reference;
                    const auto face=std::ranges::find(primitive_reference_geometry_.triangle_references,*reference);
                    if(face==primitive_reference_geometry_.triangle_references.end()||!face->surface)return;
                    if(!old.valid())workspace::initialize_mech_keyway_placement(pending,primitive_reference_geometry_);
                    else for(auto& ref:pending.placement.references)
                        if(ref.owner_id==old.owner_id&&ref.semantic_key==old.semantic_key&&ref.instance_path==old.instance_path){
                            ref.owner_id=reference->owner_id;ref.semantic_key=reference->semantic_key;ref.instance_path=reference->instance_path;
                        }
                    dialog->initialize_mech_keyway_placement(pending.placement);
                    const auto& surface=*face->surface;
                    const double station=(pending.placement.x-surface.origin.x)*surface.axis.x+
                        (pending.placement.y-surface.origin.y)*surface.axis.y+(pending.placement.z-surface.origin.z)*surface.axis.z;
                    const int direction=(surface.axial_min+surface.axial_max)*.5<station?-1:1;
                    panel->set_support(*reference,surface,direction);
                }else panel->set_limit(row-1,*reference);
                finish();
                if(enter_placement){
                    const auto next=dialog->first_empty_position_index();
                    if(next<3)start_primitive_reference_selection(next,true);
                }
            }catch(const std::exception& error){state_->setText(tr(error.what()));}
        };
    };
    panel->refresh();
    connect(dialog,&QObject::destroyed,this,[this]{feature_reference_pick_={};feature_reference_end_={};viewer_->set_inspected_faces({});});
}
}
