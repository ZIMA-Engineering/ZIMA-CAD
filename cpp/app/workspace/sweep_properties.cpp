#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::transform_sketch_container(
    const std::string& container_id,
    zima::document::FeatureKind target_kind) {
    using zima::document::FeatureKind;
    if (properties_dialog_ != nullptr ||
        (target_kind != FeatureKind::Extrusion &&
         target_kind != FeatureKind::Revolution)) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto* source = part->session.document().find_container(container_id);
    if (source == nullptr || source->feature_kind != FeatureKind::Sketch) return;
    const auto sketch = std::find_if(part->session.document().sketches.begin(),
        part->session.document().sketches.end(), [&](const auto& value) {
            return value.owner_container_id == source->id;
        });
    if (sketch == part->session.document().sketches.end()) return;

    auto draft = target_kind == FeatureKind::Extrusion
        ? zima::document::PartDocument::create_extrusion_container(sketch->id)
        : zima::document::PartDocument::create_revolution_container(sketch->id);
    // Container identity, history position and placement belong to the
    // existing Sketch container.  Only its feature definition is replaced.
    draft.id = source->id;
    draft.feature_parent_id = source->id;
    draft.container_origin = source->container_origin;
    draft.placement = source->placement;
    draft.suppressed = source->suppressed;
    draft.combine_mode = source->combine_mode;
    if (target_kind == FeatureKind::Extrusion) {
        draft.extrusion.profile_plane_offset = sketch->plane_offset;
    } else {
        draft.revolution.profile_plane_offset = sketch->plane_offset;
        const auto axis_count = std::count_if(sketch->segments.begin(),
            sketch->segments.end(), [](const auto& segment) {
                return segment.construction && segment.centerline;
            });
        // A missing/ambiguous axis is intentionally allowed at this point:
        // Properties opens first and its SKETCH action is how the user fixes
        // the profile before OK performs strict Revolution validation.
        if (axis_count == 1) {
            draft.revolution.axis_segment_id = revolution_axis_segment_id(
                *sketch, draft.revolution.axis_segment_id);
        }
    }
    pending_profile_transform_original_ = *source;
    pending_profile_feature_ = std::move(draft);
    show_primitive_properties(target_kind, container_id);
    // A failed dialog launch must not leave a hidden armed transform.
    if (properties_dialog_ == nullptr) {
        pending_profile_feature_.reset();
        pending_profile_transform_original_.reset();
    }
}

