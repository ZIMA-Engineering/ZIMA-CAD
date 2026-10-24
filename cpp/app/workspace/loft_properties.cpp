#include "workspace_internal.hpp"
#include "../loft_dialog.hpp"
#include <zima/document/general_surface.hpp>
#include "../../common/interaction_colors.hpp"
#include <zima/document/loft.hpp>
#include <zima/document/container_origin_display.hpp>
#include <zima/document/solid_state_reference_views.hpp>
#include <zima/workspace/loft_operations.hpp>
#include <limits>
#include <unordered_map>
namespace zima::app {
using namespace workspace_detail;
namespace {
struct LoftEditorCache {
    std::uint64_t revision=std::numeric_limits<std::uint64_t>::max(),generation{};
    std::vector<std::string> profiles,guides;
    std::optional<document::Placement> placement,scene_placement;
    std::optional<std::pair<bool,bool>> origin_display;
    std::optional<std::pair<document::ProfileResultType,double>> dimensions;
    std::set<std::string> inspected;
    std::vector<document::GeneralSurfaceBoundary> sketches;
    kernel::ViewerMesh definition;
    std::unordered_map<std::string,std::string> aliases;
    unsigned preview_builds{},scene_publications{};
};
}
void AssemblyWorkspaceWindow::show_loft_properties(const std::string& id) {
    if(properties_dialog_||!active_sketch_id_.empty())return;
    auto* part=workspace_.open_part(workspace_.active_document_id());if(!part)return;
    const auto document_id=part->session.document().document_id;
    const auto occurrence=resolve_active_occurrence(document_id);if(!occurrence)return;
    const auto& source=part->session.document();
    const auto* body=source.body_history.find(source.body_history.active_body_id());
    if(!body||body->derived_copy){state_->setText(tr("Activate an editable Body before creating a Loft."));return;}
    const auto* stored=source.find_container(id);
    if(!id.empty()&&(!stored||stored->feature_kind!=document::FeatureKind::Loft))return;
    if(stored)if(const auto* owner=source.body_owner_for_object(stored->id);!owner||owner->scope.id!=body->scope.id) {
        state_->setText(tr("Activate the owning Body before editing its history."));return;
    }

    auto feature=stored?*stored:document::create_loft();
    if(!stored) {
        feature.name=tr("Přechod").toStdString();
        for(auto& value:feature.loft.sketches) {
            auto sketch=sketcher::Sketch::from_serialized(value.sketch_serialized);sketch.name=tr("Skica").toStdString();
            value.sketch_serialized=sketch.serialized();value.sketch_feature->name=sketch.name;value.sketch_feature->feature.automatic_name=sketch.name;
        }
    }
    const auto rollback=stored?part->session.rollback_boundary(id):std::optional<document::HistoryRollbackBoundary>{};
    if(stored&&!rollback){state_->setText(tr("Regenerate the Part before editing this feature."));return;}
    const auto* input=rollback&&rollback->input_body?&*rollback->input_body:
        part->session.calculated_boundaries().empty()?nullptr:&part->session.calculated_boundaries().back();
    bool allow_subtract=input&&input->has_solid();
    if(input)if(const auto found=input->body_outputs.find(body->scope.id);found!=input->body_outputs.end())
        allow_subtract=found->second->has_solid();
    auto* dialog=new LoftDialog(feature,[this,document_id](auto pending){
        static_cast<void>(workspace::commit_loft(workspace_,kernel_,document_id,std::move(pending)));
    },allow_subtract,this);
    if(rollback)part_rollback_=PartRollbackContext{document_id,*occurrence,rollback->history_index,rollback->input_body};
    properties_dialog_=dialog;properties_dialog_instance_path_=*occurrence;
    primitive_parameter_owner_id_=feature.id;primitive_reference_dialog_=dialog;
    auto geometry=input?input->mesh.original_references:kernel::ViewerReferenceGeometry{};
    if(stored&&!part->session.calculated_boundaries().empty())
        geometry=document::solid_state_editor_reference_geometry(source,part->session.calculated_boundaries().back(),id,std::move(geometry));
    append_reference_geometry(geometry,source.origin_viewer_mesh().original_references);
    append_reference_geometry(geometry,source.construction_viewer_mesh().original_references);
    append_reference_geometry(geometry,source.history_origin_reference_geometry_before(feature.id));
    append_reference_geometry(geometry,source.sketch_placement_reference_geometry(feature.id));
    geometry=source.construction_reference_geometry_for(body->scope.id,std::move(geometry));
    std::set<std::string> own{feature.id,feature.feature_id,feature.container_origin.id};
    auto limit=source.effective_history_cursor();
    if(stored)for(std::size_t i=0;i<source.history_order.size();++i)if(source.history_order[i].id==id){limit=i;break;}
    for(std::size_t i=limit;i<source.history_order.size();++i)own.insert(source.history_order[i].id);
    for(const auto& value:source.history)if(own.contains(value.id)){own.insert(value.feature_id);own.insert(value.container_origin.id);}
    for(const auto& value:source.constructions)if(own.contains(value.id)){own.insert(value.entity_id);own.insert(value.container_origin.id);}
    for(auto& face:geometry.triangle_references)if(own.contains(face.owner_id))face={};
    std::erase_if(geometry.edges,[&](const auto& edge){return own.contains(edge.reference.owner_id);});
    std::erase_if(geometry.points,[&](const auto& point){return own.contains(point.reference.owner_id);});
    std::erase_if(geometry.axes,[&](const auto& axis){return own.contains(axis.reference.owner_id);});
    primitive_reference_geometry_=geometry;track_tree_edit(dialog);
    tree_->setProperty("commandSelectionActive",true);
    const auto cache=std::make_shared<LoftEditorCache>();
    dialog->edit_sketch_properties=[this,dialog,cache](unsigned stage){cache->scene_placement.reset();show_owned_sketch_properties(dialog,stage);};
    dialog->request_placement=[this,dialog](std::size_t index){
        feature_reference_pick_={};feature_reference_end_={};
        start_primitive_reference_selection(index);
    };
    dialog->changed=[this,dialog,document_id,geometry,body_id=body->scope.id,path=*occurrence,cache]{
        if(!dialog->isVisible())return;
        auto* live=workspace_.open_part(document_id);if(!live)return;
        if(cache->revision!=live->session.revision()||cache->generation!=live->session.data_generation()) {
            cache->revision=live->session.revision();cache->generation=live->session.data_generation();cache->placement.reset();
        }
        const bool valid=dialog->resolve_pending_placement(geometry);
        auto& pending=dialog->pending;
        std::string sketch_error;
        if(!cache->placement||*cache->placement!=pending.placement||cache->sketches!=pending.loft.sketches)
            try{document::resolve_loft_sketches(pending,geometry);}catch(const std::exception& error){sketch_error=error.what();}
        primitive_translation_dof_=document::point_constraint_remaining_dof(pending.placement.references,geometry);
        parameter_dimension_preview_=pending;construction_dimension_object_id_=pending.id;
        viewer_->set_feature_preview_owners({pending.feature_id,pending.container_origin.id});
        const bool preview_changed=!cache->placement||*cache->placement!=pending.placement||
            cache->profiles!=pending.loft.profiles||cache->guides!=pending.loft.guides||cache->sketches!=pending.loft.sketches;
        if(preview_changed) {
            cache->definition=document::loft_definition_mesh(live->session.document(),pending,geometry);
            cache->placement=pending.placement;cache->profiles=pending.loft.profiles;cache->guides=pending.loft.guides;cache->sketches=pending.loft.sketches;++cache->preview_builds;
        }
        const bool base_changed=preview_changed||!cache->scene_placement||*cache->scene_placement!=pending.placement||
            cache->origin_display!=std::pair{pending.origin_point_visible,pending.origin_text_visible}||
            cache->dimensions!=std::pair{pending.loft.result_type,pending.loft.thickness};
        if(base_changed) {
            document::PartDocument carrier;document::ConstructionObject origin;
            origin.id=pending.id;origin.entity_id=pending.feature_id;origin.container_origin=pending.container_origin;
            origin.kind=document::ConstructionKind::Point;origin.origin={pending.placement.x,pending.placement.y,pending.placement.z};
            origin.rotation={pending.placement.rotation_x,pending.placement.rotation_y,pending.placement.rotation_z};origin.reference_valid=false;
            carrier.constructions.push_back(origin);primitive_origin_preview_mesh_=carrier.construction_viewer_mesh(pending.id);
            primitive_origin_preview_mesh_->dimensions=cache->definition.dimensions;
            primitive_origin_preview_mesh_->points.push_back(document::container_origin_marker(pending,true));
            cache->scene_placement=pending.placement;cache->origin_display=std::pair{pending.origin_point_visible,pending.origin_text_visible};
            cache->dimensions=std::pair{pending.loft.result_type,pending.loft.thickness};
            preserve_view_on_refresh_=true;refresh_scene();++cache->scene_publications;
        }
        if(base_changed||preview_changed||cache->inspected!=dialog->inspected_sources()) {
            auto preview=cache->definition;
            preview=live->session.document().place_body_mesh(std::move(preview),body_id);
            const auto point=[this,&path](kernel::Vec3 value){return path.empty()?value:
                workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),assembly::InstancePath::decode(path),value);};
            for(auto& edge:preview.edges){edge.reference.instance_path=path;edge.color=interaction::selected.name().toStdString();for(auto& value:edge.points)value=point(value);}
            for(auto& vertex:preview.points){vertex.reference.instance_path=path;vertex.position=point(vertex.position);}
            viewer_->set_transient_edges(std::move(preview.edges));viewer_->set_transient_model_points(std::move(preview.points));
            cache->inspected=dialog->inspected_sources();
        }
        dialog->setProperty("loftPreviewBuildCount",cache->preview_builds);
        dialog->setProperty("loftBaseSceneCount",cache->scene_publications);
        auto inspected_keys=highlighted_reference_edge_keys(*dialog);
        for(const auto& value:pending.loft.sketches)if(dialog->inspected_sources().contains(value.sketch_feature->id)) {
            const auto prefix="loft:input:from:"+value.sketch_feature->feature.sketch_id+":";
            for(const auto& edge:cache->definition.edges)if(edge.reference.semantic_key.starts_with(prefix))
                inspected_keys.insert({pending.id,edge.reference.semantic_key,path});
            for(const auto& point:cache->definition.points)if(point.reference.semantic_key.starts_with(prefix))
                inspected_keys.insert({pending.id,point.reference.semantic_key,path});
        }
        viewer_->set_constraint_reference_highlights({},std::move(inspected_keys));
        if(base_changed||preview_changed||!sketch_error.empty())try {
            if(!sketch_error.empty())throw std::runtime_error(sketch_error);
            if(!valid)throw std::runtime_error("Chybí reference umístění kontejneru");
            document::validate_loft(pending);
            dialog->set_status(std::ranges::any_of(pending.loft.sketches,[](const auto& value){return !value.sketch_feature->placement.reference_valid;})
                ?tr("Chybí reference umístění skici; zachována poslední poloha v Loftu."):tr("Skici jsou připravené. OK vypočítá přechod."));
        }catch(const std::exception& error){dialog->set_status(tr(error.what()));}
        if(!pending_primitive_reference_index_&&!local_origin_selection_active_) {
            feature_reference_pick_={};feature_reference_end_={};set_primitive_properties_dimension_selection();
        }
    };
    connect(dialog,&QDialog::finished,this,[this,dialog]{
        dialog->changed={};dialog->edit_sketch_properties={};dialog->request_placement={};feature_reference_pick_={};feature_reference_end_={};
        pending_primitive_reference_index_.reset();suspended_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
        local_origin_selection_active_=false;primitive_reference_dialog_=nullptr;primitive_reference_geometry_={};
        primitive_origin_preview_mesh_.reset();parameter_dimension_preview_.reset();construction_dimension_object_id_.clear();
        primitive_parameter_owner_id_.clear();part_rollback_.reset();properties_dialog_=nullptr;properties_dialog_instance_path_.clear();
        viewer_->set_feature_preview_owners({});viewer_->set_original_container_selection(false);viewer_->set_original_face_selection(false);
        viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->set_constraint_reference_highlights({},{});
        viewer_->set_transient_edges({});viewer_->clear_selection();tree_->setProperty("commandSelectionActive",false);
        preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    });
    dialog->refresh();dialog->show();dialog->changed();
}
}
