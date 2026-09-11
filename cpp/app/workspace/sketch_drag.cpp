#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


bool AssemblyWorkspaceWindow::begin_sketch_point_drag(
    const zima::viewer::ViewerCandidate& candidate) {
    if (qEnvironmentVariableIsSet("ZIMA_SKETCH_TRACE")) {
        QStringList workspace_selected;
        for (const auto& id : selected_sketch_geometry_ids_) {
            workspace_selected.push_back(QString::fromStdString(id));
        }
        QStringList viewer_selected;
        for (const auto& selected : viewer_->sketch_selection()) {
            viewer_selected.push_back(
                QString::fromStdString(selected.semantic_key));
        }
        qInfo().noquote() << "SKETCH_TRACE|WORKSPACE_DRAG_REQUEST|candidate="
            << QString::fromStdString(candidate.semantic_key)
            << "|workspace_selected=" << workspace_selected.join(',')
            << "|viewer_selected=" << viewer_selected.join(',');
    }
    if (properties_dialog_ != nullptr || sketch_segment_active_ ||
        sketch_rectangle_active_ || sketch_polygon_active_ ||
        sketch_offset_dialog_ || sketch_mirror_active_ || sketch_circle_active_ || sketch_arc_active_ ||
        sketch_ellipse_active_ || sketch_elliptical_arc_active_ ||
        sketch_bspline_active_ ||
        sketch_coincident_active_ || sketch_midpoint_active_ ||
        sketch_symmetric_active_ || sketch_concentric_active_ ||
        sketch_tangent_active_ || sketch_common_tangent_active_ ||
        sketch_segment_pair_active_ || sketch_corner_fillet_active_ ||
        sketch_universal_dimension_active_ ||
        candidate.owner_id != active_sketch_id_) return false;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
    constexpr std::string_view corner_handle_prefix{"corner_radius_handle:"};
    if (candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
        candidate.semantic_key.starts_with(corner_handle_prefix)) {
        const auto payload = candidate.semantic_key.substr(
            corner_handle_prefix.size());
        const auto separator = payload.rfind(':');
        const auto radius_id = separator == std::string::npos
            ? std::string{} : payload.substr(0, separator);
        const auto radius = std::find_if(
            sketch->corner_radii.begin(), sketch->corner_radii.end(),
            [&](const auto& value) { return value.id == radius_id; });
        if (radius == sketch->corner_radii.end()) return false;
        if(sweep_profile_sketch_draft_) { section_drag_sketches_={*sweep_profile_sketch_draft_}; }
        else if (const auto* part = workspace_.open_part(workspace_.active_document_id())) {
            sketch_drag_document_ = part->session.document();
        } else if (const auto* assembly = workspace_.open_assembly(
                       workspace_.active_document_id())) {
            assembly_sketch_drag_document_ = assembly->session.document();
        } else {
            return false;
        }
        sketch_corner_drag_source_ = *sketch;
        sketch_corner_drag_first_segment_id_ = radius->first_segment_id;
        sketch_corner_drag_second_segment_id_ = radius->second_segment_id;
        sketch_corner_drag_vertex_id_ = radius->vertex_id;
        sketch_drag_point_id_.clear();
        sketch_drag_changed_ = false;
        state_->setText(tr(
            "Radius rohu: tažením bílého tečného bodu změňte nebo skryjte radius."));
        return true;
    }
    const auto viewer_selection = viewer_->sketch_selection();
    std::vector<std::string> selected_segments;
    // Only candidates which are still visibly selected in the View may arm
    // the corner gesture. The workspace mirror can outlive a viewer clear or
    // a Sketch switch; using that stale pair blocked every unrelated point.
    for (const auto& selected : viewer_selection) {
        if (selected.owner_id != active_sketch_id_ ||
            selected.kind != zima::viewer::CandidateKind::SketchSegment ||
            !selected.semantic_key.starts_with("segment:")) continue;
        const auto selected_id = selected.semantic_key.substr(8);
        if (std::any_of(sketch->segments.begin(), sketch->segments.end(),
                [&](const auto& segment) { return segment.id == selected_id; }) &&
            std::find(selected_segments.begin(), selected_segments.end(),
                selected_id) == selected_segments.end()) {
            selected_segments.push_back(selected_id);
        }
    }
    // Tree/box selection is the persistent application-side truth. During
    // the press that starts a drag, MeshView may already have promoted the
    // shared point to its confirmed candidate and no longer expose both
    // segments through sketch_selection(). Recover the still-selected
    // segment pair before considering an ordinary point/group move.
    for (const auto& selected_id : selected_sketch_geometry_ids_) {
        if (std::ranges::find(selected_segments, selected_id) !=
            selected_segments.end()) continue;
        if (std::ranges::any_of(sketch->segments,
                [&](const auto& segment) { return segment.id == selected_id; })) {
            selected_segments.push_back(selected_id);
        }
    }
    if (selected_segments.size() == 2) {
        const auto first = std::find_if(sketch->segments.begin(), sketch->segments.end(),
            [&](const auto& value) { return value.id == selected_segments[0]; });
        const auto second = std::find_if(sketch->segments.begin(), sketch->segments.end(),
            [&](const auto& value) { return value.id == selected_segments[1]; });
        if (first != sketch->segments.end() && second != sketch->segments.end()) {
            std::string shared_point_id;
            std::string offered_corner_point_id;
            const auto coincident = [&](const std::string& start,
                                        const std::string& target) {
                std::vector<std::string> pending{start};
                std::unordered_set<std::string> visited;
                while (!pending.empty()) {
                    auto point_id = std::move(pending.back());
                    pending.pop_back();
                    if (!visited.insert(point_id).second) continue;
                    if (point_id == target) return true;
                    for (const auto& constraint : sketch->constraints) {
                        if (constraint.suppressed || constraint.kind !=
                                zima::sketcher::ConstraintKind::Coincident) continue;
                        if (constraint.first_point_id == point_id)
                            pending.push_back(constraint.second_point_id);
                        else if (constraint.second_point_id == point_id)
                            pending.push_back(constraint.first_point_id);
                    }
                }
                return false;
            };
            for (const auto& first_point_id : {
                     first->first_point_id, first->second_point_id}) {
                for (const auto& second_point_id : {
                         second->first_point_id, second->second_point_id}) {
                    if (coincident(first_point_id, second_point_id)) {
                        shared_point_id = first_point_id;
                        if (candidate.semantic_key == "point:" + first_point_id)
                            offered_corner_point_id = first_point_id;
                        else if (candidate.semantic_key == "point:" + second_point_id)
                            offered_corner_point_id = second_point_id;
                        break;
                    }
                }
                if (!shared_point_id.empty()) break;
            }
            const bool pressed_shared_point =
                candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
                !offered_corner_point_id.empty();
            // Selecting the second segment is not the fillet gesture.  Start
            // only on the subsequent press of their shared persisted point;
            // otherwise the Ctrl-selection click itself consumes and clears
            // the just-completed pair before the user can drag the corner.
            if (!shared_point_id.empty() && pressed_shared_point) {
                if (qEnvironmentVariableIsSet("ZIMA_SKETCH_TRACE")) {
                    qInfo().noquote() << "SKETCH_TRACE|BRANCH|CORNER_FILLET|segments="
                        << QString::fromStdString(first->id) << ','
                        << QString::fromStdString(second->id) << "|point="
                        << QString::fromStdString(shared_point_id);
                }
                if(sweep_profile_sketch_draft_) { section_drag_sketches_={*sweep_profile_sketch_draft_}; }
                else if (const auto* part = workspace_.open_part(workspace_.active_document_id())) {
                    sketch_drag_document_ = part->session.document();
                } else if (const auto* assembly = workspace_.open_assembly(
                               workspace_.active_document_id())) {
                    assembly_sketch_drag_document_ = assembly->session.document();
                } else {
                    return false;
                }
                sketch_corner_drag_source_ = *sketch;
                // Normalize a C-connected pair to one persisted vertex in
                // the transient source. The same normalization is committed
                // transactionally by add_corner_fillet during the drag.
                static_cast<void>(sketch_corner_drag_source_->add_corner_fillet(
                    first->id, second->id, 0.0));
                const auto normalized_first = std::ranges::find_if(
                    sketch_corner_drag_source_->segments,
                    [&](const auto& value) { return value.id == first->id; });
                const auto normalized_second = std::ranges::find_if(
                    sketch_corner_drag_source_->segments,
                    [&](const auto& value) { return value.id == second->id; });
                if (normalized_first == sketch_corner_drag_source_->segments.end() ||
                    normalized_second == sketch_corner_drag_source_->segments.end())
                    return false;
                for (const auto& point_id : {normalized_first->first_point_id,
                                             normalized_first->second_point_id}) {
                    if (point_id == normalized_second->first_point_id ||
                        point_id == normalized_second->second_point_id) {
                        shared_point_id = point_id;
                        break;
                    }
                }
                sketch_corner_drag_first_segment_id_ = first->id;
                sketch_corner_drag_second_segment_id_ = second->id;
                sketch_corner_drag_vertex_id_ = shared_point_id;
                sketch_drag_point_id_.clear();
                sketch_drag_changed_ = false;
                state_->setText(tr(
                    "Zaoblení rohu: tažením společného bodu určete poloměr."));
                return true;
            }
            // Two selected segments explicitly request the corner-fillet
            // gesture. Never silently fall through to the ordinary point
            // mover when the pressed point is not their common endpoint;
            // that made the whole constrained geometry jump instead of
            // explaining why the fillet could not start.
            state_->setText(tr(
                "Zaoblení rohu: táhněte společným bodem dvou vybraných úseček."));
            return false;
        }
    }
    if (candidate.kind != zima::viewer::CandidateKind::SketchPoint ||
        !candidate.semantic_key.starts_with("point:")) return false;
    const auto point_id = candidate.semantic_key.substr(6);
    const auto* point = sketch->find_point(point_id);
    if (point == nullptr || point->fixed) {
        state_->setText(tr("Fixovaný bod nelze táhnout."));
        return false;
    }
    if(sweep_profile_sketch_draft_) { section_drag_sketches_={*sweep_profile_sketch_draft_}; }
    else if (const auto* part = workspace_.open_part(workspace_.active_document_id())) {
        sketch_drag_document_ = part->session.document();
    } else if (const auto* assembly =
                   workspace_.open_assembly(workspace_.active_document_id())) {
        assembly_sketch_drag_document_ = assembly->session.document();
    } else {
        return false;
    }
    sketch_drag_point_id_ = point_id;
    if (qEnvironmentVariableIsSet("ZIMA_SKETCH_TRACE")) {
        qInfo().noquote() << "SKETCH_TRACE|BRANCH|MOVE_POINT|point="
            << QString::fromStdString(point_id)
            << "|selected_segment_count=" << selected_segments.size();
    }
    sketch_drag_changed_ = false;
    return true;
}