void AssemblyWorkspaceWindow::show_sweep2d_properties(const std::string& id) {
    show_sweep_properties(zima::document::FeatureKind::Sweep2D,id);
}
void AssemblyWorkspaceWindow::show_helical_sweep_properties(const std::string& id) {
    show_sweep_properties(zima::document::FeatureKind::HelicalSweep,id);
}
void AssemblyWorkspaceWindow::show_sweep_properties(zima::document::FeatureKind kind,const std::string& id) {
    if(properties_dialog_||!active_sketch_id_.empty())return;
    auto* part=workspace_.open_part(workspace_.active_document_id());if(!part)return;
    const auto occurrence=resolve_active_occurrence(part->session.document().document_id);if(!occurrence)return;
    const bool planar=kind==zima::document::FeatureKind::Sweep2D;
    auto initial=planar?zima::document::PartDocument::create_sweep2d_container():zima::document::PartDocument::create_helical_sweep_container();
    if(!id.empty()){
        const auto* stored=part->session.document().find_container(id);if(!stored||stored->feature_kind!=kind)return;
        initial=*stored;const auto boundary=part->session.rollback_boundary(id);
        const auto index=part->session.document().history_index(id);
        if(!index)return;
        part_rollback_=PartRollbackContext{part->session.document().document_id,*occurrence,*index,
            boundary ? boundary->input_body : std::nullopt};
    }
    const auto document_id=part->session.document().document_id;
    const auto commit=[this,document_id,editing=!id.empty()](auto c){
        auto* target=workspace_.open_part(document_id);if(!target)throw std::runtime_error("Part není otevřen");
        auto next=target->session.document();
        if(editing){auto* stored=next.find_container(c.id);if(!stored)throw std::runtime_error("Kontejner neexistuje");*stored=std::move(c);}
        else{next.insert_history_entry(zima::document::PartHistoryKind::Feature,c.id);next.history.push_back(std::move(c));}
        auto calculated=calculate_part_with_resolved_references(next,&target->session.calculated_boundaries());
        target->session.commit(std::move(next),std::move(calculated));
    };
    SweepPlacementDialog* dialog=planar?static_cast<SweepPlacementDialog*>(new Sweep2DDialog(initial,commit,this)):
        static_cast<SweepPlacementDialog*>(new HelicalSweepDialog(initial,commit,this));
    properties_dialog_=dialog;track_tree_edit(dialog);properties_dialog_instance_path_=*occurrence;
    primitive_parameter_owner_id_=initial.id;primitive_reference_dialog_=dialog;

    auto geometry=part->session.calculated_boundaries().empty()?zima::kernel::ViewerReferenceGeometry{}:part->session.calculated_boundaries().back().mesh.original_references;
    const auto& source=part->session.document();
    append_reference_geometry(geometry,source.origin_viewer_mesh().original_references);
    append_reference_geometry(geometry,source.construction_viewer_mesh().original_references);
    append_reference_geometry(geometry,source.history_origin_reference_geometry_before(initial.id));
    const auto* owner_body=source.body_owner_for_object(initial.id);
    const auto body_id=owner_body ? owner_body->scope.id : source.body_history.active_body_id();
    if(!body_id.empty())geometry=source.construction_reference_geometry_for(body_id,std::move(geometry));
    std::set<std::string> excluded{initial.id,initial.feature_id,initial.container_origin.id};
    if(!source.history_order.empty()){
        auto limit=source.effective_history_cursor();
        if(!id.empty())for(std::size_t i=0;i<source.history_order.size();++i)if(source.history_order[i].id==id){limit=i;break;}
        for(std::size_t i=limit;i<source.history_order.size();++i)excluded.insert(source.history_order[i].id);
    }else if(!id.empty())for(std::size_t i=source.history_index(id).value_or(0);i<source.history.size();++i)excluded.insert(source.history[i].id);
    for(const auto& c:source.history)if(excluded.contains(c.id)){excluded.insert(c.feature_id);excluded.insert(c.container_origin.id);}
    for(const auto& c:source.constructions)if(excluded.contains(c.id)){excluded.insert(c.entity_id);excluded.insert(c.container_origin.id);}
    for(auto& ref:geometry.triangle_references)if(excluded.contains(ref.owner_id))ref={};
    std::erase_if(geometry.edges,[&](const auto& e){return excluded.contains(e.reference.owner_id);});
    std::erase_if(geometry.points,[&](const auto& p){return excluded.contains(p.reference.owner_id);});
    std::erase_if(geometry.axes,[&](const auto& a){return excluded.contains(a.reference.owner_id);});
    primitive_reference_geometry_=geometry;
    dialog->request_placement=[this,dialog](std::size_t i){
        feature_reference_pick_={};feature_reference_end_={};
        start_primitive_reference_selection(i);
    };
    if(auto* planar_dialog=dynamic_cast<Sweep2DDialog*>(dialog)) {
        planar_dialog->request_path_plane=[this,planar_dialog,geometry] {
            pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
            set_local_origin_selection_mode(false);planar_dialog->set_active_reference_index(std::nullopt);
            planar_dialog->set_path_active(true);
            auto path_geometry=geometry;
            if(primitive_origin_preview_mesh_)append_reference_geometry(path_geometry,primitive_origin_preview_mesh_->original_references);
            const auto accepts=[this,planar_dialog,path_geometry](const zima::viewer::ViewerCandidate& candidate) {
                const bool own_plane=candidate.owner_id==planar_dialog->pending.container_origin.id &&
                    (candidate.semantic_key=="origin:plane:xy"||candidate.semantic_key=="origin:plane:yz"||candidate.semantic_key=="origin:plane:xz");
                if(!placement_reference_candidate_has_stable_geometry(candidate)||
                    (candidate.kind!=zima::viewer::CandidateKind::Plane&&candidate.kind!=zima::viewer::CandidateKind::Face)||
                    candidate.instance_path!=properties_dialog_instance_path_||(!own_plane&&planar_dialog->owns_reference_owner(candidate.owner_id)))return false;
                return zima::document::PartDocument::sweep2d_accepts_path_plane(
                    {{},candidate.owner_id,candidate.semantic_key},path_geometry);
            };
            tree_->setProperty("commandSelectionActive",true);
            viewer_->set_selection_contract({zima::viewer::CandidateKind::Plane,zima::viewer::CandidateKind::Face});
            viewer_->set_candidate_filter(accepts);
            feature_reference_pick_=[this,planar_dialog,accepts](const zima::viewer::ViewerCandidate& candidate) {
                if(!accepts(candidate))return;
                feature_reference_pick_={};feature_reference_end_={};viewer_->clear_selection();tree_->clearSelection();
                const auto label=candidate.owner_id==planar_dialog->pending.container_origin.id
                    ? tr("Počátek kontejneru / %1").arg(QString::fromStdString(candidate.semantic_key).section(':',-1).toUpper())
                    : QString::fromStdString(candidate.semantic_key);
                planar_dialog->set_path_plane({{},candidate.owner_id,candidate.semantic_key},label);
            };
            feature_reference_end_=[this,planar_dialog] {
                feature_reference_pick_={};feature_reference_end_={};planar_dialog->end_path_entry();
                planar_dialog->clear_reference_highlights();tree_->setProperty("commandSelectionActive",false);
                viewer_->clear_selection();tree_->clearSelection();planar_dialog->changed();
            };
            state_->setText(tr("Vyberte rovinu nebo rovinnou plochu pro skicu dráhy."));
        };
    }
    dialog->changed=[this,dialog,planar,geometry,body_id]{
        const auto* planar_editor=dynamic_cast<Sweep2DDialog*>(dialog);
        if(!dialog->isVisible()&&!(planar_editor&&planar_editor->point_order_open()))return;
        const bool valid=dialog->resolve_pending_placement(geometry);
        auto& c=dialog->pending;
        if(auto* planar_dialog=dynamic_cast<Sweep2DDialog*>(dialog)) {
            for(const auto& ref:c.placement.references)if(!ref.orientation_only&&zima::document::PartDocument::sweep2d_accepts_path_plane(ref,geometry)) {
                planar_dialog->seed_path_plane(ref,QString::fromStdString(ref.semantic_key));break;
            }
        }
        primitive_translation_dof_=zima::document::point_constraint_remaining_dof(c.placement.references,geometry);
        zima::document::PartDocument preview;
        zima::document::ConstructionObject origin;origin.id=c.id;origin.entity_id=c.feature_id;origin.container_origin=c.container_origin;
        origin.kind=zima::document::ConstructionKind::Point;origin.origin={c.placement.x,c.placement.y,c.placement.z};origin.rotation={c.placement.rotation_x,c.placement.rotation_y,c.placement.rotation_z};origin.reference_valid=false;
        preview.constructions.push_back(origin);primitive_origin_preview_mesh_=preview.construction_viewer_mesh(c.id);
        parameter_dimension_preview_=c;construction_dimension_object_id_=c.id;
        viewer_->set_feature_preview_owners({c.feature_id,c.container_origin.id});
        preserve_view_on_refresh_=true;refresh_scene();
        auto highlights=highlighted_reference_edge_keys(*dialog);
        if(auto* planar_dialog=dynamic_cast<Sweep2DDialog*>(dialog);planar_dialog&&planar_dialog->path_inspected()&&c.sweep2d.path_plane) {
            const auto& ref=*c.sweep2d.path_plane;
            highlights.insert({ref.owner_id,ref.semantic_key,properties_dialog_instance_path_});
            if(ref.semantic_key=="plane")highlights.insert({ref.owner_id,"border",properties_dialog_instance_path_});
            feature_reference_end_=[this,planar_dialog]{feature_reference_pick_={};feature_reference_end_={};planar_dialog->end_path_entry();planar_dialog->changed();};
        }
        viewer_->set_constraint_reference_highlights({},std::move(highlights));
        zima::kernel::ViewerMesh preview_mesh;
        auto& edges=preview_mesh.edges;
        try{
            if(!valid&&!c.placement.references.empty())throw std::runtime_error("Chybí reference umístění kontejneru");
            if(planar)zima::document::PartDocument::resolve_sweep2d_planes(c,geometry);
            if(planar)preview_mesh=zima::document::PartDocument::sweep2d_preview_mesh(c);
            else edges=zima::document::PartDocument::helical_preview_edges(c);
            dialog->set_status(tr("Dráha připravena. OK vytvoří těleso."));
        }catch(const std::exception& e){dialog->set_status(QString::fromUtf8(e.what()));}
        auto sketches=planar?zima::document::PartDocument::sweep2d_sketch_edges(c):
            zima::document::PartDocument::helical_sketch_edges(c);
        edges.insert(edges.end(),std::make_move_iterator(sketches.begin()),std::make_move_iterator(sketches.end()));
        if(!body_id.empty())if(const auto* part=workspace_.open_part(workspace_.active_document_id())) {
            preview_mesh=part->session.document().place_body_mesh(std::move(preview_mesh),body_id);
        }
        for(auto& e:edges){e.reference.instance_path=properties_dialog_instance_path_;if(!properties_dialog_instance_path_.empty())for(auto& p:e.points)p=workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),zima::assembly::InstancePath::decode(properties_dialog_instance_path_),p);}
        std::vector<std::pair<zima::kernel::Vec3,std::string>> labels;
        std::vector<zima::kernel::Vec3> points;
        const auto scene_point=[&](zima::kernel::Vec3 point){return properties_dialog_instance_path_.empty()?point:
            workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),zima::assembly::InstancePath::decode(properties_dialog_instance_path_),point);};
        for(const auto& marker:preview_mesh.constraint_markers)labels.emplace_back(scene_point(marker.position),marker.label);
        for(const auto& point:preview_mesh.points)points.push_back(scene_point(point.position));
        viewer_->set_transient_labels(std::move(labels));viewer_->set_transient_points(std::move(points));
        viewer_->set_transient_edges(std::move(edges));
        if(auto* planar_dialog=dynamic_cast<Sweep2DDialog*>(dialog);planar_dialog&&planar_dialog->path_active())
            planar_dialog->request_path_plane();
        else if(!pending_primitive_reference_index_&&!feature_reference_pick_&&!local_origin_selection_active_)set_primitive_properties_dimension_selection();
    };
    dialog->edit_sketch=[this,dialog,planar,geometry,body_id](unsigned stage){
        try{
            QString frame_warning;
            auto framed=dialog->pending;
            try {
                if(planar)zima::document::PartDocument::resolve_sweep2d_planes(framed,geometry);
                else zima::document::PartDocument::reframe_helical_sketches(framed,stage);
                dialog->pending=std::move(framed);
            } catch(const std::exception& error) {
                frame_warning=tr("Skica používá uloženou rovinu; závislost není dořešená: %1").arg(QString::fromUtf8(error.what()));
            }
            sweep_profile_sketch_draft_=zima::sketcher::Sketch::from_serialized(planar?dialog->pending.sweep2d.sketch_data(stage):dialog->pending.helical.sketches.at(stage));
            embedded_sketch_finished_=[this,dialog,stage](auto s){
                properties_dialog_=dialog;primitive_reference_dialog_=dialog;dialog->set_sketch(stage,s);dialog->show();dialog->raise();
                preserve_view_on_refresh_=true;refresh_scene();dialog->changed();
            };
            feature_reference_pick_={};pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;dialog->set_active_reference_index(std::nullopt);dialog->clear_reference_highlights();
            set_local_origin_selection_mode(false);local_origin_selection_dialog_=nullptr;primitive_reference_dialog_=nullptr;
            viewer_->set_constraint_reference_highlights({},{});primitive_origin_preview_mesh_.reset();parameter_dimension_preview_.reset();
            dialog->hide();properties_dialog_=nullptr;viewer_->set_transient_edges({});viewer_->set_transient_labels({});viewer_->set_transient_points({});
            active_sketch_id_=sweep_profile_sketch_draft_->id;selected_sketch_id_=active_sketch_id_;
            clear_selected_sketch_geometry();viewer_->clear_selection();tree_->clearSelection();
            preserve_view_on_refresh_=true;refresh_scene();align_active_sketch_view();
            if(planar&&stage>0) {
                auto guide=zima::sketcher::Sketch::from_serialized(dialog->pending.sweep2d.path_sketch).viewer_mesh().edges;
                if(!body_id.empty())if(const auto* part=workspace_.open_part(workspace_.active_document_id())) {
                    zima::kernel::ViewerMesh mesh;mesh.edges=std::move(guide);guide=part->session.document().place_body_mesh(std::move(mesh),body_id).edges;
                }
                for(auto& edge:guide)if(!properties_dialog_instance_path_.empty())for(auto& point:edge.points)
                    point=workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),zima::assembly::InstancePath::decode(properties_dialog_instance_path_),point);
                viewer_->set_transient_edges(std::move(guide));
            }
            state_->setText(frame_warning.isEmpty() ? tr("Nakreslete geometrii a potom zvolte Dokončit skicu.") : frame_warning);
        }catch(const std::exception& e){dialog->set_status(QString::fromUtf8(e.what()));}
    };
    connect(dialog,&QDialog::finished,this,[this]{
        feature_reference_pick_={};feature_reference_end_={};pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
        local_origin_selection_dialog_=nullptr;local_origin_selection_active_=false;visible_local_origin_ids_.clear();visible_occurrence_origin_paths_.clear();selectable_local_origin_container_ids_.clear();suspended_primitive_reference_index_.reset();suspended_construction_reference_index_.reset();
        primitive_reference_dialog_=nullptr;primitive_reference_geometry_={};primitive_origin_preview_mesh_.reset();parameter_dimension_preview_.reset();construction_dimension_object_id_.clear();
        viewer_->set_constraint_reference_highlights({},{});viewer_->set_feature_preview_owners({});
        properties_dialog_=nullptr;properties_dialog_instance_path_.clear();primitive_parameter_owner_id_.clear();
        part_rollback_.reset();viewer_->set_transient_edges({});viewer_->set_transient_labels({});viewer_->set_transient_points({});viewer_->set_candidate_filter({});viewer_->set_selection_contract({});viewer_->clear_selection();
        tree_->setProperty("commandSelectionActive",false);preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    });
    preserve_view_on_refresh_=true;refresh_scene();dialog->show();dialog->changed();
    if(id.empty())start_primitive_reference_selection(0,true);
}

