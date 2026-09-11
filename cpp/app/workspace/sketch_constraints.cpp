#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::clear_completed_sketch_interaction() {
    if (viewer_ == nullptr) return;
    // A repeatable Sketch command starts its next object with a completely
    // clean interaction state.  Clearing only transient edges leaves center
    // points/inference labels behind, while a last click accepted through a
    // snap candidate can also retain a confirmed object and suppress hover.
    viewer_->clear_selection();
    viewer_->set_transient_edges({});
    viewer_->set_transient_points({});
    viewer_->set_transient_labels({});
    // Candidate confirmation, tree synchronization and scene rebuilding all
    // run inside the same LMB event. A later stage of that event may restore
    // the pressed candidate after the command callback has already cleaned
    // it. Repeat the cleanup once the event is fully unwound so completed
    // geometry can never leave the whole active Sketch cyan or suppress the
    // next command hover.
    QTimer::singleShot(0, viewer_, [view = viewer_] {
        view->clear_selection();
        view->set_transient_edges({});
        view->set_transient_points({});
        view->set_transient_labels({});
    });
}

void AssemblyWorkspaceWindow::clear_sketch_confirmed_selection() {
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_text_id_.clear();
    selected_sketch_point_id_.clear();
    if (tree_ != nullptr) tree_->clearSelection();
    if (viewer_ == nullptr) return;
    viewer_->clear_selection();
    QTimer::singleShot(0, viewer_, [view = viewer_] {
        view->clear_selection();
    });
}

void AssemblyWorkspaceWindow::constrain_selected_segment(
    zima::sketcher::ConstraintKind kind) {
    if (sketch_segment_active_ || sketch_rectangle_active_ || sketch_polygon_active_ ||
        sketch_offset_dialog_ || sketch_mirror_active_ || sketch_circle_active_ || sketch_arc_active_ ||
        sketch_ellipse_active_ || sketch_elliptical_arc_active_ ||
        sketch_bspline_active_ || sketch_coincident_active_ || sketch_midpoint_active_ ||
        sketch_symmetric_active_ || sketch_concentric_active_ ||
        sketch_tangent_active_ || sketch_common_tangent_active_ ||
        sketch_segment_pair_active_ ||
        selected_sketch_segment_id_.empty() ||
        active_sketch_id_.empty() || properties_dialog_ != nullptr) return;
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                static_cast<void>(sketch.add_segment_constraint(
                    selected_sketch_segment_id_, kind));
            })) return;
        selected_sketch_segment_id_.clear();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(kind == zima::sketcher::ConstraintKind::Horizontal
            ? tr("Úsečka je vodorovná.") : tr("Úsečka je svislá."));
    } catch (const std::exception& error) {
        // Sketch commands report a rejected relation in the shared status
        // area. A modal native warning would interrupt the active Sketcher
        // command contract and can strand keyboard/view interaction.
        state_->setText(tr("Vazbu nelze vytvořit: %1")
            .arg(QString::fromUtf8(error.what())));
    }
}

void AssemblyWorkspaceWindow::start_sketch_coincident(
    zima::sketcher::ConstraintKind kind) {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return;
    if (active_sketch() == nullptr) return;
    if (kind != zima::sketcher::ConstraintKind::Coincident &&
        kind != zima::sketcher::ConstraintKind::Horizontal &&
        kind != zima::sketcher::ConstraintKind::Vertical) return;
    const auto selected_reference = selected_sketch_point_id_;
    cancel_sketch_segment();
    sketch_coincident_active_ = true;
    pending_point_pair_constraint_kind_ = kind;
    pending_coincident_point_id_ = selected_reference;
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    viewer_->set_selection_contract(kind == zima::sketcher::ConstraintKind::Coincident
        ? std::vector{zima::viewer::CandidateKind::SketchPoint,
                      zima::viewer::CandidateKind::SketchAxis,
                      zima::viewer::CandidateKind::SketchExternalReference}
        : std::vector{zima::viewer::CandidateKind::SketchPoint,
                      zima::viewer::CandidateKind::SketchSegment});
    state_->setText(!pending_coincident_point_id_.empty()
        ? kind == zima::sketcher::ConstraintKind::Horizontal
            ? tr("Vodorovnost bodů: vyberte řízený bod.")
            : kind == zima::sketcher::ConstraintKind::Vertical
                ? tr("Svislost bodů: vyberte řízený bod.")
                : tr("Totožnost bodů: vyberte druhý bod.")
        : kind == zima::sketcher::ConstraintKind::Horizontal
            ? tr("Vodorovnost bodů: vyberte první bod. Escape příkaz zruší.")
            : kind == zima::sketcher::ConstraintKind::Vertical
                ? tr("Svislost bodů: vyberte první bod. Escape příkaz zruší.")
                : tr("Totožnost bodů: vyberte první bod. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_coincident() {
    sketch_coincident_active_ = false;
    pending_coincident_point_id_.clear();
    pending_point_pair_constraint_kind_ =
        zima::sketcher::ConstraintKind::Coincident;
}

void AssemblyWorkspaceWindow::start_sketch_midpoint() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return;
    if (active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_midpoint_active_ = true;
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_point_id_.clear();
    viewer_->set_selection_contract({zima::viewer::CandidateKind::SketchPoint});
    state_->setText(tr("Bod ve středu: vyberte bod. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_midpoint() {
    sketch_midpoint_active_ = false;
    pending_midpoint_point_id_.clear();
}

void AssemblyWorkspaceWindow::accept_sketch_midpoint_selection(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_midpoint_active_ || candidate.owner_id != active_sketch_id_) return;
    if (pending_midpoint_point_id_.empty()) {
        if (candidate.kind != zima::viewer::CandidateKind::SketchPoint ||
            !candidate.semantic_key.starts_with("point:")) return;
        pending_midpoint_point_id_ = candidate.semantic_key.substr(6);
        viewer_->set_selection_contract({zima::viewer::CandidateKind::SketchSegment});
        state_->setText(tr("Bod ve středu: vyberte úsečku nebo konstrukční čáru."));
        return;
    }
    if (candidate.kind != zima::viewer::CandidateKind::SketchSegment ||
        !candidate.semantic_key.starts_with("segment:")) return;
    const auto segment_id = candidate.semantic_key.substr(8);
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                static_cast<void>(sketch.add_midpoint_constraint(
                    pending_midpoint_point_id_, segment_id));
            })) return;
        pending_midpoint_point_id_.clear();
        clear_completed_sketch_interaction();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr(
            "Vazba bodu ve středu byla vytvořena. Vyberte bod další vazby."));
    } catch (const std::exception& error) {
        state_->setText(tr("Vazbu bodu ve středu nelze vytvořit: %1")
            .arg(QString::fromUtf8(error.what())));
    }
}