void AssemblyWorkspaceWindow::update_sketch_point_drag(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_drag_document_ && !assembly_sketch_drag_document_ && section_drag_sketches_.empty()) return;
    auto& sketches = !section_drag_sketches_.empty() ? section_drag_sketches_ : sketch_drag_document_
        ? sketch_drag_document_->sketches
        : assembly_sketch_drag_document_->sketches;
    const auto current_sketch = std::find_if(
        sketches.begin(), sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (current_sketch == sketches.end()) return;
    const auto position = current_sketch->intersect_ray(origin, direction);
    if (!position) return;
    auto next_sketch = std::find_if(sketches.begin(), sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch_corner_drag_source_) {
        const auto& source = *sketch_corner_drag_source_;
        const auto first = std::find_if(source.segments.begin(), source.segments.end(),
            [&](const auto& value) {
                return value.id == sketch_corner_drag_first_segment_id_;
            });
        const auto second = std::find_if(source.segments.begin(), source.segments.end(),
            [&](const auto& value) {
                return value.id == sketch_corner_drag_second_segment_id_;
            });
        const auto* vertex = source.find_point(sketch_corner_drag_vertex_id_);
        if (first == source.segments.end() || second == source.segments.end() ||
            vertex == nullptr) return;
        const auto outer_point = [&](const auto& segment) {
            return source.find_point(segment.first_point_id == vertex->id
                ? segment.second_point_id : segment.first_point_id);
        };
        const auto* first_outer = outer_point(*first);
        const auto* second_outer = outer_point(*second);
        if (first_outer == nullptr || second_outer == nullptr) return;
        const std::array first_vector{
            first_outer->x - vertex->x, first_outer->y - vertex->y};
        const std::array second_vector{
            second_outer->x - vertex->x, second_outer->y - vertex->y};
        const double first_length = std::hypot(first_vector[0], first_vector[1]);
        const double second_length = std::hypot(second_vector[0], second_vector[1]);
        if (first_length <= 1.0e-12 || second_length <= 1.0e-12) return;
        const std::array first_direction{
            first_vector[0] / first_length, first_vector[1] / first_length};
        const std::array second_direction{
            second_vector[0] / second_length, second_vector[1] / second_length};
        const double cosine = std::clamp(
            first_direction[0] * second_direction[0] +
                first_direction[1] * second_direction[1], -1.0, 1.0);
        const double half_angle_tangent = std::tan(std::acos(cosine) * 0.5);
        if (!std::isfinite(half_angle_tangent) ||
            half_angle_tangent <= 1.0e-12) return;
        const std::array cursor_vector{
            (*position)[0] - vertex->x, (*position)[1] - vertex->y};
        const double tangent_distance = std::max(0.0, std::max(
            cursor_vector[0] * first_direction[0] +
                cursor_vector[1] * first_direction[1],
            cursor_vector[0] * second_direction[0] +
                cursor_vector[1] * second_direction[1]));
        const double maximum_radius = std::min(first_length, second_length) *
            half_angle_tangent * (1.0 - 1.0e-6);
        const double radius = std::min(
            tangent_distance * half_angle_tangent, maximum_radius);
        *next_sketch = source;
        const bool had_radius = std::any_of(
            source.corner_radii.begin(), source.corner_radii.end(),
            [&](const auto& value) {
                return value.vertex_id == sketch_corner_drag_vertex_id_ &&
                    ((value.first_segment_id ==
                          sketch_corner_drag_first_segment_id_ &&
                      value.second_segment_id ==
                          sketch_corner_drag_second_segment_id_) ||
                     (value.first_segment_id ==
                          sketch_corner_drag_second_segment_id_ &&
                      value.second_segment_id ==
                          sketch_corner_drag_first_segment_id_));
            });
        try {
            static_cast<void>(next_sketch->add_corner_fillet(
                sketch_corner_drag_first_segment_id_,
                sketch_corner_drag_second_segment_id_, radius));
        } catch (const std::exception& error) {
            state_->setText(QString::fromUtf8(error.what()));
            return;
        }
        sketch_drag_changed_ = radius > 1.0e-9 || had_radius;
        show_sketch_drag_preview(*next_sketch);
        state_->setText(tr("Zaoblení rohu: R %1 mm").arg(radius, 0, 'f', 3));
        return;
    }
    if (next_sketch == sketches.end()) return;
    bool moved = false;
    moved = next_sketch->move_point(
        sketch_drag_point_id_, (*position)[0], (*position)[1]);
    if (!moved) {
        state_->setText(tr("Bod nelze přesunout mimo vazby nebo absolutní meze."));
        return;
    }
    sketch_drag_changed_ = true;
    show_sketch_drag_preview(*next_sketch);
    state_->setText(tr("Tažení bodu: poloha je pouze transientní do puštění LMB."));
}

