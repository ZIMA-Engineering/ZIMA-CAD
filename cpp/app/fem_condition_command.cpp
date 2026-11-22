#include "assembly_workspace_window.hpp"
#include "fem_condition_dialog.hpp"
#include "confirmed_face_hit.hpp"
#include "reference_display.hpp"
#include "workspace/workspace_internal.hpp"
#include <zima/workspace/fem_condition_operations.hpp>
#include <zima/document/fem_presentation.hpp>
#include <zima/document/placement_reference_geometry.hpp>
#include <zima/kernel/stable_id.hpp>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QSignalBlocker>
#include <QPointer>

namespace zima::app {
using namespace workspace_detail;
const document::FemCondition* AssemblyWorkspaceWindow::active_fem_condition(const std::string& id)const {
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    return part?workspace::find_fem_condition(part->session.document(),part->session.document().body_history.active_body_id(),id):nullptr;
}

kernel::ViewerMesh AssemblyWorkspaceWindow::fem_display_mesh()const {
    const auto* part=workspace_.open_part(workspace_.active_document_id());if(!part)return {};
    auto result=part->session.body_context_mesh();const auto& doc=part->session.document();
    const auto* mesh=show_fem_deformation_&&!properties_dialog_?active_fem_deformation():nullptr;
    if(!mesh&&show_generated_fem_mesh_&&!properties_dialog_)mesh=active_generated_fem_mesh();
    const auto* body=doc.body_history.find(doc.body_history.active_body_id());if(!body||!body->fem)return result;
    kernel::ViewerMesh source_mesh;
    if(mesh) {
        source_mesh=std::move(result);
        result=*mesh;
    }
    const auto& source=mesh?source_mesh:result;
    for(const auto& condition:body->fem->conditions) {
        if(fem_condition_preview_&&fem_condition_preview_->id==condition.id)continue;
        append_mesh(result,document::fem_condition_presentation(condition,source));
    }
    if(!active_sketch_id_.empty()&&sweep_profile_sketch_draft_)
        append_mesh(result,sketch_viewer_mesh(*sweep_profile_sketch_draft_));
    if(fem_condition_preview_) {
        append_mesh(result,doc.origin_viewer_mesh());
        auto origins=doc.body_origin_reference_geometry();
        kernel::ViewerMesh frame;frame.original_references=std::move(origins);
        // The shared Origin picker reveals only the frames explicitly requested.
        append_mesh(frame,local_origin_display_mesh(frame.original_references,visible_local_origin_ids_));append_mesh(result,std::move(frame));
    }
    if(const auto* condition=active_fem_condition(fem_inspected_condition_);condition&&construction_dimension_object_id_==condition->id&&!properties_dialog_) {
        const auto& p=condition->placement;
        for(int i=0;i<3;++i) {
            kernel::ViewerDimension dimension;dimension.label_only=true;
            dimension.reference={condition->id,"fem:parameter:"+std::string(1,char('x'+i)),{}};
            dimension.value=i==0?p.x:i==1?p.y:p.z;dimension.label_prefix=std::string(1,char('X'+i))+" = ";
            dimension.witness_first=dimension.witness_second=dimension.line_first=dimension.line_second={p.x,p.y,p.z};
            dimension.label_position=kernel::Vec3{p.x,p.y+5*(i+1),p.z};result.dimensions.push_back(std::move(dimension));
        }
    }
    return result;
}

void AssemblyWorkspaceWindow::select_fem_condition(const std::string& id,bool inspect) {
    if(!active_fem_condition(id)||properties_dialog_)return;
    if(inspect){fem_inspected_condition_=id;show_parameter_dimensions(id);}
    const QSignalBlocker blocked(tree_);
    for(QTreeWidgetItemIterator it(tree_);*it;++it)if((*it)->data(0,Qt::UserRole+3)=="fem-condition"&&(*it)->data(0,Qt::UserRole).toString().toStdString()==id){tree_->clearSelection();tree_->setCurrentItem(*it);(*it)->setSelected(true);break;}
    if(!inspect)viewer_->confirm_reference(id,{},workspace_.active_occurrence_path(),viewer::CandidateKind::Container);
}

void AssemblyWorkspaceWindow::remove_fem_condition(const std::string& id) {
    if(properties_dialog_||!active_fem_condition(id))return;
    const auto* part=workspace_.open_part(workspace_.active_document_id());const auto& doc=part->session.document();
    try{static_cast<void>(workspace::erase_fem_condition(workspace_,workspace::prepare_fem_edit(doc,doc.body_history.active_body_id()),id));
        if(fem_inspected_condition_==id)fem_inspected_condition_.clear();if(construction_dimension_object_id_==id)construction_dimension_object_id_.clear();preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();}
    catch(const std::exception& e){state_->setText(tr(e.what()));}
}

void AssemblyWorkspaceWindow::show_fem_condition_properties(document::FemConditionKind kind,const std::string& id) {
    if(properties_dialog_||!active_fem_body())return;
    const auto document_id=workspace_.active_document_id();const auto* part=workspace_.open_part(document_id);const auto& doc=part->session.document();
    const auto body_id=doc.body_history.active_body_id();const auto* body=doc.body_history.find(body_id);
    kernel::BodySnapshot source;workspace::BodyGraphEdit edit;
    try{source=workspace::read_fem_source(part->session,body_id);edit=workspace::prepare_fem_edit(doc,body_id);primitive_reference_geometry_=workspace::fem_placement_geometry(part->session,body_id);}
    catch(const std::exception& e){state_->setText(tr(e.what()));return;}
    document::FemCondition initial;
    if(!id.empty()){const auto* found=active_fem_condition(id);if(!found)return;initial=*found;}
    else {
        initial.id=kernel::make_stable_id();initial.kind=kind;initial.region=kind==document::FemConditionKind::Pressure?document::FemRegionKind::Surface:document::FemRegionKind::Point;
        for(unsigned n=1;;++n){initial.name=fem_condition_name(kind).toStdString()+" "+std::to_string(n);
            if(std::ranges::none_of(body->fem->conditions,[&](const auto& c){return c.name==initial.name;}))break;}
    }
    auto* dialog=new FemConditionDialog(initial,[this,edit](auto value){static_cast<void>(workspace::commit_fem_condition(workspace_,edit,std::move(value)));},this);
    properties_dialog_=dialog;primitive_reference_dialog_=dialog;primitive_parameter_owner_id_=initial.id;
    construction_dimension_object_id_=initial.id;fem_condition_preview_=initial;fem_inspected_condition_.clear();
    dialog->reference_label=[this](const auto& r){
        const auto* part=workspace_.open_part(workspace_.active_document_id());const auto* owner=part?part->session.document().find_container(r.owner_id):nullptr;
        return QString::fromStdString(owner?owner->name:r.owner_id)+" / "+reference_display_label(r,primitive_reference_geometry_).value_or(QString::fromStdString(r.semantic_key));
    };dialog->refresh_references();
    const auto update_highlights=[this,dialog] {
        auto keys=highlighted_reference_edge_keys(*dialog);const auto value=dialog->pending_value();
        for(auto row:dialog->inspected_geometry())if(row<value.references.size()){const auto& r=value.references[row];keys.insert({r.owner_id,r.semantic_key,r.instance_path});}
        viewer_->set_constraint_reference_highlights({},std::move(keys));
    };
    dialog->set_reference_highlights_changed_callback(update_highlights);
    auto updating=std::make_shared<bool>(false);
    auto carrier=std::make_shared<document::PartDocument>(document::PartDocument::create_default());carrier->document_id=initial.id;
    auto graph=document::BodyHistoryGraph{};const auto frame_id=graph.create_body("frame");carrier->set_body_history(graph);
    const auto origin_template=carrier->origin_viewer_mesh();
    dialog->changed=[this,dialog,source,updating,update_highlights,carrier,frame_id,origin_template] {
        if(*updating)return;const QScopedValueRollback guard(*updating,true);
        auto value=dialog->pending_value();auto& p=value.placement;kernel::Vec3 base;bool oriented=false;
        const auto geometry=document::PlacementReferenceGeometryIndex(primitive_reference_geometry_).select(p.references);
        const bool valid=document::resolve_placement(p,geometry,&base,&oriented);
        const auto constraint=document::point_constraint_state(p.references,geometry,{p.x,p.y,p.z});primitive_translation_dof_=constraint.remaining_dof;
        dialog->set_translation_constraint_state(constraint,{p.x,p.y,p.z});
        dialog->set_rotation_constraint_state(document::orientation_constraint_state(p.references,geometry,true,{p.x,p.y,p.z}));
        dialog->set_orientation_base_rotation(base,oriented);dialog->set_resolved_rotation({p.rotation_x,p.rotation_y,p.rotation_z},valid);
        fem_condition_preview_=value;auto preview=document::fem_condition_presentation(value,source->mesh);
        if(value.projected_sketch) {
            auto sketch=document::fem_condition_sketch(value);
            document::PartDocument projection;
            auto feature=document::PartDocument::create_feature_container(sketch.id);
            feature.id=value.id;
            feature.feature.type=document::FeatureType::Projection;
            for(const auto& ref:value.references) {
                document::ExtrusionParameters::EndTarget target;
                target.reference={ref.owner_id,ref.semantic_key,ref.instance_path};
                feature.feature.projection.targets.push_back(std::move(target));
            }
            if(feature.feature.projection.targets.empty())append_mesh(preview,sketch_viewer_mesh(sketch));
            else {
                projection.sketches.push_back(std::move(sketch));
                append_mesh(preview,projection.projection_preview_mesh(feature,source->mesh));
            }
        }
        // Reuse the native Origin presentation, with the analysis container's
        // frame; no material feature or extra CAD calculation is created.
        auto frame=*carrier->body_history.find(frame_id);frame.scope.placement=p;carrier->body_history.update_body(frame);
        append_mesh(preview,carrier->place_body_mesh(origin_template,frame_id));
        viewer_->set_transient_context_mesh(std::move(preview));update_highlights();
    };
    const auto end_geometry=[this,dialog]{dialog->end_input();feature_reference_pick_={};feature_reference_end_={};tree_->setProperty("commandSelectionActive",false);viewer_->set_original_container_selection(false);viewer_->set_selection_contract({});viewer_->set_candidate_filter({});viewer_->clear_selection();};
    dialog->stop_geometry=end_geometry;
    dialog->edit_sketch=[this,dialog,end_geometry](sketcher::Sketch) {
        end_geometry();
        const auto value=fem_condition_preview_.value_or(dialog->pending_value());
        sweep_profile_sketch_draft_=document::fem_condition_sketch(value);
        active_sketch_id_=sweep_profile_sketch_draft_->id;
        selected_sketch_id_=active_sketch_id_;
        const QPointer<FemConditionDialog> guard(dialog);
        embedded_sketch_finished_=[this,guard](auto sketch) {
            if(!guard)return;
            properties_dialog_=guard;
            primitive_reference_dialog_=guard;
            guard->set_owned_sketch(std::move(sketch));
            guard->show();guard->raise();
            preserve_view_on_refresh_=true;refresh_scene();
        };
        primitive_reference_dialog_=nullptr;
        properties_dialog_=nullptr;
        dialog->hide();
        viewer_->set_transient_context_mesh({});
        viewer_->clear_selection();clear_selected_sketch_geometry();
        preserve_view_on_refresh_=true;refresh_scene();align_active_sketch_view();
    };
    dialog->request_geometry=[this,dialog,source,end_geometry](std::size_t row) {
        if(pending_primitive_reference_index_){pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;dialog->set_active_reference_index(std::nullopt);}
        const auto value=dialog->pending_value();
        const auto region=value.projected_sketch?document::FemRegionKind::Surface:value.region;
        std::set<std::pair<std::string,std::string>> available;
        const auto collect=[&](const auto& mesh) {
            if(region==document::FemRegionKind::Point)for(const auto& point:mesh.points)available.emplace(point.reference.owner_id,point.reference.semantic_key);
            if(region==document::FemRegionKind::Curve)for(const auto& edge:mesh.edges)if(!edge.points.empty())available.emplace(edge.reference.owner_id,edge.reference.semantic_key);
            if(region==document::FemRegionKind::Surface)for(const auto& face:mesh.triangle_references)available.emplace(face.owner_id,face.semantic_key);
        };
        collect(source->mesh);collect(source->mesh.original_references);
        const auto path=workspace_.active_occurrence_path();
        const auto allowed=[available=std::move(available),region,path](const viewer::ViewerCandidate& c){
            const auto kind=region==document::FemRegionKind::Point?viewer::CandidateKind::Vertex:region==document::FemRegionKind::Curve?viewer::CandidateKind::Edge:viewer::CandidateKind::Face;
            return c.instance_path==path&&(c.kind==kind||(region==document::FemRegionKind::Curve&&c.kind==viewer::CandidateKind::SketchCurve))&&
                available.contains({c.owner_id,c.semantic_key});
        };
        viewer_->set_selection_contract({region==document::FemRegionKind::Point?viewer::CandidateKind::Vertex:region==document::FemRegionKind::Curve?viewer::CandidateKind::Edge:viewer::CandidateKind::Face});
        viewer_->set_candidate_filter(allowed);tree_->setProperty("commandSelectionActive",true);
        feature_reference_end_=end_geometry;
        feature_reference_pick_=[this,dialog,row,allowed,end_geometry](const auto& candidate){
            if(!allowed(candidate))return;
            document::FemRegionReference r{candidate.owner_id,candidate.semantic_key,{}};
            const auto pending=dialog->pending_value();std::optional<kernel::Vec3> seed;
            if(pending.projected_sketch&&pending.region==document::FemRegionKind::Surface) {
                const auto ray=viewer_->ray_at(viewer_->last_pointer_position());if(!ray)return;
                seed=confirmed_face_hit(viewer_->candidate_face_triangles(candidate),ray->first,ray->second);
                if(!seed)return;
                const auto path=workspace_.active_occurrence_path();
                if(!path.empty())seed=workspace_.occurrence_point_from_scene(workspace_.displayed_document_id(),
                    assembly::InstancePath::decode(path),*seed);
            }
            end_geometry();dialog->set_geometry(row,std::move(r),seed);
        };
    };
    dialog->set_reference_request_callback([this,end_geometry](std::size_t row){end_geometry();start_primitive_reference_selection(row);});
    connect(dialog,&QDialog::finished,this,[this,dialog] {
        if(properties_dialog_!=dialog)return;
        properties_dialog_=nullptr;primitive_reference_dialog_=nullptr;primitive_reference_geometry_={};primitive_parameter_owner_id_.clear();construction_dimension_object_id_.clear();
        pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;feature_reference_pick_={};feature_reference_end_={};fem_condition_preview_.reset();
        viewer_->set_transient_context_mesh({});viewer_->set_constraint_reference_highlights({},{});viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->clear_selection();
        tree_->setProperty("commandSelectionActive",false);preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    });
    bind_local_origin_selection(dialog);preserve_view_on_refresh_=true;refresh_scene();dialog->show();dialog->changed();
    if(initial.region!=document::FemRegionKind::Point&&initial.references.empty())dialog->arm(0);
}
} // namespace zima::app