void AssemblyWorkspaceWindow::start_sketch_symmetric() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return;
    if (active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_symmetric_active_ = true;
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_point_id_.clear();
    viewer_->set_selection_contract({zima::viewer::CandidateKind::SketchPoint});
    state_->setText(tr("Symetrická vazba: vyberte referenční bod. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::set_sketch_symmetric_axis_contract() {
    viewer_->set_selection_contract({zima::viewer::CandidateKind::SketchSegment,
                                     zima::viewer::CandidateKind::SketchAxis});
    const auto* sketch = active_sketch();
    if (sketch == nullptr) {
        viewer_->set_candidate_filter([](const auto&) { return false; });
        return;
    }
    std::set<std::string> allowed_keys;
    for (const auto& segment : sketch->segments) {
        if (segment.construction) allowed_keys.insert("segment:" + segment.id);
    }
    const auto owner_id = sketch->id;
    viewer_->set_candidate_filter(
        [owner_id, allowed_keys = std::move(allowed_keys)](const auto& candidate) {
            if (candidate.owner_id != owner_id) return false;
            if (candidate.kind == zima::viewer::CandidateKind::SketchAxis) {
                return candidate.semantic_key == "sketch_axis:x" ||
                    candidate.semantic_key == "sketch_axis:y";
            }
            return candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
                allowed_keys.contains(candidate.semantic_key);
        });
}

void AssemblyWorkspaceWindow::accept_sketch_symmetric_selection(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_symmetric_active_ || candidate.owner_id != active_sketch_id_) return;
    if (pending_symmetric_point_ids_.size() < 2) {
        if (candidate.kind != zima::viewer::CandidateKind::SketchPoint ||
            !candidate.semantic_key.starts_with("point:")) return;
        const auto point_id = candidate.semantic_key.substr(6);
        if (std::find(pending_symmetric_point_ids_.begin(),
                      pending_symmetric_point_ids_.end(), point_id) !=
            pending_symmetric_point_ids_.end()) {
            state_->setText(tr("Symetrická vazba: vyberte jiný druhý bod."));
            return;
        }
        pending_symmetric_point_ids_.push_back(point_id);
        if (pending_symmetric_point_ids_.size() == 1) {
            state_->setText(tr("Symetrická vazba: vyberte řízený bod."));
        } else {
            set_sketch_symmetric_axis_contract();
            state_->setText(tr(
                "Symetrická vazba: vyberte konstrukční čáru jako osu."));
        }
        return;
    }
    const bool base_axis = candidate.kind ==
            zima::viewer::CandidateKind::SketchAxis &&
        (candidate.semantic_key == "sketch_axis:x" ||
         candidate.semantic_key == "sketch_axis:y");
    if (!base_axis &&
        (candidate.kind != zima::viewer::CandidateKind::SketchSegment ||
         !candidate.semantic_key.starts_with("segment:"))) return;
    const auto axis_id = base_axis
        ? candidate.semantic_key : candidate.semantic_key.substr(8);
    try {
        const auto* current = active_sketch();
        if (current == nullptr) return;
        const auto axis = std::find_if(current->segments.begin(), current->segments.end(),
            [&](const auto& value) { return value.id == axis_id; });
        if (!base_axis &&
            (axis == current->segments.end() || !axis->construction)) {
            state_->setText(tr(
                "Symetrická vazba: osou musí být konstrukční čára."));
            return;
        }
        if (!mutate_active_sketch([&](auto& sketch) {
                static_cast<void>(sketch.add_symmetric_constraint(
                    pending_symmetric_point_ids_[0],
                    pending_symmetric_point_ids_[1], axis_id));
            })) return;
        pending_symmetric_point_ids_.clear();
        clear_completed_sketch_interaction();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr(
            "Symetrická vazba byla vytvořena. Vyberte referenční bod další vazby."));
    } catch (const std::exception& error) {
        state_->setText(tr("Symetrickou vazbu nelze vytvořit: %1")
            .arg(QString::fromUtf8(error.what())));
    }
}