void AssemblyWorkspaceWindow::end_sketch_point_drag() {
    if (!sketch_drag_document_ && !assembly_sketch_drag_document_ && section_drag_sketches_.empty()) return;
    const bool corner_radius_drag = sketch_corner_drag_source_.has_value();
    const std::string dragged_point_id = sketch_drag_point_id_;
    sketch_drag_point_id_.clear();
    const bool changed = sketch_drag_changed_;
    sketch_drag_changed_ = false;
    if (changed) {
        if(!section_drag_sketches_.empty()) { const auto draft=section_drag_sketches_.front();mutate_active_sketch([&](auto& target){target=draft;}); }
        else if (auto* part = workspace_.open_part(workspace_.active_document_id());
            part != nullptr && sketch_drag_document_) {
            part->session.commit(std::move(*sketch_drag_document_),
                part->session.calculated_boundaries());
        } else if (auto* assembly =
                       workspace_.open_assembly(workspace_.active_document_id());
                   assembly != nullptr && assembly_sketch_drag_document_) {
            assembly->session.commit(std::move(*assembly_sketch_drag_document_));
        }
        preserve_view_on_refresh_ = true;
        refresh_tabs();
    }
    sketch_drag_document_.reset();
    assembly_sketch_drag_document_.reset();
    section_drag_sketches_.clear();
    sketch_corner_drag_source_.reset();
    sketch_corner_drag_first_segment_id_.clear();
    sketch_corner_drag_second_segment_id_.clear();
    sketch_corner_drag_vertex_id_.clear();
    // A SketchPoint press arms a possible drag before the ordinary selection
    // callback runs. Releasing it without a mouse move is therefore an
    // ordinary one-click selection, not a scene-changing operation. Rebuilding
    // here used to clear the freshly synchronized Tree/View selection and made
    // the point unavailable to the immediately following Delete key.
    if (changed) refresh_scene();
    if (!dragged_point_id.empty()) {
        const auto* sketch = active_sketch();
        if (sketch != nullptr && sketch->find_point(dragged_point_id) != nullptr) {
            clear_selected_sketch_geometry();
            selected_sketch_point_id_ = dragged_point_id;
            selected_sketch_geometry_ids_ = {dragged_point_id};
            {
                // Rebuild selection as one atomic application-side state. The
                // ordinary Tree callback would otherwise clear the point on
                // the intermediate clearSelection() notification.
                const QSignalBlocker tree_signals(tree_);
                tree_->clearSelection();
                QTreeWidgetItemIterator iterator(tree_);
                while (*iterator != nullptr) {
                    auto* item = *iterator;
                    if (item->data(0, Qt::UserRole + 3).toString() ==
                            QStringLiteral("sketch-geometry") &&
                        item->data(0, Qt::UserRole).toString() ==
                            QString::fromStdString(dragged_point_id) &&
                        item->data(0, Qt::UserRole + 4).toString() ==
                            QString::fromStdString(active_sketch_id_)) {
                        item->setSelected(true);
                        tree_->setCurrentItem(item);
                        tree_->scrollToItem(item);
                        break;
                    }
                    ++iterator;
                }
            }
            viewer_->confirm_reference(active_sketch_id_,
                "point:" + dragged_point_id, {},
                zima::viewer::CandidateKind::SketchPoint);
            sketch_fix_point_action_->setEnabled(true);
        }
    }
    state_->setText(changed
        ? corner_radius_drag
            ? tr("Zaoblení rohu bylo uloženo jako jedna revize.")
            : tr("Poloha bodu byla uložena jako jedna revize.")
        : corner_radius_drag
            ? tr("Zaoblení rohu nebylo vytvořeno.")
            : tr("Poloha bodu se nezměnila."));
}