void AssemblyWorkspaceWindow::show_sweep3d_properties(
    const std::string& container_id) {
    if (properties_dialog_ != nullptr) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto* edited = container_id.empty()
        ? nullptr : part->session.document().find_container(container_id);
    if (!container_id.empty() &&
        (edited == nullptr || edited->feature_kind !=
            zima::document::FeatureKind::Sweep3D)) return;
    const bool edit_mode = edited != nullptr;
    const auto initial = edit_mode ? *edited
        : zima::document::PartDocument::create_sweep3d_container();
    const std::string document_id = part->session.document().document_id;
    const int decimal_places = document_decimal_places(part->session.document());
    const bool allow_subtract = !part->session.document().history.empty() &&
        !(edit_mode && part->session.document().history.front().id == initial.id);

    auto* dialog = new ConstructionPropertiesDialog(initial, edit_mode,
        allow_subtract,
        [this, document_id, edit_mode](
                zima::document::HistoryContainer committed) {
            auto* target_part = workspace_.open_part(document_id);
            if (target_part == nullptr)
                throw std::runtime_error("Part is no longer open");
            auto next = target_part->session.document();
            if (edit_mode) {
                auto* target = next.find_container(committed.id);
                if (target == nullptr)
                    throw std::runtime_error("Sweep/Loft no longer exists");
                *target = std::move(committed);
            } else {
                next.insert_history_entry(
                    zima::document::PartHistoryKind::Feature, committed.id);
                next.history.push_back(std::move(committed));
            }
            const auto& previous = target_part->session.calculated_boundaries();
            auto calculated = calculate_part_with_resolved_references(
                next, &previous);
            target_part->session.commit(
                std::move(next), std::move(calculated));
        }, this, decimal_places);

    dialog->set_reference_request_callback(
        [this](std::size_t index) {
            start_construction_reference_selection(index);
        });
    dialog->set_curve_point_edit_request_callback(
        [this, dialog](std::optional<std::size_t> index) {
            show_curve_point_properties(dialog, index);
        });
    dialog->set_curve_axis_request_callback(
        [this, dialog](std::size_t index) {
            start_curve_axis_selection(dialog, index);
        });
    dialog->set_curve_axis_cycle_callback([this, dialog] {
        if (curve_axis_dialog_ != dialog) return;
        pending_curve_axis_index_.reset();
        curve_axis_dialog_ = nullptr;
        viewer_->clear_selection();
        set_construction_properties_dimension_selection();
    });
    dialog->set_sweep_profile_edit_request_callback(
        [this, dialog](std::size_t profile_index) {
            show_sweep_profile_sketch(dialog, profile_index);
        });
    dialog->set_reference_highlights_changed_callback([this, dialog] {
        viewer_->set_constraint_reference_highlights(
            {}, highlighted_reference_edge_keys(*dialog));
    });
    construction_reference_dialog_ = dialog;
    properties_dialog_ = dialog;
    track_tree_edit(dialog);


    dialog->set_preview_callback(
        [this, document_id, dialog](zima::document::ConstructionObject preview) {
            auto* source = workspace_.open_part(document_id);
            if (source == nullptr) return;
            auto pending = dialog->pending_sweep_value();
            auto next = source->session.document();
            if (auto* existing = next.find_container(pending.id)) {
                *existing = pending;
            } else {
                next.insert_history_entry(zima::document::PartHistoryKind::Feature, pending.id);
                next.history.push_back(pending);
            }
            const auto& calculated = source->session.calculated_boundaries();
            auto reference_geometry =
                construction_reference_source_geometry(calculated);
            next.resolve_constructions(reference_geometry);
            const auto* resolved = next.find_container(pending.id);
            if (resolved == nullptr) return;

            auto display_path = sweep_display_path(*resolved);
            zima::document::PartDocument carrier;
            carrier.constructions.push_back(display_path);
            construction_preview_mesh_ =
                carrier.construction_viewer_mesh(display_path.id, 0.0, true);
            append_mesh(*construction_preview_mesh_,
                zima::document::sweep3d_profiles_viewer_mesh(*resolved));
            zima::document::visit_feature_sketches(*resolved,[&](const auto& data,std::size_t){
                const auto display=zima::sketcher::Sketch::from_serialized(data).viewer_mesh();
                construction_preview_mesh_->dimensions.insert(construction_preview_mesh_->dimensions.end(),display.dimensions.begin(),display.dimensions.end());
            });
            construction_parameter_preview_ = display_path;
            append_reference_geometry(reference_geometry,
                next.origin_viewer_mesh().original_references);
            append_reference_geometry(reference_geometry,
                next.construction_viewer_mesh().original_references);
            append_reference_geometry(reference_geometry,
                next.history_origin_reference_geometry_before(pending.id));
            reference_geometry = next.construction_reference_geometry_for(
                pending.id, std::move(reference_geometry));
            auto placement_for_dialog = resolved->placement;
            zima::kernel::Vec3 placement_base_rotation{
                placement_for_dialog.absolute_rotation_x,
                placement_for_dialog.absolute_rotation_y,
                placement_for_dialog.absolute_rotation_z};
            bool orientation_from_reference = false;
            const bool placement_valid = zima::document::resolve_placement(
                placement_for_dialog, reference_geometry,
                &placement_base_rotation, &orientation_from_reference);
            construction_reference_geometry_ = std::move(reference_geometry);

            const auto constraint_state =
                zima::document::point_constraint_state(
                    preview.references, construction_reference_geometry_);
            construction_translation_dof_ = constraint_state.remaining_dof;
            dialog->set_translation_constraint_state(
                constraint_state, display_path.origin);
            const auto rotation_state =
                zima::document::orientation_constraint_state(
                    preview.references, construction_reference_geometry_, true,
                    display_path.origin);
            construction_rotation_dof_ = rotation_state.remaining_dof;
            dialog->set_rotation_constraint_state(rotation_state);
            dialog->set_orientation_base_rotation(
                placement_base_rotation, orientation_from_reference);
            dialog->set_resolved_rotation(
                {placement_for_dialog.rotation_x,
                 placement_for_dialog.rotation_y,
                 placement_for_dialog.rotation_z},
                placement_valid);

            std::set<std::string> preview_owners{
                resolved->id, display_path.id, display_path.entity_id,
                display_path.container_origin.id};
            for (const auto& point : display_path.curve_points) {
                preview_owners.insert(point.id);
                preview_owners.insert(point.entity_id);
                preview_owners.insert(point.container_origin.id);
            }
            viewer_->set_feature_preview_owners(preview_owners);
            preserve_view_on_refresh_ = true;
            refresh_scene();
            if (!pending_construction_reference_index_) {
                tree_->setProperty("commandSelectionActive", false);
                set_construction_properties_dimension_selection();
            }
        });

    if (edit_mode) {
        const auto history_index = part->session.document().history_index(initial.id);
        if (history_index) {
            const auto input = *history_index == 0
                ? std::optional<zima::kernel::BodyResult>{}
                : part->session.calculated_boundary(*history_index);
            part_rollback_ = PartRollbackContext{document_id,
                active_occurrence_path_, *history_index, input};
            preserve_view_on_refresh_ = true;
            refresh_scene();
        }
    }
    viewer_->set_editing_origin_visible(true);
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (properties_dialog_ == dialog) properties_dialog_ = nullptr;
        if (construction_reference_dialog_ == dialog)
            construction_reference_dialog_ = nullptr;
        if (curve_axis_dialog_ == dialog) curve_axis_dialog_ = nullptr;
        construction_dimension_object_id_.clear();
        pending_curve_axis_index_.reset();
        pending_construction_reference_index_.reset();
        construction_reference_auto_advance_ = false;
        construction_preview_mesh_.reset();
        construction_parameter_preview_.reset();
        construction_reference_geometry_ = {};
        part_rollback_.reset();
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_transient_edges({});
        viewer_->set_feature_preview_owners({});
        viewer_->set_editing_origin_visible(false);
        viewer_->set_candidate_filter({});
        viewer_->set_selection_contract({});
        viewer_->clear_selection();
        viewer_->set_constraint_reference_highlights({}, {});
        refresh_tabs();
        preserve_view_on_refresh_ = true;
        refresh_scene();
    });
    dialog->show();
    const auto first = dialog->first_empty_position_index();
    if (first < 3) {
        start_construction_reference_selection(first, true);
    } else {
        tree_->setProperty("commandSelectionActive", false);
        set_construction_properties_dimension_selection();
    }
}

} // namespace zima::app