void AssemblyWorkspaceWindow::set_sketch_concentric_contract() {
    viewer_->set_selection_contract({zima::viewer::CandidateKind::SketchCurve,
        zima::viewer::CandidateKind::SketchExternalReference});
    const auto owner_id = active_sketch_id_;
    std::set<std::string> circular_references;
    if (const auto* sketch=active_sketch()) for (const auto& reference:sketch->external_references)
        if (zima::sketcher::external_reference_circle(reference)) circular_references.insert(reference.id);
    viewer_->set_candidate_filter([owner_id,circular_references](const auto& candidate) {
        if (candidate.owner_id != owner_id) return false;
        if (candidate.kind == zima::viewer::CandidateKind::SketchExternalReference) {
            const auto id=sketch_external_reference_id_from_key(candidate.semantic_key);
            return id && circular_references.contains(*id);
        }
        return candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
            (candidate.semantic_key.starts_with("circle:") ||
             candidate.semantic_key.starts_with("arc:") ||
             candidate.semantic_key.starts_with("ellipse:") ||
             candidate.semantic_key.starts_with("elliptical_arc:"));
    });
}

void AssemblyWorkspaceWindow::start_sketch_concentric() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return;
    if (active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_concentric_active_ = true;
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_point_id_.clear();
    set_sketch_concentric_contract();
    state_->setText(tr(
        "Soustředná vazba: vyberte referenční kružnici, oblouk nebo elipsu."));
}

void AssemblyWorkspaceWindow::accept_sketch_concentric_selection(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_concentric_active_ ||
        (candidate.kind != zima::viewer::CandidateKind::SketchCurve &&
         candidate.kind != zima::viewer::CandidateKind::SketchExternalReference) ||
        candidate.owner_id != active_sketch_id_) return;
    std::string geometry_id;
    for (const std::string_view prefix : {
            std::string_view{"circle:"}, std::string_view{"arc:"},
            std::string_view{"ellipse:"}, std::string_view{"elliptical_arc:"}}) {
        if (candidate.semantic_key.starts_with(prefix)) {
            geometry_id = candidate.semantic_key.substr(prefix.size());
            break;
        }
    }
    if (candidate.kind == zima::viewer::CandidateKind::SketchExternalReference)
        geometry_id=sketch_external_reference_id_from_key(candidate.semantic_key).value_or("");
    if (geometry_id.empty()) return;
    if (pending_concentric_geometry_id_.empty()) {
        pending_concentric_geometry_id_ = geometry_id;
        state_->setText(tr(
            "Soustředná vazba: vyberte řízenou kružnici, oblouk nebo elipsu."));
        return;
    }
    if (geometry_id == pending_concentric_geometry_id_) {
        state_->setText(tr("Soustředná vazba: vyberte jinou druhou křivku."));
        return;
    }
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                const bool second_external=std::ranges::any_of(sketch.external_references,
                    [&](const auto& reference){return reference.id==geometry_id;});
                static_cast<void>(sketch.add_concentric_constraint(
                    second_external ? geometry_id : pending_concentric_geometry_id_,
                    second_external ? pending_concentric_geometry_id_ : geometry_id));
            })) return;
        pending_concentric_geometry_id_.clear();
        clear_completed_sketch_interaction();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr(
            "Soustředná vazba byla vytvořena. Vyberte referenční křivku další vazby."));
    } catch (const std::exception& error) {
        state_->setText(tr("Soustřednou vazbu nelze vytvořit: %1")
            .arg(QString::fromUtf8(error.what())));
    }
}

void AssemblyWorkspaceWindow::set_sketch_tangent_contract() {
    const auto curve_candidate = [](const auto& candidate) {
        return candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
            (candidate.semantic_key.starts_with("circle:") ||
             candidate.semantic_key.starts_with("arc:") ||
             candidate.semantic_key.starts_with("ellipse:") ||
             candidate.semantic_key.starts_with("elliptical_arc:") ||
             candidate.semantic_key.starts_with("bspline:"));
    };
    const auto segment_candidate = [](const auto& candidate) {
        return candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
            candidate.semantic_key.starts_with("segment:");
    };
    const auto curve_pair_candidate = [](const auto& candidate) {
        return candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
            (candidate.semantic_key.starts_with("circle:") ||
             candidate.semantic_key.starts_with("arc:") ||
             candidate.semantic_key.starts_with("ellipse:") ||
             candidate.semantic_key.starts_with("elliptical_arc:") ||
             candidate.semantic_key.starts_with("bspline:"));
    };
    if (pending_tangent_geometry_id_.empty()) {
        viewer_->set_selection_contract({
            zima::viewer::CandidateKind::SketchSegment,
            zima::viewer::CandidateKind::SketchCurve});
    } else if (pending_tangent_reference_is_segment_) {
        viewer_->set_selection_contract({zima::viewer::CandidateKind::SketchCurve});
    } else if (pending_tangent_reference_supports_curve_pair_) {
        viewer_->set_selection_contract({
            zima::viewer::CandidateKind::SketchSegment,
            zima::viewer::CandidateKind::SketchCurve});
    } else {
        viewer_->set_selection_contract({zima::viewer::CandidateKind::SketchSegment});
    }
    const auto owner_id = active_sketch_id_;
    const bool first_pending = !pending_tangent_geometry_id_.empty();
    const bool reference_is_segment = pending_tangent_reference_is_segment_;
    const bool reference_supports_curve_pair =
        pending_tangent_reference_supports_curve_pair_;
    const auto pending_geometry_id = pending_tangent_geometry_id_;
    viewer_->set_candidate_filter(
        [this, owner_id, first_pending, reference_is_segment,
         reference_supports_curve_pair, pending_geometry_id,
         curve_candidate, segment_candidate,
         curve_pair_candidate](const auto& candidate) {
            if (candidate.owner_id != owner_id) return false;
            if (!first_pending) {
                return segment_candidate(candidate) || curve_candidate(candidate);
            }
            const auto* sketch=active_sketch();
            const bool pending_spline=sketch && std::ranges::any_of(sketch->bsplines,
                [&](const auto& value){return value.id==pending_geometry_id;});
            const bool candidate_spline=candidate.semantic_key.starts_with("bspline:");
            if ((pending_spline && candidate.kind==zima::viewer::CandidateKind::SketchCurve) ||
                (candidate_spline && !reference_is_segment)) {
                return sketch && sketch->spline_tangent_contact(pending_geometry_id,
                    candidate.semantic_key.substr(candidate.semantic_key.find(':')+1)).has_value();
            }
            const auto separator = candidate.semantic_key.find(':');
            if (separator != std::string::npos &&
                candidate.semantic_key.substr(separator + 1) == pending_geometry_id) {
                return false;
            }
            if (reference_is_segment) return curve_candidate(candidate);
            return segment_candidate(candidate) ||
                (reference_supports_curve_pair &&
                 curve_pair_candidate(candidate));
        });
}

