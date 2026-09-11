#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::start_sketch_point() {
    if (properties_dialog_ != nullptr) return;
    if (active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_point_active_ = true;
    set_sketch_placement_selection_contract();
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    state_->setText(tr("Bod skici: určete polohu. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::start_sketch_segment(bool construction) {
    if (properties_dialog_ != nullptr) return;
    if (active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_segment_active_ = true;
    set_sketch_placement_selection_contract();
    sketch_segment_construction_ = construction;
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    pending_segment_start_.reset();
    viewer_->set_transient_edges({});
    state_->setText(construction
        ? tr("Konstrukční čára: určete první bod. Escape příkaz zruší.")
        : tr("Úsečka skici: určete první bod. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::start_sketch_polyline() {
    start_sketch_segment();
    if (!sketch_segment_active_) return;
    sketch_polyline_active_ = true;
    sketch_polyline_arc_mode_ = false;
    pending_polyline_tangent_geometry_id_.clear();
    state_->setText(tr(
        "Lomená čára: určete první bod. Prostřední tlačítko ukončí aktuální řetězec."));
}

void AssemblyWorkspaceWindow::cancel_sketch_segment() {
    template_region_picking_=false;template_region_first_.reset();
    // Switching directly from Trim to another Sketch command commits the
    // accumulated preview as one revision. Escape cancels Trim explicitly in
    // cancel_current_sketch_step() before it reaches this general reset path.
    if (sketch_trim_active_) {
        static_cast<void>(finish_sketch_trim());
        return;
    }
    if (auto* dialog=dynamic_cast<SketchConstraintsDialog*>(sketch_constraints_dialog_.data()))
        dialog->clear_active_choice();
    sketch_external_reference_active_ = false;
    selected_sketch_external_reference_id_.clear();
    if (sketch_external_reference_action_ != nullptr) {
        const QSignalBlocker blocker(sketch_external_reference_action_);
        sketch_external_reference_action_->setChecked(false);
    }
    if (sketch_external_profile_action_ != nullptr) {
        const QSignalBlocker blocker(sketch_external_profile_action_);
        sketch_external_profile_action_->setChecked(false);
    }
    sketch_external_profile_active_ = false;
    sketch_point_active_ = false;
    sketch_segment_active_ = false;
    sketch_segment_construction_ = false;
    sketch_polyline_active_ = false;
    sketch_polyline_arc_mode_ = false;
    pending_polyline_tangent_geometry_id_.clear();
    sketch_rectangle_active_ = false;
    sketch_rectangle_axis_selecting_ = false;
    pending_rectangle_axis_id_.clear();
    sketch_polygon_active_ = false;
    sketch_corner_fillet_active_ = false;
    pending_corner_fillet_segment_id_.clear();
    cancel_sketch_trim();
    sketch_mirror_active_ = false;
    sketch_mirror_selecting_sources_ = false;
    sketch_circle_active_ = false;
    sketch_arc_active_ = false;
    sketch_arc_clockwise_ = false;
    sketch_ellipse_active_ = false;
    sketch_elliptical_arc_active_ = false;
    sketch_bspline_active_ = false;
    sketch_bspline_interpolating_ = false;
    sketch_coincident_active_ = false;
    sketch_midpoint_active_ = false;
    sketch_symmetric_active_ = false;
    sketch_concentric_active_ = false;
    sketch_tangent_active_ = false;
    sketch_common_tangent_active_ = false;
    sketch_segment_pair_active_ = false;
    sketch_point_dimension_active_ = false;
    if (sketch_dimension_action_ != nullptr) {
        const QSignalBlocker blocker(sketch_dimension_action_);
        sketch_dimension_action_->setChecked(false);
    }
    sketch_universal_dimension_active_ = false;
    universal_dimension_references_.clear();
    universal_pending_dimension_.reset();
    universal_corner_radius_dimension_id_.clear();
    universal_dimension_cursor_.reset();
    if (sketch_universal_dimension_action_ != nullptr) {
        const QSignalBlocker blocker(sketch_universal_dimension_action_);
        sketch_universal_dimension_action_->setChecked(false);
    }
    sketch_line_pair_dimension_active_ = false;
    pending_point_dimension_first_id_.clear();
    pending_point_dimension_second_id_.clear();
    pending_point_dimension_vertex_id_.clear();
    pending_point_dimension_cursor_.reset();
    pending_line_dimension_reference_id_.clear();
    pending_sketch_dimension_.reset();
    universal_dimension_layout_={};
    pending_corner_radius_dimension_id_.clear();
    pending_segment_start_.reset();
    pending_sketch_snap_geometry_id_.clear();
    pending_sketch_snap_kind_.reset();
    pending_segment_start_snap_geometry_id_.clear();
    pending_segment_start_snap_kind_.reset();
    pending_rectangle_corner_snap_geometry_id_.clear();
    pending_rectangle_corner_snap_kind_.reset();
    sketch_segment_inference_cycle_ = 0;
    sketch_skip_candidate_snap_ = false;
    sketch_polyline_arc_mode_ = false;
    pending_rectangle_corner_.reset();
    pending_polygon_center_.reset();
    pending_mirror_geometry_ids_.clear();
    pending_mirror_axis_id_.clear();
    pending_circle_center_.reset();
    pending_circle_tangent_inference_.reset();
    pending_arc_center_.reset();
    pending_arc_start_.reset();
    pending_ellipse_center_.reset();
    pending_ellipse_major_.reset();
    pending_elliptical_arc_center_.reset();
    pending_elliptical_arc_major_.reset();
    pending_elliptical_arc_minor_.reset();
    pending_elliptical_arc_start_.reset();
    pending_elliptical_arc_reversed_ = false;
    pending_curve_point_snaps_.clear();
    pending_bspline_points_.clear();
    pending_coincident_point_id_.clear();
    pending_midpoint_point_id_.clear();
    pending_symmetric_point_ids_.clear();
    pending_concentric_geometry_id_.clear();
    pending_tangent_geometry_id_.clear();
    pending_tangent_reference_is_segment_ = false;
    pending_tangent_reference_supports_curve_pair_ = false;
    pending_common_tangent_curve_id_.clear();
    pending_common_tangent_first_hint_.reset();
    sketch_pointer_position_.reset();
    pending_pair_geometry_id_.clear();
    pending_pair_reference_is_circular_ = false;
    // A previously confirmed Sketch entity suppresses hover updates in the
    // shared viewer.  Every tool transition must release that latch so the
    // common candidate list can immediately offer points, the local origin
    // and both infinite Sketch axes for visual snapping.
    if (viewer_ != nullptr) {
        viewer_->clear_selection();
        viewer_->set_sketch_box_selection(false, {});
    }
    viewer_->set_candidate_filter({});
    viewer_->set_transient_edges({});
    viewer_->set_sketch_cursor(std::nullopt);
    viewer_->set_sketch_relation_highlights({});
    sync_sketch_tool_action_checks();
}

bool AssemblyWorkspaceWindow::confirm_current_sketch_step() {
    return finish_edge_treatment_selection() || finish_sketch_bspline() ||
        finish_sketch_polyline() || finish_sketch_mirror() ||
        finish_sketch_trim();
}

bool AssemblyWorkspaceWindow::finish_current_sketch_tool() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return false;
    const bool active = sketch_point_active_ || sketch_segment_active_ ||
        sketch_external_reference_active_ ||
        sketch_rectangle_active_ || sketch_polygon_active_ || sketch_trim_active_ ||
        sketch_circle_active_ || sketch_offset_dialog_ || sketch_mirror_active_ || sketch_arc_active_ ||
        sketch_ellipse_active_ || sketch_elliptical_arc_active_ ||
        sketch_bspline_active_ || sketch_coincident_active_ ||
        sketch_midpoint_active_ || sketch_symmetric_active_ ||
        sketch_concentric_active_ || sketch_tangent_active_ ||
        sketch_common_tangent_active_ ||
        sketch_segment_pair_active_ || sketch_point_dimension_active_ ||
        sketch_universal_dimension_active_ ||
        sketch_line_pair_dimension_active_ || pending_sketch_dimension_ ||
        sketch_corner_fillet_active_;
    if (active) {
        // A B-spline is the one drawing command whose object is intentionally
        // accumulated across an arbitrary number of LMB confirmations.  The
        // universal MMB double-click therefore has two jobs here: commit the
        // points that were already confirmed, then leave the command.  It
        // must never sample the cursor position belonging to the finishing
        // gesture (MeshView does not route MMB through world_click_callback).
        if (sketch_bspline_active_ && !pending_bspline_points_.empty()) {
            if (pending_bspline_points_.size() < 3) {
                state_->setText(tr(
                    "B-spline vyžaduje alespoň 3 potvrzené body; "
                    "neúplná křivka byla zrušena."));
                // The universal finishing gesture must still leave the tool.
                // Fewer than three points own no committable geometry, so the
                // general reset below can discard them transactionally.
            } else {
                static_cast<void>(finish_sketch_bspline());
                // A failed model mutation deliberately leaves the pending input
                // intact so the user can correct or cancel it explicitly.
                if (!pending_bspline_points_.empty()) return true;
            }
        }
        if (sketch_trim_active_ && sketch_trim_changed_) {
            static_cast<void>(finish_sketch_trim());
            state_->setText(tr("Ořezání bylo dokončeno; aktivní je Výběr."));
            return true;
        }
        cancel_sketch_segment();
        // Finishing a Sketch command with MMB double-click is a command-state
        // transition, not a selection gesture.  Do not carry the command's
        // temporary participants into ordinary Selection, where they would
        // be rendered as one cyan multi-selection (often the whole Sketch).
        clear_sketch_confirmed_selection();
        preserve_view_on_refresh_ = true;
        refresh_scene();
    } else {
        viewer_->clear_selection();
    }
    state_->setText(tr("Nástroj skici byl dokončen; aktivní je Výběr."));
    return true;
}

bool AssemblyWorkspaceWindow::cancel_current_sketch_step(
    bool right_click_behavior) {
    if (active_sketch_id_.empty()) return false;
    if (!right_click_behavior && sketch_external_reference_active_) {
        set_sketch_external_reference_mode(false);
        state_->setText(tr("Výběr externích referencí byl ukončen."));
        return true;
    }
    if (sketch_trim_active_) {
        cancel_sketch_trim();
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(tr("Ořezání bylo zrušeno beze změny skici."));
        return true;
    }
    if (right_click_behavior && sketch_segment_active_ &&
        !sketch_polyline_active_ && pending_segment_start_) {
        ++sketch_segment_inference_cycle_;
        sketch_inference_cycle_refresh_ = true;
        static_cast<void>(viewer_->refresh_current_pointer_preview());
        state_->setText(tr(
            "Úsečka: RMB přepnulo platnou variantu inference; potvrzení uloží zobrazenou vazbu."));
        return true;
    }
    if (right_click_behavior && sketch_polygon_active_ && pending_polygon_center_) {
        sketch_polygon_sides_ = sketch_polygon_sides_ == 4 ? 6
            : sketch_polygon_sides_ == 6 ? 8 : 4;
        viewer_->set_transient_edges({});
        state_->setText(tr("Mnohoúhelník: %1 stran. Určete vrchol; RMB přepíná počet stran.")
            .arg(sketch_polygon_sides_));
        return true;
    }
    if (right_click_behavior && sketch_polyline_active_ && pending_segment_start_) {
        if (sketch_polyline_arc_mode_) {
            sketch_polyline_arc_mode_ = false;
            viewer_->set_transient_edges({});
            state_->setText(tr(
                "Lomená čára: následující část bude úsečka. RMB přepíná oblouk."));
            return true;
        }
        if (!pending_polyline_tangent_geometry_id_.empty()) {
            sketch_polyline_arc_mode_ = true;
            viewer_->set_transient_edges({});
            state_->setText(tr(
                "Lomená čára: následující část bude tečný oblouk. RMB přepíná úsečku."));
        } else {
            state_->setText(tr(
                "Oblouk je dostupný až po první úsečce nebo oblouku řetězce."));
        }
        return true;
    }
    if (right_click_behavior && sketch_arc_active_ && pending_arc_start_) {
        sketch_arc_clockwise_ = !sketch_arc_clockwise_;
        viewer_->set_transient_edges({});
        state_->setText(sketch_arc_clockwise_
            ? tr("Oblouk: směr po hodinových ručičkách. Určete koncový bod.")
            : tr("Oblouk: směr proti hodinovým ručičkám. Určete koncový bod."));
        return true;
    }
    if (right_click_behavior && sketch_rectangle_active_ && pending_rectangle_corner_ &&
        pending_rectangle_axis_id_.empty()) {
        const auto* sketch = active_sketch();
        if (sketch == nullptr) return false;
        std::set<std::string> construction_axes{
            "sketch_axis:x", "sketch_axis:y"};
        for (const auto& segment : sketch->segments) {
            if (segment.construction) construction_axes.insert(segment.id);
        }
        if (construction_axes.empty()) {
            state_->setText(tr(
                "Orientovaný obdélník vyžaduje konstrukční čáru jako osu."));
            return true;
        }
        sketch_rectangle_axis_selecting_ = true;
        viewer_->set_selection_contract({
            zima::viewer::CandidateKind::SketchSegment,
            zima::viewer::CandidateKind::SketchAxis});
        const auto owner_id = active_sketch_id_;
        viewer_->set_candidate_filter(
            [owner_id, construction_axes](const auto& candidate) {
                if (candidate.owner_id != owner_id) return false;
                if (candidate.kind == zima::viewer::CandidateKind::SketchAxis) {
                    return construction_axes.contains(candidate.semantic_key);
                }
                return candidate.kind ==
                        zima::viewer::CandidateKind::SketchSegment &&
                    candidate.semantic_key.starts_with("segment:") &&
                    construction_axes.contains(candidate.semantic_key.substr(8));
            });
        viewer_->set_transient_edges({});
        state_->setText(tr(
            "Orientovaný obdélník: vyberte osu X/Y nebo konstrukční čáru jako osu souměrnosti."));
        return true;
    }
    const bool relation_tool = sketch_coincident_active_ ||
        sketch_midpoint_active_ || sketch_symmetric_active_ ||
        sketch_concentric_active_ || sketch_tangent_active_ ||
        sketch_common_tangent_active_ ||
        sketch_segment_pair_active_ || sketch_point_dimension_active_ ||
        sketch_universal_dimension_active_ ||
        sketch_line_pair_dimension_active_ || pending_sketch_dimension_ ||
        !pending_corner_radius_dimension_id_.empty() ||
        sketch_corner_fillet_active_;
    if (relation_tool) {
        const bool has_pending_relation =
            !pending_coincident_point_id_.empty() ||
            !pending_midpoint_point_id_.empty() ||
            !pending_symmetric_point_ids_.empty() ||
            !pending_concentric_geometry_id_.empty() ||
            !pending_tangent_geometry_id_.empty() ||
            !pending_common_tangent_curve_id_.empty() ||
            !pending_pair_geometry_id_.empty() ||
            !pending_point_dimension_first_id_.empty() ||
            !pending_point_dimension_second_id_.empty() ||
            !pending_line_dimension_reference_id_.empty() ||
            pending_sketch_dimension_.has_value() ||
            !universal_dimension_references_.empty() ||
            universal_pending_dimension_.has_value() ||
            !pending_corner_radius_dimension_id_.empty() ||
            !pending_corner_fillet_segment_id_.empty();
        if (!right_click_behavior && !has_pending_relation) {
            cancel_sketch_segment();
            preserve_view_on_refresh_ = true;
            refresh_scene();
            state_->setText(tr("Nástroj vazby nebo kóty byl ukončen."));
            return true;
        }
        pending_coincident_point_id_.clear();
        pending_midpoint_point_id_.clear();
        pending_symmetric_point_ids_.clear();
        pending_concentric_geometry_id_.clear();
        pending_tangent_geometry_id_.clear();
        pending_tangent_reference_is_segment_ = false;
        pending_tangent_reference_supports_curve_pair_ = false;
        pending_common_tangent_curve_id_.clear();
        pending_common_tangent_first_hint_.reset();
        pending_pair_geometry_id_.clear();
        pending_pair_reference_is_circular_ = false;
        pending_point_dimension_first_id_.clear();
        pending_point_dimension_second_id_.clear();
        pending_point_dimension_vertex_id_.clear();
        pending_point_dimension_cursor_.reset();
        pending_line_dimension_reference_id_.clear();
        pending_sketch_dimension_.reset();
    universal_dimension_layout_={};
        universal_dimension_references_.clear();
        universal_pending_dimension_.reset();
        universal_corner_radius_dimension_id_.clear();
        universal_dimension_cursor_.reset();
        pending_corner_radius_dimension_id_.clear();
        pending_corner_fillet_segment_id_.clear();
        viewer_->clear_selection();
        viewer_->set_candidate_filter({});
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(tr("Aktuální výběr vazby nebo kóty byl zrušen."));
        return true;
    }
    const bool active = sketch_point_active_ || sketch_segment_active_ ||
        sketch_rectangle_active_ || sketch_polygon_active_ || sketch_circle_active_ ||
        sketch_arc_active_ || sketch_ellipse_active_ ||
        sketch_elliptical_arc_active_ || sketch_bspline_active_;
    if (!active) return false;
    const bool has_pending_geometry = pending_segment_start_.has_value() ||
        pending_rectangle_corner_.has_value() || pending_polygon_center_.has_value() ||
        pending_circle_center_.has_value() || pending_arc_center_.has_value() ||
        pending_arc_start_.has_value() || pending_ellipse_center_.has_value() ||
        pending_ellipse_major_.has_value() ||
        pending_elliptical_arc_center_.has_value() ||
        pending_elliptical_arc_major_.has_value() ||
        pending_elliptical_arc_minor_.has_value() ||
        pending_elliptical_arc_start_.has_value() ||
        !pending_bspline_points_.empty();
    if (!right_click_behavior && !has_pending_geometry) {
        cancel_sketch_segment();
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(tr("Nástroj geometrie byl ukončen."));
        return true;
    }
    pending_segment_start_.reset();
    pending_sketch_snap_geometry_id_.clear();
    pending_sketch_snap_kind_.reset();
    pending_segment_start_snap_geometry_id_.clear();
    pending_segment_start_snap_kind_.reset();
    pending_rectangle_corner_snap_geometry_id_.clear();
    pending_rectangle_corner_snap_kind_.reset();
    sketch_segment_inference_cycle_ = 0;
    sketch_skip_candidate_snap_ = false;
    pending_rectangle_corner_.reset();
    pending_polygon_center_.reset();
    pending_circle_center_.reset();
    pending_circle_tangent_inference_.reset();
    pending_arc_center_.reset();
    pending_arc_start_.reset();
    pending_ellipse_center_.reset();
    pending_ellipse_major_.reset();
    pending_elliptical_arc_center_.reset();
    pending_elliptical_arc_major_.reset();
    pending_elliptical_arc_minor_.reset();
    pending_elliptical_arc_start_.reset();
    pending_bspline_points_.clear();
    pending_curve_point_snaps_.clear();
    viewer_->set_transient_edges({});
    state_->setText(tr("Aktuální rozpracovaná geometrie byla zrušena; nástroj zůstává aktivní."));
    return true;
}

bool AssemblyWorkspaceWindow::finish_sketch_polyline() {
    if (!sketch_polyline_active_) return false;
    pending_segment_start_.reset();
    pending_segment_start_snap_geometry_id_.clear();
    pending_segment_start_snap_kind_.reset();
    sketch_polyline_arc_mode_ = false;
    pending_polyline_tangent_geometry_id_.clear();
    viewer_->set_transient_edges({});
    state_->setText(tr(
        "Řetězec lomené čáry dokončen. Kliknutím začnete nový řetězec."));
    return true;
}

void AssemblyWorkspaceWindow::start_sketch_rectangle() {
    if (properties_dialog_ != nullptr) return;
    if (active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_rectangle_active_ = true;
    set_sketch_placement_selection_contract();
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    state_->setText(tr("Obdélník skici: určete první roh. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_rectangle() {
    sketch_rectangle_active_ = false;
    sketch_rectangle_axis_selecting_ = false;
    pending_rectangle_axis_id_.clear();
    pending_rectangle_corner_.reset();
    pending_rectangle_corner_snap_geometry_id_.clear();
    pending_rectangle_corner_snap_kind_.reset();
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_polygon(unsigned sides) {
    if (properties_dialog_ != nullptr ||
        (sides != 4 && sides != 6 && sides != 8)) return;
    if (active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_polygon_active_ = true;
    set_sketch_placement_selection_contract();
    sketch_polygon_sides_ = sides;
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    state_->setText(tr("Pravidelný %1úhelník: určete střed. Escape příkaz zruší.")
        .arg(sides));
}

void AssemblyWorkspaceWindow::cancel_sketch_polygon() {
    sketch_polygon_active_ = false;
    pending_polygon_center_.reset();
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_trim() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return;
    // Finishing the previous command may replace the document/draft.
    cancel_sketch_segment();
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
    clear_selected_sketch_geometry();
    sketch_trim_preview_ = *sketch;
    sketch_trim_topology_ = zima::sketcher::sketch_trim_topology(
        *sketch_trim_preview_, true);
    sketch_trim_active_ = true;
    sketch_trim_changed_ = false;
    viewer_->clear_selection();
    sync_sketch_tool_action_checks();
    preserve_view_on_refresh_ = true;
    refresh_scene();
    state_->setText(tr(
        "Ořezání: klikněte na část, nebo tažením přeškrtněte více částí. "
        "Prostřední klik potvrdí vše jako jednu revizi; Escape vše zahodí."));
}

void AssemblyWorkspaceWindow::cancel_sketch_trim() {
    sketch_trim_active_ = false;
    sketch_trim_changed_ = false;
    sketch_trim_preview_.reset();
    sketch_trim_topology_.clear();
    sketch_trim_path_.clear();
    sketch_trim_pressed_piece_.reset();
    if (viewer_ != nullptr) viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_corner_fillet() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return;
    cancel_sketch_segment();
    sketch_corner_fillet_active_ = true;
    pending_corner_fillet_segment_id_.clear();
    selection_action_->setChecked(true);
    viewer_->set_selection_contract({
        zima::viewer::CandidateKind::SketchSegment});
    const auto owner_id = active_sketch_id_;
    viewer_->set_candidate_filter([owner_id](const auto& candidate) {
        return candidate.owner_id == owner_id &&
            candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
            candidate.semantic_key.starts_with("segment:");
    });
    state_->setText(tr("Zaoblení rohu: vyberte první úsečku."));
}

void AssemblyWorkspaceWindow::accept_sketch_corner_fillet_segment(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_corner_fillet_active_ ||
        candidate.owner_id != active_sketch_id_ ||
        candidate.kind != zima::viewer::CandidateKind::SketchSegment ||
        !candidate.semantic_key.starts_with("segment:")) return;
    const auto segment_id = candidate.semantic_key.substr(8);
    if (pending_corner_fillet_segment_id_.empty()) {
        pending_corner_fillet_segment_id_ = segment_id;
        viewer_->set_candidate_filter(
            [owner_id = active_sketch_id_, segment_id](const auto& value) {
                return value.owner_id == owner_id &&
                    value.kind == zima::viewer::CandidateKind::SketchSegment &&
                    value.semantic_key.starts_with("segment:") &&
                    value.semantic_key.substr(8) != segment_id;
            });
        state_->setText(tr("Zaoblení rohu: vyberte druhou připojenou úsečku."));
        return;
    }
    const auto first_segment_id = pending_corner_fillet_segment_id_;
    sketch_corner_fillet_active_ = false;
    pending_corner_fillet_segment_id_.clear();
    viewer_->set_candidate_filter({});
    zima::sketcher::SketchDimension initial;
    initial.id = "corner-fillet-preview";
    initial.kind = zima::sketcher::DimensionKind::Radius;
    initial.value = 1.0;
    auto* dialog = new SketchDimensionPropertiesDialog(
        std::move(initial), false,
        [this, sketch_id = active_sketch_id_, first_segment_id,
         second_segment_id = segment_id](zima::sketcher::SketchDimension value) {
            if (active_sketch_id_ != sketch_id ||
                !mutate_active_sketch([&](auto& sketch) {
                    static_cast<void>(sketch.add_corner_fillet(
                        first_segment_id, second_segment_id, value.value));
                })) {
                throw std::runtime_error("Sketch is no longer active");
            }
            preserve_view_on_refresh_ = true;
        }, this, tr("Zaoblení rohu"));
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        refresh_tabs();
        refresh_scene();
    });
    dialog->show();
}

bool AssemblyWorkspaceWindow::begin_sketch_trim_gesture(
    const std::optional<zima::viewer::ViewerCandidate>& candidate,
    const zima::kernel::Vec3& origin,
    const zima::kernel::Vec3& direction) {
    if (!sketch_trim_active_ || !sketch_trim_preview_) return false;
    const auto position = sketch_trim_preview_->intersect_ray(origin, direction);
    if (!position) return false;
    sketch_trim_path_ = {*position};
    sketch_trim_pressed_piece_.reset();
    if (candidate &&
        candidate->kind == zima::viewer::CandidateKind::SketchTrimPiece &&
        candidate->owner_id == active_sketch_id_ &&
        candidate->semantic_key.starts_with("trim_piece:")) {
        const auto index_text = candidate->semantic_key.substr(11);
        if (!index_text.empty() && std::all_of(
                index_text.begin(), index_text.end(), [](unsigned char character) {
                    return std::isdigit(character) != 0;
                })) {
            try {
                const auto index = static_cast<std::size_t>(std::stoull(index_text));
                if (index < sketch_trim_topology_.size()) {
                    sketch_trim_pressed_piece_ = index;
                    zima::kernel::ViewerEdge highlight;
                    for (const auto& point : sketch_trim_topology_[index].points) {
                        highlight.points.push_back(
                            sketch_trim_preview_->world_point(point[0], point[1]));
                    }
                    viewer_->set_transient_edges({std::move(highlight)});
                }
            } catch (const std::exception&) {
                sketch_trim_pressed_piece_.reset();
            }
        }
    }
    return true;
}

void AssemblyWorkspaceWindow::update_sketch_trim_gesture(
    const zima::kernel::Vec3& origin,
    const zima::kernel::Vec3& direction) {
    if (!sketch_trim_active_ || !sketch_trim_preview_ ||
        sketch_trim_path_.empty()) return;
    const auto position = sketch_trim_preview_->intersect_ray(origin, direction);
    if (!position) return;
    const auto& previous = sketch_trim_path_.back();
    const double distance = std::hypot(
        (*position)[0] - previous[0], (*position)[1] - previous[1]);
    if (distance > viewer_->world_tolerance_for_pixels(0.5)) {
        sketch_trim_path_.push_back(*position);
    }
    double path_length = 0.0;
    for (std::size_t index = 1; index < sketch_trim_path_.size(); ++index) {
        path_length += std::hypot(
            sketch_trim_path_[index][0] - sketch_trim_path_[index - 1][0],
            sketch_trim_path_[index][1] - sketch_trim_path_[index - 1][1]);
    }
    std::vector<zima::kernel::ViewerEdge> overlay;
    if (path_length > viewer_->world_tolerance_for_pixels(3.0)) {
        const auto crossed = zima::sketcher::sketch_trim_pieces_crossed_by_path(
            sketch_trim_topology_, sketch_trim_path_,
            viewer_->world_tolerance_for_pixels(4.0));
        overlay.reserve(crossed.size() + 1);
        for (const auto& piece : crossed) {
            zima::kernel::ViewerEdge edge;
            for (const auto& point : piece.points) {
                edge.points.push_back(
                    sketch_trim_preview_->world_point(point[0], point[1]));
            }
            overlay.push_back(std::move(edge));
        }
        zima::kernel::ViewerEdge gesture;
        for (const auto& point : sketch_trim_path_) {
            gesture.points.push_back(
                sketch_trim_preview_->world_point(point[0], point[1]));
        }
        overlay.push_back(std::move(gesture));
    } else if (sketch_trim_pressed_piece_ &&
               *sketch_trim_pressed_piece_ < sketch_trim_topology_.size()) {
        zima::kernel::ViewerEdge edge;
        for (const auto& point :
             sketch_trim_topology_[*sketch_trim_pressed_piece_].points) {
            edge.points.push_back(
                sketch_trim_preview_->world_point(point[0], point[1]));
        }
        overlay.push_back(std::move(edge));
    }
    viewer_->set_transient_edges(std::move(overlay));
}

void AssemblyWorkspaceWindow::end_sketch_trim_gesture() {
    if (!sketch_trim_active_ || !sketch_trim_preview_ ||
        sketch_trim_path_.empty()) return;
    double path_length = 0.0;
    for (std::size_t index = 1; index < sketch_trim_path_.size(); ++index) {
        path_length += std::hypot(
            sketch_trim_path_[index][0] - sketch_trim_path_[index - 1][0],
            sketch_trim_path_[index][1] - sketch_trim_path_[index - 1][1]);
    }
    std::vector<zima::sketcher::SketchTrimPiece> removed;
    if (path_length > viewer_->world_tolerance_for_pixels(3.0)) {
        removed = zima::sketcher::sketch_trim_pieces_crossed_by_path(
            sketch_trim_topology_, sketch_trim_path_,
            viewer_->world_tolerance_for_pixels(4.0));
    } else if (sketch_trim_pressed_piece_ &&
               *sketch_trim_pressed_piece_ < sketch_trim_topology_.size()) {
        removed.push_back(sketch_trim_topology_[*sketch_trim_pressed_piece_]);
    }
    sketch_trim_path_.clear();
    sketch_trim_pressed_piece_.reset();
    viewer_->set_transient_edges({});
    if (removed.empty()) {
        state_->setText(tr(
            "Ořezání: gesto nezasáhlo žádnou ořezatelnou část."));
        return;
    }
    try {
        static_cast<void>(zima::sketcher::apply_sketch_trim(
            *sketch_trim_preview_, removed));
        sketch_trim_changed_ = true;
        sketch_trim_topology_ = zima::sketcher::sketch_trim_topology(
            *sketch_trim_preview_, true);
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(tr(
            "Ořezání je pouze v náhledu. Pokračujte LMB; dvojklik MMB "
            "dokončí příkaz a Escape obnoví původní skicu."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
}

bool AssemblyWorkspaceWindow::finish_sketch_trim() {
    if (!sketch_trim_active_) return false;
    if (active_sketch() == nullptr || !sketch_trim_preview_) {
        cancel_sketch_trim();
        cancel_sketch_segment();
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(tr(
            "Ořezání bylo ukončeno, protože aktivní skica již není dostupná."));
        return true;
    }
    if (!sketch_trim_changed_) {
        cancel_sketch_trim();
        cancel_sketch_segment();
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(tr("Ořezání ukončeno beze změny."));
        return true;
    }
    try {
        if (!mutate_active_sketch(
                [&](auto& sketch) { sketch = *sketch_trim_preview_; })) {
            cancel_sketch_trim();
            cancel_sketch_segment();
            preserve_view_on_refresh_ = true;
            refresh_scene();
            state_->setText(tr(
                "Ořezání nebylo možné uložit; aktivní je opět Výběr."));
            return true;
        }
        // Return through the same complete tool-reset path as every other
        // Sketch command. Merely clearing sketch_trim_active_ left the viewer
        // candidate contract and toolbar state in Trim mode, so points and
        // Dimensions stopped responding after a successful Trim.
        cancel_sketch_trim();
        cancel_sketch_segment();
        clear_selected_sketch_geometry();
        viewer_->clear_selection();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("Ořezání skici bylo potvrzeno jako jedna Part revize."));
    } catch (const std::exception& error) {
        cancel_sketch_trim();
        cancel_sketch_segment();
        clear_selected_sketch_geometry();
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(tr("Ořezání nebylo uloženo: %1. Aktivní je Výběr.")
            .arg(QString::fromUtf8(error.what())));
    }
    return true;
}

void AssemblyWorkspaceWindow::start_sketch_mirror() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return;
    if (active_sketch() == nullptr) return;
    const std::string source_id = !selected_sketch_segment_id_.empty()
        ? selected_sketch_segment_id_
        : !selected_sketch_circle_id_.empty() ? selected_sketch_circle_id_
        : !selected_sketch_arc_id_.empty() ? selected_sketch_arc_id_
        : !selected_sketch_ellipse_id_.empty() ? selected_sketch_ellipse_id_
        : !selected_sketch_elliptical_arc_id_.empty()
            ? selected_sketch_elliptical_arc_id_
        : !selected_sketch_bspline_id_.empty() ? selected_sketch_bspline_id_
        : selected_sketch_point_id_;
    const auto selected_ids = selected_sketch_geometry_ids_;
    if (source_id.empty() && selected_ids.empty()) {
        state_->setText(tr(
            "Zrcadlení: nejprve ve Výběru označte geometrii, potom spusťte příkaz."));
        return;
    }
    cancel_sketch_segment();
    sketch_mirror_active_ = true;
    sketch_mirror_selecting_sources_ = false;
    if (!selected_ids.empty()) {
        pending_mirror_geometry_ids_.assign(
            selected_ids.begin(), selected_ids.end());
    } else if (!source_id.empty()) {
        pending_mirror_geometry_ids_ = {source_id};
    }
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_point_id_.clear();
    viewer_->clear_selection();
    viewer_->set_selection_contract(sketch_mirror_selecting_sources_
        ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                      zima::viewer::CandidateKind::SketchPoint,
                      zima::viewer::CandidateKind::SketchCurve}
        : std::vector{zima::viewer::CandidateKind::SketchSegment,
                      zima::viewer::CandidateKind::SketchAxis});
    sketch_mirror_action_->setEnabled(false);
    state_->setText(tr(
        "Zrcadlení: LMB vyberte úsečku nebo osu X/Y skici. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_mirror() {
    sketch_mirror_active_ = false;
    sketch_mirror_selecting_sources_ = false;
    pending_mirror_geometry_ids_.clear();
    pending_mirror_axis_id_.clear();
}

void AssemblyWorkspaceWindow::accept_sketch_mirror_source(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_offset_dialog_ || sketch_mirror_active_ || !sketch_mirror_selecting_sources_ ||
        candidate.owner_id != active_sketch_id_) return;
    std::string source_id;
    if (candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
        candidate.semantic_key.starts_with("segment:")) {
        source_id = candidate.semantic_key.substr(8);
    } else if (candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
               candidate.semantic_key.starts_with("point:")) {
        source_id = candidate.semantic_key.substr(6);
    } else if (candidate.kind == zima::viewer::CandidateKind::SketchCurve) {
        for (const std::string_view prefix : {
                std::string_view{"circle:"}, std::string_view{"arc:"},
                std::string_view{"ellipse:"}, std::string_view{"elliptical_arc:"},
                std::string_view{"bspline:"}}) {
            if (candidate.semantic_key.starts_with(prefix)) {
                source_id = candidate.semantic_key.substr(prefix.size());
                break;
            }
        }
    }
    if (source_id.empty()) return;
    const auto selected = std::find(
        pending_mirror_geometry_ids_.begin(), pending_mirror_geometry_ids_.end(),
        source_id);
    if (selected == pending_mirror_geometry_ids_.end()) {
        pending_mirror_geometry_ids_.push_back(std::move(source_id));
    } else {
        pending_mirror_geometry_ids_.erase(selected);
    }
    viewer_->clear_selection();
    state_->setText(tr(
        "Zrcadlení: vybráno %1 položek; prostředním kliknutím pokračujte na osu.")
        .arg(pending_mirror_geometry_ids_.size()));
}

void AssemblyWorkspaceWindow::accept_sketch_mirror_axis(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_offset_dialog_ || sketch_mirror_active_ || pending_mirror_geometry_ids_.empty() ||
        candidate.owner_id != active_sketch_id_) return;
    std::string axis_id;
    if (candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
        candidate.semantic_key.starts_with("segment:")) {
        axis_id = candidate.semantic_key.substr(8);
    } else if (candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
               (candidate.semantic_key == "sketch_axis:x" ||
                candidate.semantic_key == "sketch_axis:y")) {
        axis_id = candidate.semantic_key;
    } else {
        return;
    }
    if (std::find(pending_mirror_geometry_ids_.begin(),
                  pending_mirror_geometry_ids_.end(), axis_id) !=
        pending_mirror_geometry_ids_.end()) {
        state_->setText(tr("Zdrojová úsečka nemůže být současně osou zrcadlení."));
        return;
    }
    pending_mirror_axis_id_ = std::move(axis_id);
    static_cast<void>(finish_sketch_mirror());
}

bool AssemblyWorkspaceWindow::finish_sketch_mirror() {
    if (!sketch_mirror_active_) return false;
    if (sketch_mirror_selecting_sources_) {
        if (pending_mirror_geometry_ids_.empty()) {
            state_->setText(tr("Zrcadlení: vyberte alespoň jednu geometrii nebo bod."));
            return true;
        }
        sketch_mirror_selecting_sources_ = false;
        viewer_->clear_selection();
        viewer_->set_selection_contract({zima::viewer::CandidateKind::SketchSegment,
                                         zima::viewer::CandidateKind::SketchAxis});
        state_->setText(tr(
            "Zrcadlení: vyberte úsečku nebo osu X/Y skici. Escape příkaz zruší."));
        return true;
    }
    if (pending_mirror_geometry_ids_.empty() || pending_mirror_axis_id_.empty()) {
        state_->setText(tr("Zrcadlení: nejprve vyberte osu."));
        return true;
    }
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                static_cast<void>(sketch.mirror_geometry(
                    pending_mirror_geometry_ids_, pending_mirror_axis_id_));
            })) return false;
        cancel_sketch_mirror();
        viewer_->clear_selection();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("Geometrie skici byla zrcadlena jako jedna revize."));
        return true;
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
        return true;
    }
}

void AssemblyWorkspaceWindow::start_sketch_circle() {
    if (properties_dialog_ != nullptr) return;
    if (active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_circle_active_ = true;
    set_sketch_placement_selection_contract();
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    state_->setText(tr("Kružnice skici: určete střed. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_circle() {
    sketch_circle_active_ = false;
    pending_circle_center_.reset();
    pending_circle_tangent_inference_.reset();
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_arc() {
    if (properties_dialog_ != nullptr) return;
    if (active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_arc_active_ = true;
    set_sketch_placement_selection_contract();
    sketch_arc_clockwise_ = false;
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    state_->setText(tr("Oblouk skici: určete střed. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_arc() {
    sketch_arc_active_ = false;
    sketch_arc_clockwise_ = false;
    pending_arc_center_.reset();
    pending_arc_start_.reset();
    pending_curve_point_snaps_.clear();
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_ellipse() {
    if (properties_dialog_ != nullptr) return;
    if (active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_ellipse_active_ = true;
    set_sketch_placement_selection_contract();
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    state_->setText(tr("Elipsa skici: určete střed. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_ellipse() {
    sketch_ellipse_active_ = false;
    pending_ellipse_center_.reset();
    pending_ellipse_major_.reset();
    pending_curve_point_snaps_.clear();
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_elliptical_arc() {
    if (properties_dialog_ != nullptr) return;
    if (active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_elliptical_arc_active_ = true;
    set_sketch_placement_selection_contract();
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    state_->setText(tr(
        "Eliptický oblouk: určete střed. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_elliptical_arc() {
    sketch_elliptical_arc_active_ = false;
    pending_elliptical_arc_center_.reset();
    pending_elliptical_arc_major_.reset();
    pending_elliptical_arc_minor_.reset();
    pending_elliptical_arc_start_.reset();
    pending_elliptical_arc_reversed_ = false;
    pending_curve_point_snaps_.clear();
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_bspline(bool interpolating) {
    if (properties_dialog_ != nullptr) return;
    if (active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_bspline_active_ = true;
    sketch_bspline_interpolating_ = interpolating;
    set_sketch_placement_selection_contract();
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    state_->setText(interpolating
        ? tr("Interpolační spline: LMB potvrzuje body na křivce; dvojklik MMB "
             "dokončí na posledním potvrzeném bodě.")
        : tr("B-spline: LMB potvrzuje řídicí body; dvojklik MMB dokončí na "
             "posledním potvrzeném bodě."));
}

void AssemblyWorkspaceWindow::cancel_sketch_bspline() {
    sketch_bspline_active_ = false;
    sketch_bspline_interpolating_ = false;
    pending_bspline_points_.clear();
    viewer_->set_command_snap_points({});
    pending_curve_point_snaps_.clear();
    viewer_->set_transient_edges({});
}

bool AssemblyWorkspaceWindow::accept_sketch_bspline_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    const auto* sketch = active_sketch();
    if (!sketch_bspline_active_ || sketch == nullptr) {
        return false;
    }
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_bspline_points_.empty()) {
        const auto& previous = pending_bspline_points_.back();
        if (std::hypot((*position)[0] - previous[0], (*position)[1] - previous[1]) <=
            1.0e-6) {
            state_->setText(tr("Po sobě jdoucí body spline musí být odlišné."));
            return true;
        }
    }
    pending_bspline_points_.push_back(*position);
    if(pending_bspline_points_.size()>=3) {
        const auto& first=pending_bspline_points_.front();
        viewer_->set_command_snap_points({{sketch->world_point(first[0],first[1]),
            {active_sketch_id_,"point:pending-spline-start",{}}}});
    }
    pending_curve_point_snaps_.push_back({
        std::exchange(pending_sketch_snap_geometry_id_, {}),
        std::exchange(pending_sketch_snap_kind_, std::nullopt)});
    const auto spline_name = sketch_bspline_interpolating_
        ? tr("Interpolační spline") : tr("B-spline");
    state_->setText(pending_bspline_points_.size() >= 3
        ? tr("%1: %2 potvrzených bodů; dvojklik MMB dokončí.")
              .arg(spline_name).arg(pending_bspline_points_.size())
        : tr("%1: %2 potvrzených bodů; pro křivku jsou potřeba alespoň 3.")
              .arg(spline_name).arg(pending_bspline_points_.size()));
    return true;
}

bool AssemblyWorkspaceWindow::finish_sketch_bspline() {
    if (!sketch_bspline_active_) return false;
    if (pending_bspline_points_.size() < 3) {
        state_->setText(tr("Spline vyžaduje alespoň 3 potvrzené body."));
        return true;
    }
    const bool interpolating = sketch_bspline_interpolating_;
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                const auto degree = std::min<unsigned>(3,
                    static_cast<unsigned>(pending_bspline_points_.size() - 1));
                const auto spline_id = sketch.add_bspline(
                    pending_bspline_points_, degree, false, false, 1.0e-6,
                    interpolating);
                const auto spline = std::ranges::find_if(sketch.bsplines,
                    [&](const auto& value) { return value.id == spline_id; });
                if (spline == sketch.bsplines.end()) return;
                const auto point_ids=spline->control_point_ids;
                for (std::size_t index = 0;
                     index < point_ids.size() &&
                         index < pending_curve_point_snaps_.size(); ++index) {
                    if(pending_curve_point_snaps_[index].first=="pending-spline-start") continue;
                    apply_sketch_point_snap(sketch, point_ids[index],
                        pending_curve_point_snaps_[index].first,
                        pending_curve_point_snaps_[index].second);
                }
            })) return true;
        pending_bspline_points_.clear();
    viewer_->set_command_snap_points({});
        pending_curve_point_snaps_.clear();
        clear_completed_sketch_interaction();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(interpolating
            ? tr("Interpolační spline vytvořena. Klikáním můžete vytvořit další.")
            : tr("B-spline vytvořena. Klikáním můžete vytvořit další."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
    return true;
}

void AssemblyWorkspaceWindow::preview_sketch_bspline_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_bspline_active_ || pending_bspline_points_.empty()) return;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) {
        viewer_->set_transient_edges({});
        viewer_->set_transient_points({});
        viewer_->set_transient_labels({});
        return;
    }
    auto preview_points = pending_bspline_points_;
    // The pointer often stays on the just-confirmed point, especially
    // while snapped to an axis. Do not append a second copy that add_bspline
    // would merge into an invalid pair of consecutive control-point IDs.
    const auto& last=preview_points.back();
    if (std::hypot((*position)[0]-last[0],(*position)[1]-last[1])>1.0e-6)
        preview_points.push_back(*position);
    if (preview_points.size() < 3) {
        zima::kernel::ViewerEdge preview;
        for (const auto& point : preview_points) {
            preview.points.push_back(sketch->world_point(point[0], point[1]));
        }
        viewer_->set_transient_edges({std::move(preview)});
        std::vector<zima::kernel::Vec3> accepted;
        for (const auto& point : pending_bspline_points_)
            accepted.push_back(sketch->world_point(point[0], point[1]));
        viewer_->set_transient_points(std::move(accepted));
        return;
    }
    try {
        auto preview_sketch = *sketch;
        const auto degree = std::min<unsigned>(3,
            static_cast<unsigned>(preview_points.size() - 1));
        const auto preview_id = preview_sketch.add_bspline(
            preview_points, degree, false, false, 1.0e-6,
            sketch_bspline_interpolating_);
        auto mesh = preview_sketch.viewer_mesh();
        const auto edge = std::find_if(mesh.edges.rbegin(), mesh.edges.rend(),
            [&](const auto& value) {
                return value.reference.semantic_key == "bspline:" + preview_id;
            });
        viewer_->set_transient_edges(edge == mesh.edges.rend()
            ? std::vector<zima::kernel::ViewerEdge>{}
            : std::vector<zima::kernel::ViewerEdge>{*edge});
    } catch (const std::exception& error) {
        // Preview is transient; a temporarily invalid shape must never let
        // an exception escape into Qt's mouse-event dispatch.
        viewer_->set_transient_edges({});
        state_->setText(tr("Náhled spliny: %1").arg(QString::fromUtf8(error.what())));
    }
    std::vector<zima::kernel::Vec3> accepted;
    for (const auto& point : pending_bspline_points_)
        accepted.push_back(sketch->world_point(point[0], point[1]));
    viewer_->set_transient_points(std::move(accepted));
}

void AssemblyWorkspaceWindow::show_sketch_constraints() {
    if (!active_sketch()) return;
    if (sketch_constraints_dialog_) { sketch_constraints_action_->setChecked(true);sketch_constraints_dialog_->raise();return; }
    using K=zima::sketcher::ConstraintKind;
    const std::vector<SketchConstraintsDialog::Row> rows{
        {K::Horizontal,sketch_horizontal_action_},{K::Vertical,sketch_vertical_action_},
        {K::Parallel,sketch_parallel_action_},{K::EqualLength,sketch_equal_length_action_},
        {K::Perpendicular,sketch_perpendicular_action_},{K::Coincident,sketch_coincident_action_},
        {K::Midpoint,sketch_midpoint_action_},{K::Symmetric,sketch_symmetric_action_},
        {K::Tangent,sketch_tangent_action_},{K::Concentric,sketch_concentric_action_},
        {std::nullopt,sketch_fix_point_action_}};
    auto* dialog=new SketchConstraintsDialog(sketch_inference_settings_,rows,[this](auto settings){
        sketch_inference_settings_=std::move(settings);
        pending_sketch_snap_geometry_id_.clear();pending_sketch_snap_kind_.reset();
        sketch_segment_inference_cycle_=0;
        const auto filter=[&](auto& kind){if (kind && !automatic_constraint_enabled(*kind)) kind.reset();};
        filter(pending_segment_start_snap_kind_);filter(pending_rectangle_corner_snap_kind_);
        for (auto& snap : pending_curve_point_snaps_) filter(snap.second);
        pending_circle_tangent_inference_.reset();
        viewer_->set_sketch_relation_highlights({});
    },this);
    sketch_constraints_dialog_=dialog;
    sketch_constraints_action_->setChecked(true);
    connect(dialog,&QDialog::finished,this,[this,dialog] {
        if (dialog->has_active_choice()) {
            cancel_sketch_segment();
            clear_sketch_confirmed_selection();
            preserve_view_on_refresh_=true;
            refresh_scene();
        }
        sketch_constraints_action_->setChecked(false);
        sketch_constraints_dialog_=nullptr;
    });
    dialog->show();
}

} // namespace zima::app
