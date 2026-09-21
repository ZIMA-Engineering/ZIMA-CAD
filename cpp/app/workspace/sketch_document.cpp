#include <zima/workspace/assembly_scene.hpp>
#include "workspace_internal.hpp"
#include "../sketch_point_pick_priority.hpp"
#include <zima/workspace/sketch_operations.hpp>

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::set_sketch_external_reference_mode(bool enabled) {
    const bool was_active = sketch_external_reference_active_;
    const bool requested_profile_mode = sketch_external_profile_active_;
    if (enabled) {
        if (properties_dialog_ != nullptr || active_sketch_id_.empty()) {
            enabled = false;
        } else if (active_sketch() == nullptr) enabled = false;
    }
    if (enabled) {
        cancel_sketch_segment();
        sketch_external_profile_active_ = requested_profile_mode;
        sketch_external_reference_active_ = true;
        selected_sketch_segment_id_.clear();
        selected_sketch_circle_id_.clear();
        selected_sketch_arc_id_.clear();
        selected_sketch_ellipse_id_.clear();
        selected_sketch_elliptical_arc_id_.clear();
        selected_sketch_bspline_id_.clear();
        selected_sketch_text_id_.clear();
        selected_sketch_external_reference_id_.clear();
        selected_sketch_point_id_.clear();
        selection_action_->setChecked(true);
        viewer_->clear_selection();
    } else {
        sketch_external_reference_active_ = false;
        selected_sketch_external_reference_id_.clear();
    }
    {
        const QSignalBlocker blocker(sketch_external_reference_action_);
        sketch_external_reference_action_->setChecked(
            enabled && !sketch_external_profile_active_);
    }
    {
        const QSignalBlocker blocker(sketch_external_profile_action_);
        sketch_external_profile_action_->setChecked(
            enabled && sketch_external_profile_active_);
    }
    if (!enabled) sketch_external_profile_active_ = false;
    if (enabled || was_active) {
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(enabled
            ? sketch_external_profile_active_
                ? tr("Reference → obrys: vyberte persistovanou původní hranu.")
                : tr("Externí reference: vyberte persistovanou původní plochu, "
                     "hranu, vrchol nebo osu. Pravým tlačítkem lze přepínat kandidáty.")
            : tr("Režim externích referencí byl ukončen."));
    }
}

void AssemblyWorkspaceWindow::accept_sketch_external_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_external_reference_active_ ||
        (sketch_external_profile_active_ &&
         candidate.kind != zima::viewer::CandidateKind::Edge) ||
        (candidate.kind == zima::viewer::CandidateKind::Face
             ? candidate.geometry != zima::viewer::CandidateGeometry::Display &&
                   candidate.geometry !=
                       zima::viewer::CandidateGeometry::OriginalReference
             : candidate.geometry !=
                   zima::viewer::CandidateGeometry::OriginalReference) ||
        (candidate.kind != zima::viewer::CandidateKind::Edge &&
         candidate.kind != zima::viewer::CandidateKind::Vertex &&
         candidate.kind != zima::viewer::CandidateKind::Axis &&
         candidate.kind != zima::viewer::CandidateKind::Face)) return;
    try {
        const auto* active=active_sketch();if(!active)return;
        const auto owner=workspace_.active_document_id();auto pending=*active;
        auto path=candidate.instance_path;
        if(workspace_.open_assembly(owner)&&owner!=workspace_.displayed_document_id()) {
            const auto& prefix=workspace_.active_occurrence_path();
            if(prefix.empty()||!path.starts_with(prefix))throw std::invalid_argument(
                "External reference requires an exact component occurrence");
            path.erase(0,prefix.size());
        }
        const auto kind=candidate.kind==zima::viewer::CandidateKind::Edge?zima::sketcher::ExternalReferenceKind::Edge:
            candidate.kind==zima::viewer::CandidateKind::Axis?zima::sketcher::ExternalReferenceKind::Axis:
            candidate.kind==zima::viewer::CandidateKind::Face?zima::sketcher::ExternalReferenceKind::Face:zima::sketcher::ExternalReferenceKind::Point;
        auto reference=workspace::prepare_sketch_external_reference(workspace_,owner,pending,kind,
            candidate.owner_id,candidate.semantic_key,path,sketch_reference_draft_body_id(),section_dialog_!=nullptr);
        const auto reference_id=reference.id;pending.add_external_reference(std::move(reference));
        if(sketch_external_profile_active_)static_cast<void>(pending.add_external_profile_geometry(reference_id));
        // Drafts are local to the parent dialog. Only its final Part commit
        // publishes the derived Assembly dependency together with the Sketch.
        if(!mutate_active_sketch([&](auto& target){target=std::move(pending);}))return;
        preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
        state_->setText(tr("Externí reference byla uložena bez volání OCCT. Vyberte další zdroj."));
    }catch(const std::exception& error) {
        state_->setText(tr("Externí referenci nelze vytvořit: %1").arg(QObject::tr(error.what())));
    }
}