bool AssemblyWorkspaceWindow::begin_placement_reference_drag(
    const zima::viewer::ViewerCandidate& candidate) {
    if (candidate.kind != zima::viewer::CandidateKind::Dimension ||
        !candidate.semantic_key.starts_with("placement-reference:") ||
        candidate.owner_id != workspace_.active_document_id() ||
        properties_dialog_ != nullptr) return false;
    auto* assembly = workspace_.open_assembly(candidate.owner_id);
    if (assembly == nullptr) return false;
    const auto separator = candidate.semantic_key.rfind(':');
    if (separator == std::string::npos || separator <= 20) return false;
    const std::string occurrence_id =
        candidate.semantic_key.substr(20, separator - 20);
    std::size_t row_index{};
    try {
        row_index = static_cast<std::size_t>(
            std::stoul(candidate.semantic_key.substr(separator + 1)));
    } catch (const std::exception&) {
        return false;
    }
    const auto* occurrence =
        assembly->session.document().find_occurrence(occurrence_id);
    if (occurrence == nullptr ||
        row_index >= occurrence->placement_references.size()) return false;
    const auto& row = occurrence->placement_references[row_index];
    if(row.offset_locked)return false;
    if (row.mate_type != zima::assembly::MateKind::PlaneCoincident &&
        row.mate_type != zima::assembly::MateKind::PlaneAngle) return false;
    zima::kernel::Vec3 reference_point;
    zima::kernel::Vec3 reference_direction;
    {
        const auto target =
            assembly->session.document().resolve_plane(row.target_reference);
        if (target.status != zima::assembly::MateStatus::Valid) return false;
        reference_point = target.plane.point;
        reference_direction = target.plane.normal;
    }
    placement_reference_drag_document_ = assembly->session.document();
    placement_reference_drag_document_id_ = candidate.owner_id;
    placement_reference_drag_occurrence_id_ = occurrence_id;
    placement_reference_drag_index_ = row_index;
    placement_reference_drag_axis_point_ = reference_point;
    placement_reference_drag_axis_direction_ = reference_direction;
    placement_reference_drag_angular_ =
        row.mate_type == zima::assembly::MateKind::PlaneAngle;
    if(placement_reference_drag_angular_) {
        if(row.flip)placement_reference_drag_axis_direction_={-reference_direction.x,-reference_direction.y,-reference_direction.z};
        placement_reference_drag_plane_normal_=assembly->session.document().placement_reference_angle_axis(row);
        if(candidate.geometry_index<viewer_->mesh().dimensions.size())
            placement_reference_drag_plane_normal_=viewer_->mesh().dimensions[candidate.geometry_index].plane_normal;
    }
    placement_reference_drag_changed_ = false;
    state_->setText(placement_reference_drag_angular_
        ? tr("Tažením měníte úhel reference umístění.")
        : tr("Tažením měníte odsazení reference umístění."));
    return true;
}

