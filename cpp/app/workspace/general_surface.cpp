#include "workspace_internal.hpp"
#include "../general_surface_dialog.hpp"
#include "../sketch_properties_dialog.hpp"
#include "../primitive_properties_dialog.hpp"
#include "../confirmed_face_hit.hpp"
#include <zima/document/general_surface.hpp>
#include <zima/document/solid_state_reference_views.hpp>
namespace zima::app {
using namespace workspace_detail;
void AssemblyWorkspaceWindow::show_general_surface_sketch_properties(GeneralSurfaceDialog* parent,unsigned stage) {
    if(properties_dialog_!=parent||stage>=parent->pending.general_surface.boundaries.size())return;
    const auto& boundary=parent->pending.general_surface.boundaries[stage];if(boundary.curve)return;
    QPointer<GeneralSurfaceDialog> guard(parent);
    auto draft=std::make_shared<document::GeneralSurfaceBoundary>(boundary);
    const auto sketch_id=draft->sketch_feature->feature.sketch_id;
    const auto source_geometry=primitive_reference_geometry_;
    const auto framed=[guard,stage,draft,source_geometry](const document::HistoryContainer& owned) {
        if(!guard||!draft->sketch_feature||owned.id!=draft->sketch_feature->id||
            owned.feature.type!=document::FeatureType::Sketch)throw std::invalid_argument("Invalid owned surface boundary.");
        auto feature=guard->pending;auto value=*draft;value.sketch_feature=owned;
        auto sketch=sketcher::Sketch::from_serialized(value.sketch_serialized);
        sketch.name=owned.name;sketch.plane_offset=owned.feature.profile_plane_offset;
        value.sketch_serialized=sketch.serialized();feature.general_surface.boundaries.at(stage)=std::move(value);
        document::resolve_general_surface_sketch(feature.general_surface.boundaries.at(stage),
            document::general_surface_boundary_reference_geometry(feature,stage,source_geometry));
        return feature;
    };
    auto* dialog=new PrimitivePropertiesDialog(*draft->sketch_feature,true,false,
        [guard,stage,framed](auto owned){if(guard){auto feature=framed(owned);
            guard->pending.general_surface.boundaries.at(stage)=std::move(feature.general_surface.boundaries.at(stage));guard->refresh();}},this);
    dialog->setObjectName("generalSurfaceSketchProperties");
    dialog->setProperty("retainForSketchEditing",true);
    dialog->findChild<QComboBox*>("featureType")->setEnabled(false);
    dialog->set_commit_required(true);
    dialog->set_profile_plane_selection(sketcher::Sketch::from_serialized(draft->sketch_serialized),[draft](auto plane,bool automatic){
        auto sketch=sketcher::Sketch::from_serialized(draft->sketch_serialized);sketch.plane=plane;sketch.plane_auto=automatic;draft->sketch_serialized=sketch.serialized();
    });
    // Keep the common picker and placement solver, adapting only this owned
    // editor's initial face-click position from the scene to its parent frame.
    const auto local_pick=[this,guard,dialog](const viewer::ViewerCandidate& candidate) {
        if(!guard||!pending_primitive_reference_index_||primitive_reference_dialog_!=dialog)return;
        if(*pending_primitive_reference_index_==0&&dialog->references_without(0).empty()&&
            candidate.kind==viewer::CandidateKind::Face) {
            if(const auto ray=viewer_->ray_at(viewer_->last_pointer_position()))
            if(auto hit=confirmed_face_hit(viewer_->candidate_face_triangles(candidate),ray->first,ray->second)) {
                const auto path=assembly::InstancePath::decode(workspace_.active_occurrence_path());
                if(!path.occurrence_ids.empty())*hit=workspace_.occurrence_point_from_scene(workspace_.displayed_document_id(),path,*hit);
                if(const auto* part=workspace_.open_part(workspace_.active_document_id())) {
                    const auto& doc=part->session.document();auto* body=doc.body_owner_for_object(guard->pending.id);
                    if(!body)body=doc.body_history.find(doc.body_history.active_body_id());
                    if(body)*hit=container_dimension_frame(body->scope.placement).inverse_point(*hit);
                }
                *hit=container_dimension_frame(guard->pending.placement).inverse_point(*hit);
                const auto seed=dialog->placement_seed();
                if(!seed.value_locks.contains("x"))dialog->set_inline_parameter_value("placement:x",hit->x);
                if(!seed.value_locks.contains("y"))dialog->set_inline_parameter_value("placement:y",hit->y);
                if(!seed.value_locks.contains("z"))dialog->set_inline_parameter_value("placement:z",hit->z);
            }
        }
        accept_primitive_reference(candidate,false);
    };
    dialog->set_edit_sketch_callback([this,dialog,guard,stage,framed,draft,local_pick](auto owned) {
        if(!guard)return;
        auto feature=framed(owned);
        *draft=feature.general_surface.boundaries.at(stage);
        sweep_profile_sketch_draft_=document::general_surface_display_sketch(feature,*draft);
        helical_sketch_context_=document::general_surface_definition_mesh(feature);
        const auto id=sweep_profile_sketch_draft_->id;
        std::erase_if(helical_sketch_context_->edges,[&](const auto& edge){return edge.reference.owner_id==id;});
        std::erase_if(helical_sketch_context_->points,[&](const auto& point){return point.reference.owner_id==id;});
        QPointer<PrimitivePropertiesDialog> sketch_guard(dialog);
        embedded_sketch_finished_=[this,sketch_guard,draft,local_pick](auto sketch) {
            helical_sketch_context_.reset();if(!sketch_guard)return;
            draft->sketch_serialized=sketch.serialized();
            properties_dialog_=sketch_guard;sketch_guard->show();sketch_guard->raise();
            primitive_reference_dialog_=sketch_guard;
            feature_reference_pick_=local_pick;
            sketch_guard->refresh_sketch_preview();
        };
        viewer_->set_transient_edges({});viewer_->set_transient_points({});viewer_->set_transient_labels({});
        feature_reference_pick_={};
        dialog->hide();properties_dialog_=nullptr;active_sketch_id_=id;selected_sketch_id_=id;
        clear_selected_sketch_geometry();viewer_->clear_selection();tree_->clearSelection();
        preserve_view_on_refresh_=true;refresh_scene();align_active_sketch_view();
        state_->setText(tr("Nakreslete geometrii a potom zvolte Dokončit skicu."));
    });
    parent->set_active_reference_index(std::nullopt);parent->clear_reference_highlights();
    pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
    feature_reference_pick_=local_pick;feature_reference_end_={};set_local_origin_selection_mode(false);
    local_origin_selection_dialog_=nullptr;primitive_reference_dialog_=dialog;
    primitive_origin_preview_mesh_.reset();parameter_dimension_preview_.reset();
    viewer_->set_constraint_reference_highlights({},{});viewer_->clear_selection();
    parent->hide();properties_dialog_=dialog;primitive_parameter_owner_id_=draft->sketch_feature->id;track_tree_edit(dialog);
    dialog->set_reference_request_callback([this](std::size_t i){start_primitive_reference_selection(i);});
    dialog->set_reference_highlights_changed_callback([this,dialog]{viewer_->set_constraint_reference_highlights({},highlighted_reference_edge_keys(*dialog));});
    connect(dialog,&QObject::destroyed,this,[this,guard,sketch_id,source_geometry] {
        feature_reference_pick_={};feature_reference_end_={};
        helical_sketch_context_.reset();
        if(active_sketch_id_==sketch_id)active_sketch_id_.clear();
        if(selected_sketch_id_==sketch_id)selected_sketch_id_.clear();
        properties_dialog_=guard;primitive_reference_dialog_=guard;
        primitive_reference_geometry_=source_geometry;pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
        set_local_origin_selection_mode(false);local_origin_selection_dialog_=nullptr;
        viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->clear_selection();viewer_->set_constraint_reference_highlights({},{});
        if(guard)primitive_parameter_owner_id_=guard->pending.id;
        if(guard){guard->show();guard->raise();guard->changed();}
    });
    dialog->set_preview_callback([this,guard,dialog,framed,stage,source_geometry](const auto& owned) {
        if(!guard||!dialog->isVisible())return;
        auto feature=framed(owned);const auto& placement=feature.general_surface.boundaries.at(stage).sketch_feature->placement;
        primitive_reference_geometry_=document::general_surface_boundary_reference_geometry(feature,stage,source_geometry);
        const kernel::Vec3 point{placement.x,placement.y,placement.z};kernel::Vec3 base;
        auto resolved=placement;static_cast<void>(document::resolve_placement(resolved,primitive_reference_geometry_,&base));
        dialog->set_translation_constraint_state(document::point_constraint_state(placement.references,primitive_reference_geometry_,point),point);
        dialog->set_rotation_constraint_state(document::orientation_constraint_state(placement.references,primitive_reference_geometry_,true,point));
        dialog->set_orientation_base_rotation(base,std::ranges::any_of(placement.references,[](const auto& ref){return ref.orientation_drives_rotation;}));
        dialog->set_resolved_rotation({placement.rotation_x,placement.rotation_y,placement.rotation_z},placement.reference_valid);
        document::PartDocument origins;
        document::ConstructionObject origin;origin.kind=document::ConstructionKind::Point;
        const auto& value=*feature.general_surface.boundaries.at(stage).sketch_feature;
        origin.id=value.id;origin.entity_id=value.feature_id;origin.container_origin=value.container_origin;origin.name=value.name;
        origin.origin=point;origin.rotation={placement.rotation_x,placement.rotation_y,placement.rotation_z};origin.reference_valid=false;
        origins.constructions={origin};
        primitive_origin_preview_mesh_=document::general_surface_place_definition_mesh(feature.placement,origins.construction_viewer_mesh(value.id));
        origin.id=feature.id;origin.entity_id=feature.feature_id;origin.container_origin=feature.container_origin;origin.name=feature.name;
        origin.origin={feature.placement.x,feature.placement.y,feature.placement.z};
        origin.rotation={feature.placement.rotation_x,feature.placement.rotation_y,feature.placement.rotation_z};
        origins.constructions={origin};auto parent_origin=origins.construction_viewer_mesh(feature.id);
        append_reference_geometry(primitive_origin_preview_mesh_->original_references,parent_origin.original_references);
        primitive_origin_preview_mesh_->edges.insert(primitive_origin_preview_mesh_->edges.end(),parent_origin.edges.begin(),parent_origin.edges.end());
        primitive_origin_preview_mesh_->points.insert(primitive_origin_preview_mesh_->points.end(),parent_origin.points.begin(),parent_origin.points.end());
        primitive_origin_preview_mesh_->axes.insert(primitive_origin_preview_mesh_->axes.end(),parent_origin.axes.begin(),parent_origin.axes.end());
        viewer_->set_feature_preview_owners({value.id,value.feature_id,value.container_origin.id});
        preserve_view_on_refresh_=true;refresh_scene();
        auto mesh=document::general_surface_definition_mesh(feature);
        if(const auto* part=workspace_.open_part(workspace_.active_document_id()))
            if(const auto* body=part->session.document().body_owner_for_object(feature.id))mesh=part->session.document().place_body_mesh(std::move(mesh),body->scope.id);
            else if(const auto* body=part->session.document().body_history.find(part->session.document().body_history.active_body_id()))mesh=part->session.document().place_body_mesh(std::move(mesh),body->scope.id);
        for(auto& edge:mesh.edges) {
            edge.reference.instance_path=properties_dialog_instance_path_;
            if(!properties_dialog_instance_path_.empty())for(auto& point:edge.points)point=workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),assembly::InstancePath::decode(properties_dialog_instance_path_),point);
        }
        viewer_->set_transient_edges(std::move(mesh.edges));
    });
    bind_local_origin_selection(dialog);
    dialog->show();dialog->refresh_sketch_preview();
}
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
        },this,document_decimal_places(part->session.document()),true);
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
        construction_reference_geometry_=document::general_surface_boundary_reference_geometry(*resolved,stage,geometry);
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
    dialog->show();preserve_view_on_refresh_=true;refresh_scene();
}
} // namespace zima::app