void AssemblyWorkspaceWindow::align_active_sketch_view(bool fit_view) {
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    const auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    if (viewer_ == nullptr || (part == nullptr && assembly == nullptr) ||
        active_sketch_id_.empty()) return;
    // Owned, pending Sweep sketches are edited through the same active-sketch
    // accessor as ordinary document sketches, before they enter history.
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
    // `plane` is only the local XY/XZ/YZ choice. Once the container has
    // placement/orientation references, the real sketch plane can point in
    // any world direction; using the enum here caused Sketcher to claim a
    // normal view while retaining the oblique view shown in 03.png.
    zima::kernel::Vec3 direction = sketch->resolved_normal;
    const double direction_length = std::sqrt(direction.x * direction.x +
        direction.y * direction.y + direction.z * direction.z);
    if (direction_length <= 1.0e-12) {
        direction = sketch->plane == zima::sketcher::SketchPlane::XY
            ? zima::kernel::Vec3{0.0, 0.0, 1.0}
            : sketch->plane == zima::sketcher::SketchPlane::XZ
                ? zima::kernel::Vec3{0.0, 1.0, 0.0}
                : zima::kernel::Vec3{1.0, 0.0, 0.0};
    }
    // The model frame already contains placement FRONT/BACK, in-plane
    // ROTATE and angular corrections.  Use its resolved X axis to derive the
    // camera roll instead of replaying those Placement flags a second time.
    // This makes entry into Sketcher show the exact owning-container frame,
    // including the intended side of a supporting solid face.
    zima::kernel::Vec3 screen_x = sketch->resolved_x_axis;
    if (sketch_view_state_id_ != active_sketch_id_) {
        sketch_view_state_id_ = active_sketch_id_;
        // Sketcher controls are relative camera overrides. The initial view
        // itself comes entirely from the persisted model frame above.
        sketch_view_back_ = false;
        sketch_view_quarter_turns_ = 0;
    }
    if (sketch_view_back_) direction = {-direction.x, -direction.y, -direction.z};
    if (const auto* body = sketch_body(*sketch)) {
        const auto frame = container_dimension_frame(body->scope.placement);
        direction = frame.vector(direction);
        screen_x = frame.vector(screen_x);
    }
    if (part != nullptr &&
        workspace_.open_assembly(workspace_.displayed_document_id()) != nullptr) {
        const auto occurrence = resolve_active_occurrence(
            part->session.document().document_id);
        if (!occurrence || occurrence->empty()) return;
        direction = workspace_.occurrence_direction_to_scene(
            workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(*occurrence), direction);
        screen_x = workspace_.occurrence_direction_to_scene(
            workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(*occurrence), screen_x);
    }
    if (assembly != nullptr && workspace_.active_document_id() != workspace_.displayed_document_id() &&
        !workspace_.active_occurrence_path().empty()) {
        const auto path = zima::assembly::InstancePath::decode(workspace_.active_occurrence_path());
        direction = workspace_.occurrence_direction_to_scene(workspace_.displayed_document_id(), path, direction);
        screen_x = workspace_.occurrence_direction_to_scene(workspace_.displayed_document_id(), path, screen_x);
    }
    const double frame_roll = camera_roll_for_direction(
        direction, screen_x, 0.0);
    viewer_->set_view_direction(direction, static_cast<float>(
        frame_roll + sketch_view_quarter_turns_ * 90.0 + (sketch->drawing_template ? 180.0 : 0.0)));
    if (fit_view) viewer_->fit_all();
    state_->setText(tr("Pohled je kolmý k rovině aktivní skici."));
}

