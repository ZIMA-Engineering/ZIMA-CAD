#include "workspace_internal.hpp"
#include "../general_surface_dialog.hpp"
#include "../loft_dialog.hpp"
#include <zima/document/loft.hpp>
#include "../sketch_properties_dialog.hpp"
#include "../primitive_properties_dialog.hpp"
#include "../feature_parameter_panel.hpp"
#include "../confirmed_face_hit.hpp"
#include <zima/document/general_surface.hpp>
#include <zima/document/solid_state_reference_views.hpp>
#include <zima/document/sketch_placement.hpp>
namespace zima::app {
using namespace workspace_detail;
bool AssemblyWorkspaceWindow::is_owned_sketch_reference(const viewer::ViewerCandidate& candidate) const {
    if(!sweep_profile_sketch_draft_||sweep_profile_sketch_draft_->id!=active_sketch_id_||
       (!candidate.instance_path.empty()&&candidate.instance_path!=workspace_.active_occurrence_path()))return false;
    const auto matches=[&](const auto& value){return value.reference.owner_id==candidate.owner_id&&
        value.reference.semantic_key==candidate.semantic_key;};
    return std::ranges::any_of(owned_sketch_reference_geometry_.edges,matches)||
        std::ranges::any_of(owned_sketch_reference_geometry_.points,matches)||
        std::ranges::any_of(owned_sketch_reference_geometry_.axes,matches);
}
bool AssemblyWorkspaceWindow::is_loft_profile_point_reference(const viewer::ViewerCandidate& candidate) const {
    if(!sweep_profile_sketch_draft_||sweep_profile_sketch_draft_->id!=active_sketch_id_||
       (!candidate.instance_path.empty()&&candidate.instance_path!=workspace_.active_occurrence_path()))return false;
    return std::ranges::any_of(loft_profile_reference_geometry_.points,[&](const auto& point){
        return point.reference.owner_id==candidate.owner_id&&point.reference.semantic_key==candidate.semantic_key;
    });
}
void AssemblyWorkspaceWindow::show_general_surface_sketch_properties(GeneralSurfaceDialog* parent,unsigned stage) {
    show_owned_sketch_properties(parent,stage);
}
void AssemblyWorkspaceWindow::show_owned_sketch_properties(SweepPlacementDialog* parent,unsigned stage) {
    QPointer<SweepPlacementDialog> guard(parent);
    show_owned_sketch_properties(parent,parent->pending,stage,[guard] {
        if(auto* loft=dynamic_cast<LoftDialog*>(guard.data()))loft->refresh();
        else if(auto* surface=dynamic_cast<GeneralSurfaceDialog*>(guard.data()))surface->refresh();
    },[guard]{if(guard&&guard->changed)guard->changed();});
}
void AssemblyWorkspaceWindow::show_owned_sketch_properties(ui::PropertiesSubWindow* parent,
        document::HistoryContainer& definition,unsigned stage,
        std::function<void()> refresh,std::function<void()> resume) {
    auto* pending=&definition;
    const auto boundaries=[](auto& feature)->auto& {return feature.feature_kind==document::FeatureKind::Loft?feature.loft.sketches:feature.general_surface.boundaries;};
    const auto carrier=[](const auto& feature){return feature.feature_kind==document::FeatureKind::Loft?document::loft_sketch_editor_carrier(feature):feature;};

    if(properties_dialog_!=parent||stage>=boundaries((*pending)).size())return;
    const auto& boundary=boundaries((*pending))[stage];if(boundary.curve)return;
    QPointer<ui::PropertiesSubWindow> guard(parent);
    auto draft=std::make_shared<document::GeneralSurfaceBoundary>(boundary);
    const auto sketch_id=draft->sketch_feature->feature.sketch_id;
    const auto source_geometry=primitive_reference_geometry_;
    const auto framed=[guard,pending,stage,draft,source_geometry,carrier](const document::HistoryContainer& owned) {
        if(!guard||!draft->sketch_feature||owned.id!=draft->sketch_feature->id||
            owned.feature.type!=document::FeatureType::Sketch)throw std::invalid_argument("Invalid owned surface boundary.");
        auto feature=carrier((*pending));auto value=*draft;value.sketch_feature=owned;
        auto sketch=sketcher::Sketch::from_serialized(value.sketch_serialized);
        sketch.name=owned.name;sketch.plane_offset=owned.feature.profile_plane_offset;
        value.sketch_serialized=sketch.serialized();feature.general_surface.boundaries.at(stage)=std::move(value);
        document::resolve_general_surface_sketch(feature.general_surface.boundaries.at(stage),
            document::general_surface_boundary_reference_geometry(feature,stage,source_geometry));
        return feature;
    };
    auto* dialog=new PrimitivePropertiesDialog(*draft->sketch_feature,true,false,
        [guard,pending,stage,framed,boundaries,refresh](auto owned){if(guard){auto feature=framed(owned);
            boundaries((*pending)).at(stage)=std::move(feature.general_surface.boundaries.at(stage));refresh();}},this);
    dialog->setObjectName("generalSurfaceSketchProperties");
    dialog->setProperty("retainForSketchEditing",true);
    dialog->findChild<QComboBox*>("featureType")->setEnabled(false);
    dialog->set_commit_required(true);
    dialog->set_profile_plane_selection(sketcher::Sketch::from_serialized(draft->sketch_serialized),[draft](auto plane,bool automatic){
        auto sketch=sketcher::Sketch::from_serialized(draft->sketch_serialized);sketch.plane=plane;sketch.plane_auto=automatic;draft->sketch_serialized=sketch.serialized();
    });
    // Keep the common picker and placement solver, adapting only this owned
    // editor's initial face-click position from the scene to its parent frame.
    const auto local_pick=[this,guard,pending,dialog](const viewer::ViewerCandidate& candidate) {
        if(!guard||!pending_primitive_reference_index_||primitive_reference_dialog_!=dialog)return;
        if(*pending_primitive_reference_index_==0&&dialog->references_without(0).empty()&&
            candidate.kind==viewer::CandidateKind::Face) {
            if(const auto ray=viewer_->ray_at(viewer_->last_pointer_position()))
            if(auto hit=confirmed_face_hit(viewer_->candidate_face_triangles(candidate),ray->first,ray->second)) {
                const auto path=assembly::InstancePath::decode(workspace_.active_occurrence_path());
                if(!path.occurrence_ids.empty())*hit=workspace_.occurrence_point_from_scene(workspace_.displayed_document_id(),path,*hit);
                if(const auto* part=workspace_.open_part(workspace_.active_document_id())) {
                    const auto& doc=part->session.document();auto* body=doc.body_owner_for_object((*pending).id);
                    if(!body)body=doc.body_history.find(doc.body_history.active_body_id());
                    if(body)*hit=container_dimension_frame(body->scope.placement).inverse_point(*hit);
                }
                *hit=container_dimension_frame((*pending).placement).inverse_point(*hit);
                const auto seed=dialog->placement_seed();
                if(!seed.value_locks.contains("x"))dialog->set_inline_parameter_value("placement:x",hit->x);
                if(!seed.value_locks.contains("y"))dialog->set_inline_parameter_value("placement:y",hit->y);
                if(!seed.value_locks.contains("z"))dialog->set_inline_parameter_value("placement:z",hit->z);
            }
        }
        accept_primitive_reference(candidate,false);
    };
    dialog->set_edit_sketch_callback([this,dialog,guard,pending,stage,framed,draft,local_pick](auto owned) {
        if(!guard)return;
        // Placement entry belongs to Properties, not to the embedded Sketcher.
        // In particular, automatic next-reference entry after one support must
        // not consume the endpoints subsequently selected while drawing.
        static_cast<void>(finish_active_reference_selection());
        set_local_origin_selection_mode(false);
        primitive_reference_dialog_=nullptr;
        auto feature=framed(owned);
        *draft=feature.general_surface.boundaries.at(stage);
        sweep_profile_sketch_draft_=document::general_surface_display_sketch(feature,*draft);
        helical_sketch_context_=document::general_surface_definition_mesh(feature);
        auto preceding=feature;preceding.general_surface.boundaries.resize(stage);
        owned_sketch_reference_geometry_=document::general_surface_definition_mesh(preceding).original_references;
        owned_sketch_reference_parent_=feature;
        // Inactive siblings are context, with their original identities and
        // exact supports retained separately for explicit reference entry.
        auto references=std::move(helical_sketch_context_->original_references);
        std::set<std::string> curve_owners;
        for(const auto& boundary:feature.general_surface.boundaries)if(boundary.curve) {
            curve_owners.insert(boundary.curve->id);curve_owners.insert(boundary.curve->entity_id);
        }
        std::vector<kernel::ViewerEdge> curves;
        std::vector<kernel::ViewerPoint> curve_points;
        for(const auto& edge:helical_sketch_context_->edges)
            if(curve_owners.contains(edge.reference.owner_id)&&edge.reference.semantic_key.starts_with("curve:"))curves.push_back(edge);
        for(const auto& point:helical_sketch_context_->points)
            if(curve_owners.contains(point.display_owner_id)&&point.reference.semantic_key=="point")curve_points.push_back(point);
        keep_only_inactive_sketch_profile(*helical_sketch_context_,{});
        helical_sketch_context_->original_references=std::move(references);
        for(auto& edge:helical_sketch_context_->edges)edge.display_owner_id=edge.reference.owner_id;
        helical_sketch_context_->edges.insert(helical_sketch_context_->edges.end(),curves.begin(),curves.end());
        helical_sketch_context_->points.insert(helical_sketch_context_->points.end(),curve_points.begin(),curve_points.end());
        loft_profile_reference_geometry_={};
        if((*pending).feature_kind==document::FeatureKind::Loft) {
            // Other owned Sketches are read-only profile context. Their
            // construction/reference aids belong only to their own editor,
            // just as for ordinary inactive Part Sketches. Retain the exact
            // source packet for reference resolution, without drawing its aids.
            auto loft=(*pending);loft.loft.sketches=feature.general_surface.boundaries;
            const auto anchors=document::loft_profile_anchor_mesh(loft,stage);
            loft_profile_reference_geometry_=anchors.original_references;
            for(const auto& point:anchors.points) {
                std::erase_if(helical_sketch_context_->points,[&](const auto& old){return old.reference==point.reference;});
                helical_sketch_context_->points.push_back(point);
            }
        }
        const auto id=sweep_profile_sketch_draft_->id;
        std::erase_if(helical_sketch_context_->edges,[&](const auto& edge){return edge.reference.owner_id==id;});
        std::erase_if(helical_sketch_context_->points,[&](const auto& point){return point.reference.owner_id==id;});
        QPointer<PrimitivePropertiesDialog> sketch_guard(dialog);
        embedded_sketch_finished_=[this,sketch_guard,draft,local_pick](auto sketch) {
            helical_sketch_context_.reset();loft_profile_reference_geometry_={};owned_sketch_reference_geometry_={};owned_sketch_reference_parent_.reset();if(!sketch_guard)return;
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
    if(auto* placement=dynamic_cast<PlacementReferenceDialog*>(parent)) {
        placement->set_active_reference_index(std::nullopt);placement->clear_reference_highlights();
    }
    pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
    feature_reference_pick_=local_pick;feature_reference_end_={};set_local_origin_selection_mode(false);
    local_origin_selection_dialog_=nullptr;primitive_reference_dialog_=dialog;
    primitive_origin_preview_mesh_.reset();parameter_dimension_preview_.reset();
    viewer_->set_constraint_reference_highlights({},{});viewer_->clear_selection();
    parent->hide();properties_dialog_=dialog;primitive_parameter_owner_id_=draft->sketch_feature->id;track_tree_edit(dialog);
    dialog->set_reference_request_callback([this](std::size_t i){start_primitive_reference_selection(i);});
    dialog->set_reference_highlights_changed_callback([this,dialog]{viewer_->set_constraint_reference_highlights({},highlighted_reference_edge_keys(*dialog));});
    connect(dialog,&QObject::destroyed,this,[this,guard,pending,sketch_id,source_geometry,resume] {
        feature_reference_pick_={};feature_reference_end_={};
        owned_sketch_origin_ids_.clear();
        helical_sketch_context_.reset();
        loft_profile_reference_geometry_={};
        owned_sketch_reference_geometry_={};
        owned_sketch_reference_parent_.reset();
        if(active_sketch_id_==sketch_id)active_sketch_id_.clear();
        if(selected_sketch_id_==sketch_id)selected_sketch_id_.clear();
        properties_dialog_=guard;primitive_reference_dialog_=dynamic_cast<PlacementReferenceDialog*>(guard.data());
        primitive_reference_geometry_=source_geometry;pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
        set_local_origin_selection_mode(false);local_origin_selection_dialog_=nullptr;
        viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->clear_selection();viewer_->set_constraint_reference_highlights({},{});
        if(guard)primitive_parameter_owner_id_=(*pending).id;
        if(guard){guard->show();guard->raise();resume();}
    });
    dialog->set_preview_callback([this,guard,pending,dialog,framed,stage,source_geometry](const auto& owned) {
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
        {
            // Reuse the ordinary Plane presentation from the resolved owned
            // Sketch frame, including the chosen plane and its offset. The
            // same frame is handed to Sketcher; no second placement solve or
            // persistent construction is introduced by this display carrier.
            const auto sketch=sketcher::Sketch::from_serialized(feature.general_surface.boundaries.at(stage).sketch_serialized);
            const auto axes=document::sketch_container_frame_axes(sketch);
            origin.kind=document::ConstructionKind::Plane;
            origin.base_plane=sketch.plane==sketcher::SketchPlane::XY?document::LocalDatumPlane::XY:
                sketch.plane==sketcher::SketchPlane::XZ?document::LocalDatumPlane::XZ:document::LocalDatumPlane::YZ;
            origin.rotation=euler_degrees_from_frame_columns(axes[0],axes[1],axes[2]);origin.absolute_rotation=origin.rotation;
            origin.direction=sketch.resolved_normal;origin.entity_origin=sketch.resolved_origin;origin.offset=sketch.plane_offset;
            origin.origin={sketch.resolved_origin.x-sketch.resolved_normal.x*sketch.plane_offset,
                sketch.resolved_origin.y-sketch.resolved_normal.y*sketch.plane_offset,
                sketch.resolved_origin.z-sketch.resolved_normal.z*sketch.plane_offset};
            origin.reference_valid=placement.reference_valid;
        }
        origins.constructions={origin};
        primitive_origin_preview_mesh_=document::general_surface_place_definition_mesh(feature.placement,origins.construction_viewer_mesh(value.id));
        origin.kind=document::ConstructionKind::Point;origin.reference_valid=false;origin.offset=0;
        origin.id=feature.id;origin.entity_id=feature.feature_id;origin.container_origin=feature.container_origin;origin.name=feature.name;
        origin.origin={feature.placement.x,feature.placement.y,feature.placement.z};
        origin.rotation={feature.placement.rotation_x,feature.placement.rotation_y,feature.placement.rotation_z};
        origins.constructions={origin};auto parent_origin=origins.construction_viewer_mesh(feature.id);
        append_reference_geometry(primitive_origin_preview_mesh_->original_references,parent_origin.original_references);
        primitive_origin_preview_mesh_->edges.insert(primitive_origin_preview_mesh_->edges.end(),parent_origin.edges.begin(),parent_origin.edges.end());
        primitive_origin_preview_mesh_->points.insert(primitive_origin_preview_mesh_->points.end(),parent_origin.points.begin(),parent_origin.points.end());
        primitive_origin_preview_mesh_->axes.insert(primitive_origin_preview_mesh_->axes.end(),parent_origin.axes.begin(),parent_origin.axes.end());
        // These origins belong to preceding children of the draft parent;
        // they do not exist as top-level history containers during creation.
        auto sibling_origins=local_origin_display_mesh(primitive_reference_geometry_,visible_local_origin_ids_);
        append_mesh(*primitive_origin_preview_mesh_,document::general_surface_place_definition_mesh(feature.placement,std::move(sibling_origins)));
        std::optional<kernel::ViewerMesh> loft_preview;
        if((*pending).feature_kind==document::FeatureKind::Loft) {
            auto loft=(*pending);loft.loft.sketches=feature.general_surface.boundaries;
            append_mesh(*primitive_origin_preview_mesh_,document::loft_profile_anchor_mesh(loft,stage));
            if(const auto* live=workspace_.open_part(workspace_.active_document_id())) {
                loft_preview=document::loft_definition_mesh(live->session.document(),loft,source_geometry);
                primitive_origin_preview_mesh_->dimensions=loft_preview->dimensions;
            }
        }
        viewer_->set_feature_preview_owners({value.id,value.feature_id,value.container_origin.id});
        preserve_view_on_refresh_=true;refresh_scene();
        // Owned-profile Properties uses the parent's authored preview too;
        // the raw Sketch editor packet also contains read-only reference lines.
        auto mesh=loft_preview?std::move(*loft_preview):document::general_surface_definition_mesh(feature);
        if(const auto* part=workspace_.open_part(workspace_.active_document_id()))
            if(const auto* body=part->session.document().body_owner_for_object(feature.id))mesh=part->session.document().place_body_mesh(std::move(mesh),body->scope.id);
            else if(const auto* body=part->session.document().body_history.find(part->session.document().body_history.active_body_id()))mesh=part->session.document().place_body_mesh(std::move(mesh),body->scope.id);
        for(auto& edge:mesh.edges) {
            edge.reference.instance_path=properties_dialog_instance_path_;
            if(!properties_dialog_instance_path_.empty())for(auto& point:edge.points)point=workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),assembly::InstancePath::decode(properties_dialog_instance_path_),point);
        }
        viewer_->set_transient_edges(std::move(mesh.edges));
    });
    // Label validation runs immediately when the shared rows are bound. The
    // owned editor must already expose its pending parent's local datum;
    // the outer scene cannot resolve references to an uncommitted Loft.
    primitive_reference_geometry_=document::general_surface_boundary_reference_geometry(carrier((*pending)),stage,source_geometry);
    owned_sketch_origin_ids_.clear();
    for(std::size_t i=0;i<stage;++i)if(const auto& owned=boundaries(*pending)[i].sketch_feature) {
        for(const auto& id:{owned->id,owned->feature_id,owned->feature.sketch_id})
            owned_sketch_origin_ids_.emplace(id,owned->container_origin.id);
    }
    connect(dialog,&QDialog::finished,this,[this]{owned_sketch_origin_ids_.clear();});
    bind_local_origin_selection(dialog);
    {
        // The automatically inherited parent frame is one attachment bundle.
        // View-plane authoring replaces that bundle only after a confirmed
        // pick; merely arming or cancelling entry must retain all three rows.
        auto* plane_input=dialog->findChild<QTableWidget*>("featureProfilePlaneView");
        auto* plane_choice=dialog->findChild<QComboBox*>("featureProfilePlane");
        const auto end_plane_entry=[this,dialog,plane_input,local_pick] {
            // Default Origin may end entry without changing the plane combo.
            // Retire both its visual and command state before restoring picks.
            if(auto* selector=dynamic_cast<WorkPlaneSelector*>(plane_input->parentWidget()))
                selector->set_entry_active(false);
            feature_reference_end_={};feature_reference_pick_=local_pick;
            pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
            dialog->set_active_reference_index({});dialog->clear_reference_highlights();
            set_primitive_properties_dimension_selection();
        };
        const auto selected_plane=[this,dialog](const viewer::ViewerCandidate& offered)->std::optional<document::ConstructionReference> {
            const auto candidate=placement_reference_source_candidate(offered);
            if((candidate.kind!=viewer::CandidateKind::Plane&&candidate.kind!=viewer::CandidateKind::Face)||
               dialog->owns_reference_owner(candidate.owner_id)||!placement_origin_allowed(candidate.owner_id)||
               !placement_reference_candidate_has_stable_geometry(candidate))return {};
            if(candidate.instance_path!=properties_dialog_instance_path_)return {};
            document::ConstructionReference reference{{},candidate.owner_id,candidate.semantic_key,0.,candidate_supports_offset(candidate)};
            if(!document::PartDocument::sweep2d_accepts_path_plane(reference,primitive_reference_geometry_))return {};
            return reference;
        };
        connect(plane_choice,&QComboBox::currentIndexChanged,dialog,[this,end_plane_entry] {
            if(feature_reference_end_)end_plane_entry();
        });
        for(auto* widget:dialog->findChildren<QWidget*>())if(auto* panel=dynamic_cast<FeatureParameterPanel*>(widget))
            panel->on_work_plane_reference([this,dialog,guard,pending,plane_input,plane_choice,local_pick,end_plane_entry,selected_plane] {
                if(!guard)return;
                const auto references=dialog->pending_value().placement.references;
                const bool inherited=document::placement_references_use_whole_origin(references)&&
                    std::ranges::all_of(references,[&](const auto& ref){return ref.owner_id==(*pending).container_origin.id;});
                if(!inherited){start_primitive_reference_selection(0);return;}
                pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
                dialog->set_active_reference_index({});dialog->clear_reference_highlights();
                if(auto* item=dynamic_cast<ui::ReferenceCellItem*>(plane_input->item(0,1)))item->set_active_input(true);
                plane_input->viewport()->update();tree_->setProperty("commandSelectionActive",true);
                viewer_->set_original_face_selection(false);
                viewer_->set_selection_contract({viewer::CandidateKind::Plane,viewer::CandidateKind::Face});
                viewer_->set_candidate_filter([selected_plane](const auto& candidate){return selected_plane(candidate).has_value();});
                feature_reference_end_=[end_plane_entry]{auto end=end_plane_entry;end();};
                feature_reference_pick_=[this,dialog,plane_choice,local_pick,end_plane_entry,selected_plane](const auto& offered) {
                    const auto reference=selected_plane(offered);if(!reference)return;
                    const auto original=dialog->pending_value().placement;const int original_choice=plane_choice->currentIndex();
                    for(auto* child:dialog->findChildren<QObject*>())if(auto* section=dynamic_cast<ui::ContainerPlacementSection*>(child)) {
                        section->initialize_from_references({},{});
                        plane_choice->setCurrentIndex(plane_choice->findData(QStringLiteral("auto")));
                        dialog->refresh_sketch_preview();start_primitive_reference_selection(0);
                        local_pick(offered);
                        const auto entered=dialog->pending_value().placement.references;
                        if(entered.empty()||entered.front().owner_id!=reference->owner_id||entered.front().semantic_key!=reference->semantic_key) {
                            section->initialize_from_references(original.references,{});section->initialize_numeric_values(original);
                            plane_choice->setCurrentIndex(original_choice);dialog->refresh_sketch_preview();
                        }else viewer_->remember_reference_face(offered);
                        break;
                    }
                    end_plane_entry();
                };
                state_->setText(tr("Vyberte stabilní geometrii pro umístění kontejneru."));
            });
        dialog->set_reference_request_callback([this,end_plane_entry](std::size_t index){end_plane_entry();start_primitive_reference_selection(index);});
        // Initialize the shared rows from the owned Sketch parent datum.
        // The outer draft owns the parent; the child is not a Body-level feature.
        auto* button=dialog->findChild<QPushButton*>("containerDefaultOriginButton");
        if(button) {
            QObject::disconnect(button,&QPushButton::clicked,nullptr,nullptr);
            button->setIcon(resource_icon("origin-document"));
            connect(button,&QPushButton::clicked,this,[this,guard,pending,dialog,end_plane_entry] {
                if(!guard||primitive_reference_dialog_!=dialog)return;
                end_plane_entry();
                set_local_origin_selection_mode(false);pending_primitive_reference_index_.reset();
                dialog->set_active_reference_index({});dialog->clear_reference_highlights();
                const auto definition=document::create_loft_sketch((*pending));
                for(auto* object:dialog->findChildren<QObject*>())
                    if(auto* section=dynamic_cast<ui::ContainerPlacementSection*>(object))
                        section->initialize_from_references(definition.sketch_feature->placement.references,
                            [](const auto& key){return QString::fromStdString(key);});
                dialog->refresh_sketch_preview();set_primitive_properties_dimension_selection();viewer_->clear_selection();
            });
        }
    }
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