void AssemblyWorkspaceWindow::start_sketch_tangent() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return;
    if (active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_tangent_active_ = true;
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_point_id_.clear();
    set_sketch_tangent_contract();
    state_->setText(tr(
        "Tečná vazba: vyberte referenční úsečku, kružnici, oblouk, elipsu, "
        "eliptický oblouk nebo B-spline."));
}

void AssemblyWorkspaceWindow::accept_sketch_tangent_selection(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_tangent_active_ || candidate.owner_id != active_sketch_id_) return;
    std::string geometry_id;
    bool is_segment = false;
    bool supports_curve_pair = false;
    if (candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
        candidate.semantic_key.starts_with("segment:")) {
        geometry_id = candidate.semantic_key.substr(8);
        is_segment = true;
    } else if (candidate.kind == zima::viewer::CandidateKind::SketchCurve) {
        for (const std::string_view prefix : {
                std::string_view{"circle:"}, std::string_view{"arc:"},
                std::string_view{"ellipse:"},
                std::string_view{"elliptical_arc:"},
                std::string_view{"bspline:"}}) {
            if (candidate.semantic_key.starts_with(prefix)) {
                geometry_id = candidate.semantic_key.substr(prefix.size());
                // Spline pairs require a shared endpoint; the selection
                // contract checks the persisted C junction before offering them.
                supports_curve_pair = true;
                break;
            }
        }
    }
    if (geometry_id.empty()) return;
    if (pending_tangent_geometry_id_.empty()) {
        pending_tangent_geometry_id_ = geometry_id;
        pending_tangent_reference_is_segment_ = is_segment;
        pending_tangent_reference_supports_curve_pair_ = supports_curve_pair;
        set_sketch_tangent_contract();
        if (candidate.semantic_key.starts_with("bspline:")) {
            state_->setText(tr("Tečná vazba: vyberte druhou splinu se společným koncovým bodem C, "
                "stejnou splinu s konci spojenými C (alespoň 4 různé body), nebo úsečku."));
            return;
        }
        state_->setText(is_segment
            ? tr("Tečná vazba: vyberte řízenou kružnici, oblouk, elipsu, "
                 "eliptický oblouk nebo B-spline.")
            : supports_curve_pair
                ? tr("Tečná vazba: vyberte řízenou úsečku, kružnici, oblouk, "
                     "elipsu nebo eliptický oblouk.")
                : tr("Tečná vazba: vyberte řízenou úsečku."));
        return;
    }
    if (geometry_id == pending_tangent_geometry_id_ &&
        !active_sketch()->spline_tangent_contact(geometry_id,geometry_id)) {
        state_->setText(tr("Tečná vazba: vyberte jinou druhou geometrii."));
        return;
    }
    const bool line_curve_pair =
        is_segment != pending_tangent_reference_is_segment_;
    const bool curve_pair =
        !is_segment && !pending_tangent_reference_is_segment_ &&
        pending_tangent_reference_supports_curve_pair_ && supports_curve_pair;
    if (!line_curve_pair && !curve_pair) {
        state_->setText(tr(
            "Tuto dvojici geometrií tečná vazba zatím nepodporuje."));
        return;
    }
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                static_cast<void>(sketch.add_tangent_constraint(
                    pending_tangent_geometry_id_, geometry_id));
            })) return;
        pending_tangent_geometry_id_.clear();
        pending_tangent_reference_is_segment_ = false;
        pending_tangent_reference_supports_curve_pair_ = false;
        clear_completed_sketch_interaction();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr(
            "Tečná vazba byla vytvořena. Vyberte referenční geometrii další vazby."));
    } catch (const std::exception& error) {
        state_->setText(tr("Tečnou vazbu nelze vytvořit: %1")
            .arg(QString::fromUtf8(error.what())));
    }
}