void AssemblyWorkspaceWindow::flip_active_sketch_view() {
    if (active_sketch_id_.empty()) return;
    // Initialize the camera-only state from the active sketch before changing it.
    if (sketch_view_state_id_ != active_sketch_id_) align_active_sketch_view(false);
    sketch_view_back_ = !sketch_view_back_;
    align_active_sketch_view(false);
    state_->setText(tr("Pohled na aktivní skicu byl převrácen."));
}

void AssemblyWorkspaceWindow::rotate_active_sketch_view() {
    if (active_sketch_id_.empty()) return;
    // Rotation is deliberately a view operation; it never changes the sketch plane.
    if (sketch_view_state_id_ != active_sketch_id_) align_active_sketch_view(false);
    sketch_view_quarter_turns_ = (sketch_view_quarter_turns_ + 1) % 4;
    align_active_sketch_view(false);
    state_->setText(tr("Pohled na aktivní skicu byl otočen o 90°."));
}

const zima::sketcher::Sketch* AssemblyWorkspaceWindow::active_sketch() const {
    if (active_sketch_id_.empty()) return nullptr;
    if (sweep_profile_sketch_draft_ &&
        sweep_profile_sketch_draft_->id == active_sketch_id_) {
        return &*sweep_profile_sketch_draft_;
    }

    const std::vector<zima::sketcher::Sketch>* sketches{};
    if (const auto* part = workspace_.open_part(workspace_.active_document_id())) {
        sketches = &part->session.document().sketches;
    } else if (const auto* assembly =
                   workspace_.open_assembly(workspace_.active_document_id())) {
        sketches = &assembly->session.document().sketches;
    }
    if (sketches == nullptr) return nullptr;
    const auto found = std::find_if(sketches->begin(), sketches->end(),
        [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
    return found == sketches->end() ? nullptr : &*found;
}

std::string AssemblyWorkspaceWindow::sketch_reference_draft_body_id() const {
    if (sweep_profile_sketch_draft_ && sweep_profile_sketch_draft_->id == active_sketch_id_)
        if (const auto* body = sketch_body(*sweep_profile_sketch_draft_)) return body->scope.id;
    return {};
}

const zima::document::BodyHistory* AssemblyWorkspaceWindow::sketch_body(
    const zima::sketcher::Sketch& sketch) const {
    if(section_dialog_&&sweep_profile_sketch_draft_&&sweep_profile_sketch_draft_->id==sketch.id)return nullptr;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (!part) return nullptr;
    const auto& document = part->session.document();
    if (const auto* body = document.body_owner_for_object(sketch.id)) return body;
    if (const auto* body = document.body_owner_for_object(sketch.owner_container_id)) return body;
    if (sweep_profile_sketch_draft_ && sweep_profile_sketch_draft_->id == sketch.id)
        return document.body_history.find(document.body_history.active_body_id());
    return nullptr;
}

zima::kernel::ViewerMesh AssemblyWorkspaceWindow::place_sketch_mesh(
    const zima::sketcher::Sketch& sketch, zima::kernel::ViewerMesh mesh) const {
    if (const auto* body = sketch_body(sketch))
        return workspace_.open_part(workspace_.active_document_id())->session.document()
            .place_body_mesh(std::move(mesh), body->scope.id);
    return mesh;
}

zima::kernel::ViewerMesh AssemblyWorkspaceWindow::sketch_input_mesh(
    const zima::document::DocumentSession& session) const {
    const auto& document = session.document();
    if (!document.body_history.bodies().empty()) {
        auto context = document.body_history;
        if (part_rollback_ && part_rollback_->part_document_id == document.document_id &&
            part_rollback_->history_limit < document.history.size()) {
            const auto& edited = document.history[part_rollback_->history_limit];
            if (const auto* owner = context.owner(edited.id)) {
                context.activate(owner->scope.id);
                context.set_history_cursor(owner->scope.id, context.rollback_before(edited.id).entry_count);
            }
        }
        return session.body_context_mesh(&context);
    }
    if (part_rollback_ && part_rollback_->part_document_id == document.document_id)
        return part_rollback_->input_body ? part_rollback_->input_body->mesh : zima::kernel::ViewerMesh{};
    const auto count = document.body_operation_count_at_history_cursor();
    const auto& calculated = session.calculated_boundaries();
    return count && count <= calculated.size() ? calculated[count-1].mesh : zima::kernel::ViewerMesh{};
}

void AssemblyWorkspaceWindow::show_sketch_drag_preview(const zima::sketcher::Sketch& sketch) {
    zima::kernel::ViewerMesh display;
    if(section_dialog_&&sweep_profile_sketch_draft_){display=section_preview_source_;append_mesh(display,sketch.viewer_mesh());}
    else if (const auto* part = workspace_.open_part(workspace_.active_document_id())) {
        display = sketch_input_mesh(part->session);
        append_mesh(display, place_sketch_mesh(sketch, sketch.viewer_mesh()));
        if (workspace_.open_assembly(workspace_.displayed_document_id())) {
            const auto occurrence = resolve_active_occurrence(part->session.document().document_id);
            if (!occurrence || occurrence->empty()) return;
            zima::kernel::BodyResult source;
            source.mesh = std::move(display);
            display = workspace_.build_scene_with_part_override(workspace_.displayed_document_id(),
                zima::assembly::InstancePath::decode(*occurrence), std::move(source));
        }
    } else if (assembly_sketch_drag_document_) {
        display = assembly_sketch_drag_document_->build_scene();
        append_mesh(display, sketch.viewer_mesh());
        if (workspace_.active_document_id() != workspace_.displayed_document_id() &&
            !workspace_.active_occurrence_path().empty()) {
            display = workspace::build_scene_with_assembly_override(workspace_, workspace_.displayed_document_id(),
                zima::assembly::InstancePath::decode(workspace_.active_occurrence_path()),
                *assembly_sketch_drag_document_, &display);
        }
    }
    viewer_->set_mesh(std::move(display), false);
}

zima::kernel::ViewerMesh AssemblyWorkspaceWindow::sketch_viewer_mesh(
    const zima::sketcher::Sketch& source) const {
    const auto pending_presentation = [&](zima::kernel::ViewerMesh mesh, const std::string& key) {
        for(auto& dimension:mesh.dimensions) if(dimension.reference.semantic_key==key) {
            dimension.arrows_reversed=universal_dimension_layout_.arrows_reversed;
            dimension.radius_center_line_hidden=universal_dimension_layout_.radius_center_line_hidden;
        }
        return mesh;
    };
    const auto& sketch = source.id == active_sketch_id_ && active_sketch()
        ? *active_sketch() : source;
    // Every Sketcher host (rollback, embedded profiles, templates and
    // Assemblies) displays and picks the same pending trim topology.
    if (sketch_trim_active_ && sketch_trim_preview_ && sketch.id == active_sketch_id_) {
        const auto& preview = *sketch_trim_preview_;
        auto mesh = preview.viewer_mesh();
        std::set<std::string> geometry_ids;
        for (const auto& piece : sketch_trim_topology_) geometry_ids.insert(piece.geometry_id);
        std::erase_if(mesh.edges, [&](const auto& edge) {
            const auto separator = edge.reference.semantic_key.find(':');
            return separator != std::string::npos &&
                geometry_ids.contains(edge.reference.semantic_key.substr(separator + 1));
        });
        for (std::size_t i = 0; i < sketch_trim_topology_.size(); ++i) {
            zima::kernel::ViewerEdge edge;
            edge.reference = {preview.id, "trim_piece:" + std::to_string(i), {}};
            edge.overlay = true;
            for (const auto& point : sketch_trim_topology_[i].points)
                edge.points.push_back(preview.world_point(point[0], point[1]));
            mesh.edges.push_back(std::move(edge));
        }
        return place_sketch_mesh(preview, std::move(mesh));
    }
    if (sketch.id == active_sketch_id_ &&
        !universal_corner_radius_dimension_id_.empty() &&
        universal_dimension_cursor_) {
        auto preview = sketch;
        const auto radius = std::ranges::find_if(preview.corner_radii,
            [&](const auto& value) {
                return value.id == universal_corner_radius_dimension_id_;
            });
        if (radius != preview.corner_radii.end()) {
            radius->dimension_visible = true;
            radius->dimension_placement = *universal_dimension_cursor_;
        }
        return place_sketch_mesh(sketch, pending_presentation(preview.viewer_mesh(),"corner_dimension:"+universal_corner_radius_dimension_id_));
    }
    if (sketch.id == active_sketch_id_ && universal_pending_dimension_ &&
        universal_dimension_cursor_) {
        auto preview = sketch;
        auto dimension = *universal_pending_dimension_;
        if (universal_dimension_references_.size() >= 2 &&
            universal_dimension_references_[0].kind ==
                UniversalDimensionReferenceKind::Point &&
            universal_dimension_references_[1].kind ==
                UniversalDimensionReferenceKind::Point &&
            dimension.kind == zima::sketcher::DimensionKind::Distance) {
            const auto first = sketch_point_reference_position(sketch,
                universal_dimension_references_[0].id);
            const auto second = sketch_point_reference_position(sketch,
                universal_dimension_references_[1].id);
            if (first && second) {
                const auto kind = zima::sketcher::classify_linear_dimension(
                    *first, *second, *universal_dimension_cursor_);
                dimension = sketch.create_point_dimension(
                    universal_dimension_references_[0].id,
                    universal_dimension_references_[1].id, kind);
            }
        }
        dimension.placement = *universal_dimension_cursor_;
        const auto key="dimension:"+dimension.id;
        preview.dimensions.push_back(std::move(dimension));
        return place_sketch_mesh(sketch, pending_presentation(preview.viewer_mesh(),key));
    }
    if (sketch.id != active_sketch_id_ || !pending_point_dimension_cursor_) {
        return place_sketch_mesh(sketch, sketch.viewer_mesh());
    }
    auto preview = sketch;
    try {
        if (!pending_corner_radius_dimension_id_.empty()) {
            const auto radius = std::find_if(preview.corner_radii.begin(),
                preview.corner_radii.end(), [&](const auto& value) {
                    return value.id == pending_corner_radius_dimension_id_;
                });
            if (radius != preview.corner_radii.end()) {
                radius->dimension_visible = true;
                radius->dimension_placement = *pending_point_dimension_cursor_;
            }
        } else if (pending_sketch_dimension_) {
            auto dimension = *pending_sketch_dimension_;
            dimension.placement = *pending_point_dimension_cursor_;
            preview.dimensions.push_back(std::move(dimension));
        } else if (sketch_point_dimension_active_ &&
                   !pending_point_dimension_first_id_.empty() &&
                   !pending_point_dimension_second_id_.empty()) {
            auto kind = zima::sketcher::DimensionKind::Distance;
            const auto first = sketch_point_reference_position(sketch,
                pending_point_dimension_first_id_);
            const auto second = sketch_point_reference_position(sketch,
                pending_point_dimension_second_id_);
            if (first && second) {
                kind = zima::sketcher::classify_linear_dimension(
                    *first, *second, *pending_point_dimension_cursor_);
            }
            auto dimension = sketch.create_point_dimension(
                pending_point_dimension_first_id_,
                pending_point_dimension_second_id_, kind);
            dimension.placement = *pending_point_dimension_cursor_;
            preview.dimensions.push_back(std::move(dimension));
        }
    } catch (const std::exception&) {
        // A transient cursor must never make the persisted Sketch disappear.
        return place_sketch_mesh(sketch, sketch.viewer_mesh());
    }
    auto mesh=preview.viewer_mesh();
    for(auto& d:mesh.dimensions) {
        const bool pending_corner=d.reference.semantic_key=="corner_dimension:"+pending_corner_radius_dimension_id_;
        const bool pending_new=d.reference.semantic_key.starts_with("dimension:") &&
            std::ranges::none_of(sketch.dimensions,[&](const auto& old){return "dimension:"+old.id==d.reference.semantic_key;});
        if(pending_corner||pending_new) {
            d.arrows_reversed=universal_dimension_layout_.arrows_reversed;
            d.radius_center_line_hidden=universal_dimension_layout_.radius_center_line_hidden;
        }
    }
    return place_sketch_mesh(sketch,std::move(mesh));
}

bool AssemblyWorkspaceWindow::mutate_active_sketch(
    const std::function<void(zima::sketcher::Sketch&)>& mutation) {
    if (active_sketch_id_.empty()) return false;
    const auto apply = [&](auto& pending) {
        std::set<std::string> old;
        for(const auto& d:pending.dimensions)old.insert(d.id);
        mutation(pending);
        if(sketch_universal_dimension_active_ || pending_sketch_dimension_ ||
           !pending_corner_radius_dimension_id_.empty() || sketch_point_dimension_active_) {
            for(const auto& d:pending.dimensions)if(!old.contains(d.id))
                zima::kernel::store_dimension_layout(pending.dimension_layouts,
                    {pending.id,"dimension:"+d.id,{}},universal_dimension_layout_);
            const auto corner=!universal_corner_radius_dimension_id_.empty()?universal_corner_radius_dimension_id_:pending_corner_radius_dimension_id_;
            if(!corner.empty())zima::kernel::store_dimension_layout(pending.dimension_layouts,
                {pending.id,"corner_dimension:"+corner,{}},universal_dimension_layout_);
        }
    };
    if (sweep_profile_sketch_draft_ &&
        sweep_profile_sketch_draft_->id == active_sketch_id_) {
        auto next = *sweep_profile_sketch_draft_;
        workspace::apply_sketch_geometry(next,apply);
        if(section_dialog_){section_sketch_undo_.push_back(*sweep_profile_sketch_draft_);section_sketch_redo_.clear();}
        sweep_profile_sketch_draft_ = std::move(next);
        return true;
    }

    if (workspace_.open_part(workspace_.active_document_id()) || workspace_.open_assembly(workspace_.active_document_id())) {
        try {
            static_cast<void>(workspace::mutate_document_sketch(workspace_,workspace_.active_document_id(),active_sketch_id_,apply));
            return true;
        } catch(const workspace::SketchOperationError& error) {
            if(std::string_view(error.code)=="sketch_not_found")return false;
            throw;
        }
    }
    return false;
}

bool AssemblyWorkspaceWindow::accept_sketch_text_ray(
    const zima::kernel::Vec3& origin,
    const zima::kernel::Vec3& direction) {
    if (!sketch_text_active_ || sketch_text_dialog_ == nullptr ||
        !editing_sketch_text_id_.empty()) return false;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    sketch_text_dialog_->set_anchor((*position)[0], (*position)[1]);
    state_->setText(tr("Poloha textu určena. Upravte parametry a potvrďte OK."));
    return true;
}

void AssemblyWorkspaceWindow::finish_active_sketch() {
    if (active_sketch_id_.empty() || properties_dialog_ != nullptr) return;
    // Flush a pending trim before validating or copying an embedded Sketch.
    if (sketch_trim_active_ && !finish_sketch_trim()) return;
    if (sweep_profile_sketch_draft_ && embedded_sketch_finished_) {
        // Returning to Properties must also allow an incomplete Section draft,
        // so its owning dialog can be cancelled. OK validates the final cut.
        auto sketch=*sweep_profile_sketch_draft_;
        auto finished=std::move(embedded_sketch_finished_);
        cancel_sketch_segment();active_sketch_id_.clear();clear_selected_sketch_geometry();
        viewer_->clear_selection();sweep_profile_sketch_draft_.reset();
        finished(std::move(sketch));return;
    }
    if (sweep_profile_sketch_draft_ &&
        sweep_profile_sketch_draft_->id == active_sketch_id_ &&
        sweep_profile_parent_dialog_ != nullptr &&
        sweep_profile_sketch_index_) {
        auto sketch = *sweep_profile_sketch_draft_;
        auto* parent = sweep_profile_parent_dialog_;
        const auto profile_index = *sweep_profile_sketch_index_;
        cancel_sketch_segment();
        active_sketch_id_.clear();
        clear_selected_sketch_geometry();
        viewer_->clear_selection();
        sweep_profile_sketch_draft_.reset();
        sweep_profile_parent_dialog_ = nullptr;
        sweep_profile_sketch_index_.reset();
        properties_dialog_ = parent;
        construction_reference_dialog_ = parent;
        parent->set_sweep_profile_sketch(profile_index, sketch);
        parent->show();
        parent->raise();
        parent->activateWindow();
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(tr(
            "Skica byla uložena do návrhu Sweep/Loftu. Potvrďte celý "
            "kontejner tlačítkem OK."));
        return;
    }

    const std::string finished_sketch_id = active_sketch_id_;
    std::string return_container_id;
    std::optional<zima::document::FeatureKind> return_feature_kind;
    if (auto* part = workspace_.open_part(workspace_.active_document_id())) {
        if (pending_profile_feature_ && property_owned_sketch_draft_ &&
            property_owned_sketch_draft_->id == finished_sketch_id &&
            !part->session.document().find_container(pending_profile_feature_->id)) {
            return_container_id = pending_profile_feature_->id;
            return_feature_kind = pending_profile_feature_->feature_kind;
        }
        const auto sketch = std::find_if(part->session.document().sketches.begin(),
            part->session.document().sketches.end(), [&](const auto& value) {
                return value.id == finished_sketch_id;
            });
        if (sketch != part->session.document().sketches.end()) {
            return_container_id = sketch->owner_container_id;
            if (const auto* owner = part->session.document().find_container(
                    return_container_id)) {
                return_feature_kind = pending_profile_feature_ && pending_profile_feature_->id == return_container_id
                    ? pending_profile_feature_->feature_kind : owner->feature_kind;
            }
        }
    } else if (auto* assembly = workspace_.open_assembly(
                   workspace_.active_document_id())) {
        const auto sketch = std::find_if(assembly->session.document().sketches.begin(),
            assembly->session.document().sketches.end(), [&](const auto& value) {
                return value.id == finished_sketch_id;
            });
        if (sketch != assembly->session.document().sketches.end()) {
            return_container_id = sketch->owner_container_id;
            if (pending_profile_feature_ &&
                pending_profile_feature_->id == return_container_id) {
                return_feature_kind = pending_profile_feature_->feature_kind;
            } else if (const auto* cut =
                           assembly->session.document().find_cut(return_container_id)) {
                return_feature_kind = cut->definition.feature_kind;
            } else if (assembly->session.document().find_sketch_container(return_container_id)) {
                return_feature_kind = zima::document::FeatureKind::Sketch;
            }
        }
    }
    selected_sketch_id_ = active_sketch_id_;
    cancel_sketch_segment();
    active_sketch_id_.clear();
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_text_id_.clear();
    selected_sketch_point_id_.clear();
    viewer_->clear_selection();
    preserve_view_on_refresh_ = true;
    refresh_scene();
    state_->setText(tr("Skica dokončena. Návrat do vlastností kontejneru."));
    if (return_feature_kind && !return_container_id.empty()) {
        QTimer::singleShot(0, this,
            [this, finished_sketch_id, return_container_id,
             feature_kind = *return_feature_kind] {
                if (feature_kind == zima::document::FeatureKind::Sketch) {
                    show_sketch_properties(finished_sketch_id);
                } else if (feature_kind ==
                               zima::document::FeatureKind::Extrusion ||
                           feature_kind ==
                               zima::document::FeatureKind::Revolution) {
                    const auto* assembly = workspace_.open_assembly(
                        workspace_.active_document_id());
                    const auto* part = workspace_.open_part(workspace_.active_document_id());
                    if ((assembly != nullptr &&
                         assembly->session.document().find_cut(return_container_id) == nullptr) ||
                        (part != nullptr &&
                         part->session.document().find_container(return_container_id) == nullptr)) {
                        show_primitive_properties(feature_kind);
                    } else {
                        show_primitive_properties(feature_kind, return_container_id);
                    }
                }
            });
    }
}

void AssemblyWorkspaceWindow::set_sketch_placement_selection_contract() {
    if (viewer_ == nullptr) return;
    viewer_->set_selection_contract({
        zima::viewer::CandidateKind::SketchAxis,
        zima::viewer::CandidateKind::SketchSegment,
        zima::viewer::CandidateKind::SketchPoint,
        zima::viewer::CandidateKind::SketchCurve,
        zima::viewer::CandidateKind::SketchExternalReference});
    viewer_->set_candidate_priority(sketch_point_pick_priority);
}

} // namespace zima::app