void AssemblyWorkspaceWindow::update_placement_reference_drag(
    const zima::kernel::Vec3& ray_origin,
    const zima::kernel::Vec3& ray_direction) {
    if (!placement_reference_drag_document_) return;
    double value{};
    try {
        value = placement_reference_drag_angular_
            ? zima::assembly::AssemblyDocument::project_angular_drag_value(
                placement_reference_drag_axis_point_,
                placement_reference_drag_axis_direction_,
                placement_reference_drag_plane_normal_,
                ray_origin, ray_direction)
            : zima::assembly::AssemblyDocument::project_linear_drag_value(
                placement_reference_drag_axis_point_,
                placement_reference_drag_axis_direction_,
                ray_origin, ray_direction);
    } catch (const std::invalid_argument&) {
        return;
    }
    auto* occurrence = placement_reference_drag_document_->find_occurrence(
        placement_reference_drag_occurrence_id_);
    if (occurrence == nullptr ||
        placement_reference_drag_index_ >= occurrence->placement_references.size())
        return;
    auto& row = occurrence->placement_references[placement_reference_drag_index_];
    if (row.lower_limit) value = std::max(value, *row.lower_limit);
    if (row.upper_limit) value = std::min(value, *row.upper_limit);
    if (std::abs(value - row.offset) <= 1.0e-9) return;
    const double previous_value = row.offset;
    row.offset = value;
    try { placement_reference_drag_document_->calculate_placement_references(); }
    catch (const std::exception& error) {
        row.offset = previous_value;
        state_->setText(QString::fromUtf8(error.what()));
        return;
    }
    placement_reference_drag_changed_ = true;
    if (placement_reference_drag_document_id_ != workspace_.displayed_document_id() &&
        !active_occurrence_path_.empty()) {
        viewer_->set_mesh(workspace_.build_scene_with_assembly_override(
            workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(active_occurrence_path_),
            *placement_reference_drag_document_), false);
    } else {
        viewer_->set_mesh(placement_reference_drag_document_->build_scene(), false);
    }
    state_->setText(placement_reference_drag_angular_
        ? tr("Úhel reference: %1°").arg(value, 0, 'f', 3)
        : tr("Odsazení reference: %1 mm").arg(value, 0, 'f', 3));
}

