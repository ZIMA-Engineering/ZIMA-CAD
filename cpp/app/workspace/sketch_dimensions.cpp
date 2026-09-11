#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::start_sketch_universal_dimension() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return;
    cancel_sketch_segment();
    sketch_universal_dimension_active_ = true;
    sketch_universal_dimension_action_->setChecked(true);
    reset_sketch_universal_dimension(true);
    state_->setText(tr(
        "Univerzální kóta: vyberte bod, úsečku, osu, kružnici nebo oblouk."));
}

void AssemblyWorkspaceWindow::reset_sketch_universal_dimension(bool keep_active) {
    universal_dimension_layout_={};
    universal_dimension_references_.clear();
    universal_pending_dimension_.reset();
    universal_corner_radius_dimension_id_.clear();
    universal_dimension_cursor_.reset();
    clear_sketch_confirmed_selection();
    if (!keep_active) {
        sketch_universal_dimension_active_ = false;
        viewer_->set_dimension_layout_editable((!properties_dialog_||!properties_dialog_->isVisible()));
        if (sketch_universal_dimension_action_ != nullptr) {
            const QSignalBlocker blocker(sketch_universal_dimension_action_);
            sketch_universal_dimension_action_->setChecked(false);
        }
        viewer_->set_selection_contract({});
        viewer_->set_candidate_filter({});
        return;
    }
    set_sketch_universal_dimension_contract();
}

void AssemblyWorkspaceWindow::set_sketch_universal_dimension_contract() {
    if (!sketch_universal_dimension_active_ || viewer_ == nullptr) return;
    const auto& refs = universal_dimension_references_;
    const bool three_points = refs.size() == 3 &&
        std::ranges::all_of(refs, [](const auto& value) {
            return value.kind == UniversalDimensionReferenceKind::Point;
        });
    const bool angular_pending = universal_pending_dimension_ &&
        (universal_pending_dimension_->kind ==
             zima::sketcher::DimensionKind::AngleBetween ||
         universal_pending_dimension_->kind ==
             zima::sketcher::DimensionKind::AngleThreePoint);
    const bool line_pair_pending = refs.size() == 2 &&
        refs[0].kind == UniversalDimensionReferenceKind::Line &&
        refs[1].kind == UniversalDimensionReferenceKind::Line;
    const bool placement_only =
        (refs.empty() && (universal_pending_dimension_.has_value() ||
                         !universal_corner_radius_dimension_id_.empty())) ||
        (angular_pending && !line_pair_pending) ||
        refs.size() >= 4 || (refs.size() == 3 && !three_points);
    if (placement_only) {
        viewer_->set_selection_contract({});
        viewer_->set_candidate_filter([](const auto&) { return false; });
        return;
    }
    std::vector<zima::viewer::CandidateKind> kinds;
    if (refs.empty()) {
        kinds = {zima::viewer::CandidateKind::SketchPoint,
                 zima::viewer::CandidateKind::SketchSegment,
                 zima::viewer::CandidateKind::SketchAxis,
                 zima::viewer::CandidateKind::SketchExternalReference,
                 zima::viewer::CandidateKind::Dimension};
    } else if (refs.size() == 1 &&
               refs[0].kind == UniversalDimensionReferenceKind::Point) {
        kinds = {zima::viewer::CandidateKind::SketchPoint,
                 zima::viewer::CandidateKind::SketchSegment,
                 zima::viewer::CandidateKind::SketchAxis,
                 zima::viewer::CandidateKind::SketchExternalReference};
    } else if (refs.size() == 1) {
        kinds = {zima::viewer::CandidateKind::SketchPoint,
                 zima::viewer::CandidateKind::SketchSegment,
                 zima::viewer::CandidateKind::SketchAxis,
                 zima::viewer::CandidateKind::SketchExternalReference};
    } else if (line_pair_pending) {
        // A line + axis/construction pair may still receive a second real
        // line to build a symmetric angle/distance dimension about that
        // axis; keep segments (and further axes, filtered on click) offered.
        kinds = {zima::viewer::CandidateKind::SketchSegment,
                 zima::viewer::CandidateKind::SketchAxis};
    } else if (refs.size() == 2) {
        const bool point_pair = refs[0].kind ==
                UniversalDimensionReferenceKind::Point &&
            refs[1].kind == UniversalDimensionReferenceKind::Point;
        kinds = {zima::viewer::CandidateKind::SketchPoint};
        if (point_pair) {
            kinds.push_back(zima::viewer::CandidateKind::SketchSegment);
            kinds.push_back(zima::viewer::CandidateKind::SketchAxis);
        }
    } else if (three_points) {
        kinds = {zima::viewer::CandidateKind::SketchPoint};
    }
    if (refs.empty() &&
        !universal_pending_dimension_ &&
        universal_corner_radius_dimension_id_.empty()) {
        kinds.push_back(zima::viewer::CandidateKind::SketchCurve);
    }
    viewer_->set_selection_contract(kinds);
    const auto owner = active_sketch_id_;
    const auto references = refs;
    std::set<std::string> excluded_points;
    if (references.size() == 1 &&
        references[0].kind == UniversalDimensionReferenceKind::Line) {
        if (const auto* sketch = active_sketch()) {
            const auto segment = std::ranges::find_if(sketch->segments,
                [&](const auto& value) {
                    return value.id == references[0].id;
                });
            if (segment != sketch->segments.end()) {
                excluded_points.insert(segment->first_point_id);
                excluded_points.insert(segment->second_point_id);
            }
        }
    }
    const bool initial = references.empty() && !universal_pending_dimension_ &&
        universal_corner_radius_dimension_id_.empty();
    viewer_->set_candidate_filter(
        [owner, references, excluded_points, initial](const auto& candidate) {
            if (candidate.owner_id != owner) return false;
            std::string id;
            UniversalDimensionReferenceKind kind{};
            if (candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
                candidate.semantic_key.starts_with("point:")) {
                id = candidate.semantic_key.substr(6);
                kind = UniversalDimensionReferenceKind::Point;
                if (excluded_points.contains(id)) return false;
            } else if (candidate.kind ==
                           zima::viewer::CandidateKind::SketchSegment &&
                       candidate.semantic_key.starts_with("segment:")) {
                id = candidate.semantic_key.substr(8);
                kind = UniversalDimensionReferenceKind::Line;
            } else if (candidate.kind ==
                           zima::viewer::CandidateKind::SketchAxis &&
                       (candidate.semantic_key == "sketch_axis:x" ||
                        candidate.semantic_key == "sketch_axis:y")) {
                id = candidate.semantic_key;
                kind = UniversalDimensionReferenceKind::Line;
            } else if (candidate.kind == zima::viewer::CandidateKind::
                           SketchExternalReference &&
                       candidate.semantic_key ==
                           "external_point:sketch_origin") {
                id = "sketch_origin";
                kind = UniversalDimensionReferenceKind::Point;
            } else if (initial && candidate.kind ==
                           zima::viewer::CandidateKind::Dimension &&
                       candidate.semantic_key.starts_with("dimension:")) {
                return true;
            } else if (initial && candidate.kind ==
                           zima::viewer::CandidateKind::SketchCurve) {
                return candidate.semantic_key.starts_with("circle:") ||
                    candidate.semantic_key.starts_with("arc:") ||
                    candidate.semantic_key.starts_with("corner_radius:");
            } else {
                return false;
            }
            const bool repeated_first_line_for_symmetry =
                references.size() == 2 &&
                references[0].kind == UniversalDimensionReferenceKind::Line &&
                references[1].kind == UniversalDimensionReferenceKind::Line &&
                kind == UniversalDimensionReferenceKind::Line &&
                id == references[0].id;
            const bool repeated_point_for_symmetry =
                references.size() == 2 &&
                kind == UniversalDimensionReferenceKind::Point &&
                ((references[0].kind ==
                      UniversalDimensionReferenceKind::Point &&
                  references[1].kind ==
                      UniversalDimensionReferenceKind::Line &&
                  id == references[0].id) ||
                 (references[0].kind ==
                      UniversalDimensionReferenceKind::Line &&
                  references[1].kind ==
                      UniversalDimensionReferenceKind::Point &&
                  id == references[1].id));
            return repeated_first_line_for_symmetry ||
                repeated_point_for_symmetry ||
                std::ranges::none_of(references, [&](const auto& value) {
                return value.kind == kind && value.id == id;
            });
        });
    if (references.size() == 1 &&
        references[0].kind == UniversalDimensionReferenceKind::Line) {
        // A line endpoint and its owning line overlap by definition. For the
        // second angular direction offer the line itself first; the point
        // remains in this same list and is reachable by RMB cycling.
        viewer_->set_candidate_priority([](const auto& candidate) {
            if (candidate.kind ==
                    zima::viewer::CandidateKind::SketchSegment) return 0;
            if (candidate.kind ==
                    zima::viewer::CandidateKind::SketchAxis) return 1;
            if (candidate.kind ==
                    zima::viewer::CandidateKind::SketchPoint) return 2;
            return 3;
        });
    }
}

