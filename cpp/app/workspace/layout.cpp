#include "workspace_internal.hpp"
#include <zima/workspace/body_operations.hpp>

namespace zima::app {
using namespace workspace_detail;

namespace {


class StatusOperationProgressBar final : public QProgressBar {
public:
    using QProgressBar::QProgressBar;

protected:
    void paintEvent(QPaintEvent*) override {
        QStyleOptionProgressBar option;
        initStyleOption(&option);
        // Native styles commonly suppress text for an indeterminate 0..0
        // progress bar. The status contract always shows the current phase.
        option.text = format();
        option.textVisible = true;
        QPainter painter(this);
        style()->drawControl(
            QStyle::CE_ProgressBar, &option, &painter, this);
    }
};

} // namespace



void AssemblyWorkspaceWindow::create_layout() {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    tabs_ = new QTabBar(central);
    tabs_->setObjectName("documentTabs");
    tabs_->setTabsClosable(false); // Explicit styled buttons below, independent of native tab-close painting.
    tabs_->setMovable(false);
    tabs_->setExpanding(false);
    tabs_->setUsesScrollButtons(true);
    tabs_->setIconSize(QSize(18, 18));
    tabs_->setStyleSheet(
        "QTabBar::tab { padding:7px 12px; margin-right:2px;"
        " border:1px solid rgba(255,255,255,35); border-bottom:none;"
        " border-top-left-radius:5px; border-top-right-radius:5px; }"
        "QTabBar::tab:selected { background:rgba(77,216,17,145); color:#fff;"
        " font-weight:700; border-color:#4DD811; }"
        "QTabBar::tab:!selected { background:rgba(255,255,255,18); }"
        "QTabBar::tab:hover:!selected { background:rgba(77,216,17,55); }");
    document_splitter_ = new QSplitter(Qt::Horizontal, central);
    document_splitter_->setObjectName("documentSplitter");
    auto* history_tree = new zima::app::HistoryTreeWidget(document_splitter_);
    tree_ = history_tree;
    history_tree->reorder_enabled = [this](QTreeWidgetItem* item) { return tree_item_reorder_enabled(item); };
    history_tree->reorder_requested = [this](QTreeWidgetItem* item,const QString& before,bool commit) {
        return reorder_tree_item(item,before,commit);
    };
    history_tree->body_cursor_moved = [this](const QString& owner, std::size_t cursor) {
        auto* part = workspace_.open_part(workspace_.active_document_id());
        if (!part || !part_history_insertion_allowed()) return;
        if(!owner.isEmpty()) {
            const auto* body=part->session.document().body_history.find(owner.toStdString());
            if(!body || body->scope.id!=part->session.document().body_history.active_body_id())return;
        }
        if(!workspace::set_body_history_cursor(workspace_,workspace_.active_document_id(),cursor,owner.toStdString()))return;
        preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
    };
    history_tree->history_cursor_moved = [this](std::size_t cursor) {
        auto* part = workspace_.open_part(workspace_.active_document_id());
        if (part == nullptr || !part_history_insertion_allowed()) return;
        auto next = part->session.document();
        if (next.effective_history_cursor() ==
            std::min(cursor, next.history_order.size())) return;
        next.set_history_cursor(cursor);
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
    };
    tree_->setObjectName("documentTree");
    auto tree_font = tree_->font();
    tree_font.setPixelSize(11);
    tree_->setFont(tree_font);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setColumnCount(1);
    tree_->setItemDelegate(new ReferenceTreeDelegate(tree_));
    tree_->setHeaderLabels({tr("DÍL")});
    tree_->setMinimumWidth(280);
    tree_->header()->setMinimumHeight(38);
    // Shared Part / Assembly / Drawing navigation lives in the Tree header,
    // exactly where the Python workspace exposes it.  It is deliberately not
    // a separate application command: its target depends on the displayed
    // document and remains available in every application mode.
    document_kind_button_ = new QToolButton(tree_->header());
    document_kind_button_->setObjectName("documentKindButton");
    document_kind_button_->setAutoRaise(true);
    connect(document_kind_button_, &QToolButton::clicked, this,
        [this] { navigate_document_kind(); });
    tree_->setStyleSheet(
        "QTreeWidget::item:selected, QTreeWidget::item:selected:active,"
        " QTreeWidget::item:selected:!active { background-color:#356E22;"
        " color:#fff; } QTreeWidget::item:hover { background-color:transparent; }");
    viewer_ = new zima::viewer::MeshView;
    viewer_->setObjectName("modelViewer");
    viewer_->set_origin_visibility_filter([this](const auto& reference) {
        if (!properties_dialog_ || !properties_dialog_->property("originSelectionBound").toBool() ||
            !workspace_.open_assembly(workspace_.displayed_document_id())) return true;
        return reference.instance_path.empty() ||
            visible_occurrence_origin_paths_.contains(reference.instance_path) ||
            (reference.instance_path == active_occurrence_path_ &&
             visible_local_origin_ids_.contains(reference.owner_id));
    });
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Dimension,
                                     zima::viewer::CandidateKind::Occurrence});
    viewer_->set_confirmation_callback([this](const auto& candidate) {
        if(accept_measurement(candidate))return;
        if(section_confirmation(candidate))return;
        if (!properties_dialog_ && candidate.kind==zima::viewer::CandidateKind::Vertex &&
            candidate.semantic_key=="origin:point" && candidate.instance_path==selected_component_origin_path_) return;
        // The purple origin is a drag control, not a replacement reference.
        if (component_placement_dialog_ && candidate.kind == zima::viewer::CandidateKind::Vertex &&
            candidate.semantic_key == "origin:point" && candidate.instance_path == properties_dialog_instance_path_ &&
            candidate.owner_id == component_placement_dialog_->pending_value().source_document_id + ":origin") return;
        if (local_origin_selection_active_) {
            toggle_local_origin_visibility(candidate);
            return;
        }
        if (auto* color_dialog = dynamic_cast<AppearanceDialog*>(properties_dialog_);
            color_dialog != nullptr) {
            color_dialog->select_face(candidate);
            return;
        }

        if (curve_axis_dialog_ != nullptr && pending_curve_axis_index_) {
            accept_curve_axis_reference(candidate);
            return;
        }
        if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
            candidate.semantic_key.starts_with("parameter:")) {
            construction_dimension_object_id_ = candidate.owner_id;
            state_->setText(tr(
                "Kóta parametru byla vybrána. Dvojklik otevře editaci hodnoty."));
            return;
        }
        if (construction_reference_dialog_ != nullptr &&
            pending_construction_reference_index_) {
            accept_construction_reference(candidate);
            return;
        }
        if (component_placement_dialog_ != nullptr &&
            pending_component_placement_index_) {
            accept_component_placement_reference(candidate);
            return;
        }
        if (feature_reference_pick_) {auto pick=feature_reference_pick_;pick(candidate);return;}
        if (shaft_thread_dialog_ != nullptr && shaft_thread_dialog_->active_reference()>=0) {
            accept_shaft_thread_reference(candidate);return;
        }
        if (extrusion_target_dialog_ != nullptr) {
            accept_extrusion_target(candidate);
            return;
        }
        if (primitive_reference_dialog_ != nullptr &&
            pending_primitive_reference_index_) {
            accept_primitive_reference(candidate);
            return;
        }
        if (shell_face_selection_active_) {
            accept_shell_face(candidate);
            return;
        }
        if (drill_point_face_selection_active_) {
            accept_drill_point_face(candidate);
            return;
        }
        if (edge_treatment_selection_) {
            accept_edge_treatment(candidate);
            return;
        }
        if (normal_view_selection_active_) {
            accept_normal_view_reference(candidate);
            return;
        }
        if (orientation_dialog_ != nullptr) {
            accept_orientation_reference(candidate);
            return;
        }
        if (sketch_external_reference_active_) {
            accept_sketch_external_reference(candidate);
            return;
        }
        if (sketch_rectangle_axis_selecting_) {
            accept_sketch_rectangle_axis(candidate);
            return;
        }
        if (sketch_corner_fillet_active_) {
            accept_sketch_corner_fillet_segment(candidate);
            return;
        }
        if (sketch_coincident_active_) {
            accept_sketch_coincident_point(candidate);
            return;
        }
        if (sketch_midpoint_active_) {
            accept_sketch_midpoint_selection(candidate);
            return;
        }
        if (sketch_symmetric_active_) {
            accept_sketch_symmetric_selection(candidate);
            return;
        }
        if (sketch_concentric_active_) {
            accept_sketch_concentric_selection(candidate);
            return;
        }
        if (sketch_tangent_active_) {
            accept_sketch_tangent_selection(candidate);
            return;
        }
        if (sketch_common_tangent_active_) {
            accept_sketch_common_tangent_selection(candidate);
            return;
        }
        if (sketch_segment_pair_active_) {
            accept_sketch_segment_pair(candidate);
            return;
        }
        if (sketch_universal_dimension_active_) {
            accept_sketch_universal_dimension(candidate);
            return;
        }
        if (pending_sketch_dimension_ &&
            ((pending_sketch_dimension_->kind ==
                    zima::sketcher::DimensionKind::Distance &&
              !pending_sketch_dimension_->geometry_id.empty() &&
              pending_sketch_dimension_->second_geometry_id.empty()) ||
             pending_sketch_dimension_->kind ==
                    zima::sketcher::DimensionKind::AngleThreePoint)) {
            // A first segment is still only a provisional length dimension.
            // A second line candidate must reach the unified dimension state
            // machine so it can replace that preview with an angular one.
            accept_sketch_point_dimension(candidate);
            return;
        }
        if (sketch_point_dimension_active_) {
            accept_sketch_point_dimension(candidate);
            return;
        }
        if (sketch_line_pair_dimension_active_) {
            accept_sketch_line_pair_dimension(candidate);
            return;
        }
        if (sketch_offset_dialog_) {
            if(candidate.owner_id==active_sketch_id_ && sketch_offset_dialog_->entering_reference()) {
                for(const std::string prefix:{"segment:","circle:","arc:","ellipse:","elliptical_arc:","bspline:"})
                    if(candidate.semantic_key.starts_with(prefix)) {sketch_offset_dialog_->set_source(candidate.semantic_key.substr(prefix.size()));break;}
            }
            return;
        }
        if (sketch_mirror_active_) {
            if (sketch_mirror_selecting_sources_) {
                accept_sketch_mirror_source(candidate);
            } else {
                accept_sketch_mirror_axis(candidate);
            }
            return;
        }
        // Ordinary confirmation owns exactly one current candidate. Clear
        // every geometry-specific latch before interpreting the new one;
        // otherwise selecting a Dimension/Constraint after a Segment left
        // the old Segment active even though the Tree showed the relation.
        // That stale latch also kept commands such as Horizontal and Length
        // enabled and could apply them to geometry that was no longer the
        // confirmed viewer candidate.
        if(candidate.kind==zima::viewer::CandidateKind::TemplateImage && candidate.owner_id==active_sketch_id_) {
            select_template_image(candidate.semantic_key.substr(15));return;
        }
        if(candidate.kind==zima::viewer::CandidateKind::TemplateRegion && candidate.owner_id==active_sketch_id_) {
            select_template_region(candidate.semantic_key.substr(14));return;
        }
        const bool additive_sketch_selection =
            QApplication::keyboardModifiers().testFlag(Qt::ControlModifier) &&
            candidate.owner_id == active_sketch_id_ &&
            (candidate.kind == zima::viewer::CandidateKind::SketchSegment ||
             candidate.kind == zima::viewer::CandidateKind::SketchCurve ||
             candidate.kind == zima::viewer::CandidateKind::SketchText ||
             candidate.kind == zima::viewer::CandidateKind::SketchPoint ||
             candidate.kind ==
                 zima::viewer::CandidateKind::SketchExternalReference);
        const auto previous_sketch_selection = selected_sketch_geometry_ids_;
        clear_selected_sketch_geometry();
        if (additive_sketch_selection) {
            selected_sketch_geometry_ids_ = previous_sketch_selection;
        }
        if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
            candidate.owner_id == active_sketch_id_ &&
            candidate.semantic_key.starts_with("dimension:")) {
            const auto dimension_id = QString::fromStdString(
                candidate.semantic_key.substr(10));
            tree_->clearSelection();
            QTreeWidgetItemIterator iterator(tree_);
            while (*iterator != nullptr) {
                auto* item = *iterator;
                if (item->data(0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("part-sketch-dimension") &&
                    item->data(0, Qt::UserRole).toString() == dimension_id) {
                    item->setSelected(true);
                    tree_->setCurrentItem(item);
                    tree_->scrollToItem(item);
                    break;
                }
                ++iterator;
            }
            // Selecting the Tree item emits itemSelectionChanged and normally
            // restores this candidate through synchronize_tree_selection().
            // Keep the View selection deterministic even when the item was
            // already current and Qt therefore emits no selection signal.
            viewer_->confirm_reference(active_sketch_id_,
                "dimension:" + dimension_id.toStdString(), {},
                zima::viewer::CandidateKind::Dimension);
            state_->setText(tr("Vybrána kóta skici."));
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchConstraint &&
            candidate.owner_id == active_sketch_id_ &&
            candidate.semantic_key.starts_with("constraint:")) {
            const auto constraint_id = QString::fromStdString(
                candidate.semantic_key.substr(11));
            tree_->clearSelection();
            QTreeWidgetItemIterator iterator(tree_);
            while (*iterator != nullptr) {
                auto* item = *iterator;
                if (item->data(0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("part-sketch-constraint") &&
                    item->data(0, Qt::UserRole).toString() == constraint_id) {
                    item->setSelected(true);
                    tree_->setCurrentItem(item);
                    tree_->scrollToItem(item);
                    break;
                }
                ++iterator;
            }
            viewer_->confirm_reference(active_sketch_id_,
                "constraint:" + constraint_id.toStdString(), {},
                zima::viewer::CandidateKind::SketchConstraint);
            state_->setText(tr("Vybrána vazba skici."));
        } else if (candidate.kind ==
                       zima::viewer::CandidateKind::SketchConstraint &&
                   candidate.owner_id == active_sketch_id_ &&
                   candidate.semantic_key.starts_with("fixed:")) {
            selected_sketch_point_id_ = candidate.semantic_key.substr(6);
            tree_->clearSelection();
            QTreeWidgetItemIterator iterator(tree_);
            while (*iterator != nullptr) {
                auto* item = *iterator;
                if (item->data(0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("sketch-geometry") &&
                    item->data(0, Qt::UserRole).toString() ==
                        QString::fromStdString(selected_sketch_point_id_)) {
                    item->setSelected(true);
                    tree_->setCurrentItem(item);
                    tree_->scrollToItem(item);
                    break;
                }
                ++iterator;
            }
            sketch_fix_point_action_->setEnabled(true);
            state_->setText(tr("Vybrán fixovaný bod skici."));
        } else if (candidate.kind == zima::viewer::CandidateKind::Occurrence) {
            select_occurrence(candidate.instance_path);
        } else if (candidate.kind == zima::viewer::CandidateKind::Container) {
            select_container(candidate.owner_id);
            viewer_->set_feature_selected_edge_indices(
                edge_treatment_feature_edges(
                    candidate.owner_id, candidate.instance_path));
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
                   candidate.owner_id == active_sketch_id_ &&
                   candidate.semantic_key.starts_with("segment:")) {
            selected_sketch_segment_id_ = candidate.semantic_key.substr(8);
            selected_sketch_circle_id_.clear();
            selected_sketch_arc_id_.clear();
            selected_sketch_ellipse_id_.clear();
            selected_sketch_elliptical_arc_id_.clear();
            selected_sketch_bspline_id_.clear();
            selected_sketch_point_id_.clear();
            sketch_horizontal_action_->setEnabled(true);
            sketch_vertical_action_->setEnabled(true);
            sketch_dimension_action_->setEnabled(true);
            sketch_dimension_x_action_->setEnabled(true);
            sketch_dimension_y_action_->setEnabled(true);
            sketch_angle_dimension_action_->setEnabled(true);
            sketch_fix_point_action_->setEnabled(false);
            state_->setText(tr("Vybrána úsečka skici."));
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
                   candidate.owner_id == active_sketch_id_ &&
                   candidate.semantic_key.starts_with("circle:")) {
            selected_sketch_circle_id_ = candidate.semantic_key.substr(7);
            selected_sketch_arc_id_.clear();
            selected_sketch_ellipse_id_.clear();
            selected_sketch_elliptical_arc_id_.clear();
            selected_sketch_bspline_id_.clear();
            selected_sketch_segment_id_.clear();
            selected_sketch_point_id_.clear();
            sketch_horizontal_action_->setEnabled(false);
            sketch_vertical_action_->setEnabled(false);
            sketch_dimension_action_->setEnabled(true);
            sketch_dimension_x_action_->setEnabled(false);
            sketch_dimension_y_action_->setEnabled(false);
            sketch_angle_dimension_action_->setEnabled(false);
            sketch_fix_point_action_->setEnabled(false);
            sketch_radius_dimension_action_->setEnabled(true);
            sketch_diameter_dimension_action_->setEnabled(true);
            state_->setText(tr("Vybrána kružnice skici."));
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
                   candidate.owner_id == active_sketch_id_ &&
                   candidate.semantic_key.starts_with("arc:")) {
            selected_sketch_segment_id_.clear();
            selected_sketch_circle_id_.clear();
            selected_sketch_arc_id_ = candidate.semantic_key.substr(4);
            selected_sketch_ellipse_id_.clear();
            selected_sketch_elliptical_arc_id_.clear();
            selected_sketch_bspline_id_.clear();
            selected_sketch_point_id_.clear();
            sketch_horizontal_action_->setEnabled(false);
            sketch_vertical_action_->setEnabled(false);
            sketch_dimension_action_->setEnabled(true);
            sketch_dimension_x_action_->setEnabled(false);
            sketch_dimension_y_action_->setEnabled(false);
            sketch_angle_dimension_action_->setEnabled(false);
            sketch_fix_point_action_->setEnabled(false);
            sketch_radius_dimension_action_->setEnabled(true);
            sketch_diameter_dimension_action_->setEnabled(true);
            state_->setText(tr("Vybrán oblouk skici."));
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
                   candidate.owner_id == active_sketch_id_ &&
                   candidate.semantic_key.starts_with("ellipse:")) {
            selected_sketch_segment_id_.clear();
            selected_sketch_circle_id_.clear();
            selected_sketch_arc_id_.clear();
            selected_sketch_ellipse_id_ = candidate.semantic_key.substr(8);
            selected_sketch_elliptical_arc_id_.clear();
            selected_sketch_bspline_id_.clear();
            selected_sketch_point_id_.clear();
            sketch_horizontal_action_->setEnabled(false);
            sketch_vertical_action_->setEnabled(false);
            sketch_dimension_action_->setEnabled(true);
            sketch_dimension_x_action_->setEnabled(false);
            sketch_dimension_y_action_->setEnabled(false);
            sketch_angle_dimension_action_->setEnabled(false);
            sketch_radius_dimension_action_->setEnabled(false);
            sketch_diameter_dimension_action_->setEnabled(false);
            sketch_ellipse_major_dimension_action_->setEnabled(true);
            sketch_ellipse_minor_dimension_action_->setEnabled(true);
            sketch_ellipse_rotation_dimension_action_->setEnabled(true);
            sketch_fix_point_action_->setEnabled(false);
            state_->setText(tr("Vybrána elipsa skici."));
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
                   candidate.owner_id == active_sketch_id_ &&
                   candidate.semantic_key.starts_with("elliptical_arc:")) {
            selected_sketch_segment_id_.clear();
            selected_sketch_circle_id_.clear();
            selected_sketch_arc_id_.clear();
            selected_sketch_ellipse_id_.clear();
            selected_sketch_elliptical_arc_id_ = candidate.semantic_key.substr(15);
            selected_sketch_bspline_id_.clear();
            selected_sketch_point_id_.clear();
            sketch_horizontal_action_->setEnabled(false);
            sketch_vertical_action_->setEnabled(false);
            sketch_dimension_action_->setEnabled(false);
            sketch_dimension_x_action_->setEnabled(false);
            sketch_dimension_y_action_->setEnabled(false);
            sketch_angle_dimension_action_->setEnabled(false);
            sketch_radius_dimension_action_->setEnabled(false);
            sketch_diameter_dimension_action_->setEnabled(false);
            sketch_ellipse_major_dimension_action_->setEnabled(false);
            sketch_ellipse_minor_dimension_action_->setEnabled(false);
            sketch_ellipse_rotation_dimension_action_->setEnabled(false);
            sketch_fix_point_action_->setEnabled(false);
            state_->setText(tr("Vybrán eliptický oblouk skici."));
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
                   candidate.owner_id == active_sketch_id_ &&
                   candidate.semantic_key.starts_with("bspline:")) {
            selected_sketch_segment_id_.clear();
            selected_sketch_circle_id_.clear();
            selected_sketch_arc_id_.clear();
            selected_sketch_ellipse_id_.clear();
            selected_sketch_elliptical_arc_id_.clear();
            selected_sketch_bspline_id_ = candidate.semantic_key.substr(8);
            selected_sketch_point_id_.clear();
            sketch_horizontal_action_->setEnabled(false);
            sketch_vertical_action_->setEnabled(false);
            sketch_dimension_action_->setEnabled(false);
            sketch_dimension_x_action_->setEnabled(false);
            sketch_dimension_y_action_->setEnabled(false);
            sketch_angle_dimension_action_->setEnabled(false);
            sketch_radius_dimension_action_->setEnabled(false);
            sketch_diameter_dimension_action_->setEnabled(false);
            sketch_fix_point_action_->setEnabled(false);
            state_->setText(tr("Vybrána B-spline skici."));
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchText &&
                   candidate.owner_id == active_sketch_id_) {
            const auto text_id = sketch_text_id_from_key(candidate.semantic_key);
            if (!text_id) return;
            selected_sketch_segment_id_.clear();
            selected_sketch_circle_id_.clear();
            selected_sketch_arc_id_.clear();
            selected_sketch_ellipse_id_.clear();
            selected_sketch_elliptical_arc_id_.clear();
            selected_sketch_bspline_id_.clear();
            selected_sketch_point_id_.clear();
            selected_sketch_text_id_ = *text_id;
            sketch_horizontal_action_->setEnabled(false);
            sketch_vertical_action_->setEnabled(false);
            sketch_dimension_action_->setEnabled(false);
            sketch_dimension_x_action_->setEnabled(false);
            sketch_dimension_y_action_->setEnabled(false);
            sketch_angle_dimension_action_->setEnabled(false);
            sketch_radius_dimension_action_->setEnabled(false);
            sketch_diameter_dimension_action_->setEnabled(false);
            sketch_fix_point_action_->setEnabled(false);
            state_->setText(tr("Vybrán text skici."));
        } else if (candidate.kind ==
                       zima::viewer::CandidateKind::SketchExternalReference &&
                   candidate.owner_id == active_sketch_id_) {
            const auto reference_id = sketch_external_reference_id_from_key(
                candidate.semantic_key);
            if (!reference_id) return;
            selected_sketch_segment_id_.clear();
            selected_sketch_circle_id_.clear();
            selected_sketch_arc_id_.clear();
            selected_sketch_ellipse_id_.clear();
            selected_sketch_elliptical_arc_id_.clear();
            selected_sketch_bspline_id_.clear();
            selected_sketch_text_id_.clear();
            selected_sketch_point_id_.clear();
            selected_sketch_external_reference_id_ = *reference_id;
            sketch_horizontal_action_->setEnabled(false);
            sketch_vertical_action_->setEnabled(false);
            sketch_dimension_action_->setEnabled(false);
            sketch_dimension_x_action_->setEnabled(false);
            sketch_dimension_y_action_->setEnabled(false);
            sketch_angle_dimension_action_->setEnabled(false);
            sketch_radius_dimension_action_->setEnabled(false);
            sketch_diameter_dimension_action_->setEnabled(false);
            sketch_fix_point_action_->setEnabled(false);
            state_->setText(tr("Vybrána externí reference skici."));
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
                   candidate.owner_id == active_sketch_id_ &&
                   candidate.semantic_key.starts_with("point:")) {
            selected_sketch_point_id_ = candidate.semantic_key.substr(6);
            selected_sketch_segment_id_.clear();
            selected_sketch_circle_id_.clear();
            selected_sketch_arc_id_.clear();
            selected_sketch_ellipse_id_.clear();
            selected_sketch_elliptical_arc_id_.clear();
            selected_sketch_bspline_id_.clear();
            sketch_horizontal_action_->setEnabled(false);
            sketch_vertical_action_->setEnabled(false);
            sketch_dimension_action_->setEnabled(true);
            sketch_dimension_x_action_->setEnabled(true);
            sketch_dimension_y_action_->setEnabled(true);
            sketch_angle_dimension_action_->setEnabled(false);
            sketch_radius_dimension_action_->setEnabled(false);
            sketch_diameter_dimension_action_->setEnabled(false);
            sketch_fix_point_action_->setEnabled(true);
            state_->setText(tr("Vybrán bod skici."));
        }
        if (candidate.owner_id == active_sketch_id_ &&
            (candidate.kind == zima::viewer::CandidateKind::SketchSegment ||
             candidate.kind == zima::viewer::CandidateKind::SketchCurve ||
             candidate.kind == zima::viewer::CandidateKind::SketchText ||
             candidate.kind == zima::viewer::CandidateKind::SketchPoint ||
             candidate.kind ==
                 zima::viewer::CandidateKind::SketchExternalReference)) {
            const std::string selected_id =
                !selected_sketch_segment_id_.empty() ? selected_sketch_segment_id_
                : !selected_sketch_circle_id_.empty() ? selected_sketch_circle_id_
                : !selected_sketch_arc_id_.empty() ? selected_sketch_arc_id_
                : !selected_sketch_ellipse_id_.empty() ? selected_sketch_ellipse_id_
                : !selected_sketch_elliptical_arc_id_.empty()
                    ? selected_sketch_elliptical_arc_id_
                : !selected_sketch_bspline_id_.empty() ? selected_sketch_bspline_id_
                : !selected_sketch_text_id_.empty() ? selected_sketch_text_id_
                : !selected_sketch_external_reference_id_.empty()
                    ? selected_sketch_external_reference_id_
                : selected_sketch_point_id_;
            if (!selected_id.empty()) {
                if (additive_sketch_selection) {
                    if (!selected_sketch_geometry_ids_.erase(selected_id)) {
                        selected_sketch_geometry_ids_.insert(selected_id);
                    }
                } else {
                    selected_sketch_geometry_ids_ = {selected_id};
                    tree_->clearSelection();
                }
                QTreeWidgetItemIterator iterator(tree_);
                while (*iterator != nullptr) {
                    auto* item = *iterator;
                    const auto role = item->data(0, Qt::UserRole + 3).toString();
                    if ((role == QStringLiteral("sketch-geometry") ||
                         role == QStringLiteral("sketch-external-reference")) &&
                        item->data(0, Qt::UserRole).toString() ==
                            QString::fromStdString(selected_id)) {
                        item->setSelected(
                            selected_sketch_geometry_ids_.contains(selected_id));
                        tree_->setCurrentItem(item);
                        tree_->scrollToItem(item);
                        break;
                    }
                    ++iterator;
                }
            }
        }
        const bool sketch_tools_available = !active_sketch_id_.empty();
        for (auto* action : {
                 sketch_horizontal_action_, sketch_vertical_action_,
                 sketch_universal_dimension_action_, sketch_dimension_action_,
                 sketch_dimension_x_action_,
                 sketch_dimension_y_action_, sketch_point_line_dimension_action_,
                 sketch_symmetric_dimension_action_,
                 sketch_three_point_angle_dimension_action_,
                 sketch_angle_dimension_action_}) {
            action->setEnabled(sketch_tools_available);
        }
        sketch_trim_action_->setEnabled(!active_sketch_id_.empty());
        sketch_mirror_action_->setEnabled(
            !active_sketch_id_.empty() && !sketch_mirror_active_);
        sketch_offset_action_->setEnabled(!active_sketch_id_.empty() && !properties_dialog_);
    });
    viewer_->set_empty_confirmation_callback([this] {
        if(measurement_dialog_){tree_->clearSelection();return;}
        assembly_dimension_path_.clear();update_assembly_dimension_visibility();
        clear_selected_sketch_geometry();
        viewer_->set_feature_selected_edges({});
        tree_->clearSelection();
        tree_->setCurrentItem(nullptr);
        if (properties_dialog_ == nullptr &&
            !construction_dimension_object_id_.empty()) {
            construction_dimension_object_id_.clear();
            preserve_view_on_refresh_ = true;
            refresh_scene();
        }
    });
    viewer_->set_dimension_lock_query([this](const auto& reference){return parameter_value_locked(reference.owner_id,reference.semantic_key);});
    viewer_->set_object_frame_provider([this](const zima::kernel::ViewerMesh& mesh){
        std::map<zima::kernel::ObjectEnvelopeKey,zima::kernel::ModelEnvelope> frames;
        const auto id=workspace_.active_document_id();
        if(const auto* part=workspace_.open_part(id)) {
            frames=active_occurrence_path_.empty()?zima::document::part_annotation_envelopes(part->session.document(),mesh):zima::document::part_annotation_frames(part->session.document());
        }
        if(!active_occurrence_path_.empty()) {
            const auto path=zima::assembly::InstancePath::decode(active_occurrence_path_);
            std::map<zima::kernel::ObjectEnvelopeKey,zima::kernel::ModelEnvelope> placed;
            for(const auto& [key,value]:frames){auto frame=value;const auto point=[&](auto p){return workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),path,p);};const auto origin=point(frame.origin);for(auto& axis:frame.axes)axis=zima::kernel::dimension_sub(point(zima::kernel::dimension_add(frame.origin,axis)),origin);frame.origin=origin;placed[{key.first,active_occurrence_path_}]=frame;}frames=std::move(placed);
        }
        if(!active_occurrence_path_.empty())if(const auto* part=workspace_.open_part(id)){
            frames=zima::kernel::object_envelopes(mesh,std::move(frames));
            const auto alias=[&](const std::string& source,const std::string& owner){const auto found=frames.find({source,active_occurrence_path_});if(found!=frames.end()&&found->second.valid)frames[{owner,active_occurrence_path_}]=found->second;};
            for(const auto& c:part->session.document().history)alias(c.feature_id,c.id);
            for(const auto& c:part->session.document().constructions)alias(c.entity_id,c.id);
        }
        return frames;
    });
    viewer_->set_dimension_layout_resolver([this](const auto& reference)->std::optional<zima::kernel::DimensionLayout>{
        if(reference.owner_id==active_sketch_id_ &&
           ((!universal_corner_radius_dimension_id_.empty() && reference.semantic_key=="corner_dimension:"+universal_corner_radius_dimension_id_) ||
            (!pending_corner_radius_dimension_id_.empty() && reference.semantic_key=="corner_dimension:"+pending_corner_radius_dimension_id_)))
            return universal_dimension_layout_;
        if(const auto* sketch=active_sketch();sketch && reference.owner_id==sketch->id)
            if(const auto* layout=zima::kernel::find_dimension_layout(sketch->dimension_layouts,reference))return *layout;
        for(const auto& state:workspace_.documents()) {
            const std::vector<zima::kernel::DimensionLayoutEntry>* entries=nullptr;
            if(const auto* part=std::get_if<zima::workspace::PartState>(&state))entries=&part->session.document().dimension_layouts;
            if(const auto* assembly=std::get_if<zima::workspace::AssemblyState>(&state))entries=&assembly->session.document().dimension_layouts;
            const std::vector<zima::sketcher::Sketch>* sketches=nullptr;
            if(const auto* part=std::get_if<zima::workspace::PartState>(&state))sketches=&part->session.document().sketches;
            if(const auto* assembly=std::get_if<zima::workspace::AssemblyState>(&state))sketches=&assembly->session.document().sketches;
            if(sketches)for(const auto& sketch:*sketches)if(const auto* layout=zima::kernel::find_dimension_layout(sketch.dimension_layouts,reference))return *layout;
            if(entries)if(const auto* layout=zima::kernel::find_dimension_layout(*entries,reference))return *layout;
        }
        if(active_sketch_id_.empty())return zima::kernel::DimensionLayout{0,8.0,0,0};
        return {};
    });
    viewer_->set_dimension_layout_commit([this](const auto& reference,auto layout){commit_dimension_layout(reference,layout);});
    viewer_->set_context_menu_callback(
        [this](const auto& candidate, const QPoint& global_position) {
            if(section_dialog_&&section_dialog_->isVisible())return;
            if (!part_element_context_menu_enabled(candidate.owner_id)) return;
            if(candidate.kind==zima::viewer::CandidateKind::TemplateImage && !properties_dialog_) {
                const auto id=candidate.semantic_key.substr(15);QMenu menu(this);
                auto* properties=menu.addAction(tr("Vlastnosti…"));auto* remove=menu.addAction(tr("Odstranit"));
                const auto* chosen=menu.exec(global_position);if(chosen==properties)show_template_image_properties(id);else if(chosen==remove)remove_template_image(id);return;
            }
            if(candidate.kind==zima::viewer::CandidateKind::TemplateRegion && !properties_dialog_) {
                const auto id=candidate.semantic_key.substr(14);QMenu menu(this);
                auto* properties=menu.addAction(tr("Vlastnosti…"));auto* remove=menu.addAction(tr("Odstranit"));
                const auto* chosen=menu.exec(global_position);if(chosen==properties)show_template_region_properties(id);else if(chosen==remove)remove_template_region(id);return;
            }
            if (edge_treatment_selection_ || shell_face_selection_active_ ||
                drill_point_face_selection_active_ ||
                extrusion_target_dialog_ != nullptr ||
                sketch_external_reference_active_ || sketch_trim_active_ ||
                sketch_offset_dialog_ || sketch_mirror_active_ || sketch_coincident_active_ ||
                sketch_midpoint_active_ || sketch_symmetric_active_ ||
                sketch_concentric_active_ || sketch_tangent_active_ ||
                sketch_common_tangent_active_ ||
                sketch_segment_pair_active_ || sketch_point_dimension_active_ ||
                sketch_universal_dimension_active_) return;
            if(candidate.kind==zima::viewer::CandidateKind::Dimension && !properties_dialog_ && active_sketch_id_.empty()) {
                QMenu menu(this);auto* presentation=menu.addAction(tr("Vlastnosti kóty…"));
                presentation->setObjectName("dimensionLayoutPropertiesAction");
                auto* value=menu.addAction(tr("Upravit hodnotu…"));
                auto* lock=menu.addAction(parameter_value_locked(candidate.owner_id,candidate.semantic_key).value_or(false)?tr("Odemknout hodnotu"):tr("Zamknout hodnotu"));
                const auto* chosen=menu.exec(global_position);
                if(chosen==presentation)show_dimension_layout_properties(candidate);
                else if(chosen==value)edit_dimension_inline(candidate);
                else if(chosen==lock)toggle_parameter_value_lock(candidate.owner_id,candidate.semantic_key);
                return;
            }
            if(candidate.kind==zima::viewer::CandidateKind::Dimension && candidate.semantic_key.starts_with("placement-reference:")) {
                QMenu menu(this);auto* lock=menu.addAction(parameter_value_locked(candidate.owner_id,candidate.semantic_key).value_or(false)?tr("Odemknout hodnotu"):tr("Zamknout hodnotu"));
                if(menu.exec(global_position)==lock)toggle_parameter_value_lock(candidate.owner_id,candidate.semantic_key);return;
            }
            if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
                candidate.semantic_key.starts_with("dimension:")) {
                const auto dimension_id = candidate.semantic_key.substr(10);
                const auto* sketch = active_sketch();
                const auto existing = sketch == nullptr
                    ? std::vector<zima::sketcher::SketchDimension>::const_iterator{}
                    : std::find_if(sketch->dimensions.begin(), sketch->dimensions.end(),
                        [&](const auto& value) { return value.id == dimension_id; });
                if (sketch == nullptr || existing == sketch->dimensions.end()) return;
                const bool locked = existing->locked;
                QMenu menu(this);
                auto* lock = menu.addAction(
                    locked ? tr("Odemknout rozměr") : tr("Zamknout rozměr"));
                lock->setEnabled(existing->driving);
                menu.addSeparator();
                auto* properties = menu.addAction(tr("Vlastnosti…"));
                menu.addSeparator();
                auto* remove = menu.addAction(tr("Odstranit"));
                const auto* chosen = menu.exec(global_position);
                if (chosen == lock) {
                    if (mutate_active_sketch([&](auto& target) {
                            const auto found = std::find_if(
                                target.dimensions.begin(), target.dimensions.end(),
                                [&](const auto& value) {
                                    return value.id == dimension_id;
                                });
                            if (found != target.dimensions.end()) {
                                found->locked = !locked;
                            }
                        })) {
                        preserve_view_on_refresh_ = true;
                        refresh_tabs();
                        refresh_scene();
                    }
                } else if (chosen == properties) {
                    show_sketch_dimension_properties(
                        candidate.owner_id, dimension_id);
                } else if (chosen == remove) {
                    remove_sketch_relation(
                        candidate.owner_id, dimension_id, true);
                }
                return;
            }
            if ((candidate.kind == zima::viewer::CandidateKind::SketchPoint ||
                 candidate.kind == zima::viewer::CandidateKind::SketchSegment ||
                 candidate.kind == zima::viewer::CandidateKind::SketchCurve) &&
                candidate.owner_id == active_sketch_id_) {
                const auto separator = candidate.semantic_key.find(':');
                if (separator == std::string::npos) return;
                const auto geometry_id = candidate.semantic_key.substr(separator + 1);
                const auto* sketch = active_sketch();
                if (sketch == nullptr) return;
                std::optional<bool> construction;
                const auto point = std::find_if(sketch->points.begin(), sketch->points.end(),
                    [&](const auto& value) { return value.id == geometry_id; });
                if (point != sketch->points.end()) construction = point->construction;
                const auto inspect = [&](const auto& values) {
                    const auto found = std::find_if(values.begin(), values.end(),
                        [&](const auto& value) { return value.id == geometry_id; });
                    if (found != values.end()) construction = found->construction;
                };
                inspect(sketch->segments); inspect(sketch->circles);
                inspect(sketch->arcs); inspect(sketch->ellipses);
                inspect(sketch->elliptical_arcs); inspect(sketch->bsplines);
                if (!construction) return;
                QMenu menu(this);
                auto* properties=candidate.semantic_key.starts_with("bspline:")?menu.addAction(tr("Vlastnosti…")):nullptr;
                auto* role = menu.addAction(*construction
                    ? tr("Převést na obrys profilu")
                    : tr("Převést na pomocnou geometrii"));
                auto* remove = menu.addAction(tr("Odstranit"));
                const auto* chosen = menu.exec(global_position);
                if(properties && chosen==properties) {
                    show_sketch_bspline_properties(active_sketch_id_,geometry_id);
                } else if (chosen == role) {
                    set_active_sketch_geometry_construction(
                        geometry_id, !*construction);
                } else if (chosen == remove) {
                    if (candidate.kind ==
                            zima::viewer::CandidateKind::SketchPoint) {
                        selected_sketch_point_id_ = geometry_id;
                    } else if (candidate.kind ==
                            zima::viewer::CandidateKind::SketchSegment) {
                        selected_sketch_segment_id_ = geometry_id;
                    } else if (candidate.semantic_key.starts_with("circle:")) {
                        selected_sketch_circle_id_ = geometry_id;
                    } else if (candidate.semantic_key.starts_with("arc:")) {
                        selected_sketch_arc_id_ = geometry_id;
                    } else if (candidate.semantic_key.starts_with("ellipse:")) {
                        selected_sketch_ellipse_id_ = geometry_id;
                    } else if (candidate.semantic_key.starts_with("elliptical_arc:")) {
                        selected_sketch_elliptical_arc_id_ = geometry_id;
                    } else if (candidate.semantic_key.starts_with("bspline:")) {
                        selected_sketch_bspline_id_ = geometry_id;
                    }
                    static_cast<void>(delete_selected_sketch_geometry());
                }
                return;
            }
            if (candidate.kind ==
                    zima::viewer::CandidateKind::SketchExternalReference &&
                candidate.owner_id == active_sketch_id_) {
                const auto reference_id = sketch_external_reference_id_from_key(
                    candidate.semantic_key);
                if (!reference_id) return;
                selected_sketch_external_reference_id_ = *reference_id;
                QMenu menu(this);
                auto* remove = menu.addAction(tr("Odstranit"));
                if (menu.exec(global_position) == remove) {
                    static_cast<void>(delete_selected_sketch_geometry());
                }
                return;
            }
            if (candidate.kind == zima::viewer::CandidateKind::SketchText &&
                candidate.owner_id == active_sketch_id_) {
                const auto text_id = sketch_text_id_from_key(candidate.semantic_key);
                if (!text_id) return;
                selected_sketch_text_id_ = *text_id;
                QMenu menu(this);
                auto* properties = menu.addAction(tr("Vlastnosti"));
                auto* remove = menu.addAction(tr("Odstranit"));
                const auto* chosen = menu.exec(global_position);
                if (chosen == properties) {
                    show_sketch_text_properties(active_sketch_id_, *text_id);
                } else if (chosen == remove) {
                    static_cast<void>(delete_selected_sketch_geometry());
                }
                return;
            }
            if (candidate.kind ==
                    zima::viewer::CandidateKind::SketchConstraint &&
                candidate.owner_id == active_sketch_id_) {
                if (candidate.semantic_key.starts_with("constraint:")) {
                    QMenu menu(this);
                    auto* remove = menu.addAction(tr("Odstranit vazbu"));
                    if (menu.exec(global_position) == remove) {
                        remove_sketch_relation(active_sketch_id_,
                            candidate.semantic_key.substr(11), false);
                    }
                    return;
                }
                if (candidate.semantic_key.starts_with("fixed:")) {
                    const auto point_id = candidate.semantic_key.substr(6);
                    QMenu menu(this);
                    auto* release = menu.addAction(tr("Uvolnit bod"));
                    if (menu.exec(global_position) == release) {
                        try {
                            if (mutate_active_sketch([&](auto& sketch) {
                                    sketch.set_point_fixed(point_id, false);
                                })) {
                                preserve_view_on_refresh_ = true;
                                refresh_tabs();
                                refresh_scene();
                            }
                        } catch (const std::exception& error) {
                            state_->setText(QString::fromUtf8(error.what()));
                        }
                    }
                    return;
                }
            }
            const auto find_container_item = [this](const std::string& owner_id) {
                QTreeWidgetItemIterator iterator(tree_);
                while (*iterator != nullptr) {
                    auto* item = *iterator;
                    const auto role = item->data(0, Qt::UserRole + 3).toString();
                    if (item->data(0, Qt::UserRole).toString() ==
                            QString::fromStdString(owner_id) &&
                        (role == QStringLiteral("part-container") ||
                         role == QStringLiteral("part-construction") ||
                         role == QStringLiteral("assembly-construction") ||
                         role == QStringLiteral("part-sketch") ||
                         role == QStringLiteral("assembly-sketch") ||
                         role == QStringLiteral("assembly-cut"))) {
                        return item;
                    }
                    ++iterator;
                }
                return static_cast<QTreeWidgetItem*>(nullptr);
            };
            if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
                candidate.semantic_key.starts_with("parameter:")) {
                QMenu menu(this);
                const auto locked=parameter_value_locked(candidate.owner_id,candidate.semantic_key);
                QAction* lock=locked?menu.addAction(*locked?tr("Odemknout hodnotu"):tr("Zamknout hodnotu")):nullptr;
                auto* edit = menu.addAction(tr("Upravit hodnotu"));edit->setEnabled(!locked.value_or(false));
                auto* properties = menu.addAction(tr("Vlastnosti"));
                const auto* selected = menu.exec(global_position);
                if(lock && selected==lock){toggle_parameter_value_lock(candidate.owner_id,candidate.semantic_key);return;}
                if (selected != edit && selected != properties) return;
                if (selected == edit) {
                    edit_dimension_inline(candidate);
                    return;
                }
                if (auto* item = find_container_item(candidate.owner_id)) {
                    construction_dimension_object_id_ = candidate.owner_id;
                    show_tree_item_properties(item);
                }
                return;
            }
            if (candidate.kind == zima::viewer::CandidateKind::Container) {
                const auto* active=workspace_.open_part(workspace_.active_document_id());
                const auto* body=active?active->session.document().body_history.find(candidate.owner_id):nullptr;
                if(body&&body->derived_copy) {
                    QMenu menu(this);auto* source=menu.addAction(tr("Vlastnosti zdroje"));
                    auto* settings=menu.addAction(body->derived_copy->pattern?tr("Vlastnosti Pole"):tr("Vlastnosti Zrcadla"));
                    const auto* selected=menu.exec(global_position);
                    if(selected==source)show_derived_source_properties(candidate.owner_id);
                    else if(selected==settings)show_derived_copy_properties(candidate.owner_id);
                    return;
                }
                auto* item = find_container_item(candidate.owner_id);
                if (item == nullptr) return;
                QMenu menu(this);
                auto* edit = menu.addAction(tr("Upravit"));
                auto* properties = menu.addAction(tr("Vlastnosti"));
                QAction* transform_extrusion{};
                QAction* transform_revolution{};
                QAction* suppress_curve{};
                QAction* remove_curve{};
                if (const auto* part = workspace_.open_part(
                        workspace_.active_document_id())) {
                    const auto* container =
                        part->session.document().find_container(candidate.owner_id);
                    if (container != nullptr && container->feature_kind ==
                            zima::document::FeatureKind::Sketch) {
                        menu.addSeparator();
                        transform_extrusion = menu.addAction(
                            resource_icon("protrusion"), tr("Vytažení"));
                        transform_revolution = menu.addAction(
                            resource_icon("revolve"), tr("Rotace"));
                    }
                    const auto* construction =
                        part->session.document().find_construction(
                            candidate.owner_id);
                    if (construction != nullptr &&
                        construction->kind ==
                            zima::document::ConstructionKind::Curve3D) {
                        menu.addSeparator();
                        suppress_curve = menu.addAction(
                            construction->suppressed
                                ? tr("Obnovit") : tr("Potlačit"));
                        remove_curve = menu.addAction(tr("Odstranit"));
                    }
                }
                const auto* selected = menu.exec(global_position);
                if (selected == edit) {
                    show_parameter_dimensions(candidate.owner_id);
                } else if (selected == properties) {
                    construction_dimension_object_id_ = candidate.owner_id;
                    show_tree_item_properties(item);
                } else if (selected == transform_extrusion) {
                    transform_sketch_container(candidate.owner_id,
                        zima::document::FeatureKind::Extrusion);
                } else if (selected == transform_revolution) {
                    transform_sketch_container(candidate.owner_id,
                        zima::document::FeatureKind::Revolution);
                } else if (selected == suppress_curve) {
                    toggle_part_container_suppressed(candidate.owner_id);
                } else if (selected == remove_curve) {
                    delete_part_object(candidate.owner_id,
                        QStringLiteral("part-construction"), true);
                }
                return;
            }
            if (candidate.kind != zima::viewer::CandidateKind::Occurrence) return;
            show_component_context_menu(candidate.instance_path, global_position);
    });
    viewer_->set_world_click_callback([this](const auto& origin, const auto& direction) {
        if(measurement_dialog_)return false;
        if(template_image_ray(origin,direction))return true;
        if(template_region_ray(origin,direction,true))return true;
        auto [local_origin, local_direction] =
            active_part_local_ray(origin, direction);
        if (const auto alignment=sketch_point_alignment(local_origin,local_direction)) {
            std::tie(local_origin,local_direction)=active_sketch()->normal_ray(alignment->position[0],alignment->position[1]);
            pending_sketch_snap_geometry_id_=alignment->point_id;
            pending_sketch_snap_kind_=alignment->kind;
        }
        if (sketch_common_tangent_active_) {
            const auto* sketch = active_sketch();
            sketch_pointer_position_ = sketch == nullptr
                ? std::nullopt
                : sketch->intersect_ray(local_origin, local_direction);
        }
        if (accept_sketch_universal_dimension_ray(
                local_origin, local_direction)) return true;
        if (accept_sketch_dimension_placement_ray(
                local_origin, local_direction)) return true;
        if (accept_sketch_text_ray(local_origin, local_direction)) return true;
        if (accept_sketch_point_ray(local_origin, local_direction)) return true;
        if (accept_sketch_segment_ray(local_origin, local_direction)) return true;
        if (accept_sketch_rectangle_ray(local_origin, local_direction)) return true;
        if (accept_sketch_polygon_ray(local_origin, local_direction)) return true;
        if (accept_sketch_circle_ray(local_origin, local_direction)) return true;
        if (accept_sketch_arc_ray(local_origin, local_direction)) return true;
        if (accept_sketch_ellipse_ray(local_origin, local_direction)) return true;
        if (accept_sketch_elliptical_arc_ray(local_origin, local_direction)) return true;
        return accept_sketch_bspline_ray(local_origin, local_direction);
    });
    viewer_->set_world_pointer_callback([this](const auto& origin, const auto& direction) {
        if(measurement_dialog_)return;
        if(template_region_ray(origin,direction,false))return;
        // Fillet/Chamfer click already expands one persisted edge into its
        // complete unambiguous tangent route. Preview that exact same route
        // from the viewer's current offered candidate so hover, RMB cycling
        // and LMB confirmation never describe different operations.
        if (edge_treatment_selection_) {
            std::optional<zima::viewer::ViewerCandidate> hover_seed;
            if (const auto candidate = viewer_->offered_candidate();
                candidate &&
                candidate->kind == zima::viewer::CandidateKind::Edge &&
                candidate->geometry ==
                    zima::viewer::CandidateGeometry::Display) {
                hover_seed = *candidate;
                if (hover_seed != edge_treatment_hover_seed_) {
                    std::set<zima::viewer::EdgeKey> hovered_route;
                    for (const auto& edge :
                         viewer_->tangent_edge_route(*candidate)) {
                        hovered_route.insert(zima::viewer::edge_key(edge));
                    }
                    viewer_->set_feature_hover_edges(std::move(hovered_route));
                }
            }
            if (!hover_seed) viewer_->set_feature_hover_edges({});
            edge_treatment_hover_seed_ = std::move(hover_seed);
        } else {
            edge_treatment_hover_seed_.reset();
            std::set<std::size_t> hovered_feature;
            if (const auto candidate = viewer_->offered_candidate();
                candidate &&
                candidate->kind == zima::viewer::CandidateKind::Container) {
                hovered_feature = edge_treatment_feature_edges(
                    candidate->owner_id, candidate->instance_path);
            }
            viewer_->set_feature_hover_edge_indices(std::move(hovered_feature));
        }
        if (!sketch_inference_cycle_refresh_) {
            sketch_segment_inference_cycle_ = 0;
            sketch_skip_candidate_snap_ = false;
        }
        sketch_inference_cycle_refresh_ = false;
        auto [local_origin, local_direction] =
            active_part_local_ray(origin, direction);
        if (sketch_common_tangent_active_) {
            const auto* sketch = active_sketch();
            sketch_pointer_position_ = sketch == nullptr
                ? std::nullopt
                : sketch->intersect_ray(local_origin, local_direction);
        }
        std::optional<SketchCandidateSnap> cursor_snap;
        if (sketch_universal_dimension_active_ &&
            (universal_pending_dimension_ ||
             !universal_corner_radius_dimension_id_.empty())) {
            if (const auto* sketch = active_sketch()) {
                const auto cursor = sketch->intersect_ray(
                    local_origin, local_direction);
                const bool changed = cursor != universal_dimension_cursor_;
                universal_dimension_cursor_ = cursor;
                if (changed) {
                    preserve_view_on_refresh_ = true;
                    refresh_scene();
                }
            }
        }
        if (pending_sketch_dimension_ || !pending_corner_radius_dimension_id_.empty() ||
            (sketch_point_dimension_active_ &&
             !pending_point_dimension_second_id_.empty())) {
            if (const auto* sketch = active_sketch()) {
                const auto cursor =
                    sketch->intersect_ray(local_origin, local_direction);
                const bool changed = cursor.has_value() !=
                        pending_point_dimension_cursor_.has_value() ||
                    (cursor && pending_point_dimension_cursor_ &&
                     (std::abs((*cursor)[0] -
                                   (*pending_point_dimension_cursor_)[0]) > 1.0e-9 ||
                      std::abs((*cursor)[1] -
                                   (*pending_point_dimension_cursor_)[1]) > 1.0e-9));
                pending_point_dimension_cursor_ = cursor;
                if (changed) {
                    preserve_view_on_refresh_ = true;
                    refresh_scene();
                }
            }
        }
        if (!sketch_skip_candidate_snap_) {
            if (const auto candidate = viewer_->hovered_candidate()) {
            if (const auto snapped = sketch_candidate_snap_ray(
                    *candidate, local_origin, local_direction)) {
                cursor_snap = *snapped;
                local_origin = snapped->origin;
                local_direction = snapped->direction;
            }
            }
        }
        if (!cursor_snap) if (const auto alignment=sketch_point_alignment(local_origin,local_direction)) {
            std::tie(local_origin,local_direction)=active_sketch()->normal_ray(alignment->position[0],alignment->position[1]);
            cursor_snap=SketchCandidateSnap{local_origin,local_direction,alignment->point_id,alignment->kind};
        }
        viewer_->set_sketch_relation_highlights({});
        preview_sketch_segment_ray(local_origin, local_direction);
        preview_sketch_rectangle_ray(local_origin, local_direction);
        preview_sketch_polygon_ray(local_origin, local_direction);
        preview_sketch_circle_ray(local_origin, local_direction);
        preview_sketch_arc_ray(local_origin, local_direction);
        preview_sketch_ellipse_ray(local_origin, local_direction);
        preview_sketch_elliptical_arc_ray(local_origin, local_direction);
        preview_sketch_bspline_ray(local_origin, local_direction);
        const bool placement_tool = sketch_point_active_ || sketch_segment_active_ ||
            sketch_rectangle_active_ || sketch_polygon_active_ ||
            sketch_circle_active_ || sketch_arc_active_ ||
            sketch_ellipse_active_ || sketch_elliptical_arc_active_ ||
            sketch_bspline_active_;
        if (!placement_tool) {
            viewer_->set_sketch_cursor(std::nullopt);
    viewer_->set_sketch_relation_highlights({});
            return;
        }
        const auto* sketch = active_sketch();
        auto cursor = sketch == nullptr ? std::optional<std::array<double, 2>>{}
                                        : sketch->intersect_ray(
                                              local_origin, local_direction);
        bool related = cursor_snap.has_value();
        std::set<std::string> relation_support_ids;
        if (cursor_snap) relation_support_ids.insert(cursor_snap->support_geometry_id);

        std::string label;
        if (cursor_snap && cursor_snap->relation) {
            if (cursor_snap->support_geometry_id.starts_with("sketch_keypoint:")) {
                label = "K";
            } else {
                switch (*cursor_snap->relation) {
                case zima::sketcher::ConstraintKind::Coincident:
                    // Point-point C is a topology merge. The highlighted
                    // candidate point already communicates the operation;
                    // only point-on-geometry relations own a visible C.
                    label.clear();
                    break;
                case zima::sketcher::ConstraintKind::Midpoint:
                case zima::sketcher::ConstraintKind::MidpointOnLine:
                    label = "M";
                    break;
                case zima::sketcher::ConstraintKind::PointOnCircle:
                    label = "C";
                    break;
                case zima::sketcher::ConstraintKind::PointOnLine:
                    label = "C";
                    break;
                case zima::sketcher::ConstraintKind::Horizontal: label="H";break;
                case zima::sketcher::ConstraintKind::Vertical: label="V";break;
                default: label = "K"; break;
                }
            }
        }
        if (cursor && sketch_segment_active_ && pending_segment_start_ &&
            !cursor_snap) {
            const auto inference = inferred_sketch_segment_end(*cursor);
            *cursor = inference.position;
            for (const auto& id : {inference.reference_point_id,inference.equal_length_reference_id,
                    inference.symmetry_axis_id,inference.tangent_reference_id,inference.perpendicular_reference_id,
                    inference.parallel_reference_id,inference.midpoint_line_reference_id})
                if (!id.empty()) relation_support_ids.insert(id);

            related = inference.kind.has_value() ||
                !inference.reference_point_id.empty() ||
                !inference.equal_length_reference_id.empty() ||
                !inference.symmetry_axis_id.empty() ||
                !inference.tangent_reference_id.empty() ||
                !inference.perpendicular_reference_id.empty() ||
                !inference.parallel_reference_id.empty() ||
                !inference.midpoint_line_reference_id.empty();
            if (inference.kind == zima::sketcher::ConstraintKind::Horizontal)
                label = "H";
            else if (inference.kind == zima::sketcher::ConstraintKind::Vertical)
                label = "V";
            else if (!inference.midpoint_line_reference_id.empty()) label = "M";
            // Other segment inferences draw their own marker at the actual
            // relation anchor in preview_sketch_segment_ray().  K is reserved
            // exclusively for a real characteristic-point candidate and must
            // never be used as a generic fallback for T, perpendicular,
            // parallel, equal-length or symmetry inference.
            else label.clear();
        }
        if (cursor && sketch_arc_active_ && pending_arc_center_ &&
            pending_arc_start_) {
            const double radius = std::hypot(
                (*pending_arc_start_)[0] - (*pending_arc_center_)[0],
                (*pending_arc_start_)[1] - (*pending_arc_center_)[1]);
            const double dx = (*cursor)[0] - (*pending_arc_center_)[0];
            const double dy = (*cursor)[1] - (*pending_arc_center_)[1];
            const double length = std::hypot(dx, dy);
            if (radius > 1.0e-12 && length > 1.0e-12) {
                *cursor = {
                    (*pending_arc_center_)[0] + radius * dx / length,
                    (*pending_arc_center_)[1] + radius * dy / length};
            }
        }
        if (cursor && sketch_ellipse_active_ && pending_ellipse_center_ &&
            pending_ellipse_major_) {
            if (const auto minor = projected_ellipse_minor(
                    *pending_ellipse_center_, *pending_ellipse_major_,
                    *cursor)) {
                *cursor = *minor;
            }
        }
        if (cursor && sketch_elliptical_arc_active_ &&
            pending_elliptical_arc_center_ && pending_elliptical_arc_major_ &&
            !pending_elliptical_arc_minor_ &&
            !(cursor_snap && cursor_snap->support_geometry_id.starts_with(
                "sketch_keypoint:"))) {
            if (const auto minor = projected_ellipse_minor(
                    *pending_elliptical_arc_center_,
                    *pending_elliptical_arc_major_, *cursor)) {
                *cursor = *minor;
            }
        }
        if (cursor && sketch_elliptical_arc_active_ &&
            pending_elliptical_arc_center_ && pending_elliptical_arc_major_ &&
            pending_elliptical_arc_minor_ &&
            !(cursor_snap && cursor_snap->support_geometry_id.starts_with(
                "sketch_keypoint:"))) {
            if (const auto projected = projected_ellipse_position(
                    *pending_elliptical_arc_center_,
                    *pending_elliptical_arc_major_,
                    *pending_elliptical_arc_minor_, *cursor)) {
                *cursor = projected->position;
            }
        }
        if (cursor && sketch_circle_active_ && pending_circle_center_) {
            const auto tangent = inferred_sketch_circle_tangent(*cursor);
            if (tangent) {
                // TC is a geometric snap, not only an annotation: show the
                // cursor at the exact perpendicular contact that is also used
                // by preview and commit. Do not concatenate lower-priority C
                // or equal-radius inference labels into the contact marker.
                *cursor = tangent->contact;
                label = "C T";
                related = true;
                relation_support_ids.insert(tangent->support_id);
            } else if (const auto equal=inferred_sketch_circle_radius(*cursor)) {
                relation_support_ids.insert(equal->second);
            }
        }
        const std::vector<std::string> support_keys(relation_support_ids.begin(),relation_support_ids.end());
        for (const auto& support : support_keys) {
            if (const auto curve=sketch_keypoint_curve_id(support)) relation_support_ids.insert(*curve);
            const auto separator=support.find("||");
            if (separator!=std::string::npos) {
                relation_support_ids.insert(support.substr(0,separator));
                relation_support_ids.insert(support.substr(separator+2));
            }
        }
        std::set<zima::viewer::EdgeKey> relation_keys;
        const auto add_reference=[&](const auto& ref) {
            if (ref.owner_id!=active_sketch_id_) return;
            for (const auto& id : relation_support_ids)
                if (ref.semantic_key==id || ref.semantic_key.ends_with(":"+id))
                    relation_keys.insert({ref.owner_id,ref.semantic_key,ref.instance_path});
        };
        for (const auto& edge : viewer_->mesh().edges) add_reference(edge.reference);
        for (const auto& point : viewer_->mesh().points) add_reference(point.reference);
        for (const auto& axis : viewer_->mesh().axes) add_reference(axis.reference);
        viewer_->set_sketch_relation_highlights(std::move(relation_keys));
        viewer_->set_sketch_cursor(
            cursor && sketch ? std::optional{sketch->world_point((*cursor)[0], (*cursor)[1])}
                             : std::nullopt,
            related, std::move(label));
    });
    viewer_->set_command_gesture_callbacks(
        [this](const auto& candidate, const auto& origin, const auto& direction) {
            const auto [local_origin, local_direction] =
                active_part_local_ray(origin, direction);
            if (!sketch_trim_active_ && !sketch_skip_candidate_snap_ && candidate &&
                accept_sketch_external_snap(
                    *candidate, local_origin, local_direction)) return true;
            return begin_sketch_trim_gesture(
                candidate, local_origin, local_direction);
        },
        [this](const auto& origin, const auto& direction) {
            const auto [local_origin, local_direction] =
                active_part_local_ray(origin, direction);
            update_sketch_trim_gesture(local_origin, local_direction);
        },
        [this] { end_sketch_trim_gesture(); });
    viewer_->set_short_middle_click_callback([this] {
        if(sketch_offset_dialog_){sketch_offset_dialog_->end_entry();return true;}
        if(measurement_dialog_){measurement_dialog_->end_entry();return true;}
        if(auto* dialog=dynamic_cast<AppearanceDialog*>(properties_dialog_)){dialog->end_entry();viewer_->clear_selection();return true;}
        if(template_region_picking_){cancel_sketch_segment();preserve_view_on_refresh_=true;refresh_scene();return true;}
        if (finish_active_reference_selection()) return true;
        if (finish_parameter_dimensions()) return true;
        // Sketch geometry is confirmed exclusively by LMB.  Consume every
        // short MMB click while Sketcher is active so it can never fall back
        // to world_click_callback() and place a point of any drawing tool.
        // MMB drag remains view navigation and MMB double-click is handled
        // separately below as the universal return-to-Selection gesture.
        if (!active_sketch_id_.empty()) {
            return true;
        }
        return false;
    });
    viewer_->set_double_middle_click_callback(
        [this] {
            if (finish_parameter_dimensions() || finish_current_sketch_tool()) return true;
            if (properties_dialog_ || tree_->property("commandSelectionActive").toBool()) return false;
            tree_->clearSelection();
            viewer_->set_feature_selected_edges({});
            viewer_->clear_selection();
            return true;
        });
    viewer_->set_dimension_placement_cycle_callback([this] {
        const bool universal=sketch_universal_dimension_active_ && universal_dimension_cursor_ &&
            (universal_pending_dimension_ || !universal_corner_radius_dimension_id_.empty());
        const bool ordinary=pending_point_dimension_cursor_ &&
            (pending_sketch_dimension_ || !pending_corner_radius_dimension_id_.empty() ||
             (sketch_point_dimension_active_ && !pending_point_dimension_second_id_.empty()));
        if(!universal && !ordinary)return false;
        const auto kind = !universal_corner_radius_dimension_id_.empty() || !pending_corner_radius_dimension_id_.empty() ||
            (pending_sketch_dimension_ && pending_sketch_dimension_->kind==zima::sketcher::DimensionKind::Radius) ||
            (universal_pending_dimension_ && universal_pending_dimension_->kind==zima::sketcher::DimensionKind::Radius)
            ? zima::kernel::ViewerDimensionKind::Radius : zima::kernel::ViewerDimensionKind::Linear;
        zima::kernel::cycle_dimension_presentation(universal_dimension_layout_,kind);
        preserve_view_on_refresh_=true;refresh_scene();return true;
    });
    viewer_->set_empty_right_click_callback(
        [this] { return cancel_current_sketch_step(true); });
    viewer_->set_single_candidate_right_click_callback({});
    viewer_->set_candidate_right_click_callback(
        [this](const auto&, std::size_t candidate_index,
               std::size_t candidate_count) {
            if (!sketch_segment_active_ || sketch_polyline_active_ ||
                !pending_segment_start_) return false;
            if (!sketch_skip_candidate_snap_) {
                // Let MeshView advance through every overlapping geometry
                // candidate first. Only after the last one do we enter the
                // inferred H/V/tangent/etc. variants.
                if (candidate_index + 1 < candidate_count) return false;
                sketch_skip_candidate_snap_ = true;
                sketch_segment_inference_cycle_ = 0;
            } else {
                if (sketch_segment_inference_cycle_ + 1 >=
                        sketch_segment_inference_variant_count_) {
                    sketch_skip_candidate_snap_ = false;
                    sketch_segment_inference_cycle_ = 0;
                    viewer_->reset_candidate_cycle();
                } else {
                    ++sketch_segment_inference_cycle_;
                }
            }
            sketch_inference_cycle_refresh_ = true;
            static_cast<void>(viewer_->refresh_current_pointer_preview());
            state_->setText(tr(
                "Úsečka: RMB přepnulo další geometrický nebo inferenční kandidát."));
            return true;
        });
    viewer_->set_double_confirmation_callback([this](const auto& candidate) {
        if(measurement_dialog_)return;
        if(candidate.kind==zima::viewer::CandidateKind::TemplateImage && candidate.owner_id==active_sketch_id_) {
            show_template_image_properties(candidate.semantic_key.substr(15));return;
        }
        if(candidate.kind==zima::viewer::CandidateKind::TemplateRegion && candidate.owner_id==active_sketch_id_) {
            show_template_region_properties(candidate.semantic_key.substr(14));return;
        }
        if (sketch_tangent_active_) {
            accept_sketch_tangent_selection(candidate);
            return;
        }
        if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
            candidate.semantic_key.starts_with("parameter:")) {
            edit_dimension_inline(candidate);
        } else if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
            (candidate.semantic_key.starts_with("dimension:") ||
             candidate.semantic_key.starts_with("corner_dimension:"))) {
            edit_dimension_inline(candidate);
        } else if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
                   candidate.semantic_key.starts_with("placement-reference:") &&
                   workspace_.open_assembly(candidate.owner_id) != nullptr) {
            edit_dimension_inline(candidate);
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
                   candidate.owner_id == active_sketch_id_ &&
                   candidate.semantic_key.starts_with("bspline:")) {
            show_sketch_bspline_properties(
                active_sketch_id_, candidate.semantic_key.substr(8));
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchText &&
                   candidate.owner_id == active_sketch_id_) {
            if (const auto text_id = sketch_text_id_from_key(candidate.semantic_key)) {
                show_sketch_text_properties(active_sketch_id_, *text_id);
            }
        } else if (candidate.kind == zima::viewer::CandidateKind::Occurrence) {
            if(!properties_dialog_){
                assembly_dimension_path_=candidate.instance_path;
                update_assembly_dimension_visibility();
            }
        } else if (candidate.kind == zima::viewer::CandidateKind::Container) {
            const auto* part=workspace_.open_part(workspace_.active_document_id());
            const auto* body=part?part->session.document().body_history.find(candidate.owner_id):nullptr;
            if(body&&body->derived_copy)show_derived_source_properties(candidate.owner_id);
            else show_parameter_dimensions(candidate.owner_id);
        } else if (candidate.kind == zima::viewer::CandidateKind::Vertex) {
            const auto show_point_dimensions = [&](const auto& document) {
                const auto found = std::find_if(document.constructions.begin(),
                    document.constructions.end(), [&](const auto& object) {
                        return object.kind == zima::document::ConstructionKind::Point &&
                            object.container_origin.id == candidate.owner_id;
                    });
                if (found == document.constructions.end()) return false;
                construction_dimension_object_id_ = found->id;
                preserve_view_on_refresh_ = true;
                refresh_scene();
                return true;
            };
            if (const auto* part = workspace_.open_part(
                    workspace_.active_document_id())) {
                static_cast<void>(show_point_dimensions(part->session.document()));
            } else if (const auto* assembly = workspace_.open_assembly(
                           workspace_.active_document_id())) {
                static_cast<void>(show_point_dimensions(assembly->session.document()));
            }
        }
    });
    viewer_->set_candidate_drag_callbacks(
        [this](const auto& candidate, const auto& origin, const auto& direction) {
            return begin_placement_reference_drag(candidate) ||
                   begin_component_drag(candidate, origin, direction) ||
                   begin_sketch_point_drag(candidate);
        },
        [this](const auto& origin, const auto& direction) {
            if (placement_reference_drag_document_) {
                const auto [local_origin, local_direction] =
                    active_assembly_local_ray(origin, direction);
                update_placement_reference_drag(local_origin, local_direction);
            } else if (component_drag_document_) {
                update_component_drag(origin, direction);
            } else {
                const auto [local_origin, local_direction] =
                    active_part_local_ray(origin, direction);
                if (!sketch_drag_dimension_id_.empty()) {
                    update_sketch_dimension_drag(local_origin, local_direction);
                } else {
                    update_sketch_point_drag(local_origin, local_direction);
                }
            }
        },
        [this] {
            if (placement_reference_drag_document_) end_placement_reference_drag();
            else if (component_drag_document_) end_component_drag();
            else if (!sketch_drag_dimension_id_.empty()) end_sketch_dimension_drag();
            else end_sketch_point_drag();
        });
    model_workspace_ = viewer_;
    model_workspace_->setObjectName("modelWorkspace");
    workspace_stack_ = new QStackedWidget;
    workspace_stack_->setObjectName("workspaceStack");
    workspace_stack_->addWidget(model_workspace_);
    drawing_workspace_ = new DrawingWindow(&workspace_, false);
    drawing_workspace_->set_formats_directory(application_settings_.resolved_paths.value("Formats"));
    drawing_workspace_->set_document_changed_handler([this] { refresh_drawing_tree(); });
    drawing_workspace_->set_properties_handler([this](QDialog* dialog) {
        properties_dialog_=dialog;
        if(dialog)connect(dialog,&QObject::destroyed,this,[this,dialog] {
            if(properties_dialog_==dialog)properties_dialog_=nullptr;
        });
    });
    drawing_workspace_->set_selection_handler([this](const std::string& id) {
        if (!workspace_.open_drawing(workspace_.displayed_document_id())) return;
        const QSignalBlocker blocker(tree_);
        tree_->clearSelection(); tree_->setCurrentItem(nullptr);
        for(QTreeWidgetItemIterator it(tree_); *it; ++it)
            if ((*it)->data(0,Qt::UserRole).toString().toStdString()==id && !id.empty()) {
                tree_->setCurrentItem(*it); (*it)->setSelected(true); break;
            }
    });
    drawing_workspace_->setObjectName("drawingWorkspace");
    drawing_workspace_->setWindowFlags(Qt::Widget);
    drawing_workspace_->menuBar()->hide();
    if (auto* drawing_toolbar =
            drawing_workspace_->findChild<QToolBar*>("drawingToolbar")) {
        drawing_toolbar->hide();
    }
    workspace_stack_->addWidget(drawing_workspace_);

    auto* view_panel = new QWidget;
    auto* view_layout = new QVBoxLayout(view_panel);
    view_layout->setContentsMargins(0, 0, 0, 0);
    view_layout->setSpacing(0);
    view_layout->addWidget(view_toolbar_);
    view_layout->addWidget(workspace_stack_, 1);

    auto* tools_panel = new QWidget;
    auto* tools_layout = new QVBoxLayout(tools_panel);
    tools_layout->setContentsMargins(0, 0, 0, 0);
    tools_layout->setSpacing(0);
    tools_layout->addSpacing(view_toolbar_->sizeHint().height());
    tools_layout->addWidget(tools_toolbar_, 1);

    auto* workspace_panel = new QWidget(document_splitter_);
    auto* workspace_layout = new QHBoxLayout(workspace_panel);
    workspace_layout->setContentsMargins(0, 0, 0, 0);
    workspace_layout->setSpacing(0);
    workspace_layout->addWidget(view_panel, 1);
    workspace_layout->addWidget(tools_panel);
    document_splitter_->addWidget(tree_);
    document_splitter_->addWidget(workspace_panel);
    document_splitter_->setStretchFactor(0, 0);
    document_splitter_->setStretchFactor(1, 1);
    document_splitter_->setSizes({280, 920});

    layout->addWidget(tabs_);
    layout->addWidget(document_splitter_, 1);
    setCentralWidget(central);
    state_ = new QLabel(this);
    state_->setObjectName("workspaceState");
    state_->setText(tr("Připraveno."));
    statusBar()->addWidget(state_, 1);
    drawing_workspace_->set_status_handler([this](const QString& message) {
        if (workspace_stack_->currentWidget() == drawing_workspace_)
            state_->setText(message);
    });
    operation_progress_ = new StatusOperationProgressBar(this);
    operation_progress_->setObjectName("fileOperationProgress");
    operation_progress_->setMinimumWidth(360);
    operation_progress_->setMaximumWidth(620);
    operation_progress_->setFixedHeight(20);
    operation_progress_->setTextVisible(true);
    operation_progress_->hide();
    statusBar()->addPermanentWidget(operation_progress_);
    connect(tabs_, &QTabBar::tabCloseRequested, this,
        [this](int index) { close_document(index); });
    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (index < 0) return;
        if(measurement_dialog_)measurement_dialog_->reject();
        if(section_dialog_){QSignalBlocker block(tabs_);for(int i=0;i<tabs_->count();++i)if(tabs_->tabData(i).toString().toStdString()==section_document_id_)tabs_->setCurrentIndex(i);return;}
        const std::string previous_id = workspace_.active_document_id();
        if (!previous_id.empty() && viewer_ != nullptr) {
            document_camera_states_[previous_id] = viewer_->camera_state();
        }
        const std::string id = tabs_->tabData(index).toString().toStdString();
        workspace_.activate(id);
        workspace_.display_top_level(id);
        active_occurrence_path_.clear();
        active_sketch_id_.clear();
        selected_sketch_id_.clear();
        selected_sketch_segment_id_.clear();
        selected_sketch_circle_id_.clear();
        selected_sketch_arc_id_.clear();
        selected_sketch_ellipse_id_.clear();
        selected_sketch_elliptical_arc_id_.clear();
        selected_sketch_bspline_id_.clear();
        selected_sketch_text_id_.clear();
        selected_sketch_point_id_.clear();
        cancel_sketch_segment();
        refresh_scene();
        if (const auto camera = document_camera_states_.find(id);
            camera != document_camera_states_.end()) {
            viewer_->set_camera_state(camera->second);
        }
    });
    const auto synchronize_tree_selection = [this] {
            if(measurement_dialog_)return;
            if(auto* item=tree_->currentItem();item&&item->data(0,Qt::UserRole+3)=="document-measurement"){
                viewer_->clear_selection();return;
            }
            if(auto* item=tree_->currentItem();item&&item->data(0,Qt::UserRole+3).toString().startsWith("document-section")){
                if(!refreshing_scene_){assembly_dimension_path_.clear();update_assembly_dimension_visibility();}
                viewer_->clear_selection();return;
            }
            if (workspace_.open_drawing(workspace_.displayed_document_id())) {
                const auto items=tree_->selectedItems();
                auto* item=items.empty()?nullptr:items.front();
                drawing_workspace_->select_view(item && item->data(0,Qt::UserRole+3).toString()=="drawing-view"
                    ? item->data(0,Qt::UserRole).toString().toStdString() : std::string{});
                return;
            }
            if (local_origin_selection_active_) return;
            if (!component_drag_document_) set_selected_component_origin({});
            const auto selected_items = tree_->selectedItems();
            if (selected_items.empty()) {
                if (!refreshing_scene_) {
                    construction_dimension_object_id_.clear();
                    assembly_dimension_path_.clear();update_assembly_dimension_visibility();
                }
                clear_selected_sketch_geometry();
                viewer_->set_feature_selected_edges({});
                viewer_->clear_selection();
                return;
            }
            auto* item = tree_->currentItem();
            if (item == nullptr || !selected_items.contains(item)) {
                item = selected_items.front();
            }
            if(item->data(0,Qt::UserRole+3).toString()=="template-image") {
                const auto id=item->data(0,Qt::UserRole).toString().toStdString();selected_template_image_=id;
                viewer_->confirm_reference(active_sketch_id_,"template_image:"+id,{},zima::viewer::CandidateKind::TemplateImage);return;
            }
            if(item->data(0,Qt::UserRole+3).toString()=="template-repeat-region") {
                const auto id=item->data(0,Qt::UserRole).toString().toStdString();selected_template_region_=id;
                viewer_->confirm_reference(active_sketch_id_,"repeat_region:"+id,{},zima::viewer::CandidateKind::TemplateRegion);return;
            }
            if (item->parent() == nullptr && item->data(0, Qt::UserRole + 3).toString() != "part-result-body") {
                if(!refreshing_scene_){assembly_dimension_path_.clear();update_assembly_dimension_visibility();}
                viewer_->clear_selection(); return;
            }
            if(accept_derived_copy_tree_reference(item))return;
            if (auto* sweep=dynamic_cast<Sweep2DDialog*>(properties_dialog_);
                sweep&&sweep->path_active()&&feature_reference_pick_) {
                zima::viewer::ViewerCandidate candidate;
                candidate.geometry=zima::viewer::CandidateGeometry::OriginalReference;
                candidate.kind=zima::viewer::CandidateKind::Plane;
                candidate.instance_path=item->data(0,Qt::UserRole+1).toString().toStdString();
                const auto kind=item->data(0,Qt::UserRole+3).toString();
                if(kind=="origin-reference") {
                    candidate.owner_id=item->data(0,Qt::UserRole+6).isValid()
                        ?item->data(0,Qt::UserRole+6).toString().toStdString():item->data(0,Qt::UserRole).toString().toStdString();
                    candidate.semantic_key=item->data(0,Qt::UserRole+5).toString().toStdString();
                } else if(kind=="part-construction") {
                    const auto* part=workspace_.open_part(workspace_.active_document_id());
                    const auto* plane=part?part->session.document().find_construction(item->data(0,Qt::UserRole).toString().toStdString()):nullptr;
                    if(plane&&plane->kind==zima::document::ConstructionKind::Plane){candidate.owner_id=plane->entity_id;candidate.semantic_key="plane";}
                }
                auto pick=feature_reference_pick_;pick(candidate);
                if(sweep->path_active())state_->setText(tr("Vyberte rovinu nebo rovinnou plochu pro skicu dráhy."));
                return;
            }
            if (construction_reference_dialog_ != nullptr &&
                pending_construction_reference_index_) {
                if (!accept_construction_tree_reference(item)) {
                    state_->setText(tr(
                        "Tato položka stromu není platná reference pro zvolenou konstrukci."));
                }
                return;
            }
            if (primitive_reference_dialog_ != nullptr &&
                pending_primitive_reference_index_) {
                if (!accept_primitive_tree_reference(item)) {
                    state_->setText(tr(
                        "Tato položka stromu není platná reference pro umístění kontejneru."));
                }
                return;
            }
            if (component_placement_dialog_ != nullptr) {
                if (accept_component_placement_tree_reference(item)) return;
                if (pending_component_placement_index_) {
                    state_->setText(tr(
                        "Tato položka stromu není platná reference pro umístění komponenty."));
                    return;
                }
            }
            // Tree and View share one confirmed-selection state. Switching
            // to any ordinary Tree item must release the previous Sketch
            // geometry latch before the new item is synchronized below.
            clear_selected_sketch_geometry();
            viewer_->set_feature_selected_edges({});
            if (item->data(0, Qt::UserRole + 3).toString() ==
                    "part-sketch-dimension" ||
                item->data(0, Qt::UserRole + 3).toString() ==
                    "part-sketch-constraint") {
                const auto role = item->data(0, Qt::UserRole + 3).toString();
                const auto relation_id =
                    item->data(0, Qt::UserRole).toString().toStdString();
                const auto sketch_id =
                    item->data(0, Qt::UserRole + 4).toString().toStdString();
                const bool dimension = role == QStringLiteral(
                    "part-sketch-dimension");
                viewer_->confirm_reference(sketch_id,
                    (dimension ? "dimension:" : "constraint:") + relation_id,
                    {}, dimension ? zima::viewer::CandidateKind::Dimension
                                  : zima::viewer::CandidateKind::SketchConstraint);
                return;
            }
            if (item->data(0, Qt::UserRole + 3).toString() ==
                    "sketch-origin-reference") {
                const auto sketch_id =
                    item->data(0, Qt::UserRole + 4).toString().toStdString();
                const auto semantic_key =
                    item->data(0, Qt::UserRole).toString().toStdString();
                if (sketch_id != active_sketch_id_) return;
                const auto kind = semantic_key.starts_with("sketch_axis:")
                    ? zima::viewer::CandidateKind::SketchAxis
                    : zima::viewer::CandidateKind::SketchExternalReference;
                viewer_->confirm_reference(sketch_id, semantic_key, {}, kind);
                return;
            }
            if (item->data(0, Qt::UserRole + 3).toString() ==
                    "sketch-geometry" ||
                item->data(0, Qt::UserRole + 3).toString() ==
                    "sketch-external-reference") {
                const auto geometry_id =
                    item->data(0, Qt::UserRole).toString().toStdString();
                const auto sketch_id =
                    item->data(0, Qt::UserRole + 4).toString().toStdString();
                const auto* sketch = active_sketch();
                if (sketch == nullptr || sketch->id != sketch_id) return;
                selected_sketch_segment_id_.clear();
                selected_sketch_circle_id_.clear();
                selected_sketch_arc_id_.clear();
                selected_sketch_ellipse_id_.clear();
                selected_sketch_elliptical_arc_id_.clear();
                selected_sketch_bspline_id_.clear();
                selected_sketch_text_id_.clear();
                selected_sketch_external_reference_id_.clear();
                selected_sketch_point_id_.clear();
                zima::viewer::CandidateKind candidate_kind{};
                std::string semantic_key;
                if (std::any_of(sketch->points.begin(), sketch->points.end(),
                        [&](const auto& value) { return value.id == geometry_id; })) {
                    selected_sketch_point_id_ = geometry_id;
                    candidate_kind = zima::viewer::CandidateKind::SketchPoint;
                    semantic_key = "point:" + geometry_id;
                } else if (std::any_of(sketch->segments.begin(), sketch->segments.end(),
                               [&](const auto& value) { return value.id == geometry_id; })) {
                    selected_sketch_segment_id_ = geometry_id;
                    candidate_kind = zima::viewer::CandidateKind::SketchSegment;
                    semantic_key = "segment:" + geometry_id;
                } else if (std::any_of(sketch->circles.begin(), sketch->circles.end(),
                               [&](const auto& value) { return value.id == geometry_id; })) {
                    selected_sketch_circle_id_ = geometry_id;
                    candidate_kind = zima::viewer::CandidateKind::SketchCurve;
                    semantic_key = "circle:" + geometry_id;
                } else if (std::any_of(sketch->arcs.begin(), sketch->arcs.end(),
                               [&](const auto& value) { return value.id == geometry_id; })) {
                    selected_sketch_arc_id_ = geometry_id;
                    candidate_kind = zima::viewer::CandidateKind::SketchCurve;
                    semantic_key = "arc:" + geometry_id;
                } else if (std::any_of(sketch->ellipses.begin(), sketch->ellipses.end(),
                               [&](const auto& value) { return value.id == geometry_id; })) {
                    selected_sketch_ellipse_id_ = geometry_id;
                    candidate_kind = zima::viewer::CandidateKind::SketchCurve;
                    semantic_key = "ellipse:" + geometry_id;
                } else if (std::any_of(sketch->elliptical_arcs.begin(),
                               sketch->elliptical_arcs.end(), [&](const auto& value) {
                                   return value.id == geometry_id;
                               })) {
                    selected_sketch_elliptical_arc_id_ = geometry_id;
                    candidate_kind = zima::viewer::CandidateKind::SketchCurve;
                    semantic_key = "elliptical_arc:" + geometry_id;
                } else if (std::any_of(sketch->bsplines.begin(), sketch->bsplines.end(),
                               [&](const auto& value) { return value.id == geometry_id; })) {
                    selected_sketch_bspline_id_ = geometry_id;
                    candidate_kind = zima::viewer::CandidateKind::SketchCurve;
                    semantic_key = "bspline:" + geometry_id;
                } else if (std::any_of(sketch->texts.begin(), sketch->texts.end(),
                               [&](const auto& value) { return value.id == geometry_id; })) {
                    selected_sketch_text_id_ = geometry_id;
                    candidate_kind = zima::viewer::CandidateKind::SketchText;
                    semantic_key = "text:" + geometry_id;
                } else {
                    const auto reference = std::find_if(sketch->external_references.begin(),
                        sketch->external_references.end(), [&](const auto& value) {
                            return value.id == geometry_id;
                        });
                    if (reference == sketch->external_references.end()) return;
                    selected_sketch_external_reference_id_ = geometry_id;
                    candidate_kind =
                        zima::viewer::CandidateKind::SketchExternalReference;
                    semantic_key = reference->kind ==
                            zima::sketcher::ExternalReferenceKind::Point
                        ? "external_point:" + geometry_id
                        : reference->kind ==
                                zima::sketcher::ExternalReferenceKind::Axis
                            ? "external_axis:" + geometry_id
                            : reference->kind ==
                                    zima::sketcher::ExternalReferenceKind::Face
                                ? "external_face:" + geometry_id
                                : "external_edge:" + geometry_id;
                }
                viewer_->confirm_reference(sketch_id, semantic_key, {}, candidate_kind);
                // `confirm_reference()` deliberately does not re-enter the
                // View confirmation callback. Keep Tree-originated selection
                // context actions in the same state explicitly.
                sketch_radius_dimension_action_->setEnabled(
                    !selected_sketch_circle_id_.empty() ||
                    !selected_sketch_arc_id_.empty());
                sketch_diameter_dimension_action_->setEnabled(
                    !selected_sketch_circle_id_.empty() ||
                    !selected_sketch_arc_id_.empty());
                const bool ellipse_selected =
                    !selected_sketch_ellipse_id_.empty();
                sketch_ellipse_major_dimension_action_->setEnabled(
                    ellipse_selected);
                sketch_ellipse_minor_dimension_action_->setEnabled(
                    ellipse_selected);
                sketch_ellipse_rotation_dimension_action_->setEnabled(
                    ellipse_selected);
                sketch_fix_point_action_->setEnabled(
                    !selected_sketch_point_id_.empty());
                return;
            }
            if (item->data(0, Qt::UserRole + 3).toString() == "part-sketch" ||
                item->data(0, Qt::UserRole + 3).toString() == "assembly-sketch") {
                selected_sketch_id_ =
                    item->data(0, Qt::UserRole).toString().toStdString();
                viewer_->confirm_container(selected_sketch_id_);
                return;
            }
            if (item->data(0, Qt::UserRole + 3).toString() == "part-construction" ||
                item->data(0, Qt::UserRole + 3).toString() == "assembly-construction") {
                const auto id = item->data(0, Qt::UserRole).toString().toStdString();
                const auto* part = workspace_.open_part(workspace_.active_document_id());
                const auto* assembly =
                    workspace_.open_assembly(workspace_.active_document_id());
                const auto* object = part != nullptr
                    ? part->session.document().find_construction(id)
                    : assembly != nullptr
                        ? assembly->session.document().find_construction(id) : nullptr;
                if (object == nullptr) return;
                const auto path = item->data(0, Qt::UserRole + 1)
                    .toString().toStdString();
                viewer_->confirm_container(object->id);
                return;
            }
            if (item->data(0, Qt::UserRole + 3).toString() ==
                    "origin-reference") {
                const auto semantic = item->data(0, Qt::UserRole + 5)
                    .toString().toStdString();
                const auto owner = item->data(0, Qt::UserRole + 6).isValid()
                    ? item->data(0, Qt::UserRole + 6).toString().toStdString()
                    : item->data(0, Qt::UserRole).toString().toStdString();
                viewer_->confirm_reference(owner, semantic,
                    item->data(0, Qt::UserRole + 1).toString().toStdString(),
                    (semantic == "origin:point" || semantic == "point")
                        ? zima::viewer::CandidateKind::Vertex
                        : semantic.starts_with("origin:axis:") ||
                              semantic.starts_with("axis:")
                            ? zima::viewer::CandidateKind::Axis
                            : zima::viewer::CandidateKind::Plane);
                return;
            }
            if (item->data(0, Qt::UserRole + 3).toString() == "document-origin" ||
                item->data(0, Qt::UserRole + 3).toString() == "construction-origin") {
                viewer_->confirm_origin(
                    item->data(0, Qt::UserRole).toString().toStdString(),
                    item->data(0, Qt::UserRole + 1).toString().toStdString());
                return;
            }
            if (item->data(0,Qt::UserRole+3).toString()=="part-treatment-component") {
                const auto* part=workspace_.open_part(workspace_.active_document_id());
                const auto id=item->data(0,Qt::UserRole).toString().toStdString();
                const auto* container=part ? part->session.document().find_container(id) : nullptr;
                const auto boundary=part ? part->session.rollback_boundary(id) : std::nullopt;
                if (!container || !boundary || !boundary->input_body) return;
                const auto route=static_cast<std::size_t>(item->data(0,Qt::UserRole+6).toInt());
                const int segment=item->data(0,Qt::UserRole+7).toInt();
                auto wire=treatment_selection_wire(container->edge_treatment,route,
                    segment<0 ? std::nullopt : std::optional<std::size_t>{segment},boundary->input_body->mesh);
                const auto path=item->data(0,Qt::UserRole+1).toString().toStdString();
                if (!path.empty()) {
                    zima::kernel::BodyResult override_body;
                    override_body.mesh.edges=std::move(wire);
                    const auto scene=workspace_.build_scene_with_part_override(
                        workspace_.displayed_document_id(),zima::assembly::InstancePath::decode(path),
                        std::move(override_body));
                    wire.clear();
                    for (const auto& edge : scene.edges)
                        if (edge.reference.instance_path==path) wire.push_back(edge);
                }
                viewer_->set_feature_selected_edge_indices({});
                viewer_->confirm_container_component_wire(id,
                    item->data(0,Qt::UserRole+5).toString().toStdString(),std::move(wire),path);
                return;
            }
            if(item->data(0,Qt::UserRole+3).toString()=="part-sweep2d-sketch"){
                auto* part=workspace_.open_part(workspace_.active_document_id());
                const auto id=item->data(0,Qt::UserRole).toString().toStdString();
                const auto* feature=part?part->session.document().find_container(id):nullptr;if(!feature)return;
                try{auto c=*feature;zima::document::PartDocument::reframe_sweep2d_sketches(c);const auto stage=item->data(0,Qt::UserRole+6).toUInt();
                    auto mesh=zima::sketcher::Sketch::from_serialized(c.sweep2d.sketch_data(stage)).viewer_mesh();
                    const auto path=item->data(0,Qt::UserRole+1).toString().toStdString();
                    if(!path.empty())for(auto& e:mesh.edges)for(auto& p:e.points)p=workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),zima::assembly::InstancePath::decode(path),p);
                    viewer_->confirm_container_component_wire(id,"sweep2d-sketch:"+std::to_string(stage),std::move(mesh.edges),path);
                }catch(const std::exception& e){state_->setText(QString::fromUtf8(e.what()));}return;
            }
            if(item->data(0,Qt::UserRole+3).toString()=="part-helical-sketch"){
                auto* part=workspace_.open_part(workspace_.active_document_id());
                const auto id=item->data(0,Qt::UserRole).toString().toStdString();
                const auto* feature=part?part->session.document().find_container(id):nullptr;if(!feature)return;
                try{auto c=*feature;zima::document::PartDocument::reframe_helical_sketches(c);const auto stage=item->data(0,Qt::UserRole+6).toUInt();
                    auto mesh=zima::sketcher::Sketch::from_serialized(c.helical.sketches.at(stage)).viewer_mesh();
                    const auto path=item->data(0,Qt::UserRole+1).toString().toStdString();
                    if(!path.empty())for(auto& e:mesh.edges)for(auto& p:e.points)p=workspace_.occurrence_point_to_scene(workspace_.displayed_document_id(),zima::assembly::InstancePath::decode(path),p);
                    viewer_->confirm_container_component_wire(id,"helical-sketch:"+std::to_string(stage),std::move(mesh.edges),path);
                }catch(const std::exception& e){state_->setText(QString::fromUtf8(e.what()));}return;
            }
            if (item->data(0,Qt::UserRole+3).toString()=="part-opening-component") {
                const auto* part=workspace_.open_part(workspace_.active_document_id());
                const auto id=item->data(0,Qt::UserRole).toString().toStdString();
                const auto* opening=part ? part->session.document().find_container(id) : nullptr;
                if (!opening) return;
                const auto role=item->data(0,Qt::UserRole+5).toString().toStdString();
                const auto path=item->data(0,Qt::UserRole+1).toString().toStdString();
                viewer_->set_feature_selected_edge_indices({});
                viewer_->confirm_container_component(id,role,
                    opening_component_edges(*opening,role,viewer_->mesh(),path),path);
                return;
            }
            if (item->data(0, Qt::UserRole + 3).toString() == "part-container" ||
                item->data(0, Qt::UserRole + 3).toString() ==
                    "part-hole-component") {
                const auto owner_id =
                    item->data(0, Qt::UserRole).toString().toStdString();
                viewer_->confirm_container(owner_id);
                viewer_->set_feature_selected_edge_indices(
                    edge_treatment_feature_edges(owner_id));
            } else if (item->data(0, Qt::UserRole + 3).toString() == "part-body" ||
                       item->data(0, Qt::UserRole + 3).toString() == "part-body-boolean") {
                const auto id = item->data(0, Qt::UserRole).toString().toStdString();
                const auto* part = workspace_.open_part(workspace_.active_document_id());
                if (part && !part->session.calculated_boundaries().empty()) {
                    const auto& cache = part->session.calculated_boundaries().back();
                    const auto& outputs = item->data(0, Qt::UserRole + 3).toString() == "part-body"
                        ? cache.body_inputs : cache.body_outputs;
                    const auto found = outputs.find(id);
                    if (found != outputs.end()) viewer_->confirm_container_component_wire(id, "body", found->second->mesh.edges, {});
                    else viewer_->clear_selection();
                } else viewer_->clear_selection();
            } else if (item->data(0, Qt::UserRole + 3).toString() ==
                           "part-result-body") {
                const auto* part = workspace_.open_part(workspace_.active_document_id());
                if (part && !part->session.document().body_history.bodies().empty() &&
                    !part->session.calculated_boundaries().empty())
                    viewer_->confirm_container_component_wire(part->session.document().document_id,
                        "document-result", part->session.calculated_boundaries().back().mesh.edges, {});
                else viewer_->confirm_result_body();
            } else if (item->data(0, Qt::UserRole + 3).toString() ==
                           "part-container-entity") {
                viewer_->confirm_container(
                    item->data(0, Qt::UserRole + 6).toString().toStdString());
            } else if (item->data(0, Qt::UserRole + 3).toString() ==
                           "construction-entity") {
                const auto semantic = item->data(0, Qt::UserRole + 5)
                    .toString().toStdString();
                viewer_->confirm_reference(
                    item->data(0, Qt::UserRole).toString().toStdString(), semantic,
                    item->data(0, Qt::UserRole + 1).toString().toStdString(),
                    semantic == "axis" ? zima::viewer::CandidateKind::Axis
                                       : zima::viewer::CandidateKind::Plane);
            } else if (item->data(0, Qt::UserRole + 3).toString() ==
                           "part-construction" ||
                       item->data(0, Qt::UserRole + 3).toString() ==
                           "assembly-construction") {
                return;
            } else {
                const auto path=item->data(0, Qt::UserRole + 1).toString().toStdString();
                if(!properties_dialog_&&!refreshing_scene_){assembly_dimension_path_.clear();update_assembly_dimension_visibility();}
                viewer_->confirm_occurrence(path);
                set_selected_component_origin(path);
            }
        };
    connect(tree_, &QTreeWidget::itemSelectionChanged, this,
        synchronize_tree_selection);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this,
        &AssemblyWorkspaceWindow::update_body_color_actions);
    connect(tree_, &QTreeWidget::itemClicked, this,
        [this, synchronize_tree_selection](QTreeWidgetItem* item, int) {
            if (local_origin_selection_active_ && item) {
                auto id = item->data(0, Qt::UserRole).toString().toStdString();
                if (id.ends_with(":origin")) id.resize(id.size() - 7);
                zima::viewer::ViewerCandidate candidate;
                candidate.owner_id = id;
                candidate.instance_path = item->data(0, Qt::UserRole + 1).toString().toStdString();
                const auto kind = item->data(0, Qt::UserRole + 3).toString();
                if (!candidate.instance_path.empty() && (kind.endsWith("-occurrence") ||
                    (kind == "document-origin" && !selectable_local_origin_container_ids_.contains(id))))
                    candidate.kind = zima::viewer::CandidateKind::Occurrence;
                toggle_local_origin_visibility(candidate);
                return;
            }
            if (item != nullptr &&
                item->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("assembly-insert-here")) {
                rebuild_insert_menu();
                insert_menu_->popup(QCursor::pos());
                return;
            }
            if (construction_reference_dialog_ != nullptr ||
                pending_construction_reference_index_ ||
                primitive_reference_dialog_ != nullptr ||
                pending_primitive_reference_index_ ||
                extrusion_target_dialog_ != nullptr ||
                edge_treatment_selection_ || shell_face_selection_active_ ||
                drill_point_face_selection_active_ ||
                sketch_external_reference_active_ || sketch_trim_active_ ||
                sketch_offset_dialog_ || sketch_mirror_active_ || sketch_coincident_active_ ||
                sketch_midpoint_active_ || sketch_symmetric_active_ ||
                sketch_concentric_active_ || sketch_tangent_active_ ||
                sketch_common_tangent_active_ ||
                sketch_segment_pair_active_ || sketch_point_dimension_active_ ||
                sketch_universal_dimension_active_) return;
            synchronize_tree_selection();
        });
    // Python parity: a Tree double-click has no object action. Editing,
    // activation and Properties remain explicit context-menu commands.
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this,
        [this, synchronize_tree_selection](const QPoint& position) {
            if (construction_reference_dialog_ != nullptr ||
                pending_construction_reference_index_ ||
                primitive_reference_dialog_ != nullptr ||
                pending_primitive_reference_index_) return;
            auto* item = tree_->itemAt(position);
            if(measurement_context_menu(item,position)||section_context_menu(item,position))return;
            if (!tree_item_context_menu_enabled(item)) return;
            const auto step_kind = item->data(0, Qt::UserRole + 3).toString();
            if(step_kind=="template-image"&&!properties_dialog_) {
                const auto id=item->data(0,Qt::UserRole).toString().toStdString();QMenu menu(this);
                auto* properties=menu.addAction(tr("Vlastnosti…"));auto* remove=menu.addAction(tr("Odstranit"));
                const auto* chosen=menu.exec(tree_->viewport()->mapToGlobal(position));
                if(chosen==properties)show_template_image_properties(id);else if(chosen==remove)remove_template_image(id);return;
            }
            if(step_kind=="template-repeat-region"&&!properties_dialog_) {
                const auto id=item->data(0,Qt::UserRole).toString().toStdString();QMenu menu(this);
                auto* properties=menu.addAction(tr("Vlastnosti…"));auto* remove=menu.addAction(tr("Odstranit"));
                const auto* chosen=menu.exec(tree_->viewport()->mapToGlobal(position));
                if(chosen==properties)show_template_region_properties(id);else if(chosen==remove)remove_template_region(id);return;
            }
            if (step_kind == "part-result-body" && item->parent() == nullptr) {
                if (properties_dialog_ || !active_sketch_id_.empty() ||
                    workspace_.open_part(workspace_.displayed_document_id()) == nullptr) return;
                QMenu menu(this);
                menu.setObjectName("partActivationMenu");
                auto* activate = menu.addAction(tr("Aktivní"));
                activate->setObjectName("activatePartAction");
                auto* create_body = menu.addAction(resource_icon("result-body"), tr("Vytvořit těleso"));
                create_body->setObjectName("createBodyFromPartAction");
                auto* parameters = menu.addAction(resource_icon("parameters"), tr("Parametry…"));
                parameters->setObjectName("treeDocumentParametersAction");
                const auto selected=menu.exec(tree_->viewport()->mapToGlobal(position));
                if (selected==parameters) edit_parameters_for_document(workspace_.displayed_document_id());
                else if (selected==activate) activate_body({});
                else if (selected==create_body) { activate_body({});show_body_properties(); }
                return;
            }
            if (step_kind=="drawing-view") {
                if (properties_dialog_) return;
                drawing_workspace_->select_view(item->data(0,Qt::UserRole).toString().toStdString());
                QMenu menu(this);
                for(const auto* id:{"editDrawingViewAction","projectDrawingViewAction","deleteDrawingViewAction"})
                    menu.addAction(drawing_workspace_->findChild<QAction*>(id));
                menu.exec(tree_->viewport()->mapToGlobal(position));
                return;
            }
            if (item->parent() == nullptr) {
                if (properties_dialog_) return;
                QMenu menu(this);
                auto* parameters = menu.addAction(resource_icon("parameters"), tr("Parametry…"));
                parameters->setObjectName("treeDocumentParametersAction");
                if (menu.exec(tree_->viewport()->mapToGlobal(position)) == parameters)
                    edit_parameters_for_document(workspace_.displayed_document_id());
                return;
            }
            if (step_kind == "part-body" || step_kind == "part-body-boolean") {
                if (properties_dialog_) return;
                const auto id = item->data(0, Qt::UserRole).toString().toStdString();
                const auto* part = workspace_.open_part(workspace_.active_document_id());
                if (!part) return;
                QMenu menu(this);
                menu.setObjectName("partActivationMenu");
                auto* edit = menu.addAction(tr("Vlastnosti"));
                const auto* body=part->session.document().body_history.find(id);
                auto* dimensions=body && !body->derived_copy ? menu.addAction(tr("Edit")) : nullptr;
                if(dimensions)dimensions->setObjectName("editBodyDimensionsAction");
                auto* source_properties=body&&body->derived_copy ? menu.addAction(tr("Vlastnosti zdroje")) : nullptr;
                auto* visibility=body ? menu.addAction(body->visible?tr("Skrýt"):tr("Zobrazit")) : nullptr;
                QAction* activate = step_kind == "part-body" && !(body&&body->derived_copy) ? menu.addAction(tr("Aktivní")) : nullptr;
                if (activate) activate->setObjectName("activateBodyAction");
                auto* remove=part->session.document().body_history.active_body_id().empty()?menu.addAction(tr("Smazat")):nullptr;
                if(remove)remove->setObjectName("deleteBodyAction");
                auto* document = menu.addAction(tr("Zpět do dílu"));
                document->setObjectName("activatePartAction");
                menu.addSeparator();
                auto* before = menu.addAction(tr("Vložit před"));
                auto* after = menu.addAction(tr("Vložit za"));
                const auto selected = menu.exec(tree_->viewport()->mapToGlobal(position));
                if(remove&&selected==remove)delete_part_object(id,QStringLiteral("part-body"));
                else if (selected == edit) show_tree_item_properties(item);
                else if(dimensions&&selected==dimensions)show_parameter_dimensions(id);
                else if(visibility&&selected==visibility) {
                    auto* current=workspace_.open_part(workspace_.active_document_id());auto next=current->session.document();auto value=*next.body_history.find(id);
                    value.visible=!value.visible;next.body_history.update_body(value);current->session.commit(std::move(next),current->session.calculated_boundaries());
                    preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
                }
                else if (source_properties && selected == source_properties) show_derived_source_properties(id);
                else if (activate && selected == activate) activate_body(id);
                else if (selected == document) activate_body({});
                else if (selected == before || selected == after) {
                    auto* active = workspace_.open_part(workspace_.active_document_id());
                    auto next = active->session.document();
                    const auto& order = next.body_history.order();
                    const auto position = static_cast<std::size_t>(std::distance(order.begin(), std::ranges::find(order, id)));
                    next.body_history.set_insertion_cursor(position + (selected == after ? 1 : 0));
                    next.body_history.activate({});
                    active->session.commit(std::move(next), active->session.calculated_boundaries());
                    preserve_view_on_refresh_ = true; refresh_tabs(); refresh_scene();
                }
                return;
            }
            const auto delete_tree_selection = [this, item] {
                struct Target { std::string id; QString kind; };
                std::vector<Target> targets;
                const auto append = [&](QTreeWidgetItem* selected_item) {
                    if (!tree_item_context_menu_enabled(selected_item)) return;
                    const auto tree_kind = selected_item->data(
                        0, Qt::UserRole + 3).toString();
                    QString delete_kind;
                    if (tree_kind == QStringLiteral("part-container"))
                        delete_kind = QStringLiteral("container");
                    else if (tree_kind == QStringLiteral("part-construction") ||
                             tree_kind == QStringLiteral("assembly-construction") ||
                             tree_kind == QStringLiteral("assembly-cut") ||
                             tree_kind == QStringLiteral("assembly-sketch"))
                        delete_kind = tree_kind;
                    else if (tree_kind == QStringLiteral("part-sketch"))
                        delete_kind = QStringLiteral("sketch");
                    else return;
                    const auto id = selected_item->data(
                        0, Qt::UserRole).toString().toStdString();
                    if (id.empty() || std::any_of(targets.begin(), targets.end(),
                            [&](const auto& target) {
                                return target.id == id && target.kind == delete_kind;
                            })) return;
                    targets.push_back({id, delete_kind});
                };
                for (auto* selected_item : tree_->selectedItems()) append(selected_item);
                if (targets.empty()) append(item);
                if (targets.empty()) return;
                if (QMessageBox::question(this, tr("Odstranit objekty"),
                        targets.size() == 1
                            ? tr("Opravdu chcete vybraný objekt odstranit?")
                            : tr("Opravdu chcete odstranit všech %1 vybraných objektů?")
                                  .arg(targets.size()),
                        QMessageBox::Yes | QMessageBox::No,
                        QMessageBox::No) != QMessageBox::Yes) return;
                // Tree order is history order. Delete downstream objects
                // first so a selected dependent does not block deletion of
                // its also-selected source.
                std::reverse(targets.begin(), targets.end());
                for (const auto& target : targets) {
                    delete_part_object(target.id, target.kind, false);
                }
            };
            if (item->data(0, Qt::UserRole + 3).toString() ==
                    "sketch-geometry") {
                const auto geometry_id =
                    item->data(0, Qt::UserRole).toString().toStdString();
                const auto* sketch = active_sketch();
                if (sketch == nullptr) return;
                std::optional<bool> construction;
                const auto inspect = [&](const auto& values) {
                    const auto found = std::find_if(values.begin(), values.end(),
                        [&](const auto& value) { return value.id == geometry_id; });
                    if (found != values.end()) construction = found->construction;
                };
                inspect(sketch->segments); inspect(sketch->circles);
                inspect(sketch->arcs); inspect(sketch->ellipses);
                inspect(sketch->elliptical_arcs); inspect(sketch->bsplines);
                QMenu menu(this);
                QAction* role{};
                if (construction) {
                    role = menu.addAction(*construction
                        ? tr("Převést na obrys profilu")
                        : tr("Převést na pomocnou geometrii"));
                }
                auto* remove = menu.addAction(tr("Odstranit"));
                const auto* selected = menu.exec(
                    tree_->viewport()->mapToGlobal(position));
                if (selected == role) {
                    set_active_sketch_geometry_construction(
                        geometry_id, !*construction);
                } else if (selected == remove) {
                    item->setSelected(true);
                    tree_->setCurrentItem(item);
                    synchronize_tree_selection();
                    static_cast<void>(delete_selected_sketch_geometry());
                }
            } else if (item->data(0,Qt::UserRole+3).toString()=="part-treatment-component") {
                if (properties_dialog_!=nullptr) return;
                auto* part=workspace_.open_part(workspace_.active_document_id());
                const auto id=item->data(0,Qt::UserRole).toString().toStdString();
                const auto* container=part ? part->session.document().find_container(id) : nullptr;
                if (!container) return;
                const auto route=static_cast<std::size_t>(item->data(0,Qt::UserRole+6).toInt());
                const int segment=item->data(0,Qt::UserRole+7).toInt();
                const auto member=segment<0 ? std::nullopt : std::optional<std::size_t>{segment};
                const auto kind=container->feature_kind;
                tree_->setCurrentItem(item);
                synchronize_tree_selection();
                QMenu menu(this);
                menu.setObjectName("treatmentComponentMenu");
                auto* edit=menu.addAction(tr("Edit"));
                auto* properties=menu.addAction(tr("Vlastnosti…"));
                auto* remove=menu.addAction(tr("Delete"));
                remove->setObjectName("deleteTreatmentComponent");
                const auto* selected=menu.exec(tree_->viewport()->mapToGlobal(position));
                if (selected==edit) show_parameter_dimensions(id);
                else if (selected==properties) show_primitive_properties(kind,id);
                else if (selected==remove) {
                    try {
                        const auto boundary=part->session.rollback_boundary(id);
                        if (!boundary || !boundary->input_body)
                            throw std::runtime_error("Chybí uložený vstup operace. Regenerujte Part.");
                        auto next=part->session.document();
                        auto* target=next.find_container(id);
                        remove_treatment_selection(target->edge_treatment,route,member,boundary->input_body->mesh);
                        if (target->edge_treatment.routes.empty()) {
                            delete_part_object(id,QStringLiteral("container"),false);
                            return;
                        }
                        const auto& previous=part->session.calculated_boundaries();
                        auto calculated=calculate_part_with_resolved_references(next,&previous);
                        part->session.commit(std::move(next),std::move(calculated));
                        viewer_->clear_selection();
                        preserve_view_on_refresh_=true;
                        refresh_tabs();refresh_scene();
                    } catch (const std::exception& error) {
                        QMessageBox::warning(this,tr("Trasu nelze změnit"),error.what());
                    }
                }
            } else if(item->data(0,Qt::UserRole+3).toString()=="part-helical-sketch" || item->data(0,Qt::UserRole+3).toString()=="part-sweep2d-sketch") {
                if(properties_dialog_)return;QMenu menu(this);auto* edit=menu.addAction(tr("Edit"));
                if(menu.exec(tree_->viewport()->mapToGlobal(position))==edit)show_tree_item_properties(item);
            } else if (item->data(0,Qt::UserRole+3).toString()=="part-opening-component") {
                if (properties_dialog_ != nullptr) return;
                const auto id=item->data(0,Qt::UserRole).toString().toStdString();
                const auto role=item->data(0,Qt::UserRole+5).toString().toStdString();
                auto* part=workspace_.open_part(workspace_.active_document_id());
                if (!part || !part->session.document().find_container(id)) return;
                tree_->setCurrentItem(item);
                synchronize_tree_selection();
                QMenu menu(this);
                menu.setObjectName("openingComponentMenu");
                auto* edit=menu.addAction(tr("Edit"));
                edit->setObjectName("editOpeningComponent");
                auto* properties=menu.addAction(tr("Vlastnosti…"));
                QAction* remove=nullptr;
                if (role!="bore") {
                    remove=menu.addAction(tr("Delete"));
                    remove->setObjectName("deleteOpeningComponent");
                }
                const auto* selected=menu.exec(tree_->viewport()->mapToGlobal(position));
                if (selected==edit) show_parameter_dimensions(id,role);
                else if (selected==properties) show_primitive_properties(zima::document::FeatureKind::Thread,id);
                else if (remove && selected==remove) {
                    auto next=part->session.document();
                    auto* opening=next.find_container(id);
                    if (!opening || !disable_opening_component(*opening,role)) return;
                    try {
                        const auto& previous=part->session.calculated_boundaries();
                        auto calculated=calculate_part_with_resolved_references(next,&previous);
                        part->session.commit(std::move(next),std::move(calculated));
                        viewer_->clear_selection();
                        preserve_view_on_refresh_=true;
                        refresh_tabs();
                        refresh_scene();
                    } catch (const std::exception& error) {
                        QMessageBox::critical(this,tr("Změna otvoru selhala"),error.what());
                    }
                }
            } else if (item->data(0, Qt::UserRole + 3).toString() ==
                    "part-hole-component") {
                const auto id = item->data(
                    0, Qt::UserRole).toString().toStdString();
                const auto role = item->data(0, Qt::UserRole + 5).toString();
                auto* part = workspace_.open_part(
                    workspace_.active_document_id());
                const auto* container = part == nullptr
                    ? nullptr : part->session.document().find_container(id);
                if (container == nullptr || container->feature_kind !=
                        zima::document::FeatureKind::Hole) return;
                QMenu menu(this);
                auto* properties = menu.addAction(tr("Vlastnosti…"));
                QAction* remove_thread = role == QStringLiteral("thread")
                    ? menu.addAction(tr("Odstranit závit")) : nullptr;
                const auto* selected = menu.exec(
                    tree_->viewport()->mapToGlobal(position));
                if (selected == properties) {
                    show_primitive_properties(
                        zima::document::FeatureKind::Hole, id);
                } else if (remove_thread != nullptr &&
                           selected == remove_thread) {
                    auto next = part->session.document();
                    auto* edited = next.find_container(id);
                    if (edited == nullptr) return;
                    edited->hole.thread_enabled = false;
                    edited->hole.type = zima::document::HoleType::Plain;
                    part->session.commit(std::move(next),
                        part->session.calculated_boundaries());
                    preserve_view_on_refresh_ = true;
                    refresh_tabs();
                    refresh_scene();
                }
            } else if (item->data(0, Qt::UserRole + 3).toString() == "assembly-cut") {
                const auto id = item->data(0, Qt::UserRole).toString().toStdString();
                auto* assembly =
                    workspace_.open_assembly(workspace_.active_document_id());
                const auto* cut = assembly == nullptr
                    ? nullptr : assembly->session.document().find_cut(id);
                if (cut == nullptr) return;
                QMenu menu(this);
                auto* edit = menu.addAction(tr("Upravit"));
                auto* properties = menu.addAction(tr("Vlastnosti…"));
                auto* suppress = menu.addAction(cut->definition.suppressed
                    ? tr("Obnovit") : tr("Potlačit"));
                auto* move_up = menu.addAction(tr("Posunout výše"));
                auto* move_down = menu.addAction(tr("Posunout níže"));
                auto* remove = menu.addAction(tr("Odstranit"));
                const auto* selected = menu.exec(
                    tree_->viewport()->mapToGlobal(position));
                if (selected == edit) {
                    show_parameter_dimensions(id);
                } else if (selected == properties) {
                    show_primitive_properties(cut->definition.feature_kind, id);
                } else if (selected == suppress) {
                    const auto assembly_id = assembly->session.document().document_id;
                    workspace_.regenerate_assembly_from_open_dependencies(assembly_id);
                    assembly = workspace_.open_assembly(assembly_id);
                    if (assembly == nullptr) return;
                    auto next = assembly->session.document();
                    if (auto* value = next.find_cut(id)) {
                        value->definition.suppressed = !value->definition.suppressed;
                        calculate_assembly_cuts(next);
                        assembly->session.commit(std::move(next));
                        refresh_tabs();
                        refresh_scene();
                    }
                } else if (selected == move_up || selected == move_down) {
                    const auto assembly_id = assembly->session.document().document_id;
                    workspace_.regenerate_assembly_from_open_dependencies(assembly_id);
                    assembly = workspace_.open_assembly(assembly_id);
                    if (assembly == nullptr) return;
                    auto next = assembly->session.document();
                    const auto found = std::find_if(
                        next.cuts.begin(), next.cuts.end(), [&](const auto& value) {
                            return value.definition.id == id;
                        });
                    if (found == next.cuts.end()) return;
                    const auto index = static_cast<std::size_t>(
                        std::distance(next.cuts.begin(), found));
                    const bool upward = selected == move_up;
                    if ((upward && index == 0) ||
                        (!upward && index + 1 >= next.cuts.size())) return;
                    const auto target = upward ? index - 1 : index + 1;
                    std::swap(next.cuts[index], next.cuts[target]);
                    calculate_assembly_cuts(next);
                    assembly->session.commit(std::move(next));
                    refresh_tabs();
                    refresh_scene();
                } else if (selected == remove) {
                    delete_tree_selection();
                }
            } else if (item->data(0, Qt::UserRole + 3).toString() == "part-container") {
                const std::string id = item->data(0, Qt::UserRole).toString().toStdString();
                const auto* part = workspace_.open_part(workspace_.active_document_id());
                const auto* container = part == nullptr
                    ? nullptr : part->session.document().find_container(id);
                if (container == nullptr) return;
                QMenu menu(this);
                auto* edit = menu.addAction(tr("Upravit"));
                auto* properties = menu.addAction(tr("Vlastnosti…"));
                QAction* transform_extrusion{};
                QAction* transform_revolution{};
                if (container->feature_kind ==
                        zima::document::FeatureKind::Sketch) {
                    menu.addSeparator();
                    transform_extrusion = menu.addAction(
                        resource_icon("protrusion"), tr("Vytažení"));
                    transform_revolution = menu.addAction(
                        resource_icon("revolve"), tr("Rotace"));
                    menu.addSeparator();
                }
                auto* suppress = menu.addAction(container->suppressed
                    ? tr("Obnovit") : tr("Potlačit"));
                auto* move_up = menu.addAction(tr("Posunout výše"));
                auto* move_down = menu.addAction(tr("Posunout níže"));
                auto* remove = menu.addAction(tr("Odstranit"));
                const auto* selected = menu.exec(
                    tree_->viewport()->mapToGlobal(position));
                if (selected == edit) {
                    show_parameter_dimensions(id);
                } else if (selected == properties) {
                    if (container->feature_kind ==
                            zima::document::FeatureKind::Sketch) {
                        const auto sketch = std::find_if(
                            part->session.document().sketches.begin(),
                            part->session.document().sketches.end(),
                            [&](const auto& value) {
                                return value.owner_container_id == container->id;
                            });
                        if (sketch != part->session.document().sketches.end()) {
                            show_sketch_properties(sketch->id);
                        }
                    } else {
                        show_primitive_properties(container->feature_kind, id);
                    }
                } else if (selected == transform_extrusion) {
                    transform_sketch_container(
                        id, zima::document::FeatureKind::Extrusion);
                } else if (selected == transform_revolution) {
                    transform_sketch_container(
                        id, zima::document::FeatureKind::Revolution);
                } else if (selected == suppress) {
                    toggle_part_container_suppressed(id);
                } else if (selected == move_up) {
                    move_part_container(id, -1);
                } else if (selected == move_down) {
                    move_part_container(id, 1);
                } else if (selected == remove) {
                    delete_tree_selection();
                }
            } else if (item->data(0, Qt::UserRole + 3).toString() ==
                           "part-construction" ||
                       item->data(0, Qt::UserRole + 3).toString() ==
                           "assembly-construction") {
                const std::string id = item->data(0, Qt::UserRole).toString().toStdString();
                const auto* part = workspace_.open_part(workspace_.active_document_id());
                const auto* assembly =
                    workspace_.open_assembly(workspace_.active_document_id());
                const auto* object = part != nullptr
                    ? part->session.document().find_construction(id)
                    : assembly != nullptr
                        ? assembly->session.document().find_construction(id) : nullptr;
                if (object == nullptr) return;
                QMenu menu(this);
                auto* edit = menu.addAction(tr("Upravit"));
                auto* properties = menu.addAction(tr("Vlastnosti…"));
                QAction* suppress{};
                QAction* move_up{};
                QAction* move_down{};
                if (part != nullptr) {
                    suppress = menu.addAction(object->suppressed
                        ? tr("Obnovit") : tr("Potlačit"));
                    move_up = menu.addAction(tr("Posunout výše"));
                    move_down = menu.addAction(tr("Posunout níže"));
                }
                auto* remove = menu.addAction(tr("Odstranit"));
                const auto* selected = menu.exec(
                    tree_->viewport()->mapToGlobal(position));
                if (selected == edit) {
                    show_parameter_dimensions(id);
                } else if (selected == properties) {
                    show_construction_properties(object->kind, id);
                } else if (selected == suppress) {
                    toggle_part_container_suppressed(id);
                } else if (selected == move_up) {
                    move_part_container(id, -1);
                } else if (selected == move_down) {
                    move_part_container(id, 1);
                } else if (selected == remove) {
                    delete_tree_selection();
                }
            } else if (item->data(0, Qt::UserRole + 3).toString() == "part-sketch" ||
                       item->data(0, Qt::UserRole + 3).toString() == "assembly-sketch") {
                const auto id = item->data(0, Qt::UserRole).toString().toStdString();
                const auto* active_part =
                    workspace_.open_part(workspace_.active_document_id());
                const zima::sketcher::Sketch* part_sketch{};
                if (active_part != nullptr) {
                    const auto found = std::find_if(
                        active_part->session.document().sketches.begin(),
                        active_part->session.document().sketches.end(),
                        [&](const auto& value) { return value.id == id; });
                    if (found != active_part->session.document().sketches.end()) {
                        part_sketch = &*found;
                    }
                }
                QMenu menu(this);
                auto* edit = menu.addAction(tr("Upravit"));
                auto* properties = menu.addAction(tr("Vlastnosti…"));
                QAction* suppress{};
                QAction* move_up{};
                QAction* move_down{};
                if (part_sketch != nullptr) {
                    suppress = menu.addAction(part_sketch->suppressed
                        ? tr("Obnovit") : tr("Potlačit"));
                    move_up = menu.addAction(tr("Posunout výše"));
                    move_down = menu.addAction(tr("Posunout níže"));
                }
                auto* remove = menu.addAction(tr("Odstranit"));
                const auto* selected = menu.exec(
                    tree_->viewport()->mapToGlobal(position));
                if (selected == edit) show_parameter_dimensions(id);
                else if (selected == properties) show_sketch_properties(id);
                else if (selected == suppress) toggle_part_container_suppressed(id);
                else if (selected == move_up) move_part_container(id, -1);
                else if (selected == move_down) move_part_container(id, 1);
                else if (selected == remove) {
                    delete_tree_selection();
                }
            } else if (item->data(0, Qt::UserRole + 3).toString() ==
                           "sketch-geometry" ||
                       item->data(0, Qt::UserRole + 3).toString() ==
                           "sketch-external-reference") {
                const auto sketch_id =
                    item->data(0, Qt::UserRole + 4).toString().toStdString();
                const auto geometry_id =
                    item->data(0, Qt::UserRole).toString().toStdString();
                if (sketch_id != active_sketch_id_) return;
                const auto* sketch = active_sketch();
                if (sketch == nullptr) return;
                clear_selected_sketch_geometry();
                std::optional<bool> construction;
                bool text_geometry = false;
                bool bspline_geometry = false;
                bool external_reference = false;
                const auto point = std::find_if(sketch->points.begin(), sketch->points.end(),
                    [&](const auto& value) { return value.id == geometry_id; });
                if (point != sketch->points.end()) {
                    construction = point->construction;
                    selected_sketch_point_id_ = geometry_id;
                }
                const auto inspect = [&](const auto& values, std::string& selected) {
                    const auto found = std::find_if(values.begin(), values.end(),
                        [&](const auto& value) { return value.id == geometry_id; });
                    if (found != values.end()) {
                        construction = found->construction;
                        selected = geometry_id;
                        return true;
                    }
                    return false;
                };
                if (!construction) {
                    static_cast<void>(inspect(sketch->segments, selected_sketch_segment_id_));
                    static_cast<void>(inspect(sketch->circles, selected_sketch_circle_id_));
                    static_cast<void>(inspect(sketch->arcs, selected_sketch_arc_id_));
                    static_cast<void>(inspect(sketch->ellipses, selected_sketch_ellipse_id_));
                    static_cast<void>(inspect(
                        sketch->elliptical_arcs, selected_sketch_elliptical_arc_id_));
                    bspline_geometry = inspect(sketch->bsplines, selected_sketch_bspline_id_);
                }
                if (!construction) {
                    text_geometry = std::any_of(sketch->texts.begin(), sketch->texts.end(),
                        [&](const auto& value) { return value.id == geometry_id; });
                    if (text_geometry) selected_sketch_text_id_ = geometry_id;
                    external_reference = std::any_of(
                        sketch->external_references.begin(), sketch->external_references.end(),
                        [&](const auto& value) { return value.id == geometry_id; });
                    if (external_reference) {
                        selected_sketch_external_reference_id_ = geometry_id;
                    }
                }
                if (!construction && !text_geometry && !external_reference) return;
                QMenu menu(this);
                QAction* properties{};
                QAction* role{};
                if (text_geometry || bspline_geometry) {
                    properties = menu.addAction(tr("Vlastnosti…"));
                }
                if (construction) {
                    role = menu.addAction(*construction
                        ? tr("Převést na obrys profilu")
                        : tr("Převést na pomocnou geometrii"));
                }
                auto* remove = menu.addAction(tr("Odstranit"));
                const auto* selected = menu.exec(
                    tree_->viewport()->mapToGlobal(position));
                if (selected == properties && text_geometry) {
                    show_sketch_text_properties(sketch_id, geometry_id);
                } else if (selected == properties && bspline_geometry) {
                    show_sketch_bspline_properties(sketch_id, geometry_id);
                } else if (selected == role) {
                    set_active_sketch_geometry_construction(
                        geometry_id, !*construction);
                } else if (selected == remove) {
                    static_cast<void>(delete_selected_sketch_geometry());
                }
            } else if (item->data(0, Qt::UserRole + 3).toString() ==
                       "part-sketch-dimension") {
                const auto sketch_id =
                    item->data(0, Qt::UserRole + 4).toString().toStdString();
                const auto dimension_id =
                    item->data(0, Qt::UserRole).toString().toStdString();
                QMenu menu(this);
                auto* properties = menu.addAction(tr("Vlastnosti…"));
                auto* remove = menu.addAction(tr("Odstranit"));
                const auto* selected = menu.exec(tree_->viewport()->mapToGlobal(position));
                if (selected == properties) {
                    show_sketch_dimension_properties(sketch_id, dimension_id);
                } else if (selected == remove) {
                    remove_sketch_relation(sketch_id, dimension_id, true);
                }
            } else if (item->data(0, Qt::UserRole + 3).toString() ==
                       "part-sketch-constraint") {
                const auto sketch_id =
                    item->data(0, Qt::UserRole + 4).toString().toStdString();
                const auto constraint_id =
                    item->data(0, Qt::UserRole).toString().toStdString();
                QMenu menu(this);
                auto* remove = menu.addAction(tr("Odstranit"));
                if (menu.exec(tree_->viewport()->mapToGlobal(position)) == remove) {
                    remove_sketch_relation(sketch_id, constraint_id, false);
                }
            } else {
                show_component_context_menu(
                    item->data(0, Qt::UserRole + 1).toString().toStdString(),
                    tree_->viewport()->mapToGlobal(position));
            }
        });
}

} // namespace zima::app