void AssemblyWorkspaceWindow::set_sketch_common_tangent_contract() {
    viewer_->set_selection_contract({zima::viewer::CandidateKind::SketchCurve});
    const auto owner_id = active_sketch_id_;
    const auto first_id = pending_common_tangent_curve_id_;
    viewer_->set_candidate_filter([owner_id, first_id](const auto& candidate) {
        if (candidate.owner_id != owner_id ||
            candidate.kind != zima::viewer::CandidateKind::SketchCurve) {
            return false;
        }
        for (const std::string_view prefix : {
                std::string_view{"circle:"}, std::string_view{"arc:"},
                std::string_view{"ellipse:"},
                std::string_view{"elliptical_arc:"},
                std::string_view{"bspline:"}}) {
            if (candidate.semantic_key.starts_with(prefix)) {
                return candidate.semantic_key.substr(prefix.size()) != first_id;
            }
        }
        return false;
    });
}

void AssemblyWorkspaceWindow::start_sketch_common_tangent() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty() ||
        active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_common_tangent_active_ = true;
    pending_common_tangent_curve_id_.clear();
    pending_common_tangent_first_hint_.reset();
    sketch_pointer_position_.reset();
    clear_selected_sketch_geometry();
    set_sketch_common_tangent_contract();
    state_->setText(tr(
        "Společná tečna: klikněte poblíž požadovaného dotyku na první "
        "kružnici, oblouk, elipsu, eliptický oblouk nebo B-spline."));
}

void AssemblyWorkspaceWindow::accept_sketch_common_tangent_selection(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_common_tangent_active_ ||
        candidate.owner_id != active_sketch_id_ || !sketch_pointer_position_) {
        return;
    }
    std::string geometry_id;
    for (const std::string_view prefix : {
            std::string_view{"circle:"}, std::string_view{"arc:"},
            std::string_view{"ellipse:"},
            std::string_view{"elliptical_arc:"},
            std::string_view{"bspline:"}}) {
        if (candidate.semantic_key.starts_with(prefix)) {
            geometry_id = candidate.semantic_key.substr(prefix.size());
            break;
        }
    }
    if (geometry_id.empty()) return;
    if (pending_common_tangent_curve_id_.empty()) {
        pending_common_tangent_curve_id_ = geometry_id;
        pending_common_tangent_first_hint_ = *sketch_pointer_position_;
        set_sketch_common_tangent_contract();
        state_->setText(tr(
            "Společná tečna: klikněte poblíž požadovaného dotyku na druhou "
            "křivku. Poloha obou kliků určí větev tečny."));
        return;
    }
    if (geometry_id == pending_common_tangent_curve_id_ ||
        !pending_common_tangent_first_hint_) return;
    try {
        const auto first_id = pending_common_tangent_curve_id_;
        const auto first_hint = *pending_common_tangent_first_hint_;
        const auto second_hint = *sketch_pointer_position_;
        if (!mutate_active_sketch([&](auto& sketch) {
                static_cast<void>(sketch.add_common_tangent_segment(
                    first_id, first_hint, geometry_id, second_hint));
            })) return;
        pending_common_tangent_curve_id_.clear();
        pending_common_tangent_first_hint_.reset();
        clear_completed_sketch_interaction();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr(
            "Společná tečna byla vytvořena. Vyberte první křivku další tečny."));
    } catch (const std::exception& error) {
        state_->setText(tr("Společnou tečnu nelze vytvořit: %1")
            .arg(QString::fromUtf8(error.what())));
    }
}