void AssemblyWorkspaceWindow::accept_sketch_universal_dimension(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_universal_dimension_active_ ||
        candidate.owner_id != active_sketch_id_) return;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
    if (universal_dimension_references_.empty() &&
        !universal_pending_dimension_ &&
        universal_corner_radius_dimension_id_.empty() &&
        candidate.kind == zima::viewer::CandidateKind::Dimension &&
        candidate.semantic_key.starts_with("dimension:")) {
        const auto dimension_id = candidate.semantic_key.substr(10);
        reset_sketch_universal_dimension(false);
        tree_->clearSelection();
        QTreeWidgetItemIterator iterator(tree_);
        while (*iterator != nullptr) {
            auto* item = *iterator;
            if (item->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-sketch-dimension") &&
                item->data(0, Qt::UserRole).toString() ==
                    QString::fromStdString(dimension_id)) {
                item->setSelected(true);
                tree_->setCurrentItem(item);
                tree_->scrollToItem(item);
                break;
            }
            ++iterator;
        }
        viewer_->confirm_reference(active_sketch_id_,
            "dimension:" + dimension_id, {},
            zima::viewer::CandidateKind::Dimension);
        state_->setText(tr("Vybrána kóta skici."));
        return;
    }
    try {
        if (universal_dimension_references_.empty() &&
            !universal_pending_dimension_ &&
            candidate.kind == zima::viewer::CandidateKind::SketchCurve) {
            if (candidate.semantic_key.starts_with("circle:")) {
                universal_pending_dimension_ =
                    sketch->create_circle_diameter_dimension(
                        candidate.semantic_key.substr(7));
            } else if (candidate.semantic_key.starts_with("arc:")) {
                universal_pending_dimension_ =
                    sketch->create_arc_radius_dimension(
                        candidate.semantic_key.substr(4));
            } else if (candidate.semantic_key.starts_with("corner_radius:")) {
                universal_corner_radius_dimension_id_ =
                    candidate.semantic_key.substr(14);
            } else return;
            clear_sketch_confirmed_selection();
            set_sketch_universal_dimension_contract();
            preserve_view_on_refresh_ = true;
            refresh_scene();
            state_->setText(tr(
                "Univerzální kóta: kliknutím do prostoru určete polohu."));
            return;
        }

        UniversalDimensionReference next;
        if (candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
            candidate.semantic_key.starts_with("point:")) {
            next = {UniversalDimensionReferenceKind::Point,
                    candidate.semantic_key.substr(6)};
        } else if (candidate.kind ==
                       zima::viewer::CandidateKind::SketchSegment &&
                   candidate.semantic_key.starts_with("segment:")) {
            next = {UniversalDimensionReferenceKind::Line,
                    candidate.semantic_key.substr(8)};
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
                   (candidate.semantic_key == "sketch_axis:x" ||
                    candidate.semantic_key == "sketch_axis:y")) {
            next = {UniversalDimensionReferenceKind::Line,
                    candidate.semantic_key};
        } else if (candidate.kind == zima::viewer::CandidateKind::
                       SketchExternalReference &&
                   candidate.semantic_key ==
                       "external_point:sketch_origin") {
            next = {UniversalDimensionReferenceKind::Point, "sketch_origin"};
        } else return;

        const bool repeated_first_line_for_symmetry =
            universal_dimension_references_.size() == 2 &&
            universal_dimension_references_[0].kind ==
                UniversalDimensionReferenceKind::Line &&
            universal_dimension_references_[1].kind ==
                UniversalDimensionReferenceKind::Line &&
            next.kind == UniversalDimensionReferenceKind::Line &&
            next.id == universal_dimension_references_[0].id &&
            !sketch_axis_like_reference(*sketch,
                universal_dimension_references_[0].id) &&
            sketch_axis_like_reference(*sketch,
                universal_dimension_references_[1].id);
        const bool repeated_point_for_symmetry =
            universal_dimension_references_.size() == 2 &&
            next.kind == UniversalDimensionReferenceKind::Point &&
            ((universal_dimension_references_[0].kind ==
                  UniversalDimensionReferenceKind::Point &&
              universal_dimension_references_[1].kind ==
                  UniversalDimensionReferenceKind::Line &&
              next.id == universal_dimension_references_[0].id) ||
             (universal_dimension_references_[0].kind ==
                  UniversalDimensionReferenceKind::Line &&
              universal_dimension_references_[1].kind ==
                  UniversalDimensionReferenceKind::Point &&
              next.id == universal_dimension_references_[1].id));
        if (!repeated_first_line_for_symmetry &&
            !repeated_point_for_symmetry &&
            std::ranges::any_of(universal_dimension_references_,
                [&](const auto& value) {
                    return value.kind == next.kind && value.id == next.id;
                })) return;

        const auto& refs = universal_dimension_references_;
        if (refs.empty()) {
            universal_dimension_references_.push_back(next);
            if (next.kind == UniversalDimensionReferenceKind::Line &&
                !next.id.starts_with("sketch_axis:")) {
                universal_pending_dimension_ = sketch->create_segment_dimension(
                    next.id, zima::sketcher::DimensionKind::Distance);
            }
        } else if (refs.size() == 1 &&
                   refs[0].kind == UniversalDimensionReferenceKind::Line &&
                   next.kind == UniversalDimensionReferenceKind::Line) {
            const bool next_axis = next.id.starts_with("sketch_axis:");
            const auto reference_line_id = next_axis ? next.id : refs[0].id;
            const auto driven_line_id = next_axis ? refs[0].id : next.id;
            // Two parallel reference lines have no well-defined angle (it is
            // always a degenerate 0 deg or 180 deg). Offering an angle
            // dimension there would let the user create a meaningless
            // constraint that later fights any Horizontal/Vertical
            // constraint on either line. Fall back to the distance between
            // the parallel lines instead.
            try {
                universal_pending_dimension_ = sketch->create_line_pair_dimension(
                    reference_line_id, driven_line_id,
                    zima::sketcher::DimensionKind::DistanceLine);
            } catch (const std::invalid_argument&) {
                universal_pending_dimension_ = sketch->create_line_pair_dimension(
                    reference_line_id, driven_line_id,
                    zima::sketcher::DimensionKind::AngleBetween);
                universal_pending_dimension_->angle_presentation_reversed =
                    next_axis;
            }
            universal_dimension_references_.push_back(next);
        } else if (refs.size() == 2 &&
                   refs[0].kind == UniversalDimensionReferenceKind::Line &&
                   refs[1].kind == UniversalDimensionReferenceKind::Line &&
                   next.kind == UniversalDimensionReferenceKind::Line) {
            // Line, axis/construction line, then a second real line: build
            // the symmetric angle/distance dimension for both real lines
            // about the shared axis, mirroring how a point + axis + second
            // point yields DistanceSymmetric.
            const bool axis_first = sketch_axis_like_reference(*sketch, refs[0].id);
            const bool axis_second = sketch_axis_like_reference(*sketch, refs[1].id);
            const bool next_is_axis_like =
                sketch_axis_like_reference(*sketch, next.id);
            if (axis_first == axis_second || next_is_axis_like) return;
            const auto axis_id = axis_first ? refs[0].id : refs[1].id;
            const auto first_line_id = axis_first ? refs[1].id : refs[0].id;
            const auto second_line_id = repeated_first_line_for_symmetry
                ? std::string{} : next.id;
            const bool symmetric_angle = universal_pending_dimension_ &&
                universal_pending_dimension_->kind ==
                    zima::sketcher::DimensionKind::AngleBetween;
            universal_pending_dimension_ = sketch->create_line_symmetric_dimension(
                axis_id, first_line_id, second_line_id,
                symmetric_angle ? zima::sketcher::DimensionKind::AngleSymmetric
                                 : zima::sketcher::DimensionKind::
                                       DistanceLineSymmetric);
            universal_dimension_references_.push_back(next);
        } else if (refs.size() == 1 &&
                   refs[0].kind == UniversalDimensionReferenceKind::Point &&
                   next.kind == UniversalDimensionReferenceKind::Point) {
            universal_dimension_references_.push_back(next);
            universal_pending_dimension_ = sketch->create_point_dimension(
                refs[0].id, next.id,
                zima::sketcher::DimensionKind::Distance);
        } else if (refs.size() == 1 &&
                   refs[0].kind == UniversalDimensionReferenceKind::Point &&
                   next.kind == UniversalDimensionReferenceKind::Line) {
            universal_pending_dimension_ = sketch->create_point_line_dimension(
                refs[0].id, next.id);
            universal_dimension_references_.push_back(next);
        } else if (refs.size() == 1 &&
                   refs[0].kind == UniversalDimensionReferenceKind::Line &&
                   next.kind == UniversalDimensionReferenceKind::Point) {
            universal_pending_dimension_ = sketch->create_point_line_dimension(
                next.id, refs[0].id);
            universal_dimension_references_.push_back(next);
        } else if (refs.size() == 2 &&
                   refs[0].kind == UniversalDimensionReferenceKind::Point &&
                   refs[1].kind == UniversalDimensionReferenceKind::Point) {
            if (next.kind == UniversalDimensionReferenceKind::Point) {
                universal_pending_dimension_.reset();
            } else {
                universal_pending_dimension_ =
                    sketch->create_point_line_angle_dimension(
                        refs[0].id, refs[1].id, next.id);
            }
            universal_dimension_references_.push_back(next);
        } else if (refs.size() == 3 &&
                   refs[0].kind == UniversalDimensionReferenceKind::Point &&
                   refs[1].kind == UniversalDimensionReferenceKind::Point &&
                   refs[2].kind == UniversalDimensionReferenceKind::Point &&
                   next.kind == UniversalDimensionReferenceKind::Point) {
            universal_pending_dimension_ =
                sketch->create_four_point_angle_dimension(
                    refs[0].id, refs[1].id, refs[2].id, next.id);
            universal_dimension_references_.push_back(next);
        } else if (refs.size() == 2 &&
                   refs[0].kind == UniversalDimensionReferenceKind::Line &&
                   refs[1].kind == UniversalDimensionReferenceKind::Point &&
                   next.kind == UniversalDimensionReferenceKind::Point) {
            universal_pending_dimension_ = sketch_axis_like_reference(
                    *sketch, refs[0].id)
                ? sketch->create_symmetric_dimension(
                    refs[1].id,
                    repeated_point_for_symmetry ? std::string{} : next.id,
                    refs[0].id)
                : sketch->create_point_line_angle_dimension(
                    refs[1].id, next.id, refs[0].id);
            universal_dimension_references_.push_back(next);
        } else if (refs.size() == 2 &&
                   refs[0].kind == UniversalDimensionReferenceKind::Point &&
                   refs[1].kind == UniversalDimensionReferenceKind::Line &&
                   next.kind == UniversalDimensionReferenceKind::Point) {
            universal_pending_dimension_ = sketch_axis_like_reference(
                    *sketch, refs[1].id)
                ? sketch->create_symmetric_dimension(
                    refs[0].id,
                    repeated_point_for_symmetry ? std::string{} : next.id,
                    refs[1].id)
                : sketch->create_point_line_angle_dimension(
                    refs[0].id, next.id, refs[1].id);
            universal_dimension_references_.push_back(next);
        } else return;

        // Keep the pointer position captured while hovering the selected
        // reference.  In particular, selecting the second line converts the
        // first line's live length preview into an angular preview; clearing
        // the cursor here made that preview disappear until another mouse
        // event and broke the continuous placement interaction.
        clear_sketch_confirmed_selection();
        set_sketch_universal_dimension_contract();
        preserve_view_on_refresh_ = true;
        refresh_scene();
        if (universal_pending_dimension_ &&
            (universal_pending_dimension_->kind ==
                 zima::sketcher::DimensionKind::AngleSymmetric ||
             universal_pending_dimension_->kind ==
                 zima::sketcher::DimensionKind::DistanceLineSymmetric)) {
            state_->setText(tr(
                "Univerzální kóta: symetrická kóta přes osu je připravena; "
                "klikněte do prostoru pro umístění."));
        } else if (universal_pending_dimension_ &&
            universal_pending_dimension_->kind ==
                zima::sketcher::DimensionKind::AngleBetween) {
            state_->setText(tr(
                "Univerzální kóta: oba směry jsou definované; klikněte do "
                "prostoru pro umístění úhlové kóty."));
        } else {
            state_->setText(universal_pending_dimension_
                ? tr("Univerzální kóta: klikněte do prostoru pro uložení, "
                     "nebo vyberte další nabízenou referenci.")
                : tr("Univerzální kóta: vyberte další referenci."));
        }
    } catch (const std::exception& error) {
        state_->setText(tr("Univerzální kótu nelze sestavit: %1")
            .arg(QString::fromUtf8(error.what())));
    }
}