void AssemblyWorkspaceWindow::end_placement_reference_drag() {
    if (!placement_reference_drag_document_) return;
    const std::string document_id = placement_reference_drag_document_id_;
    auto result = std::move(*placement_reference_drag_document_);
    const bool changed = placement_reference_drag_changed_;
    placement_reference_drag_document_.reset();
    placement_reference_drag_document_id_.clear();
    placement_reference_drag_occurrence_id_.clear();
    placement_reference_drag_index_ = 0;
    placement_reference_drag_changed_ = false;
    placement_reference_drag_angular_ = false;
    if (changed) {
        if (auto* assembly = workspace_.open_assembly(document_id)) {
            assembly->session.commit(std::move(result));
        }
    }
    refresh_tabs();
    refresh_scene();
}

void AssemblyWorkspaceWindow::update_sketch_dimension_drag(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (sketch_drag_dimension_id_.empty() ||
        (!sketch_drag_document_ && !assembly_sketch_drag_document_ && section_drag_sketches_.empty())) return;
    auto& sketches = !section_drag_sketches_.empty() ? section_drag_sketches_ : sketch_drag_document_
        ? sketch_drag_document_->sketches
        : assembly_sketch_drag_document_->sketches;
    const auto sketch = std::find_if(sketches.begin(), sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == sketches.end()) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position || !sketch->set_dimension_placement(
            sketch_drag_dimension_id_, (*position)[0], (*position)[1])) return;
    sketch_drag_changed_ = true;
    show_sketch_drag_preview(*sketch);
    state_->setText(tr("Tažení kóty: umístění se uloží po puštění LMB."));
}

