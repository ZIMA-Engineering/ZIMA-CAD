#include "workspace_internal.hpp"
#include "../general_surface_dialog.hpp"
#include <zima/document/general_surface.hpp>
#include <zima/document/solid_state_reference_views.hpp>
namespace zima::app {
using namespace workspace_detail;
void AssemblyWorkspaceWindow::show_general_surface_curve_properties(GeneralSurfaceDialog* parent,unsigned stage) {
    if(properties_dialog_!=parent||stage>=parent->pending.general_surface.boundaries.size())return;
    const auto& boundary=parent->pending.general_surface.boundaries[stage];if(!boundary.curve)return;
    auto* part=workspace_.open_part(workspace_.active_document_id());if(!part)return;
    const auto document_id=workspace_.active_document_id();
    QPointer<GeneralSurfaceDialog> guard(parent);
    auto* dialog=new ConstructionPropertiesDialog(*boundary.curve,true,
        [guard,stage](auto curve){
            if(!guard)return;
            auto& owned=guard->pending.general_surface.boundaries.at(stage).curve;
            if(!owned||curve.id!=owned->id||curve.parent_construction_id!=guard->pending.id)
                throw std::invalid_argument("Invalid owned surface boundary.");
            *owned=std::move(curve);guard->refresh();
        },this,document_decimal_places(part->session.document()));
    general_surface_curve_preview_=[guard,stage](auto curve){
        if(!guard)throw std::invalid_argument("Invalid owned surface boundary.");
        auto feature=guard->pending;
        auto& owned=feature.general_surface.boundaries.at(stage).curve;
        if(!owned||owned->id!=curve.id)throw std::invalid_argument("Invalid owned surface boundary.");
        curve.parent_construction_id=feature.id;*owned=std::move(curve);return feature;
    };
    pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
    parent->set_active_reference_index(std::nullopt);parent->clear_reference_highlights();
    feature_reference_pick_={};feature_reference_end_={};
    set_local_origin_selection_mode(false);primitive_reference_dialog_=nullptr;
    primitive_origin_preview_mesh_.reset();parameter_dimension_preview_.reset();
    parent->hide();properties_dialog_=dialog;construction_reference_dialog_=dialog;
    dialog->set_reference_request_callback([this](std::size_t i){start_construction_reference_selection(i);});
    dialog->set_curve_point_edit_request_callback([this,dialog](auto i){show_curve_point_properties(dialog,i);});
    dialog->set_curve_axis_request_callback([this,dialog](auto i){start_curve_axis_selection(dialog,i);});
    dialog->set_curve_axis_cycle_callback([this,dialog]{
        if(curve_axis_dialog_!=dialog)return;
        pending_curve_axis_index_.reset();curve_axis_dialog_=nullptr;viewer_->clear_selection();set_construction_properties_dimension_selection();
    });
    dialog->set_reference_highlights_changed_callback([this,dialog]{viewer_->set_constraint_reference_highlights({},highlighted_reference_edge_keys(*dialog));});
    dialog->set_preview_callback([this,dialog,document_id,stage](auto preview){
        auto* source=workspace_.open_part(document_id);if(!source||!general_surface_curve_preview_)return;
        auto feature=general_surface_curve_preview_(preview);auto next=source->session.document();
        if(auto* stored=next.find_container(feature.id))*stored=feature;
        else {next.insert_history_entry(document::PartHistoryKind::Feature,feature.id);next.history.push_back(feature);}
        const auto& calculated=source->session.calculated_boundaries();
        auto geometry=construction_reference_source_geometry(calculated);
        const auto views=calculated.empty()?document::HistoryReferenceViews{}:document::solid_state_reference_views(next,calculated.back(),geometry);
        next.resolve_constructions(geometry,views);
        const auto* resolved=next.find_container(feature.id);if(!resolved)return;
        if(!calculated.empty())geometry=document::solid_state_editor_reference_geometry(next,calculated.back(),feature.id,std::move(geometry));
        append_reference_geometry(geometry,next.origin_viewer_mesh().original_references);
        append_reference_geometry(geometry,next.construction_viewer_mesh().original_references);
        append_reference_geometry(geometry,next.history_origin_reference_geometry_before(feature.id));
        append_reference_geometry(geometry,next.sketch_placement_reference_geometry(feature.id));
        geometry=next.construction_reference_geometry_for(feature.id,std::move(geometry));
        // Curve values and placement inputs are local to the Surface container.
        construction_reference_geometry_=document::general_surface_local_reference_geometry(*resolved,std::move(geometry));
        const auto& curve=*resolved->general_surface.boundaries.at(stage).curve;
        general_surface_curve_annotation_placement_=resolved->placement;
        construction_parameter_preview_=curve;construction_dimension_object_id_=curve.id;
        construction_preview_mesh_=document::general_surface_definition_mesh(*resolved);
        if(const auto* body=next.body_owner_for_object(feature.id))
            construction_preview_mesh_=next.place_body_mesh(std::move(*construction_preview_mesh_),body->scope.id);
        const auto point_state=document::point_constraint_state(preview.references,construction_reference_geometry_,curve.origin);
        dialog->set_translation_constraint_state(point_state,curve.origin);construction_translation_dof_=point_state.remaining_dof;
        const auto rotation_state=document::orientation_constraint_state(preview.references,construction_reference_geometry_,true,curve.origin);
        dialog->set_rotation_constraint_state(rotation_state);construction_rotation_dof_=rotation_state.remaining_dof;
        const bool oriented=curve.orientation_inherited_from_reference||std::ranges::any_of(preview.references,[](const auto& ref){return ref.orientation_drives_rotation;});
        dialog->set_orientation_base_rotation(oriented?curve.rotation_base:curve.absolute_rotation,oriented);
        dialog->set_resolved_rotation(curve.rotation,curve.reference_valid);
        dialog->set_orientation_inherited_from_reference(curve.orientation_inherited_from_reference);
        std::set<std::string> owners{curve.id,curve.entity_id,curve.container_origin.id};
        for(const auto& point:curve.curve_points){owners.insert(point.id);owners.insert(point.entity_id);owners.insert(point.container_origin.id);}
        viewer_->set_feature_preview_owners(std::move(owners));viewer_->set_transient_edges({});viewer_->set_transient_points({});viewer_->set_transient_labels({});
        // Initial reference state is needed before showing the editor. Its
        // first visible scene is published below, after fields can be offered.
        if(!dialog->isVisible())return;
        preserve_view_on_refresh_=true;refresh_scene();
        if(!pending_construction_reference_index_)set_construction_properties_dimension_selection();
    });
    track_tree_edit(dialog);viewer_->set_editing_origin_visible(true);
    connect(dialog,&QObject::destroyed,this,[this,guard]{
        general_surface_curve_preview_={};general_surface_curve_annotation_placement_.reset();construction_reference_dialog_=nullptr;curve_axis_dialog_=nullptr;
        pending_curve_axis_index_.reset();pending_construction_reference_index_.reset();construction_reference_auto_advance_=false;
        construction_preview_mesh_.reset();construction_parameter_preview_.reset();construction_reference_geometry_={};construction_dimension_object_id_.clear();
        local_origin_selection_dialog_=nullptr;local_origin_selection_active_=false;visible_local_origin_ids_.clear();visible_occurrence_origin_paths_.clear();selectable_local_origin_container_ids_.clear();
        viewer_->set_feature_preview_owners({});viewer_->set_editing_origin_visible(false);viewer_->set_constraint_reference_highlights({},{});
        viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->clear_selection();tree_->setProperty("commandSelectionActive",false);
        properties_dialog_=guard;primitive_reference_dialog_=guard;
        if(guard){guard->show();guard->raise();guard->changed();}
    });
    bind_local_origin_selection(dialog);
    dialog->show();preserve_view_on_refresh_=true;refresh_scene();
    if(dialog->first_empty_position_index()<3)start_construction_reference_selection(dialog->first_empty_position_index(),true);
}
} // namespace zima::app