bool AssemblyWorkspaceWindow::accept_sketch_universal_dimension_ray(
    const zima::kernel::Vec3& origin,
    const zima::kernel::Vec3& direction) {
    if (!sketch_universal_dimension_active_ ||
        (!universal_pending_dimension_ &&
         universal_corner_radius_dimension_id_.empty())) return false;
    // MeshView intentionally invokes the command's world-click handler before
    // it confirms the candidate computed for the very same LMB press. A
    // pending dimension therefore must decline this phase whenever the common
    // picker is offering a valid reference; confirmation will immediately
    // route that exact candidate to accept_sketch_universal_dimension(). Only
    // a genuinely empty candidate list means "place the dimension here".
    if (viewer_ != nullptr && viewer_->offered_candidate()) return false;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
    const auto cursor = sketch->intersect_ray(origin, direction);
    if (!cursor) return true;
    bool completed_symmetric_dimension = false;
    try {
        if (!universal_corner_radius_dimension_id_.empty()) {
            const auto radius_id = universal_corner_radius_dimension_id_;
            if (!mutate_active_sketch([&](auto& target) {
                    const auto radius = std::ranges::find_if(
                        target.corner_radii, [&](const auto& value) {
                            return value.id == radius_id;
                        });
                    if (radius == target.corner_radii.end())
                        throw std::runtime_error("Corner radius no longer exists");
                    radius->dimension_visible = true;
                    radius->dimension_placement = *cursor;
                })) throw std::runtime_error("Sketch is no longer active");
            universal_corner_radius_dimension_id_.clear();
        } else {
            auto dimension = *universal_pending_dimension_;
            completed_symmetric_dimension =
                dimension.kind == zima::sketcher::DimensionKind::AngleSymmetric ||
                dimension.kind ==
                    zima::sketcher::DimensionKind::DistanceLineSymmetric;
            if (universal_dimension_references_.size() >= 2 &&
                universal_dimension_references_[0].kind ==
                    UniversalDimensionReferenceKind::Point &&
                universal_dimension_references_[1].kind ==
                    UniversalDimensionReferenceKind::Point &&
                dimension.kind == zima::sketcher::DimensionKind::Distance) {
                const auto first = sketch_point_reference_position(*sketch,
                    universal_dimension_references_[0].id);
                const auto second = sketch_point_reference_position(*sketch,
                    universal_dimension_references_[1].id);
                if (first && second) {
                    const auto kind = zima::sketcher::classify_linear_dimension(
                        *first, *second, *cursor);
                    dimension = sketch->create_point_dimension(
                        universal_dimension_references_[0].id,
                        universal_dimension_references_[1].id, kind);
                }
            }
            dimension.placement = *cursor;
            if (dimension.kind ==
                    zima::sketcher::DimensionKind::AngleBetween) {
                auto sector_preview = *sketch;
                sector_preview.dimensions.push_back(dimension);
                const auto preview_mesh = sector_preview.viewer_mesh();
                const auto preview_dimension = std::ranges::find_if(
                    preview_mesh.dimensions, [&](const auto& value) {
                        return value.reference.semantic_key ==
                            "dimension:" + dimension.id;
                    });
                if (preview_dimension != preview_mesh.dimensions.end()) {
                    const double base_value = dimension.value;
                    dimension.value = preview_dimension->value;
                    dimension.angle_sector =
                        std::abs(std::abs(dimension.value) -
                            std::abs(base_value)) <= 1.0e-7
                        ? 0 : 1;
                }
            }
            if (!mutate_active_sketch([&](auto& target) {
                    target.apply_dimension(dimension);
                })) throw std::runtime_error("Sketch is no longer active");
        }
    } catch (const std::exception& error) {
        state_->setText(tr("Univerzální kótu nelze uložit: %1")
            .arg(QString::fromUtf8(error.what())));
        return true;
    }
    reset_sketch_universal_dimension(!completed_symmetric_dimension);
    preserve_view_on_refresh_ = true;
    refresh_tabs();
    refresh_scene();
    state_->setText(completed_symmetric_dimension
        ? tr("Symetrická kóta byla vytvořena; lze ji editovat a geometrii táhnout.")
        : tr("Univerzální kóta byla vytvořena. Vyberte první referenci další kóty."));
    return true;
}