void AssemblyWorkspaceWindow::accept_sketch_coincident_point(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_coincident_active_ || candidate.owner_id != active_sketch_id_) return;
    if (pending_point_pair_constraint_kind_ != zima::sketcher::ConstraintKind::Coincident &&
        pending_coincident_point_id_.empty() &&
        candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
        candidate.semantic_key.starts_with("segment:")) {
        try {
            const auto id=candidate.semantic_key.substr(8);
            if (!mutate_active_sketch([&](auto& sketch) {
                static_cast<void>(sketch.add_segment_constraint(id,pending_point_pair_constraint_kind_));
            })) return;
            clear_completed_sketch_interaction();
            preserve_view_on_refresh_=true;
            refresh_tabs();refresh_scene();
            state_->setText(tr("Vazba úsečky vytvořena. Vyberte další úsečku nebo dvojici bodů."));
        } catch (const std::exception& error) {
            state_->setText(QString::fromUtf8(error.what()));
        }
        return;
    }
    std::string point_id;
    std::string support_geometry_id;
    bool circular_support = false;
    if (candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
        candidate.semantic_key.starts_with("point:")) {
        point_id = candidate.semantic_key.substr(6);
    } else if (pending_point_pair_constraint_kind_ ==
                   zima::sketcher::ConstraintKind::Coincident &&
               candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
               (candidate.semantic_key == "sketch_axis:x" ||
                candidate.semantic_key == "sketch_axis:y")) {
        if (pending_coincident_point_id_.empty()) {
            pending_coincident_point_id_ = candidate.semantic_key;
            viewer_->set_selection_contract(
                {zima::viewer::CandidateKind::SketchPoint});
            const auto owner_id = active_sketch_id_;
            viewer_->set_candidate_filter([owner_id](const auto& value) {
                return value.owner_id == owner_id &&
                    value.kind == zima::viewer::CandidateKind::SketchPoint &&
                    value.semantic_key.starts_with("point:");
            });
            state_->setText(tr(
                "Totožnost s osou: vyberte bod, který má ležet na ose."));
            return;
        }
        support_geometry_id = candidate.semantic_key;
    } else if (candidate.kind ==
                   zima::viewer::CandidateKind::SketchExternalReference &&
               candidate.semantic_key.starts_with("external_point:")) {
        point_id = candidate.semantic_key.substr(15);
    } else if (!pending_coincident_point_id_.empty() &&
               candidate.kind ==
                   zima::viewer::CandidateKind::SketchExternalReference &&
               (candidate.semantic_key.starts_with("external_edge:") ||
                candidate.semantic_key.starts_with("external_axis:") ||
                candidate.semantic_key.starts_with("external_face:"))) {
        const auto reference_id = sketch_external_reference_id_from_key(
            candidate.semantic_key);
        if (reference_id) support_geometry_id = *reference_id;
    } else if (!pending_coincident_point_id_.empty() &&
               candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
               candidate.semantic_key.starts_with("segment:")) {
        support_geometry_id = candidate.semantic_key.substr(8);
    } else if (!pending_coincident_point_id_.empty() &&
               candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
               (candidate.semantic_key.starts_with("circle:") ||
                candidate.semantic_key.starts_with("arc:") ||
                candidate.semantic_key.starts_with("ellipse:") ||
                candidate.semantic_key.starts_with("elliptical_arc:") ||
                candidate.semantic_key.starts_with("bspline:"))) {
        for (const auto prefix : {std::string_view{"circle:"},
                 std::string_view{"arc:"}, std::string_view{"ellipse:"},
                 std::string_view{"elliptical_arc:"},
                 std::string_view{"bspline:"}}) {
            if (!candidate.semantic_key.starts_with(prefix)) continue;
            support_geometry_id = candidate.semantic_key.substr(prefix.size());
            circular_support = true;
            break;
        }
    } else {
        return;
    }
    if (pending_point_pair_constraint_kind_ !=
            zima::sketcher::ConstraintKind::Coincident &&
        (candidate.kind != zima::viewer::CandidateKind::SketchPoint ||
         point_id.empty())) return;
    if ((pending_coincident_point_id_ == "sketch_axis:x" ||
         pending_coincident_point_id_ == "sketch_axis:y") &&
        !point_id.empty()) {
        try {
            const auto axis_id = pending_coincident_point_id_;
            if (!mutate_active_sketch([&](auto& sketch) {
                    static_cast<void>(sketch.add_point_on_line_constraint(
                        point_id, axis_id));
                })) return;
            pending_coincident_point_id_.clear();
            viewer_->set_candidate_filter({});
            clear_completed_sketch_interaction();
            preserve_view_on_refresh_ = true;
            refresh_tabs();
            refresh_scene();
            state_->setText(tr(
                "Vazba bodu na osu byla vytvořena. Vyberte další referenci."));
        } catch (const std::exception& error) {
            state_->setText(QString::fromUtf8(error.what()));
        }
        return;
    }
    if (pending_coincident_point_id_.empty()) {
        pending_coincident_point_id_ = point_id;
        viewer_->set_selection_contract(pending_point_pair_constraint_kind_ ==
                zima::sketcher::ConstraintKind::Coincident
            ? std::vector{zima::viewer::CandidateKind::SketchPoint,
                          zima::viewer::CandidateKind::SketchSegment,
                          zima::viewer::CandidateKind::SketchCurve,
                          zima::viewer::CandidateKind::SketchExternalReference}
            : std::vector{zima::viewer::CandidateKind::SketchPoint});
        const auto owner_id = active_sketch_id_;
        viewer_->set_candidate_filter([owner_id](const auto& value) {
            return value.owner_id == owner_id &&
                (value.kind != zima::viewer::CandidateKind::SketchCurve ||
                 value.semantic_key.starts_with("circle:") ||
                 value.semantic_key.starts_with("arc:") ||
                 value.semantic_key.starts_with("ellipse:") ||
                 value.semantic_key.starts_with("elliptical_arc:") ||
                 value.semantic_key.starts_with("bspline:"));
        });
        state_->setText(tr(
            "Totožnost: vyberte druhý bod, úsečku, křivku nebo externí přímku."));
        return;
    }
    if (!support_geometry_id.empty()) {
        try {
            if (!mutate_active_sketch([&](auto& sketch) {
                    if (sketch.find_point(pending_coincident_point_id_) == nullptr)
                        throw std::invalid_argument("Coincident point is missing");
                    if (circular_support) {
                        static_cast<void>(sketch.add_point_on_circle_constraint(
                            pending_coincident_point_id_, support_geometry_id));
                    } else {
                        static_cast<void>(sketch.add_point_on_line_constraint(
                            pending_coincident_point_id_, support_geometry_id));
                    }
                })) return;
            pending_coincident_point_id_.clear();
            viewer_->set_candidate_filter({});
            clear_completed_sketch_interaction();
            preserve_view_on_refresh_ = true;
            refresh_tabs();
            refresh_scene();
            state_->setText(tr(
                "Vazba bodu na geometrii byla vytvořena. Vyberte další bod."));
        } catch (const std::exception& error) {
            state_->setText(QString::fromUtf8(error.what()));
        }
        return;
    }
    if (point_id == pending_coincident_point_id_) {
        state_->setText(tr("Vyberte jiný druhý bod."));
        return;
    }
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                if (pending_point_pair_constraint_kind_ ==
                        zima::sketcher::ConstraintKind::Coincident) {
                    const bool first_native =
                        sketch.find_point(pending_coincident_point_id_) != nullptr;
                    const bool second_native =
                        sketch.find_point(point_id) != nullptr;
                    if (first_native && second_native) {
                        static_cast<void>(sketch.merge_points(
                            pending_coincident_point_id_, point_id));
                    } else if (first_native != second_native) {
                        static_cast<void>(sketch.add_point_reference_constraint(
                            first_native ? pending_coincident_point_id_ : point_id,
                            first_native ? point_id : pending_coincident_point_id_));
                    } else {
                        throw std::invalid_argument(
                            "Coincident requires at least one native Sketch point");
                    }
                } else {
                    static_cast<void>(sketch.add_point_pair_constraint(
                        pending_coincident_point_id_, point_id,
                        pending_point_pair_constraint_kind_));
                }
            })) return;
        const auto completed_kind = pending_point_pair_constraint_kind_;
        pending_coincident_point_id_.clear();
        viewer_->set_candidate_filter({});
        clear_completed_sketch_interaction();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(completed_kind == zima::sketcher::ConstraintKind::Horizontal
            ? tr("Vodorovnost bodů vytvořena. Vyberte referenční bod další vazby.")
            : completed_kind == zima::sketcher::ConstraintKind::Vertical
                ? tr("Svislost bodů vytvořena. Vyberte referenční bod další vazby.")
                : tr("Vazba totožnosti vytvořena. Vyberte první bod další vazby."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
}

void AssemblyWorkspaceWindow::start_sketch_segment_pair(
    zima::sketcher::ConstraintKind kind) {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty() ||
        (kind != zima::sketcher::ConstraintKind::Parallel &&
         kind != zima::sketcher::ConstraintKind::Perpendicular &&
         kind != zima::sketcher::ConstraintKind::EqualLength)) return;
    if (active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_segment_pair_active_ = true;
    pending_pair_kind_ = kind;
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_point_id_.clear();
    set_sketch_pair_contract();
    state_->setText(kind == zima::sketcher::ConstraintKind::Parallel
        ? tr("Rovnoběžnost: vyberte úsečku, která se má pohnout. "
             "Escape příkaz zruší.")
        : kind == zima::sketcher::ConstraintKind::Perpendicular
            ? tr("Kolmost: vyberte úsečku, která se má pohnout. "
                 "Escape příkaz zruší.")
            : tr("Stejné: vyberte řízenou úsečku, kružnici nebo oblouk. "
                 "Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::set_sketch_pair_contract() {
    const bool equal = pending_pair_kind_ ==
        zima::sketcher::ConstraintKind::EqualLength;
    if (!equal && pending_pair_geometry_id_.empty()) {
        // Pair constraints follow the normal CAD contract: the first click
        // is always editable/driven geometry. Datum axes and external lines
        // are valid only as the stationary second reference.
        viewer_->set_selection_contract({
            zima::viewer::CandidateKind::SketchSegment});
    } else if (!equal) {
        viewer_->set_selection_contract({
            zima::viewer::CandidateKind::SketchSegment,
            zima::viewer::CandidateKind::SketchAxis,
            zima::viewer::CandidateKind::SketchExternalReference});
    } else if (!pending_pair_geometry_id_.empty() &&
               !pending_pair_reference_is_circular_) {
        viewer_->set_selection_contract({
            zima::viewer::CandidateKind::SketchSegment});
    } else if (!pending_pair_geometry_id_.empty()) {
        viewer_->set_selection_contract({
            zima::viewer::CandidateKind::SketchCurve,
            zima::viewer::CandidateKind::SketchExternalReference});
    } else {
        viewer_->set_selection_contract({
            zima::viewer::CandidateKind::SketchSegment,
            zima::viewer::CandidateKind::SketchCurve});
    }
    const auto owner_id = active_sketch_id_;
    const auto pending_geometry_id = pending_pair_geometry_id_;
    const bool reference_is_circular = pending_pair_reference_is_circular_;
    std::set<std::string> circular_references;
    if (const auto* sketch=active_sketch()) for(const auto& reference:sketch->external_references)
        if(zima::sketcher::external_reference_circle(reference)) circular_references.insert(reference.id);
    viewer_->set_candidate_filter(
        [owner_id, pending_geometry_id, reference_is_circular, equal, circular_references](
            const auto& candidate) {
            if (candidate.owner_id != owner_id) return false;
            const bool segment =
                candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
                candidate.semantic_key.starts_with("segment:");
            const bool base_axis = !equal &&
                candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
                (candidate.semantic_key == "sketch_axis:x" ||
                 candidate.semantic_key == "sketch_axis:y");
            const bool external_direction = !equal &&
                candidate.kind ==
                    zima::viewer::CandidateKind::SketchExternalReference &&
                (candidate.semantic_key.starts_with("external_edge:") ||
                 candidate.semantic_key.starts_with("external_axis:"));
            const auto external_id=sketch_external_reference_id_from_key(candidate.semantic_key);
            const bool external_circle = equal && !pending_geometry_id.empty() &&
                candidate.kind == zima::viewer::CandidateKind::SketchExternalReference &&
                external_id && circular_references.contains(*external_id);
            const bool circular = external_circle || (
                candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
                (candidate.semantic_key.starts_with("circle:") ||
                 candidate.semantic_key.starts_with("arc:") ||
                 candidate.semantic_key.starts_with("corner_radius:")));
            if ((!equal && !segment && !base_axis && !external_direction) ||
                (equal && !segment && !circular)) {
                return false;
            }
            const auto separator = candidate.semantic_key.find(':');
            if (separator != std::string::npos &&
                candidate.semantic_key.substr(separator + 1) ==
                    pending_geometry_id) {
                return false;
            }
            if (pending_geometry_id.empty()) {
                return equal ? segment || circular : segment;
            }
            if (equal) return reference_is_circular ? circular : segment;
            return segment || base_axis || external_direction;
        });
}

void AssemblyWorkspaceWindow::accept_sketch_segment_pair(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_segment_pair_active_ || candidate.owner_id != active_sketch_id_) {
        return;
    }
    std::string geometry_id;
    bool is_circular = false;
    if (candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
        candidate.semantic_key.starts_with("segment:")) {
        geometry_id = candidate.semantic_key.substr(8);
    } else if (pending_pair_kind_ != zima::sketcher::ConstraintKind::EqualLength &&
               candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
               (candidate.semantic_key == "sketch_axis:x" ||
                candidate.semantic_key == "sketch_axis:y")) {
        geometry_id = candidate.semantic_key;
    } else if (pending_pair_kind_ != zima::sketcher::ConstraintKind::EqualLength &&
               candidate.kind ==
                   zima::viewer::CandidateKind::SketchExternalReference &&
               (candidate.semantic_key.starts_with("external_edge:") ||
                candidate.semantic_key.starts_with("external_axis:"))) {
        const auto reference_id = sketch_external_reference_id_from_key(
            candidate.semantic_key);
        if (reference_id) geometry_id = *reference_id;
    } else if (pending_pair_kind_ == zima::sketcher::ConstraintKind::EqualLength &&
               candidate.kind == zima::viewer::CandidateKind::SketchCurve) {
        for (const std::string_view prefix : {
                std::string_view{"circle:"}, std::string_view{"arc:"},
                std::string_view{"corner_radius:"}}) {
            if (!candidate.semantic_key.starts_with(prefix)) continue;
            geometry_id = candidate.semantic_key.substr(prefix.size());
            is_circular = true;
            break;
        }
    }
    if (pending_pair_kind_ == zima::sketcher::ConstraintKind::EqualLength &&
        !pending_pair_geometry_id_.empty() &&
        candidate.kind == zima::viewer::CandidateKind::SketchExternalReference) {
        geometry_id=sketch_external_reference_id_from_key(candidate.semantic_key).value_or("");
        is_circular=true;
    }
    if (geometry_id.empty()) return;
    if (pending_pair_geometry_id_.empty()) {
        pending_pair_geometry_id_ = geometry_id;
        pending_pair_reference_is_circular_ = is_circular;
        set_sketch_pair_contract();
        state_->setText(pending_pair_kind_ == zima::sketcher::ConstraintKind::Parallel
            ? tr("Rovnoběžnost: vyberte stojící referenční úsečku nebo osu.")
            : pending_pair_kind_ == zima::sketcher::ConstraintKind::Perpendicular
                ? tr("Kolmost: vyberte stojící referenční úsečku nebo osu.")
                : is_circular
                    ? tr("Stejné: vyberte referenční kružnici nebo oblouk.")
                    : tr("Stejné: vyberte referenční úsečku."));
        return;
    }
    if (geometry_id == pending_pair_geometry_id_) {
        state_->setText(tr("Vyberte jinou referenční geometrii."));
        return;
    }
    if (is_circular != pending_pair_reference_is_circular_) {
        state_->setText(tr(
            "Stejné vyžaduje dvě úsečky nebo dvě kružnice či kruhové oblouky."));
        return;
    }
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                if (pending_pair_reference_is_circular_) {
                    static_cast<void>(sketch.add_equal_radius_constraint(
                        geometry_id, pending_pair_geometry_id_));
                } else {
                    // The model API stores reference first and driven second;
                    // UI selection deliberately uses the opposite temporal
                    // order so the object chosen first is the one that moves.
                    static_cast<void>(sketch.add_segment_pair_constraint(
                        geometry_id, pending_pair_geometry_id_,
                        pending_pair_kind_));
                }
            })) return;
        const bool equal_radius = pending_pair_reference_is_circular_;
        pending_pair_geometry_id_.clear();
        pending_pair_reference_is_circular_ = false;
        clear_completed_sketch_interaction();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(pending_pair_kind_ == zima::sketcher::ConstraintKind::Parallel
            ? tr("Rovnoběžnost vytvořena. Vyberte další řízenou úsečku.")
            : pending_pair_kind_ == zima::sketcher::ConstraintKind::Perpendicular
                ? tr("Kolmost vytvořena. Vyberte další řízenou úsečku.")
                : equal_radius
                    ? tr("Stejný poloměr vytvořen. Vyberte další řízenou geometrii.")
                    : tr("Stejná délka vytvořena. Vyberte další řízenou geometrii."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
}

void AssemblyWorkspaceWindow::toggle_selected_sketch_point_fixed() {
    if (properties_dialog_ != nullptr || sketch_coincident_active_ ||
        sketch_midpoint_active_ || sketch_symmetric_active_ ||
        sketch_concentric_active_ || sketch_tangent_active_ ||
        sketch_common_tangent_active_ ||
        sketch_segment_pair_active_ || sketch_offset_dialog_ || sketch_mirror_active_ ||
        selected_sketch_point_id_.empty() || active_sketch_id_.empty()) return;
    try {
        const auto* sketch = active_sketch();
        if (sketch == nullptr) return;
        const auto* point = sketch->find_point(selected_sketch_point_id_);
        if (point == nullptr) return;
        const bool fixed = !point->fixed;
        if (!mutate_active_sketch([&](auto& target) {
                target.set_point_fixed(selected_sketch_point_id_, fixed);
            })) return;
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(fixed ? tr("Bod je fixovaný.") : tr("Bod je uvolněný."));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Fixaci bodu nelze změnit"), error.what());
    }
}

} // namespace zima::app