void AssemblyWorkspaceWindow::end_sketch_dimension_drag() {
    if (sketch_drag_dimension_id_.empty()) return;
    sketch_drag_dimension_id_.clear();
    const bool changed = sketch_drag_changed_;
    sketch_drag_changed_ = false;
    if (changed) {
        if(!section_drag_sketches_.empty()) { const auto draft=section_drag_sketches_.front();mutate_active_sketch([&](auto& target){target=draft;}); }
        else if (auto* part = workspace_.open_part(workspace_.active_document_id());
            part != nullptr && sketch_drag_document_) {
            part->session.commit(std::move(*sketch_drag_document_),
                part->session.calculated_boundaries());
        } else if (auto* assembly =
                       workspace_.open_assembly(workspace_.active_document_id());
                   assembly != nullptr && assembly_sketch_drag_document_) {
            assembly->session.commit(std::move(*assembly_sketch_drag_document_));
        }
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
    }
    sketch_drag_document_.reset();
    assembly_sketch_drag_document_.reset();
    section_drag_sketches_.clear();
    // A simple click selects a dimension but also passes through this drag
    // gesture path.  Do not rebuild the scene on release when its placement
    // did not change: that rebuild used to discard the just-confirmed cyan
    // selection and return the dimension to its ordinary yellow colour.
}

} // namespace zima::app