void AssemblyWorkspaceWindow::start_sketch_point_dimension(
    zima::sketcher::DimensionKind kind) {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty() ||
        (kind != zima::sketcher::DimensionKind::Distance &&
         kind != zima::sketcher::DimensionKind::DistanceX &&
         kind != zima::sketcher::DimensionKind::DistanceY &&
         kind != zima::sketcher::DimensionKind::DistancePointLine &&
         kind != zima::sketcher::DimensionKind::DistanceSymmetric &&
         kind != zima::sketcher::DimensionKind::AngleThreePoint)) return;
    const std::string first_id;
    cancel_sketch_segment();
    sketch_point_dimension_active_ = true;
    sketch_dimension_action_->setChecked(
        kind == zima::sketcher::DimensionKind::Distance);
    pending_point_dimension_first_id_ = first_id;
    pending_point_dimension_vertex_id_.clear();
    pending_point_dimension_kind_ = kind;
    const bool point_to_line =
        kind == zima::sketcher::DimensionKind::DistancePointLine ||
        kind == zima::sketcher::DimensionKind::DistanceSymmetric;
    viewer_->set_selection_contract(first_id.empty()
        ? kind == zima::sketcher::DimensionKind::Distance
            ? std::vector{zima::viewer::CandidateKind::SketchPoint,
                          zima::viewer::CandidateKind::SketchSegment,
                          zima::viewer::CandidateKind::SketchCurve,
                          zima::viewer::CandidateKind::SketchAxis,
                          zima::viewer::CandidateKind::SketchExternalReference}
            : std::vector{zima::viewer::CandidateKind::SketchPoint}
        : point_to_line
        ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                      zima::viewer::CandidateKind::SketchAxis,
                      zima::viewer::CandidateKind::SketchExternalReference}
        : kind == zima::sketcher::DimensionKind::AngleThreePoint
        ? std::vector{zima::viewer::CandidateKind::SketchPoint}
        : kind == zima::sketcher::DimensionKind::Distance
        ? std::vector{zima::viewer::CandidateKind::SketchPoint,
                      zima::viewer::CandidateKind::SketchExternalReference,
                      zima::viewer::CandidateKind::SketchAxis}
        : std::vector{zima::viewer::CandidateKind::SketchPoint,
                      zima::viewer::CandidateKind::SketchExternalReference,
                      zima::viewer::CandidateKind::SketchAxis});
    const auto owner_id = active_sketch_id_;
    viewer_->set_candidate_filter([owner_id, first_id, kind](const auto& candidate) {
        if (candidate.owner_id != owner_id) return false;
        if (first_id.empty()) {
            if (candidate.kind == zima::viewer::CandidateKind::SketchPoint)
                return candidate.semantic_key.starts_with("point:");
            if (kind == zima::sketcher::DimensionKind::Distance &&
                candidate.kind == zima::viewer::CandidateKind::SketchAxis) {
                return candidate.semantic_key == "sketch_axis:x" ||
                    candidate.semantic_key == "sketch_axis:y";
            }
            if (kind == zima::sketcher::DimensionKind::Distance &&
                candidate.kind ==
                    zima::viewer::CandidateKind::SketchExternalReference) {
                return candidate.semantic_key ==
                    "external_point:sketch_origin";
            }
            return kind == zima::sketcher::DimensionKind::Distance &&
                ((candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
                  candidate.semantic_key.starts_with("segment:")) ||
                 (candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
                  (candidate.semantic_key.starts_with("circle:") ||
                   candidate.semantic_key.starts_with("arc:") ||
                   candidate.semantic_key.starts_with("corner_radius:"))));
        }
        if (kind == zima::sketcher::DimensionKind::DistancePointLine ||
            kind == zima::sketcher::DimensionKind::DistanceSymmetric) {
            return (candidate.kind ==
                        zima::viewer::CandidateKind::SketchSegment &&
                    candidate.semantic_key.starts_with("segment:")) ||
                (candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
                 (candidate.semantic_key == "sketch_axis:x" ||
                  candidate.semantic_key == "sketch_axis:y")) ||
                (candidate.kind ==
                     zima::viewer::CandidateKind::SketchExternalReference &&
                 (candidate.semantic_key.starts_with("external_edge:") ||
                  candidate.semantic_key.starts_with("external_axis:")));
        }
        if (candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
            candidate.semantic_key.starts_with("point:")) {
            return candidate.semantic_key.substr(6) != first_id;
        }
        if (candidate.kind == zima::viewer::CandidateKind::SketchAxis) {
            if (kind == zima::sketcher::DimensionKind::Distance) {
                return first_id != "sketch_origin" &&
                    (candidate.semantic_key == "sketch_axis:x" ||
                     candidate.semantic_key == "sketch_axis:y");
            }
            return (kind == zima::sketcher::DimensionKind::DistanceX &&
                    candidate.semantic_key == "sketch_axis:y") ||
                (kind == zima::sketcher::DimensionKind::DistanceY &&
                 candidate.semantic_key == "sketch_axis:x");
        }
        return candidate.kind ==
                zima::viewer::CandidateKind::SketchExternalReference &&
            candidate.semantic_key.starts_with("external_point:") &&
            !(first_id == "sketch_origin" &&
              candidate.semantic_key == "external_point:sketch_origin");
    });
    state_->setText(first_id.empty()
        ? tr("Kóta: vyberte první bod nebo lokální počátek. "
             "Escape příkaz zruší.")
        : kind == zima::sketcher::DimensionKind::AngleThreePoint
        ? tr("Tříbodový úhel: vyberte vrchol a potom druhé rameno.")
        : point_to_line
        ? kind == zima::sketcher::DimensionKind::DistanceSymmetric
            ? tr("Symetrická kóta: vyberte osu souměrnosti.")
            : tr("Vzdálenost bodu: vyberte přímku, osu nebo externí hranu.")
        : tr("Bodová kóta: vyberte druhý lokální nebo externí bod. "
             "Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::accept_sketch_point_dimension(
    const zima::viewer::ViewerCandidate& candidate) {
    const bool converts_segment_length = pending_sketch_dimension_ &&
        pending_sketch_dimension_->kind ==
            zima::sketcher::DimensionKind::Distance &&
        !pending_sketch_dimension_->geometry_id.empty() &&
        pending_sketch_dimension_->second_geometry_id.empty();
    const bool converts_three_point_angle = pending_sketch_dimension_ &&
        pending_sketch_dimension_->kind ==
            zima::sketcher::DimensionKind::AngleThreePoint;
    if ((!sketch_point_dimension_active_ && !converts_segment_length &&
         !converts_three_point_angle) ||
        candidate.owner_id != active_sketch_id_) return;
    if (converts_three_point_angle &&
        candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
        candidate.semantic_key.starts_with("point:")) {
        const auto fourth_id = candidate.semantic_key.substr(6);
        const auto& current = *pending_sketch_dimension_;
        if (fourth_id == current.first_point_id ||
            fourth_id == current.second_point_id ||
            fourth_id == current.geometry_id) return;
        if (const auto* sketch = active_sketch()) {
            pending_sketch_dimension_ = sketch->create_four_point_angle_dimension(
                current.second_point_id, current.first_point_id,
                current.geometry_id, fourth_id);
            pending_point_dimension_cursor_.reset();
            clear_sketch_confirmed_selection();
            preserve_view_on_refresh_ = true;
            refresh_scene();
        }
        return;
    }
    if (pending_sketch_dimension_ &&
        pending_sketch_dimension_->kind ==
            zima::sketcher::DimensionKind::Distance &&
        !pending_sketch_dimension_->geometry_id.empty() &&
        ((candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
          candidate.semantic_key.starts_with("segment:")) ||
         (candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
          (candidate.semantic_key == "sketch_axis:x" ||
           candidate.semantic_key == "sketch_axis:y")) ||
         (candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
          candidate.semantic_key.starts_with("point:")))) {
        if (candidate.kind == zima::viewer::CandidateKind::SketchPoint) {
            const auto* sketch = active_sketch();
            if (sketch == nullptr) return;
            const auto segment = std::ranges::find_if(sketch->segments,
                [&](const auto& value) {
                    return value.id == pending_sketch_dimension_->geometry_id;
                });
            if (segment == sketch->segments.end()) return;
            const auto third_id = candidate.semantic_key.substr(6);
            if (third_id == segment->first_point_id ||
                third_id == segment->second_point_id) return;
            try {
                pending_sketch_dimension_ =
                    sketch->create_three_point_angle_dimension(
                        segment->second_point_id, segment->first_point_id,
                        third_id);
                pending_point_dimension_cursor_.reset();
                clear_sketch_confirmed_selection();
                state_->setText(tr(
                    "Kóta: kliknutím do prostoru umístěte úhlovou kótu."));
                preserve_view_on_refresh_ = true;
                refresh_scene();
            } catch (const std::exception& error) {
                state_->setText(QString::fromUtf8(error.what()));
            }
            return;
        }
        const auto second_segment =
            candidate.kind == zima::viewer::CandidateKind::SketchAxis
                ? candidate.semantic_key : candidate.semantic_key.substr(8);
        if (second_segment == pending_sketch_dimension_->geometry_id) return;
        if (const auto* sketch = active_sketch()) {
            try {
                const bool second_is_axis = candidate.kind ==
                    zima::viewer::CandidateKind::SketchAxis;
                pending_sketch_dimension_ = sketch->create_line_pair_dimension(
                    second_is_axis ? second_segment
                                   : pending_sketch_dimension_->geometry_id,
                    second_is_axis ? pending_sketch_dimension_->geometry_id
                                   : second_segment,
                    zima::sketcher::DimensionKind::AngleBetween);
                pending_point_dimension_cursor_.reset();
                clear_sketch_confirmed_selection();
                state_->setText(tr(
                    "Kóta: třetím klikem určete polohu úhlové kóty."));
                preserve_view_on_refresh_ = true;
                refresh_scene();
            } catch (const std::exception& error) {
                state_->setText(QString::fromUtf8(error.what()));
            }
        }
        return;
    }
    if (!pending_point_dimension_second_id_.empty()) {
        const auto* sketch = active_sketch();
        if (sketch == nullptr) return;
        try {
            if (candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
                candidate.semantic_key.starts_with("point:")) {
                const auto third_id = candidate.semantic_key.substr(6);
                if (third_id == pending_point_dimension_first_id_ ||
                    third_id == pending_point_dimension_second_id_) return;
                // Unified order A,B,C: A is the angular vertex and AB/AC are
                // the two directions. The model stores arm,vertex,arm.
                pending_sketch_dimension_ =
                    sketch->create_three_point_angle_dimension(
                        pending_point_dimension_second_id_,
                        pending_point_dimension_first_id_, third_id);
            } else if ((candidate.kind ==
                            zima::viewer::CandidateKind::SketchSegment &&
                        candidate.semantic_key.starts_with("segment:")) ||
                       (candidate.kind ==
                            zima::viewer::CandidateKind::SketchAxis &&
                        (candidate.semantic_key == "sketch_axis:x" ||
                         candidate.semantic_key == "sketch_axis:y"))) {
                const auto line_id = candidate.kind ==
                        zima::viewer::CandidateKind::SketchAxis
                    ? candidate.semantic_key : candidate.semantic_key.substr(8);
                pending_sketch_dimension_ =
                    sketch->create_point_line_angle_dimension(
                        pending_point_dimension_first_id_,
                        pending_point_dimension_second_id_, line_id);
            } else {
                return;
            }
            pending_point_dimension_cursor_.reset();
            clear_sketch_confirmed_selection();
            state_->setText(tr(
                "Kóta: kliknutím do prostoru umístěte úhlovou kótu; "
                "další bod může určit druhý bod druhého směru."));
            preserve_view_on_refresh_ = true;
            refresh_scene();
        } catch (const std::exception& error) {
            state_->setText(QString::fromUtf8(error.what()));
        }
        return;
    }
    if (pending_point_dimension_first_id_.empty()) {
        if (pending_point_dimension_kind_ ==
                zima::sketcher::DimensionKind::Distance &&
            ((candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
              (candidate.semantic_key == "sketch_axis:x" ||
               candidate.semantic_key == "sketch_axis:y")) ||
             (candidate.kind ==
                  zima::viewer::CandidateKind::SketchExternalReference &&
              candidate.semantic_key ==
                  "external_point:sketch_origin"))) {
            pending_point_dimension_first_id_ = "sketch_origin";
            clear_sketch_confirmed_selection();
            preserve_view_on_refresh_ = true;
            refresh_scene();
            state_->setText(tr("Kóta: vyberte druhý bod."));
            return;
        }
        if (pending_point_dimension_kind_ ==
                zima::sketcher::DimensionKind::Distance &&
            candidate.kind == zima::viewer::CandidateKind::SketchCurve) {
            const auto* sketch = active_sketch();
            if (sketch == nullptr) return;
            if (candidate.semantic_key.starts_with("circle:")) {
                pending_sketch_dimension_ = sketch->create_circle_diameter_dimension(
                    candidate.semantic_key.substr(7));
            } else if (candidate.semantic_key.starts_with("arc:")) {
                try {
                    pending_sketch_dimension_ = sketch->create_arc_radius_dimension(
                        candidate.semantic_key.substr(4));
                } catch (const std::exception& error) {
                    state_->setText(tr("Kótu radiusu nelze vytvořit: %1")
                        .arg(QString::fromUtf8(error.what())));
                }
            } else if (candidate.semantic_key.starts_with("corner_radius:")) {
                pending_corner_radius_dimension_id_ =
                    candidate.semantic_key.substr(14);
            }
            pending_point_dimension_cursor_.reset();
            clear_sketch_confirmed_selection();
            state_->setText(tr("Kóta: třetím klikem určete její polohu."));
            preserve_view_on_refresh_ = true;
            refresh_scene();
            return;
        }
        if (pending_point_dimension_kind_ ==
                zima::sketcher::DimensionKind::Distance &&
            candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
            candidate.semantic_key.starts_with("segment:")) {
            const auto* sketch = active_sketch();
            if (sketch == nullptr) return;
            pending_sketch_dimension_ = sketch->create_segment_dimension(
                candidate.semantic_key.substr(8),
                zima::sketcher::DimensionKind::Distance);
            pending_point_dimension_cursor_.reset();
            clear_sketch_confirmed_selection();
            state_->setText(tr(
                "Kóta: vyberte druhou úsečku pro úhel, nebo třetím klikem "
                "umístěte délkovou kótu."));
            preserve_view_on_refresh_ = true;
            refresh_scene();
            return;
        }
        if (candidate.kind != zima::viewer::CandidateKind::SketchPoint ||
            !candidate.semantic_key.starts_with("point:")) return;
        pending_point_dimension_first_id_ = candidate.semantic_key.substr(6);
        clear_sketch_confirmed_selection();
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(pending_point_dimension_kind_ ==
                zima::sketcher::DimensionKind::AngleThreePoint
            ? tr("Tříbodový úhel: vyberte vrchol.")
            : tr("Kóta: vyberte druhou referenci."));
        return;
    }
    std::string second_id;
    if (pending_point_dimension_kind_ ==
            zima::sketcher::DimensionKind::AngleThreePoint) {
        if (!pending_point_dimension_vertex_id_.empty() &&
            ((candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
              candidate.semantic_key.starts_with("segment:")) ||
             (candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
              (candidate.semantic_key == "sketch_axis:x" ||
               candidate.semantic_key == "sketch_axis:y")))) {
            const auto first_id = pending_point_dimension_first_id_;
            const auto second_id = pending_point_dimension_vertex_id_;
            const auto line_id = candidate.kind ==
                    zima::viewer::CandidateKind::SketchAxis
                ? candidate.semantic_key : candidate.semantic_key.substr(8);
            sketch_point_dimension_active_ = false;
            pending_point_dimension_first_id_.clear();
            pending_point_dimension_vertex_id_.clear();
            viewer_->set_candidate_filter({});
            show_sketch_dimension_properties(active_sketch_id_, {},
                zima::sketcher::DimensionKind::AngleBetween,
                first_id, second_id, line_id);
            return;
        }
        if (candidate.kind != zima::viewer::CandidateKind::SketchPoint ||
            !candidate.semantic_key.starts_with("point:")) return;
        const auto point_id = candidate.semantic_key.substr(6);
        if (point_id == pending_point_dimension_first_id_ ||
            point_id == pending_point_dimension_vertex_id_) return;
        if (pending_point_dimension_vertex_id_.empty()) {
            pending_point_dimension_vertex_id_ = point_id;
            state_->setText(tr("Tříbodový úhel: vyberte bod druhého ramene."));
            preserve_view_on_refresh_ = true;
            refresh_scene();
            return;
        }
        const auto first_id = pending_point_dimension_first_id_;
        const auto vertex_id = pending_point_dimension_vertex_id_;
        sketch_point_dimension_active_ = false;
        pending_point_dimension_first_id_.clear();
        pending_point_dimension_vertex_id_.clear();
        pending_point_dimension_vertex_id_.clear();
        viewer_->set_candidate_filter({});
        show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::AngleThreePoint,
            first_id, vertex_id, point_id);
        return;
    }
    if (pending_point_dimension_kind_ ==
            zima::sketcher::DimensionKind::DistancePointLine ||
        pending_point_dimension_kind_ ==
            zima::sketcher::DimensionKind::DistanceSymmetric) {
        if (candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
            candidate.semantic_key.starts_with("segment:")) {
            second_id = candidate.semantic_key.substr(8);
        } else if (candidate.kind ==
                       zima::viewer::CandidateKind::SketchAxis &&
                   (candidate.semantic_key == "sketch_axis:x" ||
                    candidate.semantic_key == "sketch_axis:y")) {
            second_id = candidate.semantic_key;
        } else if (candidate.kind ==
                       zima::viewer::CandidateKind::SketchExternalReference) {
            const auto reference_id = sketch_external_reference_id_from_key(
                candidate.semantic_key);
            if (reference_id) second_id = *reference_id;
        }
        if (second_id.empty()) return;
        const auto point_id = pending_point_dimension_first_id_;
        sketch_point_dimension_active_ = false;
        pending_point_dimension_first_id_.clear();
        viewer_->set_candidate_filter({});
        const auto kind = pending_point_dimension_kind_;
        show_sketch_dimension_properties(active_sketch_id_, {}, kind,
            point_id, {}, second_id);
        return;
    }
    if (candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
        candidate.semantic_key.starts_with("point:")) {
        second_id = candidate.semantic_key.substr(6);
    } else if (candidate.kind ==
                   zima::viewer::CandidateKind::SketchExternalReference &&
               candidate.semantic_key.starts_with("external_point:")) {
        second_id = candidate.semantic_key.substr(15);
    } else if (candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
               pending_point_dimension_kind_ ==
                   zima::sketcher::DimensionKind::Distance &&
               (candidate.semantic_key == "sketch_axis:x" ||
                candidate.semantic_key == "sketch_axis:y")) {
        second_id = "sketch_origin";
    } else if (candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
               ((pending_point_dimension_kind_ ==
                    zima::sketcher::DimensionKind::DistanceX &&
                 candidate.semantic_key == "sketch_axis:y") ||
                (pending_point_dimension_kind_ ==
                    zima::sketcher::DimensionKind::DistanceY &&
                  candidate.semantic_key == "sketch_axis:x"))) {
        const auto point_id = pending_point_dimension_first_id_;
        const auto axis_id = candidate.semantic_key;
        const auto kind = pending_point_dimension_kind_;
        sketch_point_dimension_active_ = false;
        pending_point_dimension_first_id_.clear();
        pending_point_dimension_vertex_id_.clear();
        viewer_->set_candidate_filter({});
        show_sketch_dimension_properties(
            active_sketch_id_, {}, kind, point_id, {}, axis_id);
        return;
    }
    if (second_id.empty() || second_id == pending_point_dimension_first_id_) return;
    if (pending_point_dimension_kind_ ==
            zima::sketcher::DimensionKind::Distance) {
        pending_point_dimension_second_id_ = second_id;
        pending_point_dimension_cursor_.reset();
        clear_sketch_confirmed_selection();
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(tr(
            "Kóta: umístěte kótovací čáru. Uvnitř obdélníku vznikne "
            "šikmá kóta, nad/pod ním vodorovná a vlevo/vpravo svislá."));
        return;
    }
    auto first_id = pending_point_dimension_first_id_;
    const auto kind = pending_point_dimension_kind_;
    sketch_point_dimension_active_ = false;
    pending_point_dimension_first_id_.clear();
    pending_point_dimension_vertex_id_.clear();
    viewer_->set_candidate_filter({});
    if (second_id == "sketch_origin") std::swap(first_id, second_id);
    show_sketch_dimension_properties(
        active_sketch_id_, {}, kind, first_id, second_id);
}

bool AssemblyWorkspaceWindow::accept_sketch_dimension_placement_ray(
    const zima::kernel::Vec3& origin,
    const zima::kernel::Vec3& direction) {
    // MeshView dispatches the command-local world click before confirming the
    // exact candidate from the same LMB press. A provisional dimension must
    // therefore leave offered axes/segments/points to the common candidate
    // state machine; only a genuinely empty hit means dimension placement.
    if ((!pending_corner_radius_dimension_id_.empty() ||
         pending_sketch_dimension_) &&
        viewer_ != nullptr && viewer_->offered_candidate()) return false;
    if (!pending_corner_radius_dimension_id_.empty()) {
        const auto* sketch = active_sketch();
        if (sketch == nullptr) return false;
        const auto cursor = sketch->intersect_ray(origin, direction);
        if (!cursor) return true;
        const auto radius_id = pending_corner_radius_dimension_id_;
        try {
            if (!mutate_active_sketch([&](auto& target) {
                    const auto radius = std::find_if(target.corner_radii.begin(),
                        target.corner_radii.end(), [&](const auto& value) {
                            return value.id == radius_id;
                        });
                    if (radius == target.corner_radii.end()) {
                        throw std::runtime_error("Corner radius no longer exists");
                    }
                    radius->dimension_visible = true;
                    radius->dimension_placement = *cursor;
                })) {
                throw std::runtime_error("Sketch is no longer active");
            }
        } catch (const std::exception& error) {
            state_->setText(tr("Kótu nelze vytvořit: %1")
                .arg(QString::fromUtf8(error.what())));
            return true;
        }
        pending_corner_radius_dimension_id_.clear();
        pending_point_dimension_cursor_.reset();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        start_sketch_point_dimension(zima::sketcher::DimensionKind::Distance);
        return true;
    }
    if (pending_sketch_dimension_) {
        const auto* sketch = active_sketch();
        if (sketch == nullptr) return false;
        const auto cursor = sketch->intersect_ray(origin, direction);
        if (!cursor) return true;
        auto dimension = *pending_sketch_dimension_;
        const auto completed_kind = dimension.kind;
        const bool resume_unified_dimension =
            sketch_dimension_action_->isChecked();
        dimension.placement = *cursor;
        try {
            if (!mutate_active_sketch([&](auto& target) {
                    target.apply_dimension(dimension);
                })) {
                throw std::runtime_error("Sketch is no longer active");
            }
        } catch (const std::exception& error) {
            // A redundant/conflicting dimension is a rejected transaction,
            // not a fatal Qt event exception. Keep its preview available so
            // the user can cancel or choose another command, and leave the
            // persisted Sketch unchanged.
            state_->setText(tr("Kótu nelze vytvořit: %1")
                .arg(QString::fromUtf8(error.what())));
            return true;
        }
        pending_sketch_dimension_.reset();
    universal_dimension_layout_={};
        pending_point_dimension_cursor_.reset();
        selected_sketch_segment_id_.clear();
        selected_sketch_circle_id_.clear();
        selected_sketch_arc_id_.clear();
        selected_sketch_ellipse_id_.clear();
        viewer_->set_candidate_filter({});
        clear_completed_sketch_interaction();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        if (resume_unified_dimension) {
            start_sketch_point_dimension(
                zima::sketcher::DimensionKind::Distance);
            state_->setText(tr(
                "Kóta byla vytvořena. Vyberte další bod, úsečku, kružnici nebo oblouk; "
                "dvojklik MMB příkaz ukončí."));
        } else if (completed_kind ==
                       zima::sketcher::DimensionKind::DistanceLine ||
                   completed_kind ==
                       zima::sketcher::DimensionKind::AngleBetween) {
            start_sketch_line_pair_dimension(completed_kind);
            state_->setText(completed_kind ==
                    zima::sketcher::DimensionKind::AngleBetween
                ? tr("Úhlová kóta byla vytvořena. Vyberte první přímku nebo osu další kóty.")
                : tr("Kóta mezi přímkami byla vytvořena. Vyberte první úsečku další kóty."));
        } else if (completed_kind ==
                       zima::sketcher::DimensionKind::DistanceX ||
                   completed_kind ==
                       zima::sketcher::DimensionKind::DistanceY ||
                   completed_kind ==
                       zima::sketcher::DimensionKind::DistancePointLine ||
                   completed_kind ==
                       zima::sketcher::DimensionKind::DistanceSymmetric ||
                   completed_kind ==
                       zima::sketcher::DimensionKind::AngleThreePoint) {
            start_sketch_point_dimension(completed_kind);
            state_->setText(completed_kind ==
                    zima::sketcher::DimensionKind::AngleThreePoint
                ? tr("Tříbodová úhlová kóta byla vytvořena. Vyberte první bod další kóty.")
                : tr("Kóta byla vytvořena. Vyberte první referenci další kóty."));
        } else {
            state_->setText(tr(
                "Kóta byla vytvořena. Dvojklik na hodnotu ji upraví."));
        }
        return true;
    }
    if (!sketch_point_dimension_active_ ||
        pending_point_dimension_second_id_.empty()) return false;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
    const auto cursor = sketch->intersect_ray(origin, direction);
    if (!cursor) return true;
    finish_pending_linear_dimension(*cursor);
    return true;
}

void AssemblyWorkspaceWindow::finish_pending_linear_dimension(
    const std::array<double, 2>& cursor) {
    const auto* sketch = active_sketch();
    if (sketch == nullptr || pending_point_dimension_first_id_.empty() ||
        pending_point_dimension_second_id_.empty()) return;
    const auto first = sketch_point_reference_position(*sketch,
        pending_point_dimension_first_id_);
    const auto second = sketch_point_reference_position(*sketch,
        pending_point_dimension_second_id_);
    auto kind = zima::sketcher::DimensionKind::Distance;
    if (first && second) {
        kind = zima::sketcher::classify_linear_dimension(
            *first, *second, cursor);
    }
    const auto first_id = pending_point_dimension_first_id_;
    const auto second_id = pending_point_dimension_second_id_;
    sketch_point_dimension_active_ = false;
    pending_point_dimension_first_id_.clear();
    pending_point_dimension_second_id_.clear();
    pending_point_dimension_cursor_.reset();
    viewer_->set_candidate_filter({});
    show_sketch_dimension_properties(
        active_sketch_id_, {}, kind, first_id, second_id, {}, {}, cursor);
}

void AssemblyWorkspaceWindow::start_sketch_line_pair_dimension(
    zima::sketcher::DimensionKind kind) {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty() ||
        (kind != zima::sketcher::DimensionKind::DistanceLine &&
         kind != zima::sketcher::DimensionKind::AngleBetween)) return;
    const auto reference_id = selected_sketch_segment_id_;
    cancel_sketch_segment();
    sketch_line_pair_dimension_active_ = true;
    pending_line_dimension_reference_id_ = reference_id;
    pending_line_dimension_kind_ = kind;
    viewer_->set_selection_contract(reference_id.empty()
        ? kind == zima::sketcher::DimensionKind::AngleBetween
            ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                          zima::viewer::CandidateKind::SketchAxis,
                          zima::viewer::CandidateKind::SketchPoint}
            : std::vector{zima::viewer::CandidateKind::SketchSegment}
        : kind ==
            zima::sketcher::DimensionKind::AngleBetween
        ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                      zima::viewer::CandidateKind::SketchAxis}
        : std::vector{zima::viewer::CandidateKind::SketchSegment});
    const auto owner_id = active_sketch_id_;
    viewer_->set_candidate_filter([owner_id, reference_id, kind](const auto& candidate) {
        if (candidate.owner_id != owner_id) return false;
        if (reference_id.empty() &&
            kind == zima::sketcher::DimensionKind::AngleBetween &&
            candidate.kind == zima::viewer::CandidateKind::SketchPoint) {
            return candidate.semantic_key.starts_with("point:");
        }
        if (candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
            candidate.semantic_key.starts_with("segment:")) {
            return candidate.semantic_key.substr(8) != reference_id;
        }
        return kind == zima::sketcher::DimensionKind::AngleBetween &&
            candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
            !reference_id.starts_with("sketch_axis:") &&
            (candidate.semantic_key == "sketch_axis:x" ||
             candidate.semantic_key == "sketch_axis:y");
    });
    state_->setText(reference_id.empty()
        ? tr("Kóta mezi přímkami: vyberte první úsečku.")
        : kind == zima::sketcher::DimensionKind::DistanceLine
        ? tr("Vzdálenost rovnoběžek: vyberte druhou úsečku.")
        : tr("Úhel: vyberte druhou úsečku nebo osu skici."));
}

void AssemblyWorkspaceWindow::accept_sketch_line_pair_dimension(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_line_pair_dimension_active_ ||
        candidate.owner_id != active_sketch_id_) return;
    if (pending_line_dimension_kind_ ==
            zima::sketcher::DimensionKind::AngleBetween &&
        pending_line_dimension_reference_id_.empty() &&
        candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
        candidate.semantic_key.starts_with("point:")) {
        // One Angle command supports both reference forms.  A point as the
        // first candidate selects the persisted three-point contract.
        sketch_line_pair_dimension_active_ = false;
        start_sketch_point_dimension(
            zima::sketcher::DimensionKind::AngleThreePoint);
        accept_sketch_point_dimension(candidate);
        return;
    }
    std::string selected_id;
    bool selected_axis = false;
    if (candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
        candidate.semantic_key.starts_with("segment:")) {
        selected_id = candidate.semantic_key.substr(8);
    } else if (candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
               (candidate.semantic_key == "sketch_axis:x" ||
                candidate.semantic_key == "sketch_axis:y")) {
        selected_id = candidate.semantic_key;
        selected_axis = true;
    }
    if (selected_id.empty()) return;
    if (pending_line_dimension_reference_id_.empty()) {
        pending_line_dimension_reference_id_ = selected_id;
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(pending_line_dimension_kind_ ==
                zima::sketcher::DimensionKind::AngleBetween
            ? selected_axis
                ? tr("Úhel: vyberte řízenou úsečku.")
                : tr("Úhel: vyberte řízenou úsečku nebo osu skici.")
            : tr("Vzdálenost rovnoběžek: vyberte řízenou úsečku."));
        return;
    }
    const auto kind = pending_line_dimension_kind_;
    const auto selected_reference = pending_line_dimension_reference_id_;
    const auto first = selected_axis ? selected_id : selected_reference;
    const auto second = selected_axis ? selected_reference : selected_id;
    sketch_line_pair_dimension_active_ = false;
    pending_line_dimension_reference_id_.clear();
    viewer_->set_candidate_filter({});
    try {
        show_sketch_dimension_properties(
            active_sketch_id_, {}, kind, {}, {}, first, second);
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
        preserve_view_on_refresh_ = true;
        refresh_scene();
    }
}

void AssemblyWorkspaceWindow::show_sketch_dimension_properties(
    const std::string& sketch_id, const std::string& dimension_id,
    zima::sketcher::DimensionKind creation_kind,
    const std::string& first_point_id,
    const std::string& second_point_id,
    const std::string& first_geometry_id,
    const std::string& second_geometry_id,
    std::optional<std::array<double, 2>> placement) {
    if (properties_dialog_ != nullptr || sketch_segment_active_ ||
        sketch_rectangle_active_ || sketch_polygon_active_ ||
        sketch_offset_dialog_ || sketch_mirror_active_ || sketch_circle_active_ || sketch_arc_active_ ||
        sketch_ellipse_active_ || sketch_elliptical_arc_active_ ||
        sketch_bspline_active_ ||
        sketch_coincident_active_ || sketch_midpoint_active_ ||
        sketch_symmetric_active_ || sketch_concentric_active_ ||
        sketch_tangent_active_ || sketch_common_tangent_active_ ||
        sketch_segment_pair_active_ || sketch_point_dimension_active_ ||
        sketch_universal_dimension_active_ ||
        sketch_line_pair_dimension_active_) return;
    const bool active_target = sketch_id == active_sketch_id_;
    const zima::sketcher::Sketch* sketch = active_target ? active_sketch() : nullptr;
    if (sketch == nullptr) {
        const std::vector<zima::sketcher::Sketch>* sketches{};
        if (const auto* part = workspace_.open_part(
                workspace_.active_document_id())) {
            sketches = &part->session.document().sketches;
        } else if (const auto* assembly = workspace_.open_assembly(
                       workspace_.active_document_id())) {
            sketches = &assembly->session.document().sketches;
        }
        if (sketches != nullptr) {
            const auto found = std::find_if(sketches->begin(), sketches->end(),
                [&](const auto& value) { return value.id == sketch_id; });
            if (found != sketches->end()) sketch = &*found;
        }
    }
    if (sketch == nullptr) return;
    const auto existing = std::find_if(sketch->dimensions.begin(), sketch->dimensions.end(),
        [&](const auto& value) { return value.id == dimension_id; });
    const bool edit_mode = existing != sketch->dimensions.end();
    if (!dimension_id.empty() && !edit_mode) return;
    // Creation remains a Sketcher operation. Existing dimensions can be
    // inspected and edited directly from ordinary View.
    if (!active_target && !edit_mode) return;
    if (!edit_mode && selected_sketch_segment_id_.empty() &&
        selected_sketch_circle_id_.empty() && selected_sketch_arc_id_.empty() &&
        selected_sketch_ellipse_id_.empty() &&
        (first_point_id.empty() || second_point_id.empty()) &&
        (first_geometry_id.empty() || second_geometry_id.empty()) &&
        !(creation_kind == zima::sketcher::DimensionKind::AngleBetween &&
          !first_point_id.empty() && !second_point_id.empty() &&
          !first_geometry_id.empty()) &&
        !((creation_kind == zima::sketcher::DimensionKind::DistanceX ||
           creation_kind == zima::sketcher::DimensionKind::DistanceY) &&
          !first_point_id.empty() && !first_geometry_id.empty()) &&
        !((creation_kind == zima::sketcher::DimensionKind::DistancePointLine ||
           creation_kind == zima::sketcher::DimensionKind::DistanceSymmetric) &&
          !first_point_id.empty() && !first_geometry_id.empty()) &&
        !(creation_kind == zima::sketcher::DimensionKind::AngleThreePoint &&
          !first_point_id.empty() && !second_point_id.empty() &&
          !first_geometry_id.empty())) return;
    zima::sketcher::SketchDimension initial = edit_mode
        ? *existing
        : creation_kind == zima::sketcher::DimensionKind::DistancePointLine
            ? sketch->create_point_line_dimension(
                first_point_id, first_geometry_id)
        : creation_kind == zima::sketcher::DimensionKind::DistanceSymmetric
            ? sketch->create_symmetric_dimension(
                first_point_id, {}, first_geometry_id)
        : creation_kind == zima::sketcher::DimensionKind::AngleThreePoint
            ? sketch->create_three_point_angle_dimension(
                first_point_id, second_point_id, first_geometry_id)
        : creation_kind == zima::sketcher::DimensionKind::AngleBetween &&
              !first_point_id.empty() && !second_point_id.empty() &&
              !first_geometry_id.empty() && second_geometry_id.empty()
            ? sketch->create_point_line_angle_dimension(
                first_point_id, second_point_id, first_geometry_id)
        : (creation_kind == zima::sketcher::DimensionKind::DistanceX ||
           creation_kind == zima::sketcher::DimensionKind::DistanceY) &&
              !first_point_id.empty() && !first_geometry_id.empty()
            ? sketch->create_axis_dimension(first_point_id, first_geometry_id)
        : !first_geometry_id.empty() && !second_geometry_id.empty()
            ? sketch->create_line_pair_dimension(
                first_geometry_id, second_geometry_id, creation_kind)
        : !first_point_id.empty() && !second_point_id.empty()
            ? sketch->create_point_dimension(
                first_point_id, second_point_id, creation_kind)
        : creation_kind == zima::sketcher::DimensionKind::Diameter &&
              !selected_sketch_circle_id_.empty()
            ? sketch->create_circle_diameter_dimension(selected_sketch_circle_id_)
        : !selected_sketch_circle_id_.empty()
            ? sketch->create_circle_radius_dimension(selected_sketch_circle_id_)
            : !selected_sketch_arc_id_.empty()
                ? creation_kind == zima::sketcher::DimensionKind::Diameter
                    ? sketch->create_arc_diameter_dimension(selected_sketch_arc_id_)
                    : sketch->create_arc_radius_dimension(selected_sketch_arc_id_)
            : !selected_sketch_ellipse_id_.empty()
                ? creation_kind == zima::sketcher::DimensionKind::EllipseRotation
                    ? sketch->create_ellipse_rotation_dimension(
                        selected_sketch_ellipse_id_)
                    : sketch->create_ellipse_radius_dimension(
                        selected_sketch_ellipse_id_,
                        creation_kind ==
                            zima::sketcher::DimensionKind::EllipseMajorRadius)
                : sketch->create_segment_dimension(
                    selected_sketch_segment_id_, creation_kind);
    if (!edit_mode && placement) initial.placement = *placement;
    const bool segment_dimension_creation = !edit_mode &&
        !selected_sketch_segment_id_.empty() && first_point_id.empty() &&
        first_geometry_id.empty();
    auto pending_layout=std::make_shared<std::optional<zima::kernel::DimensionLayout>>();
    const zima::kernel::EdgeReference layout_reference{sketch_id,"dimension:"+initial.id,active_occurrence_path_};
    const auto commit_dimension =
        [this, sketch_id, edit_mode, creation_kind, active_target, pending_layout, layout_reference,
         segment_dimension_creation, first_geometry_id](
            zima::sketcher::SketchDimension committed) {
            const auto apply = [&](auto& target) {
                const auto found = std::find_if(target.sketches.begin(),
                    target.sketches.end(), [&](const auto& value) {
                        return value.id == sketch_id;
                    });
                if (found == target.sketches.end()) return false;
                found->apply_dimension(committed);
                if(*pending_layout)zima::kernel::store_dimension_layout(target.dimension_layouts,layout_reference,**pending_layout);
                found->validate();
                return true;
            };
            bool applied{};
            if (active_target) {
                applied = active_sketch_id_ == sketch_id &&
                    mutate_active_sketch([&](auto& target) {
                        target.apply_dimension(committed);
                        if(*pending_layout)zima::kernel::store_dimension_layout(target.dimension_layouts,layout_reference,**pending_layout);
                    });
            } else if (auto* part = workspace_.open_part(
                           workspace_.active_document_id())) {
                auto next = part->session.document();
                applied = apply(next);
                if (applied) part->session.commit(
                    std::move(next), part->session.calculated_boundaries());
            } else if (auto* assembly = workspace_.open_assembly(
                           workspace_.active_document_id())) {
                auto next = assembly->session.document();
                applied = apply(next);
                if (applied) assembly->session.commit(std::move(next));
            }
            if (!applied) {
                throw std::runtime_error("Sketch no longer exists");
            }
            selected_sketch_segment_id_.clear();
            selected_sketch_circle_id_.clear();
            selected_sketch_arc_id_.clear();
            if (!edit_mode && active_target) {
                const bool point_tool =
                    !segment_dimension_creation &&
                    (creation_kind == zima::sketcher::DimensionKind::Distance ||
                    creation_kind == zima::sketcher::DimensionKind::DistanceX ||
                    creation_kind == zima::sketcher::DimensionKind::DistanceY ||
                    creation_kind ==
                        zima::sketcher::DimensionKind::DistancePointLine ||
                    creation_kind ==
                        zima::sketcher::DimensionKind::DistanceSymmetric ||
                    creation_kind ==
                        zima::sketcher::DimensionKind::AngleThreePoint);
                const bool line_tool =
                    creation_kind == zima::sketcher::DimensionKind::DistanceLine ||
                    creation_kind == zima::sketcher::DimensionKind::AngleBetween;
                if (point_tool) {
                    sketch_point_dimension_active_ = true;
                    // Axis dimensions selected through the unified Kóta tool
                    // are stored as DistanceX/DistanceY, but repeating that
                    // derived specialized kind traps the next interaction on
                    // the same axis. The user then cannot select the other
                    // axis for the vertical dimension and can accidentally
                    // request a redundant second horizontal dimension.
                    // Resume the unified picker after every point-to-axis
                    // dimension; explicit X/Y actions still select their
                    // specialization when invoked again from the menu.
                    pending_point_dimension_kind_ =
                        !first_geometry_id.empty() &&
                        (first_geometry_id == "sketch_axis:x" ||
                         first_geometry_id == "sketch_axis:y")
                            ? zima::sketcher::DimensionKind::Distance
                            : creation_kind;
                    pending_point_dimension_first_id_.clear();
                    pending_point_dimension_vertex_id_.clear();
                } else if (line_tool) {
                    sketch_line_pair_dimension_active_ = true;
                    pending_line_dimension_kind_ = creation_kind;
                    pending_line_dimension_reference_id_.clear();
                }
            }
            preserve_view_on_refresh_ = true;
        };
    // Match the mature Sketcher interaction from the Python implementation:
    // placing a new dimension is completed directly in the View at its
    // measured value.  Opening the full Properties subwindow here breaks the
    // continuous dimension tool and obscures the geometry.  A deliberate
    // edit of an existing dimension still opens Properties (limits/driving
    // state), while a value-only change remains available by double-clicking
    // the dimension text in the View.
    if (!edit_mode && !placement) {
        // Entity selection and dimension placement are two separate LMB
        // steps.  Keep the measured dimension transient until the following
        // click in empty View space supplies the requested main-line
        // placement, matching the original Sketcher interaction.
        pending_sketch_dimension_ = std::move(initial);
        pending_point_dimension_cursor_.reset();
        sketch_point_dimension_active_ = false;
        sketch_line_pair_dimension_active_ = false;
        pending_point_dimension_first_id_.clear();
        pending_point_dimension_second_id_.clear();
        pending_point_dimension_vertex_id_.clear();
        pending_line_dimension_reference_id_.clear();
        viewer_->set_selection_contract({});
        viewer_->set_candidate_filter({});
        state_->setText(tr(
            "Kóta: kliknutím LMB do prostoru určete polohu kótovací čáry."));
        return;
    }
    if (!edit_mode) {
        try {
            commit_dimension(initial);
        } catch (const std::exception& error) {
            pending_sketch_dimension_ = std::move(initial);
            state_->setText(tr("Kótu nelze vytvořit: %1")
                .arg(QString::fromUtf8(error.what())));
            preserve_view_on_refresh_ = true;
            refresh_scene();
            return;
        }
        refresh_tabs();
        refresh_scene();
        return;
    }
    auto* dialog = new SketchDimensionPropertiesDialog(
        std::move(initial), true, commit_dimension, this);
    {
        const auto mesh=sketch->viewer_mesh();
        const auto source=std::find_if(mesh.dimensions.begin(),mesh.dimensions.end(),[&](const auto& d){return d.reference.semantic_key=="dimension:"+dimension_id;});
        if(source!=mesh.dimensions.end()){
            zima::kernel::DimensionLayout layout;
            if(const auto* stored=zima::kernel::find_dimension_layout(sketch->dimension_layouts,{sketch_id,"dimension:"+dimension_id,{}}))layout=*stored;
            if(!active_target){
                const std::vector<zima::kernel::DimensionLayoutEntry>* entries=nullptr;
                if(const auto* part=workspace_.open_part(workspace_.active_document_id()))entries=&part->session.document().dimension_layouts;
                else if(const auto* assembly=workspace_.open_assembly(workspace_.active_document_id()))entries=&assembly->session.document().dimension_layouts;
                if(entries)if(const auto* stored=zima::kernel::find_dimension_layout(*entries,layout_reference))layout=*stored;
            }
            layout.text_style.reset();
            dialog->set_presentation(*source,layout,[pending_layout](auto value){*pending_layout=std::move(value);});
        }
    }
    dialog->set_dimension_identifier(dimension_identifier(sketch_id, "dimension:" + dimension_id));
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        refresh_tabs();
        refresh_scene();
    });
    dialog->show();
}

} // namespace zima::app
