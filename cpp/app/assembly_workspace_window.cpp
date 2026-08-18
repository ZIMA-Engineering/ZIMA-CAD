#include "assembly_workspace_window.hpp"
#include "component_properties_dialog.hpp"
#include "mate_properties_dialog.hpp"
#include "primitive_properties_dialog.hpp"
#include "sketch_properties_dialog.hpp"
#include "sketch_bspline_properties_dialog.hpp"
#include "sketch_dimension_properties_dialog.hpp"

#include <zima/viewer/mesh_view.hpp>
#include <zima/interchange/interchange.hpp>

#include <QAction>
#include <QBrush>
#include <QColor>
#include <QFileDialog>
#include <QFont>
#include <QLabel>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QMenu>
#include <QSplitter>
#include <QTabBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <set>
#include <type_traits>

namespace zima::app {
namespace {

void append_mesh(zima::kernel::ViewerMesh& target, zima::kernel::ViewerMesh source) {
    const auto vertex_offset = static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(), source.vertices.begin(), source.vertices.end());
    for (const auto index : source.triangles) target.triangles.push_back(index + vertex_offset);
    target.triangle_references.insert(target.triangle_references.end(),
        source.triangle_references.begin(), source.triangle_references.end());
    target.edges.insert(target.edges.end(), source.edges.begin(), source.edges.end());
    target.points.insert(target.points.end(), source.points.begin(), source.points.end());
    target.axes.insert(target.axes.end(), source.axes.begin(), source.axes.end());
    target.dimensions.insert(target.dimensions.end(),
        source.dimensions.begin(), source.dimensions.end());
    auto& target_references = target.original_references;
    auto& source_references = source.original_references;
    const auto reference_vertex_offset =
        static_cast<std::uint32_t>(target_references.vertices.size());
    target_references.vertices.insert(target_references.vertices.end(),
        source_references.vertices.begin(), source_references.vertices.end());
    for (const auto index : source_references.triangles) {
        target_references.triangles.push_back(index + reference_vertex_offset);
    }
    target_references.triangle_references.insert(
        target_references.triangle_references.end(),
        source_references.triangle_references.begin(),
        source_references.triangle_references.end());
    target_references.edges.insert(target_references.edges.end(),
        source_references.edges.begin(), source_references.edges.end());
    target_references.points.insert(target_references.points.end(),
        source_references.points.begin(), source_references.points.end());
    target_references.axes.insert(target_references.axes.end(),
        source_references.axes.begin(), source_references.axes.end());
}

}  // namespace

AssemblyWorkspaceWindow::AssemblyWorkspaceWindow() {
    setWindowTitle(tr("ZIMA-CAD C++ – Workspace sestavy"));
    resize(1180, 760);
    create_actions();
    create_layout();
    new_assembly();
}

void AssemblyWorkspaceWindow::create_actions() {
    auto* file = menuBar()->addMenu(tr("Soubor"));
    file->addAction(tr("Nový Part"), this, [this] { new_part(); });
    file->addAction(tr("Nová sestava"), this, [this] { new_assembly(); });
    file->addAction(tr("Otevřít Part…"), this, [this] { open_part(); });
    file->addAction(tr("Otevřít sestavu…"), this, [this] { open_assembly(); });
    save_action_ = file->addAction(tr("Uložit…"), this,
        [this] { save_active_document(); });
    file->addSeparator();
    file->addAction(tr("Importovat…"), this, [this] { import_file(); });
    file->addAction(tr("Exportovat…"), this, [this] { export_file(); });
    auto* edit = menuBar()->addMenu(tr("Úpravy"));
    undo_action_ = edit->addAction(tr("Zpět"), this, [this] { undo(); });
    redo_action_ = edit->addAction(tr("Znovu"), this, [this] { redo(); });
    auto* modeling = menuBar()->addMenu(tr("Modelování"));
    box_action_ = modeling->addAction(tr("Kvádr…"), this,
        [this] { show_primitive_properties(zima::document::FeatureKind::Box); });
    cylinder_action_ = modeling->addAction(tr("Válec…"), this,
        [this] { show_primitive_properties(zima::document::FeatureKind::Cylinder); });
    extrusion_action_ = modeling->addAction(tr("Vytažení…"), this,
        [this] { show_primitive_properties(zima::document::FeatureKind::Extrusion); });
    revolution_action_ = modeling->addAction(tr("Rotace…"), this,
        [this] { show_primitive_properties(zima::document::FeatureKind::Revolution); });
    fillet_action_ = modeling->addAction(tr("Zaoblení…"), this,
        [this] { start_edge_treatment(zima::document::FeatureKind::Fillet); });
    chamfer_action_ = modeling->addAction(tr("Sražení…"), this,
        [this] { start_edge_treatment(zima::document::FeatureKind::Chamfer); });
    sketch_action_ = modeling->addAction(tr("Skica…"), this,
        [this] { show_sketch_properties(); });
    sketch_segment_action_ = modeling->addAction(tr("Úsečka skici"), this,
        [this] { start_sketch_segment(); });
    sketch_rectangle_action_ = modeling->addAction(tr("Obdélník skici"), this,
        [this] { start_sketch_rectangle(); });
    sketch_circle_action_ = modeling->addAction(tr("Kružnice skici"), this,
        [this] { start_sketch_circle(); });
    sketch_arc_action_ = modeling->addAction(tr("Oblouk skici"), this,
        [this] { start_sketch_arc(); });
    sketch_ellipse_action_ = modeling->addAction(tr("Elipsa skici"), this,
        [this] { start_sketch_ellipse(); });
    sketch_bspline_action_ = modeling->addAction(tr("B-spline skici"), this,
        [this] { start_sketch_bspline(); });
    sketch_horizontal_action_ = modeling->addAction(tr("Vodorovná úsečka"), this,
        [this] { constrain_selected_segment(zima::sketcher::ConstraintKind::Horizontal); });
    sketch_vertical_action_ = modeling->addAction(tr("Svislá úsečka"), this,
        [this] { constrain_selected_segment(zima::sketcher::ConstraintKind::Vertical); });
    sketch_coincident_action_ = modeling->addAction(tr("Shodnost bodů"), this,
        [this] { start_sketch_coincident(); });
    sketch_parallel_action_ = modeling->addAction(tr("Rovnoběžnost úseček"), this,
        [this] { start_sketch_segment_pair(
            zima::sketcher::ConstraintKind::Parallel); });
    sketch_perpendicular_action_ = modeling->addAction(tr("Kolmost úseček"), this,
        [this] { start_sketch_segment_pair(
            zima::sketcher::ConstraintKind::Perpendicular); });
    sketch_equal_length_action_ = modeling->addAction(tr("Stejná délka úseček"), this,
        [this] { start_sketch_segment_pair(
            zima::sketcher::ConstraintKind::EqualLength); });
    sketch_fix_point_action_ = modeling->addAction(tr("Fixovat/uvolnit bod"), this,
        [this] { toggle_selected_sketch_point_fixed(); });
    sketch_dimension_action_ = modeling->addAction(tr("Kóta délky úsečky…"), this,
        [this] { show_sketch_dimension_properties(active_sketch_id_); });
    sketch_dimension_x_action_ = modeling->addAction(tr("Vodorovná kóta úsečky…"), this,
        [this] { show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::DistanceX); });
    sketch_dimension_y_action_ = modeling->addAction(tr("Svislá kóta úsečky…"), this,
        [this] { show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::DistanceY); });
    sketch_angle_dimension_action_ = modeling->addAction(tr("Úhlová kóta úsečky…"), this,
        [this] { show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::Angle); });
    sketch_radius_dimension_action_ = modeling->addAction(tr("Kóta poloměru…"), this,
        [this] { show_sketch_dimension_properties(active_sketch_id_); });
    sketch_diameter_dimension_action_ = modeling->addAction(tr("Kóta průměru…"), this,
        [this] { show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::Diameter); });
    sketch_ellipse_major_dimension_action_ = modeling->addAction(
        tr("Kóta hlavní poloosy elipsy…"), this,
        [this] { show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::EllipseMajorRadius); });
    sketch_ellipse_minor_dimension_action_ = modeling->addAction(
        tr("Kóta vedlejší poloosy elipsy…"), this,
        [this] { show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::EllipseMinorRadius); });
    sketch_ellipse_rotation_dimension_action_ = modeling->addAction(
        tr("Kóta natočení elipsy…"), this,
        [this] { show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::EllipseRotation); });
    regenerate_part_action_ = modeling->addAction(tr("Regenerovat Part"), this,
        [this] { regenerate_active_part(); });
    auto* assembly = menuBar()->addMenu(tr("Sestava"));
    insert_action_ = assembly->addAction(tr("Vložit aktivní dokument"), this,
        [this] { insert_active_component(); });
    regenerate_action_ = assembly->addAction(tr("Regenerovat"), this,
        [this] { regenerate_assembly(); });
    plane_mate_action_ = assembly->addAction(tr("Vazba plocha–plocha…"), this,
        [this] { start_plane_mate(); });
    axis_mate_action_ = assembly->addAction(tr("Vazba osa–osa…"), this,
        [this] { start_axis_mate(); });
}

void AssemblyWorkspaceWindow::create_layout() {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    tabs_ = new QTabBar(central);
    tabs_->setExpanding(false);
    layout->addWidget(tabs_);
    auto* splitter = new QSplitter(central);
    auto* left = new QWidget(splitter);
    auto* left_layout = new QVBoxLayout(left);
    tree_ = new QTreeWidget(left);
    tree_->setHeaderHidden(true);
    left_layout->addWidget(tree_, 1);
    state_ = new QLabel(left);
    state_->setWordWrap(true);
    left_layout->addWidget(state_);
    viewer_ = new zima::viewer::MeshView(splitter);
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Occurrence});
    viewer_->set_confirmation_callback([this](const auto& candidate) {
        if (edge_treatment_selection_) {
            accept_edge_treatment(candidate);
            return;
        }
        if (mate_selection_active_) {
            accept_mate_reference(candidate);
            return;
        }
        if (sketch_coincident_active_) {
            accept_sketch_coincident_point(candidate);
            return;
        }
        if (sketch_segment_pair_active_) {
            accept_sketch_segment_pair(candidate);
            return;
        }
        sketch_ellipse_major_dimension_action_->setEnabled(false);
        sketch_ellipse_minor_dimension_action_->setEnabled(false);
        sketch_ellipse_rotation_dimension_action_->setEnabled(false);
        if (candidate.kind == zima::viewer::CandidateKind::Occurrence) {
            select_occurrence(candidate.instance_path);
        } else if (candidate.kind == zima::viewer::CandidateKind::Container) {
            select_container(candidate.owner_id);
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
                   candidate.owner_id == active_sketch_id_ &&
                   candidate.semantic_key.starts_with("segment:")) {
            selected_sketch_segment_id_ = candidate.semantic_key.substr(8);
            selected_sketch_circle_id_.clear();
            selected_sketch_arc_id_.clear();
            selected_sketch_ellipse_id_.clear();
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
            selected_sketch_bspline_id_.clear();
            selected_sketch_segment_id_.clear();
            selected_sketch_point_id_.clear();
            sketch_horizontal_action_->setEnabled(false);
            sketch_vertical_action_->setEnabled(false);
            sketch_dimension_action_->setEnabled(false);
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
            selected_sketch_bspline_id_.clear();
            selected_sketch_point_id_.clear();
            sketch_horizontal_action_->setEnabled(false);
            sketch_vertical_action_->setEnabled(false);
            sketch_dimension_action_->setEnabled(false);
            sketch_dimension_x_action_->setEnabled(false);
            sketch_dimension_y_action_->setEnabled(false);
            sketch_angle_dimension_action_->setEnabled(false);
            sketch_fix_point_action_->setEnabled(false);
            sketch_radius_dimension_action_->setEnabled(true);
            sketch_diameter_dimension_action_->setEnabled(false);
            state_->setText(tr("Vybrán oblouk skici."));
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
                   candidate.owner_id == active_sketch_id_ &&
                   candidate.semantic_key.starts_with("ellipse:")) {
            selected_sketch_segment_id_.clear();
            selected_sketch_circle_id_.clear();
            selected_sketch_arc_id_.clear();
            selected_sketch_ellipse_id_ = candidate.semantic_key.substr(8);
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
            sketch_ellipse_major_dimension_action_->setEnabled(true);
            sketch_ellipse_minor_dimension_action_->setEnabled(true);
            sketch_ellipse_rotation_dimension_action_->setEnabled(true);
            sketch_fix_point_action_->setEnabled(false);
            state_->setText(tr("Vybrána elipsa skici."));
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
                   candidate.owner_id == active_sketch_id_ &&
                   candidate.semantic_key.starts_with("bspline:")) {
            selected_sketch_segment_id_.clear();
            selected_sketch_circle_id_.clear();
            selected_sketch_arc_id_.clear();
            selected_sketch_ellipse_id_.clear();
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
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
                   candidate.owner_id == active_sketch_id_ &&
                   candidate.semantic_key.starts_with("point:")) {
            selected_sketch_point_id_ = candidate.semantic_key.substr(6);
            selected_sketch_segment_id_.clear();
            selected_sketch_circle_id_.clear();
            selected_sketch_arc_id_.clear();
            selected_sketch_ellipse_id_.clear();
            selected_sketch_bspline_id_.clear();
            sketch_horizontal_action_->setEnabled(false);
            sketch_vertical_action_->setEnabled(false);
            sketch_dimension_action_->setEnabled(false);
            sketch_dimension_x_action_->setEnabled(false);
            sketch_dimension_y_action_->setEnabled(false);
            sketch_angle_dimension_action_->setEnabled(false);
            sketch_radius_dimension_action_->setEnabled(false);
            sketch_diameter_dimension_action_->setEnabled(false);
            sketch_fix_point_action_->setEnabled(true);
            state_->setText(tr("Vybrán bod skici."));
        }
    });
    viewer_->set_context_menu_callback(
        [this](const auto& candidate, const QPoint& global_position) {
            if (candidate.kind != zima::viewer::CandidateKind::Occurrence) return;
            show_component_context_menu(candidate.instance_path, global_position);
        });
    viewer_->set_world_click_callback([this](const auto& origin, const auto& direction) {
        if (accept_sketch_segment_ray(origin, direction)) return true;
        if (accept_sketch_rectangle_ray(origin, direction)) return true;
        if (accept_sketch_circle_ray(origin, direction)) return true;
        if (accept_sketch_arc_ray(origin, direction)) return true;
        if (accept_sketch_ellipse_ray(origin, direction)) return true;
        return accept_sketch_bspline_ray(origin, direction);
    });
    viewer_->set_world_pointer_callback([this](const auto& origin, const auto& direction) {
        preview_sketch_segment_ray(origin, direction);
        preview_sketch_rectangle_ray(origin, direction);
        preview_sketch_circle_ray(origin, direction);
        preview_sketch_arc_ray(origin, direction);
        preview_sketch_ellipse_ray(origin, direction);
        preview_sketch_bspline_ray(origin, direction);
    });
    viewer_->set_short_middle_click_callback([this] {
        return finish_sketch_bspline();
    });
    viewer_->set_double_confirmation_callback([this](const auto& candidate) {
        if (candidate.kind == zima::viewer::CandidateKind::SketchDimension &&
            candidate.owner_id == active_sketch_id_ &&
            candidate.semantic_key.starts_with("dimension:")) {
            show_sketch_dimension_properties(
                active_sketch_id_, candidate.semantic_key.substr(10));
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
                   candidate.owner_id == active_sketch_id_ &&
                   candidate.semantic_key.starts_with("bspline:")) {
            show_sketch_bspline_properties(
                active_sketch_id_, candidate.semantic_key.substr(8));
        }
    });
    viewer_->set_candidate_drag_callbacks(
        [this](const auto& candidate) { return begin_sketch_point_drag(candidate); },
        [this](const auto& origin, const auto& direction) {
            update_sketch_point_drag(origin, direction);
        },
        [this] { end_sketch_point_drag(); });
    splitter->addWidget(left);
    splitter->addWidget(viewer_);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    setCentralWidget(central);
    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (index < 0) return;
        const std::string id = tabs_->tabData(index).toString().toStdString();
        workspace_.activate(id);
        workspace_.display_top_level(id);
        active_occurrence_path_.clear();
        active_sketch_id_.clear();
        selected_sketch_segment_id_.clear();
        selected_sketch_circle_id_.clear();
        selected_sketch_arc_id_.clear();
        selected_sketch_ellipse_id_.clear();
        selected_sketch_point_id_.clear();
        cancel_sketch_segment();
        refresh_scene();
    });
    connect(tree_, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item) {
            if (item == nullptr || item->parent() == nullptr) return;
            if (item->data(0, Qt::UserRole + 3).toString() == "assembly-mate") return;
            if (item->data(0, Qt::UserRole + 3).toString() == "part-sketch-dimension") return;
            if (item->data(0, Qt::UserRole + 3).toString() == "part-sketch") {
                active_sketch_id_ = item->data(0, Qt::UserRole).toString().toStdString();
                selected_sketch_segment_id_.clear();
                selected_sketch_point_id_.clear();
                selected_sketch_circle_id_.clear();
                selected_sketch_arc_id_.clear();
                selected_sketch_ellipse_id_.clear();
                cancel_sketch_segment();
                sketch_segment_action_->setEnabled(true);
                sketch_rectangle_action_->setEnabled(true);
                sketch_circle_action_->setEnabled(true);
                sketch_arc_action_->setEnabled(true);
                sketch_ellipse_action_->setEnabled(true);
                sketch_bspline_action_->setEnabled(true);
                sketch_coincident_action_->setEnabled(true);
                sketch_parallel_action_->setEnabled(true);
                sketch_perpendicular_action_->setEnabled(true);
                sketch_equal_length_action_->setEnabled(true);
                extrusion_action_->setEnabled(true);
                revolution_action_->setEnabled(true);
                sketch_horizontal_action_->setEnabled(false);
                sketch_vertical_action_->setEnabled(false);
                sketch_dimension_action_->setEnabled(false);
                sketch_dimension_x_action_->setEnabled(false);
                sketch_dimension_y_action_->setEnabled(false);
                sketch_angle_dimension_action_->setEnabled(false);
                sketch_radius_dimension_action_->setEnabled(false);
                sketch_diameter_dimension_action_->setEnabled(false);
                sketch_fix_point_action_->setEnabled(false);
                return;
            }
            if (item->data(0, Qt::UserRole + 3).toString() == "part-container") {
                viewer_->confirm_container(
                    item->data(0, Qt::UserRole).toString().toStdString());
            } else {
                viewer_->confirm_occurrence(
                    item->data(0, Qt::UserRole + 1).toString().toStdString());
            }
        });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
        [this](QTreeWidgetItem* item) {
            if (item == nullptr || item->parent() == nullptr) return;
            if (item->data(0, Qt::UserRole + 3).toString() == "assembly-mate") {
                show_mate_properties(
                    item->data(0, Qt::UserRole + 4).toString().toStdString(),
                    item->data(0, Qt::UserRole).toString().toStdString());
                return;
            }
            if (item->data(0, Qt::UserRole + 3).toString() == "part-sketch-dimension") {
                show_sketch_dimension_properties(
                    item->data(0, Qt::UserRole + 4).toString().toStdString(),
                    item->data(0, Qt::UserRole).toString().toStdString());
                return;
            }
            if (item->data(0, Qt::UserRole + 3).toString() == "part-sketch") {
                show_sketch_properties(
                    item->data(0, Qt::UserRole).toString().toStdString());
                return;
            }
            if (item->data(0, Qt::UserRole + 3).toString() == "part-container") {
                show_primitive_properties(
                    workspace_.open_part(workspace_.active_document_id())
                        ->session.document().find_container(
                            item->data(0, Qt::UserRole).toString().toStdString())
                        ->feature_kind,
                    item->data(0, Qt::UserRole).toString().toStdString());
                return;
            }
            const std::string source_id =
                item->data(0, Qt::UserRole + 2).toString().toStdString();
            if (workspace_.find(source_id) != nullptr) {
                active_occurrence_path_ =
                    item->data(0, Qt::UserRole + 1).toString().toStdString();
                workspace_.activate(source_id);
                refresh_tabs();
                refresh_scene();
            }
        });
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this,
        [this](const QPoint& position) {
            auto* item = tree_->itemAt(position);
            if (item == nullptr || item->parent() == nullptr) return;
            if (item->data(0, Qt::UserRole + 3).toString() == "assembly-mate") {
                show_mate_context_menu(
                    item->data(0, Qt::UserRole + 4).toString().toStdString(),
                    item->data(0, Qt::UserRole).toString().toStdString(),
                    tree_->viewport()->mapToGlobal(position));
                return;
            }
            if (item->data(0, Qt::UserRole + 3).toString() == "part-container") {
                const std::string id = item->data(0, Qt::UserRole).toString().toStdString();
                const auto* part = workspace_.open_part(workspace_.active_document_id());
                const auto* container = part == nullptr
                    ? nullptr : part->session.document().find_container(id);
                if (container != nullptr) show_primitive_properties(container->feature_kind, id);
            } else if (item->data(0, Qt::UserRole + 3).toString() == "part-sketch") {
                show_sketch_properties(
                    item->data(0, Qt::UserRole).toString().toStdString());
            } else if (item->data(0, Qt::UserRole + 3).toString() ==
                       "part-sketch-dimension") {
                show_sketch_dimension_properties(
                    item->data(0, Qt::UserRole + 4).toString().toStdString(),
                    item->data(0, Qt::UserRole).toString().toStdString());
            } else {
                show_component_context_menu(
                    item->data(0, Qt::UserRole + 1).toString().toStdString(),
                    tree_->viewport()->mapToGlobal(position));
            }
        });
}

void AssemblyWorkspaceWindow::new_part() {
    auto document = zima::document::PartDocument::create_default();
    const std::string id = document.document_id;
    workspace_.add_part(std::move(document));
    workspace_.activate(id);
    workspace_.display_top_level(id);
    active_occurrence_path_.clear();
    active_sketch_id_.clear();
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    cancel_sketch_segment();
    refresh_tabs();
    refresh_scene();
}

void AssemblyWorkspaceWindow::new_assembly() {
    auto document = zima::assembly::AssemblyDocument::create_default();
    const std::string id = document.document_id;
    workspace_.add_assembly(std::move(document));
    workspace_.activate(id);
    workspace_.display_top_level(id);
    active_occurrence_path_.clear();
    active_sketch_id_.clear();
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    cancel_sketch_segment();
    refresh_tabs();
    refresh_scene();
}

void AssemblyWorkspaceWindow::open_part() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Otevřít vypočtený Part"), {}, tr("ZIMA-CAD C++ Part (*.zcp.json)"));
    if (path.isEmpty()) return;
    try {
        std::vector<zima::kernel::BodyResult> calculated;
        auto document = zima::document::PartDocument::load(path.toStdString(), &calculated);
        const std::string id = document.document_id;
        workspace_.add_part(std::move(document), std::move(calculated), path.toStdString());
        workspace_.activate(id);
        active_occurrence_path_.clear();
        active_sketch_id_.clear();
        selected_sketch_segment_id_.clear();
        selected_sketch_point_id_.clear();
        selected_sketch_circle_id_.clear();
        selected_sketch_arc_id_.clear();
        cancel_sketch_segment();
        refresh_tabs();
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Otevření Partu selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::open_assembly() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Otevřít sestavu"), {}, tr("ZIMA-CAD C++ Assembly (*.zca.json)"));
    if (path.isEmpty()) return;
    try {
        auto document = zima::assembly::AssemblyDocument::load(path.toStdString());
        const std::string id = document.document_id;
        workspace_.add_assembly(std::move(document), path.toStdString());
        workspace_.activate(id);
        workspace_.display_top_level(id);
        active_occurrence_path_.clear();
        active_sketch_id_.clear();
        selected_sketch_segment_id_.clear();
        selected_sketch_point_id_.clear();
        selected_sketch_circle_id_.clear();
        selected_sketch_arc_id_.clear();
        cancel_sketch_segment();
        refresh_tabs();
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Otevření sestavy selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::insert_active_component() {
    const std::string source_id = workspace_.active_document_id();
    const std::string assembly_id = workspace_.displayed_document_id();
    if (workspace_.open_assembly(assembly_id) == nullptr) return;
    try {
        std::string occurrence_id;
        if (const auto* part = workspace_.open_part(source_id)) {
            occurrence_id = workspace_.insert_open_part(
                assembly_id, source_id, part->session.document().name);
        } else if (const auto* source = workspace_.open_assembly(source_id)) {
            occurrence_id = workspace_.insert_open_assembly(
                assembly_id, source_id, source->session.document().name);
        } else {
            return;
        }
        refresh_scene();
        viewer_->confirm_occurrence(
            zima::assembly::InstancePath{}.child(occurrence_id).encoded());
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Vložení selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::regenerate_assembly() {
    const std::string id = workspace_.displayed_document_id();
    try {
        workspace_.regenerate_assembly_from_open_dependencies(id);
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Regenerace selhala"), error.what());
    }
}

void AssemblyWorkspaceWindow::start_plane_mate() {
    if (properties_dialog_ != nullptr ||
        workspace_.open_assembly(workspace_.active_document_id()) == nullptr ||
        workspace_.open_assembly(workspace_.displayed_document_id()) == nullptr) return;
    pending_mate_reference_.reset();
    mate_selection_active_ = true;
    pending_mate_kind_ = zima::assembly::MateKind::PlaneCoincident;
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Face});
    state_->setText(tr("Vyberte pohyblivou rovinnou plochu."));
}

void AssemblyWorkspaceWindow::start_axis_mate() {
    if (properties_dialog_ != nullptr ||
        workspace_.open_assembly(workspace_.active_document_id()) == nullptr ||
        workspace_.open_assembly(workspace_.displayed_document_id()) == nullptr) return;
    pending_mate_reference_.reset();
    mate_selection_active_ = true;
    pending_mate_kind_ = zima::assembly::MateKind::AxisCoincident;
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Axis});
    state_->setText(tr("Vyberte pohyblivou osu."));
}

void AssemblyWorkspaceWindow::start_edge_treatment(
    zima::document::FeatureKind kind) {
    if (properties_dialog_ != nullptr ||
        (kind != zima::document::FeatureKind::Fillet &&
         kind != zima::document::FeatureKind::Chamfer)) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || workspace_.displayed_document_id() !=
            workspace_.active_document_id() || part->session.document().history.empty()) return;
    edge_treatment_selection_ = kind;
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Edge});
    state_->setText(kind == zima::document::FeatureKind::Fillet
        ? tr("Vyberte původní hranu pro zaoblení.")
        : tr("Vyberte původní hranu pro sražení."));
}

void AssemblyWorkspaceWindow::accept_edge_treatment(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!edge_treatment_selection_) return;
    if (candidate.kind != zima::viewer::CandidateKind::Edge ||
        candidate.geometry != zima::viewer::CandidateGeometry::OriginalReference ||
        candidate.owner_id.empty() || candidate.semantic_key.empty() ||
        !candidate.instance_path.empty()) {
        state_->setText(tr("Vyberte hranu původního solidu aktivního Partu."));
        return;
    }
    const auto kind = *edge_treatment_selection_;
    edge_treatment_selection_.reset();
    const zima::kernel::EdgeReference edge{candidate.owner_id, candidate.semantic_key, {}};
    auto initial = kind == zima::document::FeatureKind::Fillet
        ? zima::document::PartDocument::create_fillet_container(edge)
        : zima::document::PartDocument::create_chamfer_container(edge);
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const std::string part_id = part->session.document().document_id;
    auto* dialog = new PrimitivePropertiesDialog(
        initial, false, false,
        [this, part_id](zima::document::HistoryContainer committed) {
            auto* target = workspace_.open_part(part_id);
            if (target == nullptr) throw std::runtime_error("Part is no longer open");
            auto next = target->session.document();
            next.history.push_back(std::move(committed));
            target->session.commit(next, calculate_part(next));
        }, this);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        edge_treatment_selection_.reset();
        refresh_tabs();
        refresh_scene();
    });
    dialog->show();
}

std::optional<zima::assembly::MateReference>
AssemblyWorkspaceWindow::local_mate_reference(
    const zima::viewer::ViewerCandidate& candidate) const {
    const bool face = candidate.kind == zima::viewer::CandidateKind::Face &&
        candidate.geometry == zima::viewer::CandidateGeometry::OriginalReference &&
        pending_mate_kind_ == zima::assembly::MateKind::PlaneCoincident;
    const bool axis = candidate.kind == zima::viewer::CandidateKind::Axis &&
        candidate.geometry == zima::viewer::CandidateGeometry::OriginalReference &&
        pending_mate_kind_ == zima::assembly::MateKind::AxisCoincident;
    if ((!face && !axis) ||
        candidate.owner_id.empty() || candidate.semantic_key.empty()) return std::nullopt;
    auto path = zima::assembly::InstancePath::decode(candidate.instance_path);
    if (!active_occurrence_path_.empty()) {
        const auto prefix = zima::assembly::InstancePath::decode(active_occurrence_path_);
        if (path.occurrence_ids.size() <= prefix.occurrence_ids.size() ||
            !std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(),
                        path.occurrence_ids.begin())) return std::nullopt;
        path.occurrence_ids.erase(
            path.occurrence_ids.begin(),
            path.occurrence_ids.begin() +
                static_cast<std::ptrdiff_t>(prefix.occurrence_ids.size()));
    }
    return zima::assembly::MateReference{
        face ? zima::assembly::MateReferenceKind::Face
             : zima::assembly::MateReferenceKind::Axis,
        std::move(path),
        candidate.owner_id, candidate.semantic_key};
}

void AssemblyWorkspaceWindow::accept_mate_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    auto reference = local_mate_reference(candidate);
    if (!reference) {
        state_->setText(tr("Plocha nepatří do aktivní sestavy."));
        return;
    }
    if (!pending_mate_reference_) {
        pending_mate_reference_ = std::move(reference);
        state_->setText(pending_mate_kind_ == zima::assembly::MateKind::PlaneCoincident
            ? tr("Vyberte pevnou referenční rovinnou plochu.")
            : tr("Vyberte pevnou referenční osu."));
        return;
    }
    try {
        auto mate = zima::assembly::AssemblyDocument::create_mate(
            pending_mate_kind_ == zima::assembly::MateKind::PlaneCoincident
                ? "Plocha na plochu" : "Osa na osu",
            pending_mate_kind_,
            std::move(*pending_mate_reference_), std::move(*reference));
        pending_mate_reference_.reset();
        mate_selection_active_ = false;
        auto* dialog = new MatePropertiesDialog(
            std::move(mate),
            [this, assembly_id = workspace_.active_document_id()]
            (zima::assembly::AssemblyMate committed) {
                auto* assembly = workspace_.open_assembly(assembly_id);
                if (assembly == nullptr) throw std::runtime_error("Assembly is no longer open");
                auto next = assembly->session.document();
                next.add_mate(std::move(committed));
                next.calculate_mates();
                if (next.mates.back().status != zima::assembly::MateStatus::Valid) {
                    throw std::runtime_error(
                        "Vybrané plochy nejsou dvě dostupné rovnoběžné roviny");
                }
                assembly->session.commit(std::move(next));
            }, this);
        properties_dialog_ = dialog;
        connect(dialog, &QObject::destroyed, this, [this] {
            properties_dialog_ = nullptr;
            pending_mate_reference_.reset();
            mate_selection_active_ = false;
            refresh_tabs();
            refresh_scene();
        });
        dialog->show();
    } catch (const std::exception& error) {
        pending_mate_reference_.reset();
        mate_selection_active_ = false;
        viewer_->set_selection_contract({zima::viewer::CandidateKind::Occurrence});
        QMessageBox::warning(this, tr("Vazbu nelze vytvořit"), error.what());
        refresh_scene();
    }
}

void AssemblyWorkspaceWindow::save_active_assembly() {
    auto* assembly = workspace_.open_assembly(workspace_.displayed_document_id());
    if (assembly == nullptr) return;
    QString path = QString::fromStdString(assembly->path.string());
    if (path.isEmpty()) path = QFileDialog::getSaveFileName(
        this, tr("Uložit sestavu"), "assembly.zca.json",
        tr("ZIMA-CAD C++ Assembly (*.zca.json)"));
    if (path.isEmpty()) return;
    try {
        assembly->session.document().save(path.toStdString());
        assembly->path = path.toStdString();
        assembly->session.mark_saved();
        refresh_tabs();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Uložení sestavy selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::save_active_document() {
    if (workspace_.open_assembly(workspace_.active_document_id()) != nullptr) {
        workspace_.display_top_level(workspace_.active_document_id());
        save_active_assembly();
        return;
    }
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    QString path = QString::fromStdString(part->path.string());
    if (path.isEmpty()) path = QFileDialog::getSaveFileName(
        this, tr("Uložit Part"), "part.zcp.json",
        tr("ZIMA-CAD C++ Part (*.zcp.json)"));
    if (path.isEmpty()) return;
    try {
        part->session.document().save(path.toStdString(),
                                      part->session.calculated_boundaries());
        part->path = path.toStdString();
        part->session.mark_saved();
        refresh_tabs();
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Uložení Partu selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::import_file() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Importovat"), {},
        tr("Podporované soubory (*.dxf *.step *.stp);;DXF (*.dxf);;STEP (*.step *.stp)"));
    if (path.isEmpty()) return;
    const auto context = !active_sketch_id_.empty()
        ? zima::interchange::Context::Sketch
        : workspace_.open_part(workspace_.active_document_id()) != nullptr
            ? zima::interchange::Context::Part : zima::interchange::Context::Assembly;
    const auto format = zima::interchange::format_from_path(path.toStdString());
    if (!zima::interchange::supports(
            format, zima::interchange::Direction::Import, context)) {
        QMessageBox::warning(this, tr("Import nelze provést"), QString::fromStdString(
            zima::interchange::unsupported_reason(
                format, zima::interchange::Direction::Import, context)));
        return;
    }
    state_->setText(tr("Importní soubor připraven: %1").arg(path));
}

void AssemblyWorkspaceWindow::export_file() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Exportovat"), {},
        tr("DXF (*.dxf);;STEP (*.step);;STL (*.stl);;PNG (*.png);;JPEG (*.jpg *.jpeg)"));
    if (path.isEmpty()) return;
    const auto context = !active_sketch_id_.empty()
        ? zima::interchange::Context::Sketch
        : workspace_.open_part(workspace_.active_document_id()) != nullptr
            ? zima::interchange::Context::Part : zima::interchange::Context::Assembly;
    const auto format = zima::interchange::format_from_path(path.toStdString());
    if (!zima::interchange::supports(
            format, zima::interchange::Direction::Export, context)) {
        QMessageBox::warning(this, tr("Export nelze provést"), QString::fromStdString(
            zima::interchange::unsupported_reason(
                format, zima::interchange::Direction::Export, context)));
        return;
    }
    state_->setText(tr("Exportní soubor připraven: %1").arg(path));
}

std::vector<zima::kernel::BodyResult> AssemblyWorkspaceWindow::calculate_part(
    const zima::document::PartDocument& document) const {
    return kernel_.evaluate_history(document.kernel_operations());
}

void AssemblyWorkspaceWindow::show_primitive_properties(
    zima::document::FeatureKind feature_kind,
    const std::string& container_id) {
    if (properties_dialog_ != nullptr) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    if (container_id.empty() &&
        (feature_kind == zima::document::FeatureKind::Extrusion ||
         feature_kind == zima::document::FeatureKind::Revolution) &&
        active_sketch_id_.empty()) {
        QMessageBox::warning(this, tr("Vytažení"),
            tr("Nejprve vyberte zdrojovou skicu ve stromu Partu."));
        return;
    }
    const auto& document = part->session.document();
    const auto* edited = container_id.empty()
        ? nullptr : document.find_container(container_id);
    if (!container_id.empty() &&
        (edited == nullptr || edited->feature_kind != feature_kind)) return;
    const bool edit_mode = edited != nullptr;
    std::optional<std::string> rollback_occurrence;
    const auto edit_index = edit_mode
        ? document.history_index(container_id) : std::optional<std::size_t>{};
    if (edit_mode) {
        rollback_occurrence = resolve_active_occurrence(document.document_id);
        if (!rollback_occurrence) {
            QMessageBox::warning(
                this, tr("Nejednoznačný výskyt"),
                tr("Nejprve aktivujte přesný výskyt Partu ve stromu sestavy."));
            return;
        }
    }
    if (!edit_mode && (feature_kind == zima::document::FeatureKind::Fillet ||
                       feature_kind == zima::document::FeatureKind::Chamfer)) return;
    const auto initial = edit_mode ? *edited
        : feature_kind == zima::document::FeatureKind::Cylinder
            ? zima::document::PartDocument::create_cylinder_container()
        : feature_kind == zima::document::FeatureKind::Extrusion
            ? zima::document::PartDocument::create_extrusion_container(active_sketch_id_)
        : feature_kind == zima::document::FeatureKind::Revolution
            ? zima::document::PartDocument::create_revolution_container(active_sketch_id_)
            : zima::document::PartDocument::create_box_container();
    const bool allow_subtract = !document.history.empty() &&
        !(edit_mode && document.history.front().id == initial.id);
    const std::string part_id = document.document_id;
    auto* dialog = new PrimitivePropertiesDialog(
        initial, edit_mode, allow_subtract,
        [this, part_id, edit_mode](zima::document::HistoryContainer committed) {
            auto* target_part = workspace_.open_part(part_id);
            if (target_part == nullptr) throw std::runtime_error("Part is no longer open");
            auto next = target_part->session.document();
            if (edit_mode) {
                auto* target = next.find_container(committed.id);
                if (target == nullptr) throw std::runtime_error("Container no longer exists");
                *target = std::move(committed);
            } else {
                next.history.push_back(std::move(committed));
            }
            auto calculated = calculate_part(next);
            target_part->session.commit(std::move(next), std::move(calculated));
        }, this);
    properties_dialog_ = dialog;
    if (edit_index) {
        part_rollback_ = PartRollbackContext{
            document.document_id, *rollback_occurrence, *edit_index};
        refresh_scene();
    }
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        part_rollback_.reset();
        refresh_tabs();
        refresh_scene();
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::show_sketch_properties(const std::string& sketch_id) {
    if (properties_dialog_ != nullptr) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto& document = part->session.document();
    const auto found = std::find_if(document.sketches.begin(), document.sketches.end(),
        [&](const auto& sketch) { return sketch.id == sketch_id; });
    const bool edit_mode = found != document.sketches.end();
    if (!sketch_id.empty() && !edit_mode) return;
    auto initial = edit_mode ? *found : zima::sketcher::Sketch::create_default();
    const std::string part_id = document.document_id;
    auto* dialog = new SketchPropertiesDialog(
        std::move(initial), edit_mode,
        [this, part_id, edit_mode](zima::sketcher::Sketch committed) {
            auto* target_part = workspace_.open_part(part_id);
            if (target_part == nullptr) throw std::runtime_error("Part is no longer open");
            auto next = target_part->session.document();
            active_sketch_id_ = committed.id;
            if (edit_mode) {
                const auto target = std::find_if(next.sketches.begin(), next.sketches.end(),
                    [&](const auto& sketch) { return sketch.id == committed.id; });
                if (target == next.sketches.end()) {
                    throw std::runtime_error("Sketch no longer exists");
                }
                *target = std::move(committed);
            } else {
                next.sketches.push_back(std::move(committed));
            }
            target_part->session.commit(
                std::move(next), target_part->session.calculated_boundaries());
        }, this);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        refresh_tabs();
        refresh_scene();
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::show_sketch_bspline_properties(
    const std::string& sketch_id, const std::string& bspline_id) {
    if (properties_dialog_ != nullptr || sketch_bspline_active_) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || workspace_.displayed_document_id() !=
            workspace_.active_document_id()) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == sketch_id; });
    if (sketch == part->session.document().sketches.end()) return;
    const auto spline = std::find_if(sketch->bsplines.begin(), sketch->bsplines.end(),
        [&](const auto& value) { return value.id == bspline_id; });
    if (spline == sketch->bsplines.end()) return;
    std::vector<std::array<double, 2>> points;
    points.reserve(spline->control_point_ids.size());
    for (const auto& point_id : spline->control_point_ids) {
        const auto* point = sketch->find_point(point_id);
        if (point == nullptr) return;
        points.push_back({point->x, point->y});
    }
    const auto part_id = part->session.document().document_id;
    auto* dialog = new SketchBSplinePropertiesDialog(
        spline->degree, spline->closed, std::move(points),
        [this, part_id, sketch_id, bspline_id](
            unsigned degree, bool closed,
            const std::vector<std::array<double, 2>>& values) {
            auto* target_part = workspace_.open_part(part_id);
            if (target_part == nullptr) throw std::runtime_error("Part is no longer open");
            auto next = target_part->session.document();
            const auto target_sketch = std::find_if(
                next.sketches.begin(), next.sketches.end(),
                [&](const auto& value) { return value.id == sketch_id; });
            if (target_sketch == next.sketches.end()) {
                throw std::runtime_error("Sketch no longer exists");
            }
            const auto target_spline = std::find_if(
                target_sketch->bsplines.begin(), target_sketch->bsplines.end(),
                [&](const auto& value) { return value.id == bspline_id; });
            if (target_spline == target_sketch->bsplines.end() ||
                values.size() != target_spline->control_point_ids.size()) {
                throw std::runtime_error("B-spline no longer exists");
            }
            target_spline->degree = degree;
            target_spline->closed = closed;
            for (std::size_t index = 0; index < values.size(); ++index) {
                auto* point = target_sketch->find_point(
                    target_spline->control_point_ids[index]);
                if (point == nullptr) throw std::runtime_error("Missing B-spline control point");
                point->x = values[index][0];
                point->y = values[index][1];
            }
            target_sketch->validate();
            target_part->session.commit(
                std::move(next), target_part->session.calculated_boundaries());
        }, this);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::start_sketch_segment() {
    if (properties_dialog_ != nullptr) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || workspace_.displayed_document_id() !=
            workspace_.active_document_id()) return;
    const auto found = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
    if (found == part->session.document().sketches.end()) return;
    cancel_sketch_segment();
    sketch_segment_active_ = true;
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_bspline_id_.clear();
    pending_segment_start_.reset();
    viewer_->set_transient_edges({});
    state_->setText(tr("Úsečka skici: určete první bod. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_segment() {
    sketch_segment_active_ = false;
    sketch_rectangle_active_ = false;
    sketch_circle_active_ = false;
    sketch_arc_active_ = false;
    sketch_ellipse_active_ = false;
    sketch_bspline_active_ = false;
    sketch_coincident_active_ = false;
    sketch_segment_pair_active_ = false;
    pending_segment_start_.reset();
    pending_rectangle_corner_.reset();
    pending_circle_center_.reset();
    pending_arc_center_.reset();
    pending_arc_start_.reset();
    pending_ellipse_center_.reset();
    pending_ellipse_major_.reset();
    pending_bspline_points_.clear();
    pending_coincident_point_id_.clear();
    pending_pair_segment_id_.clear();
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_rectangle() {
    if (properties_dialog_ != nullptr) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || workspace_.displayed_document_id() !=
            workspace_.active_document_id()) return;
    const auto found = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
    if (found == part->session.document().sketches.end()) return;
    cancel_sketch_segment();
    sketch_rectangle_active_ = true;
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_bspline_id_.clear();
    state_->setText(tr("Obdélník skici: určete první roh. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_rectangle() {
    sketch_rectangle_active_ = false;
    pending_rectangle_corner_.reset();
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_circle() {
    if (properties_dialog_ != nullptr) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || workspace_.displayed_document_id() !=
            workspace_.active_document_id()) return;
    const auto found = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
    if (found == part->session.document().sketches.end()) return;
    cancel_sketch_segment();
    sketch_circle_active_ = true;
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_bspline_id_.clear();
    state_->setText(tr("Kružnice skici: určete střed. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_circle() {
    sketch_circle_active_ = false;
    pending_circle_center_.reset();
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_arc() {
    if (properties_dialog_ != nullptr) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || workspace_.displayed_document_id() !=
            workspace_.active_document_id()) return;
    const auto found = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
    if (found == part->session.document().sketches.end()) return;
    cancel_sketch_segment();
    sketch_arc_active_ = true;
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_bspline_id_.clear();
    state_->setText(tr("Oblouk skici: určete střed. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_arc() {
    sketch_arc_active_ = false;
    pending_arc_center_.reset();
    pending_arc_start_.reset();
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_ellipse() {
    if (properties_dialog_ != nullptr) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || workspace_.displayed_document_id() !=
            workspace_.active_document_id()) return;
    const auto found = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
    if (found == part->session.document().sketches.end()) return;
    cancel_sketch_segment();
    sketch_ellipse_active_ = true;
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_bspline_id_.clear();
    state_->setText(tr("Elipsa skici: určete střed. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_ellipse() {
    sketch_ellipse_active_ = false;
    pending_ellipse_center_.reset();
    pending_ellipse_major_.reset();
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_bspline() {
    if (properties_dialog_ != nullptr) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || workspace_.displayed_document_id() !=
            workspace_.active_document_id()) return;
    const auto found = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
    if (found == part->session.document().sketches.end()) return;
    cancel_sketch_segment();
    sketch_bspline_active_ = true;
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_bspline_id_.clear();
    state_->setText(tr("B-spline: přidávejte řídicí body; Enter dokončí, Escape zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_bspline() {
    sketch_bspline_active_ = false;
    pending_bspline_points_.clear();
    viewer_->set_transient_edges({});
}

bool AssemblyWorkspaceWindow::accept_sketch_bspline_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (!sketch_bspline_active_ || part == nullptr || active_sketch_id_.empty() ||
        workspace_.displayed_document_id() != workspace_.active_document_id()) return false;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_bspline_points_.empty()) {
        const auto& previous = pending_bspline_points_.back();
        if (std::hypot((*position)[0] - previous[0], (*position)[1] - previous[1]) <=
            1.0e-9) {
            state_->setText(tr("Řídicí body B-spline musí být navzájem odlišné."));
            return true;
        }
    }
    pending_bspline_points_.push_back(*position);
    state_->setText(pending_bspline_points_.size() >= 4
        ? tr("B-spline: %1 řídicích bodů; Enter dokončí.")
              .arg(pending_bspline_points_.size())
        : tr("B-spline: %1 řídicích bodů; pro kubickou křivku jsou potřeba alespoň 4.")
              .arg(pending_bspline_points_.size()));
    return true;
}

bool AssemblyWorkspaceWindow::finish_sketch_bspline() {
    if (!sketch_bspline_active_) return false;
    if (pending_bspline_points_.size() < 4) {
        state_->setText(tr("Kubická B-spline vyžaduje alespoň 4 řídicí body."));
        return true;
    }
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return true;
    try {
        auto next = part->session.document();
        const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == next.sketches.end()) return true;
        static_cast<void>(sketch->add_bspline(pending_bspline_points_));
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        pending_bspline_points_.clear();
        viewer_->set_transient_edges({});
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("B-spline vytvořena. Klikáním můžete vytvořit další."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
    return true;
}

void AssemblyWorkspaceWindow::preview_sketch_bspline_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_bspline_active_ || pending_bspline_points_.empty()) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) {
        viewer_->set_transient_edges({});
        return;
    }
    auto preview_points = pending_bspline_points_;
    preview_points.push_back(*position);
    if (preview_points.size() < 4) {
        zima::kernel::ViewerEdge preview;
        for (const auto& point : preview_points) {
            preview.points.push_back(sketch->world_point(point[0], point[1]));
        }
        viewer_->set_transient_edges({std::move(preview)});
        return;
    }
    auto preview_sketch = *sketch;
    const auto preview_id = preview_sketch.add_bspline(preview_points);
    auto mesh = preview_sketch.viewer_mesh();
    const auto edge = std::find_if(mesh.edges.rbegin(), mesh.edges.rend(),
        [&](const auto& value) {
            return value.reference.semantic_key == "bspline:" + preview_id;
        });
    viewer_->set_transient_edges(edge == mesh.edges.rend()
        ? std::vector<zima::kernel::ViewerEdge>{}
        : std::vector<zima::kernel::ViewerEdge>{*edge});
}

bool AssemblyWorkspaceWindow::accept_sketch_segment_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (!sketch_segment_active_ || part == nullptr || active_sketch_id_.empty() ||
        workspace_.displayed_document_id() != workspace_.active_document_id()) return false;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_segment_start_) {
        pending_segment_start_ = *position;
        state_->setText(tr("Úsečka skici: určete druhý bod. Escape příkaz zruší."));
        return true;
    }
    const double dx = (*position)[0] - (*pending_segment_start_)[0];
    const double dy = (*position)[1] - (*pending_segment_start_)[1];
    if (std::hypot(dx, dy) <= 1.0e-9) {
        state_->setText(tr("Úsečka musí mít nenulovou délku."));
        return true;
    }
    auto next = part->session.document();
    const auto target = std::find_if(next.sketches.begin(), next.sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (target == next.sketches.end()) return true;
    static_cast<void>(target->add_segment(
        (*pending_segment_start_)[0], (*pending_segment_start_)[1],
        (*position)[0], (*position)[1]));
    const auto calculated = part->session.calculated_boundaries();
    part->session.commit(std::move(next), calculated);
    preserve_view_on_refresh_ = true;
    pending_segment_start_.reset();
    viewer_->set_transient_edges({});
    refresh_tabs();
    refresh_scene();
    state_->setText(tr("Úsečka vytvořena. Kliknutím určete první bod další úsečky."));
    return true;
}

void AssemblyWorkspaceWindow::constrain_selected_segment(
    zima::sketcher::ConstraintKind kind) {
    if (sketch_segment_active_ || sketch_rectangle_active_ || sketch_circle_active_ ||
        sketch_arc_active_ || sketch_coincident_active_ || sketch_segment_pair_active_ ||
        selected_sketch_segment_id_.empty() ||
        active_sketch_id_.empty() || properties_dialog_ != nullptr) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || workspace_.displayed_document_id() !=
            workspace_.active_document_id()) return;
    auto next = part->session.document();
    const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == next.sketches.end()) return;
    try {
        static_cast<void>(sketch->add_segment_constraint(selected_sketch_segment_id_, kind));
        const auto calculated = part->session.calculated_boundaries();
        part->session.commit(std::move(next), calculated);
        selected_sketch_segment_id_.clear();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(kind == zima::sketcher::ConstraintKind::Horizontal
            ? tr("Úsečka je vodorovná.") : tr("Úsečka je svislá."));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Vazbu nelze vytvořit"), error.what());
    }
}

void AssemblyWorkspaceWindow::start_sketch_coincident() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || workspace_.displayed_document_id() !=
            workspace_.active_document_id()) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return;
    cancel_sketch_segment();
    sketch_coincident_active_ = true;
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    viewer_->set_selection_contract({zima::viewer::CandidateKind::SketchPoint});
    state_->setText(tr("Shodnost bodů: vyberte první bod. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_coincident() {
    sketch_coincident_active_ = false;
    pending_coincident_point_id_.clear();
}

void AssemblyWorkspaceWindow::accept_sketch_coincident_point(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_coincident_active_ ||
        candidate.kind != zima::viewer::CandidateKind::SketchPoint ||
        candidate.owner_id != active_sketch_id_ ||
        !candidate.semantic_key.starts_with("point:")) return;
    const auto point_id = candidate.semantic_key.substr(6);
    if (pending_coincident_point_id_.empty()) {
        pending_coincident_point_id_ = point_id;
        state_->setText(tr("Shodnost bodů: vyberte druhý bod."));
        return;
    }
    if (point_id == pending_coincident_point_id_) {
        state_->setText(tr("Vyberte jiný druhý bod."));
        return;
    }
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    try {
        auto next = part->session.document();
        const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == next.sketches.end()) return;
        static_cast<void>(sketch->add_coincident_constraint(
            pending_coincident_point_id_, point_id));
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        pending_coincident_point_id_.clear();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("Vazba shodnosti vytvořena. Vyberte první bod další vazby."));
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
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || workspace_.displayed_document_id() !=
            workspace_.active_document_id()) return;
    cancel_sketch_segment();
    sketch_segment_pair_active_ = true;
    pending_pair_kind_ = kind;
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_point_id_.clear();
    viewer_->set_selection_contract({zima::viewer::CandidateKind::SketchSegment});
    state_->setText(kind == zima::sketcher::ConstraintKind::Parallel
        ? tr("Rovnoběžnost: vyberte referenční úsečku. Escape příkaz zruší.")
        : kind == zima::sketcher::ConstraintKind::Perpendicular
            ? tr("Kolmost: vyberte referenční úsečku. Escape příkaz zruší.")
            : tr("Stejná délka: vyberte referenční úsečku. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::accept_sketch_segment_pair(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_segment_pair_active_ ||
        candidate.kind != zima::viewer::CandidateKind::SketchSegment ||
        candidate.owner_id != active_sketch_id_ ||
        !candidate.semantic_key.starts_with("segment:")) return;
    const auto segment_id = candidate.semantic_key.substr(8);
    if (pending_pair_segment_id_.empty()) {
        pending_pair_segment_id_ = segment_id;
        state_->setText(pending_pair_kind_ == zima::sketcher::ConstraintKind::Parallel
            ? tr("Rovnoběžnost: vyberte řízenou úsečku.")
            : pending_pair_kind_ == zima::sketcher::ConstraintKind::Perpendicular
                ? tr("Kolmost: vyberte řízenou úsečku.")
                : tr("Stejná délka: vyberte řízenou úsečku."));
        return;
    }
    if (segment_id == pending_pair_segment_id_) {
        state_->setText(tr("Vyberte jinou řízenou úsečku."));
        return;
    }
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    try {
        auto next = part->session.document();
        const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == next.sketches.end()) return;
        static_cast<void>(sketch->add_segment_pair_constraint(
            pending_pair_segment_id_, segment_id, pending_pair_kind_));
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        pending_pair_segment_id_.clear();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(pending_pair_kind_ == zima::sketcher::ConstraintKind::Parallel
            ? tr("Rovnoběžnost vytvořena. Vyberte další referenční úsečku.")
            : pending_pair_kind_ == zima::sketcher::ConstraintKind::Perpendicular
                ? tr("Kolmost vytvořena. Vyberte další referenční úsečku.")
                : tr("Stejná délka vytvořena. Vyberte další referenční úsečku."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
}

void AssemblyWorkspaceWindow::toggle_selected_sketch_point_fixed() {
    if (properties_dialog_ != nullptr || sketch_coincident_active_ ||
        sketch_segment_pair_active_ ||
        selected_sketch_point_id_.empty() || active_sketch_id_.empty()) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || workspace_.displayed_document_id() !=
            workspace_.active_document_id()) return;
    try {
        auto next = part->session.document();
        const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == next.sketches.end()) return;
        const auto* point = sketch->find_point(selected_sketch_point_id_);
        if (point == nullptr) return;
        const bool fixed = !point->fixed;
        sketch->set_point_fixed(selected_sketch_point_id_, fixed);
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(fixed ? tr("Bod je fixovaný.") : tr("Bod je uvolněný."));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Fixaci bodu nelze změnit"), error.what());
    }
}

bool AssemblyWorkspaceWindow::begin_sketch_point_drag(
    const zima::viewer::ViewerCandidate& candidate) {
    if (properties_dialog_ != nullptr || sketch_segment_active_ ||
        sketch_rectangle_active_ || sketch_circle_active_ || sketch_arc_active_ ||
        sketch_coincident_active_ || sketch_segment_pair_active_ || candidate.kind !=
            zima::viewer::CandidateKind::SketchPoint ||
        candidate.owner_id != active_sketch_id_ ||
        !candidate.semantic_key.starts_with("point:")) return false;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || workspace_.displayed_document_id() !=
            workspace_.active_document_id()) return false;
    const auto point_id = candidate.semantic_key.substr(6);
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return false;
    const auto* point = sketch->find_point(point_id);
    if (point == nullptr || point->fixed) {
        state_->setText(tr("Fixovaný bod nelze táhnout."));
        return false;
    }
    sketch_drag_document_ = part->session.document();
    sketch_drag_point_id_ = point_id;
    sketch_drag_changed_ = false;
    return true;
}

void AssemblyWorkspaceWindow::update_sketch_point_drag(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_drag_document_) return;
    const auto current_sketch = std::find_if(
        sketch_drag_document_->sketches.begin(), sketch_drag_document_->sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (current_sketch == sketch_drag_document_->sketches.end()) return;
    const auto position = current_sketch->intersect_ray(origin, direction);
    if (!position) return;
    auto next = *sketch_drag_document_;
    const auto next_sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (next_sketch == next.sketches.end() || !next_sketch->move_point(
            sketch_drag_point_id_, (*position)[0], (*position)[1])) {
        state_->setText(tr("Bod nelze přesunout mimo vazby nebo absolutní meze."));
        return;
    }
    sketch_drag_document_ = std::move(next);
    sketch_drag_changed_ = true;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto& calculated = part->session.calculated_boundaries();
    zima::kernel::ViewerMesh display = calculated.empty()
        ? zima::kernel::ViewerMesh{} : calculated.back().mesh;
    for (const auto& sketch : sketch_drag_document_->sketches) {
        append_mesh(display, sketch.viewer_mesh());
    }
    viewer_->set_mesh(std::move(display), false);
    state_->setText(tr("Tažení bodu: poloha je pouze transientní do puštění LMB."));
}

void AssemblyWorkspaceWindow::end_sketch_point_drag() {
    if (!sketch_drag_document_) return;
    auto document = std::move(*sketch_drag_document_);
    sketch_drag_document_.reset();
    sketch_drag_point_id_.clear();
    const bool changed = sketch_drag_changed_;
    sketch_drag_changed_ = false;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    if (changed) {
        part->session.commit(std::move(document), part->session.calculated_boundaries());
        preserve_view_on_refresh_ = true;
        refresh_tabs();
    }
    refresh_scene();
    state_->setText(changed ? tr("Poloha bodu byla uložena jako jedna Part revize.")
                              : tr("Poloha bodu se nezměnila."));
}

bool AssemblyWorkspaceWindow::delete_selected_sketch_geometry() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty() ||
        sketch_segment_active_ || sketch_rectangle_active_ || sketch_circle_active_ ||
        sketch_arc_active_ || sketch_ellipse_active_ || sketch_bspline_active_ ||
        sketch_coincident_active_ ||
        sketch_segment_pair_active_) return false;
    const std::string geometry_id = !selected_sketch_segment_id_.empty()
        ? selected_sketch_segment_id_
        : !selected_sketch_circle_id_.empty() ? selected_sketch_circle_id_
        : !selected_sketch_arc_id_.empty() ? selected_sketch_arc_id_
        : !selected_sketch_ellipse_id_.empty() ? selected_sketch_ellipse_id_
        : selected_sketch_bspline_id_;
    if (geometry_id.empty()) return false;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || workspace_.displayed_document_id() !=
            workspace_.active_document_id()) return false;
    try {
        auto next = part->session.document();
        const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == next.sketches.end()) return false;
        sketch->remove_geometry(geometry_id);
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        selected_sketch_segment_id_.clear();
        selected_sketch_circle_id_.clear();
        selected_sketch_arc_id_.clear();
        selected_sketch_ellipse_id_.clear();
        selected_sketch_bspline_id_.clear();
        selected_sketch_point_id_.clear();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("Geometrie skici byla odstraněna. Operaci lze vrátit přes Zpět."));
        return true;
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Geometrii nelze odstranit"), error.what());
        return true;
    }
}

void AssemblyWorkspaceWindow::show_sketch_dimension_properties(
    const std::string& sketch_id, const std::string& dimension_id,
    zima::sketcher::DimensionKind creation_kind) {
    if (properties_dialog_ != nullptr || sketch_segment_active_ ||
        sketch_rectangle_active_ || sketch_circle_active_ || sketch_arc_active_ ||
        sketch_coincident_active_ || sketch_segment_pair_active_) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || workspace_.displayed_document_id() !=
            workspace_.active_document_id()) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == sketch_id; });
    if (sketch == part->session.document().sketches.end()) return;
    const auto existing = std::find_if(sketch->dimensions.begin(), sketch->dimensions.end(),
        [&](const auto& value) { return value.id == dimension_id; });
    const bool edit_mode = existing != sketch->dimensions.end();
    if (!dimension_id.empty() && !edit_mode) return;
    if (!edit_mode && selected_sketch_segment_id_.empty() &&
        selected_sketch_circle_id_.empty() && selected_sketch_arc_id_.empty() &&
        selected_sketch_ellipse_id_.empty()) return;
    zima::sketcher::SketchDimension initial = edit_mode
        ? *existing
        : creation_kind == zima::sketcher::DimensionKind::Diameter &&
              !selected_sketch_circle_id_.empty()
            ? sketch->create_circle_diameter_dimension(selected_sketch_circle_id_)
        : !selected_sketch_circle_id_.empty()
            ? sketch->create_circle_radius_dimension(selected_sketch_circle_id_)
            : !selected_sketch_arc_id_.empty()
                ? sketch->create_arc_radius_dimension(selected_sketch_arc_id_)
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
    const std::string part_id = part->session.document().document_id;
    auto* dialog = new SketchDimensionPropertiesDialog(
        std::move(initial), edit_mode,
        [this, part_id, sketch_id](zima::sketcher::SketchDimension committed) {
            auto* target_part = workspace_.open_part(part_id);
            if (target_part == nullptr) throw std::runtime_error("Part is no longer open");
            auto next = target_part->session.document();
            const auto target_sketch = std::find_if(
                next.sketches.begin(), next.sketches.end(),
                [&](const auto& value) { return value.id == sketch_id; });
            if (target_sketch == next.sketches.end()) {
                throw std::runtime_error("Sketch no longer exists");
            }
            target_sketch->apply_dimension(std::move(committed));
            const auto calculated = target_part->session.calculated_boundaries();
            target_part->session.commit(std::move(next), calculated);
            selected_sketch_segment_id_.clear();
            selected_sketch_circle_id_.clear();
            selected_sketch_arc_id_.clear();
            preserve_view_on_refresh_ = true;
        }, this);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        refresh_tabs();
        refresh_scene();
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::preview_sketch_segment_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_segment_active_ || !pending_segment_start_) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) {
        viewer_->set_transient_edges({});
        return;
    }
    viewer_->set_transient_edges({{{
        sketch->world_point((*pending_segment_start_)[0], (*pending_segment_start_)[1]),
        sketch->world_point((*position)[0], (*position)[1])}, {}}});
}

bool AssemblyWorkspaceWindow::accept_sketch_rectangle_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_rectangle_active_) return false;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || active_sketch_id_.empty()) return false;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_rectangle_corner_) {
        pending_rectangle_corner_ = *position;
        state_->setText(tr("Obdélník skici: určete protilehlý roh."));
        return true;
    }
    if (std::abs((*position)[0] - (*pending_rectangle_corner_)[0]) <= 1.0e-9 ||
        std::abs((*position)[1] - (*pending_rectangle_corner_)[1]) <= 1.0e-9) {
        state_->setText(tr("Obdélník musí mít nenulovou šířku i výšku."));
        return true;
    }
    auto next = part->session.document();
    const auto target = std::find_if(next.sketches.begin(), next.sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (target == next.sketches.end()) return true;
    static_cast<void>(target->add_rectangle(
        (*pending_rectangle_corner_)[0], (*pending_rectangle_corner_)[1],
        (*position)[0], (*position)[1]));
    const auto calculated = part->session.calculated_boundaries();
    part->session.commit(std::move(next), calculated);
    pending_rectangle_corner_.reset();
    viewer_->set_transient_edges({});
    preserve_view_on_refresh_ = true;
    refresh_tabs();
    refresh_scene();
    state_->setText(tr("Obdélník vytvořen. Kliknutím určete první roh dalšího."));
    return true;
}

void AssemblyWorkspaceWindow::preview_sketch_rectangle_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_rectangle_active_ || !pending_rectangle_corner_) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return;
    const double x0 = (*pending_rectangle_corner_)[0];
    const double y0 = (*pending_rectangle_corner_)[1];
    const double x1 = (*position)[0];
    const double y1 = (*position)[1];
    const auto a = sketch->world_point(x0, y0);
    const auto b = sketch->world_point(x1, y0);
    const auto c = sketch->world_point(x1, y1);
    const auto d = sketch->world_point(x0, y1);
    viewer_->set_transient_edges({
        {{a, b}, {}}, {{b, c}, {}}, {{c, d}, {}}, {{d, a}, {}}});
}

bool AssemblyWorkspaceWindow::accept_sketch_circle_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_circle_active_) return false;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || active_sketch_id_.empty()) return false;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_circle_center_) {
        pending_circle_center_ = *position;
        state_->setText(tr("Kružnice skici: určete bod na obvodu."));
        return true;
    }
    const double radius = std::hypot(
        (*position)[0] - (*pending_circle_center_)[0],
        (*position)[1] - (*pending_circle_center_)[1]);
    if (radius <= 1.0e-9) {
        state_->setText(tr("Kružnice musí mít nenulový poloměr."));
        return true;
    }
    auto next = part->session.document();
    const auto target = std::find_if(next.sketches.begin(), next.sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (target == next.sketches.end()) return true;
    static_cast<void>(target->add_circle(
        (*pending_circle_center_)[0], (*pending_circle_center_)[1], radius));
    const auto calculated = part->session.calculated_boundaries();
    part->session.commit(std::move(next), calculated);
    pending_circle_center_.reset();
    viewer_->set_transient_edges({});
    preserve_view_on_refresh_ = true;
    refresh_tabs();
    refresh_scene();
    state_->setText(tr("Kružnice vytvořena. Kliknutím určete střed další kružnice."));
    return true;
}

void AssemblyWorkspaceWindow::preview_sketch_circle_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_circle_active_ || !pending_circle_center_) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return;
    const double radius = std::hypot(
        (*position)[0] - (*pending_circle_center_)[0],
        (*position)[1] - (*pending_circle_center_)[1]);
    zima::kernel::ViewerEdge preview;
    constexpr std::size_t samples = 96;
    for (std::size_t sample = 0; sample <= samples; ++sample) {
        const double angle = 2.0 * 3.14159265358979323846 *
            static_cast<double>(sample) / static_cast<double>(samples);
        preview.points.push_back(sketch->world_point(
            (*pending_circle_center_)[0] + radius * std::cos(angle),
            (*pending_circle_center_)[1] + radius * std::sin(angle)));
    }
    viewer_->set_transient_edges({std::move(preview)});
}

bool AssemblyWorkspaceWindow::accept_sketch_arc_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_arc_active_) return false;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || active_sketch_id_.empty()) return false;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_arc_center_) {
        pending_arc_center_ = *position;
        state_->setText(tr("Oblouk skici: určete počáteční bod."));
        return true;
    }
    if (!pending_arc_start_) {
        if (std::hypot((*position)[0] - (*pending_arc_center_)[0],
                       (*position)[1] - (*pending_arc_center_)[1]) <= 1.0e-9) {
            state_->setText(tr("Počáteční bod oblouku nesmí ležet ve středu."));
            return true;
        }
        pending_arc_start_ = *position;
        state_->setText(tr("Oblouk skici: určete koncový bod proti směru hodinových ručiček."));
        return true;
    }
    if (std::hypot((*position)[0] - (*pending_arc_center_)[0],
                   (*position)[1] - (*pending_arc_center_)[1]) <= 1.0e-9) {
        state_->setText(tr("Koncový bod oblouku nesmí ležet ve středu."));
        return true;
    }
    try {
        auto next = part->session.document();
        const auto target = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (target == next.sketches.end()) return true;
        static_cast<void>(target->add_arc(
            (*pending_arc_center_)[0], (*pending_arc_center_)[1],
            (*pending_arc_start_)[0], (*pending_arc_start_)[1],
            (*position)[0], (*position)[1]));
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        pending_arc_center_.reset();
        pending_arc_start_.reset();
        viewer_->set_transient_edges({});
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("Oblouk vytvořen. Kliknutím určete střed dalšího."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
    return true;
}

void AssemblyWorkspaceWindow::preview_sketch_arc_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_arc_active_ || !pending_arc_center_) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return;
    if (!pending_arc_start_) {
        viewer_->set_transient_edges({{{
            sketch->world_point((*pending_arc_center_)[0], (*pending_arc_center_)[1]),
            sketch->world_point((*position)[0], (*position)[1])}, {}}});
        return;
    }
    const double radius = std::hypot(
        (*pending_arc_start_)[0] - (*pending_arc_center_)[0],
        (*pending_arc_start_)[1] - (*pending_arc_center_)[1]);
    double start_angle = std::atan2(
        (*pending_arc_start_)[1] - (*pending_arc_center_)[1],
        (*pending_arc_start_)[0] - (*pending_arc_center_)[0]);
    double end_angle = std::atan2(
        (*position)[1] - (*pending_arc_center_)[1],
        (*position)[0] - (*pending_arc_center_)[0]);
    constexpr double full_turn = 2.0 * 3.14159265358979323846;
    while (end_angle <= start_angle) end_angle += full_turn;
    const double sweep = end_angle - start_angle;
    const auto samples = std::max<std::size_t>(2,
        static_cast<std::size_t>(std::ceil(96.0 * sweep / full_turn)));
    zima::kernel::ViewerEdge preview;
    for (std::size_t sample = 0; sample <= samples; ++sample) {
        const double angle = start_angle + sweep *
            static_cast<double>(sample) / static_cast<double>(samples);
        preview.points.push_back(sketch->world_point(
            (*pending_arc_center_)[0] + radius * std::cos(angle),
            (*pending_arc_center_)[1] + radius * std::sin(angle)));
    }
    viewer_->set_transient_edges({std::move(preview)});
}

bool AssemblyWorkspaceWindow::accept_sketch_ellipse_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_ellipse_active_) return false;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || active_sketch_id_.empty()) return false;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_ellipse_center_) {
        pending_ellipse_center_ = *position;
        state_->setText(tr("Elipsa skici: určete konec hlavní poloosy."));
        return true;
    }
    if (!pending_ellipse_major_) {
        if (std::hypot((*position)[0] - (*pending_ellipse_center_)[0],
                       (*position)[1] - (*pending_ellipse_center_)[1]) <= 1.0e-9) {
            state_->setText(tr("Hlavní poloosa elipsy musí mít nenulovou délku."));
            return true;
        }
        pending_ellipse_major_ = *position;
        state_->setText(tr("Elipsa skici: určete délku vedlejší poloosy."));
        return true;
    }
    try {
        auto next = part->session.document();
        const auto target = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (target == next.sketches.end()) return true;
        static_cast<void>(target->add_ellipse(
            (*pending_ellipse_center_)[0], (*pending_ellipse_center_)[1],
            (*pending_ellipse_major_)[0], (*pending_ellipse_major_)[1],
            (*position)[0], (*position)[1]));
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        pending_ellipse_center_.reset();
        pending_ellipse_major_.reset();
        viewer_->set_transient_edges({});
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("Elipsa vytvořena. Kliknutím určete střed další elipsy."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
    return true;
}

void AssemblyWorkspaceWindow::preview_sketch_ellipse_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_ellipse_active_ || !pending_ellipse_center_) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return;
    if (!pending_ellipse_major_) {
        viewer_->set_transient_edges({{{
            sketch->world_point((*pending_ellipse_center_)[0], (*pending_ellipse_center_)[1]),
            sketch->world_point((*position)[0], (*position)[1])}, {}}});
        return;
    }
    const double dx = (*pending_ellipse_major_)[0] - (*pending_ellipse_center_)[0];
    const double dy = (*pending_ellipse_major_)[1] - (*pending_ellipse_center_)[1];
    const double major_radius = std::hypot(dx, dy);
    const double rotation = std::atan2(dy, dx);
    const double minor_radius = std::abs(
        -((*position)[0] - (*pending_ellipse_center_)[0]) * std::sin(rotation) +
         ((*position)[1] - (*pending_ellipse_center_)[1]) * std::cos(rotation));
    if (major_radius <= 1.0e-9 || minor_radius <= 1.0e-9) return;
    zima::kernel::ViewerEdge preview;
    constexpr std::size_t samples = 96;
    preview.points.reserve(samples + 1);
    for (std::size_t sample = 0; sample <= samples; ++sample) {
        const double parameter = 2.0 * 3.14159265358979323846 *
            static_cast<double>(sample) / static_cast<double>(samples);
        const double local_x = major_radius * std::cos(parameter);
        const double local_y = minor_radius * std::sin(parameter);
        preview.points.push_back(sketch->world_point(
            (*pending_ellipse_center_)[0] + local_x * std::cos(rotation) -
                local_y * std::sin(rotation),
            (*pending_ellipse_center_)[1] + local_x * std::sin(rotation) +
                local_y * std::cos(rotation)));
    }
    viewer_->set_transient_edges({std::move(preview)});
}

void AssemblyWorkspaceWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && edge_treatment_selection_) {
        edge_treatment_selection_.reset();
        viewer_->set_selection_contract({zima::viewer::CandidateKind::Occurrence});
        state_->setText(tr("Zaoblení nebo sražení zrušeno."));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete && delete_selected_sketch_geometry()) {
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        finish_sketch_bspline()) {
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape &&
        (sketch_segment_active_ || sketch_rectangle_active_ || sketch_circle_active_ ||
         sketch_arc_active_ || sketch_ellipse_active_ || sketch_bspline_active_ ||
         sketch_coincident_active_ ||
         sketch_segment_pair_active_)) {
        cancel_sketch_segment();
        refresh_scene();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

std::optional<std::string> AssemblyWorkspaceWindow::resolve_active_occurrence(
    const std::string& part_document_id) const {
    const auto* assembly = workspace_.open_assembly(workspace_.displayed_document_id());
    if (assembly == nullptr) return std::string{};
    if (!active_occurrence_path_.empty()) {
        try {
            const auto address = workspace_.resolve_occurrence(
                workspace_.displayed_document_id(),
                zima::assembly::InstancePath::decode(active_occurrence_path_));
            if (address && address->source_document_id == part_document_id &&
                address->source_kind == zima::assembly::ComponentSourceKind::Part) {
                return active_occurrence_path_;
            }
        } catch (const std::invalid_argument&) {
            return std::nullopt;
        }
    }
    std::optional<std::string> only;
    std::set<std::string> paths;
    for (const auto& reference : assembly->session.document()
             .build_scene().original_references.triangle_references) {
        paths.insert(reference.instance_path);
    }
    for (const auto& path : paths) {
        const auto address = workspace_.resolve_occurrence(
            workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(path));
        if (!address || address->source_document_id != part_document_id) continue;
        if (only) return std::nullopt;
        only = path;
    }
    return only;
}

void AssemblyWorkspaceWindow::regenerate_active_part() {
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || properties_dialog_ != nullptr) return;
    try {
        part->session.update_calculated_boundaries(
            calculate_part(part->session.document()));
        refresh_tabs();
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Regenerace Partu selhala"), error.what());
    }
}

void AssemblyWorkspaceWindow::undo() {
    if (properties_dialog_ != nullptr) return;
    cancel_sketch_segment();
    if (auto* part = workspace_.open_part(workspace_.active_document_id())) {
        if (part->session.undo()) refresh_scene();
    } else if (auto* assembly = workspace_.open_assembly(workspace_.active_document_id())) {
        if (assembly->session.undo()) refresh_scene();
    }
    refresh_tabs();
}

void AssemblyWorkspaceWindow::redo() {
    if (properties_dialog_ != nullptr) return;
    cancel_sketch_segment();
    if (auto* part = workspace_.open_part(workspace_.active_document_id())) {
        if (part->session.redo()) refresh_scene();
    } else if (auto* assembly = workspace_.open_assembly(workspace_.active_document_id())) {
        if (assembly->session.redo()) refresh_scene();
    }
    refresh_tabs();
}

void AssemblyWorkspaceWindow::refresh_tabs() {
    tabs_->blockSignals(true);
    while (tabs_->count() > 0) tabs_->removeTab(0);
    int active_index = 0;
    for (const auto& state : workspace_.documents()) {
        std::visit([&](const auto& document) {
            using State = std::decay_t<decltype(document)>;
            const auto& model = document.session.document();
            const int index = tabs_->addTab(QString::fromStdString(
                model.name + (document.session.is_dirty() ? " *" : "")));
            tabs_->setTabData(index, QString::fromStdString(model.document_id));
            if (model.document_id == workspace_.active_document_id()) active_index = index;
        }, state);
    }
    tabs_->setCurrentIndex(active_index);
    tabs_->blockSignals(false);
}

void AssemblyWorkspaceWindow::refresh_scene() {
    tree_->clear();
    const auto* assembly = workspace_.open_assembly(workspace_.displayed_document_id());
    if (assembly == nullptr) {
        const auto* part = workspace_.open_part(workspace_.displayed_document_id());
        if (part == nullptr) {
            viewer_->set_mesh({});
            state_->setText(tr("Není vybrán zobrazovaný dokument."));
            return;
        }
        const auto& document = part->session.document();
        if (!active_sketch_id_.empty() && std::none_of(
                document.sketches.begin(), document.sketches.end(),
                [&](const auto& sketch) { return sketch.id == active_sketch_id_; })) {
            active_sketch_id_.clear();
            cancel_sketch_segment();
        }
        auto* root = new QTreeWidgetItem(tree_, {QString::fromStdString(document.name)});
        for (std::size_t index = 0; index < document.history.size(); ++index) {
            const auto& container = document.history[index];
            auto* item = new QTreeWidgetItem(root, {QString::fromStdString(container.name)});
            item->setData(0, Qt::UserRole, QString::fromStdString(container.id));
            item->setData(0, Qt::UserRole + 3, "part-container");
            if (part_rollback_ &&
                part_rollback_->part_document_id == document.document_id) {
                if (index == part_rollback_->history_limit) {
                    item->setForeground(0, QBrush(QColor(70, 190, 95)));
                    QFont font = item->font(0);
                    font.setBold(true);
                    item->setFont(0, font);
                } else if (index > part_rollback_->history_limit) {
                    item->setForeground(0, QBrush(QColor(125, 125, 125)));
                }
            }
        }
        for (const auto& sketch : document.sketches) {
            auto* item = new QTreeWidgetItem(root, {QString::fromStdString(sketch.name)});
            item->setData(0, Qt::UserRole, QString::fromStdString(sketch.id));
            item->setData(0, Qt::UserRole + 3, "part-sketch");
            for (const auto& dimension : sketch.dimensions) {
                const auto dimension_label = dimension.kind ==
                        zima::sketcher::DimensionKind::Radius
                    ? tr("Poloměr R%1 mm").arg(dimension.value, 0, 'f', 3)
                    : dimension.kind == zima::sketcher::DimensionKind::Diameter
                        ? tr("Průměr Ø%1 mm").arg(dimension.value, 0, 'f', 3)
                    : dimension.kind == zima::sketcher::DimensionKind::DistanceX
                        ? tr("Vodorovná kóta X %1 mm").arg(dimension.value, 0, 'f', 3)
                    : dimension.kind == zima::sketcher::DimensionKind::DistanceY
                        ? tr("Svislá kóta Y %1 mm").arg(dimension.value, 0, 'f', 3)
                    : dimension.kind == zima::sketcher::DimensionKind::Angle
                        ? tr("Úhlová kóta %1°").arg(dimension.value, 0, 'f', 3)
                    : dimension.kind == zima::sketcher::DimensionKind::EllipseMajorRadius
                        ? tr("Hlavní poloosa a=%1 mm").arg(dimension.value, 0, 'f', 3)
                    : dimension.kind == zima::sketcher::DimensionKind::EllipseMinorRadius
                        ? tr("Vedlejší poloosa b=%1 mm").arg(dimension.value, 0, 'f', 3)
                    : dimension.kind == zima::sketcher::DimensionKind::EllipseRotation
                        ? tr("Natočení elipsy %1°").arg(dimension.value, 0, 'f', 3)
                        : tr("Kóta %1 mm").arg(dimension.value, 0, 'f', 3);
                auto* child = new QTreeWidgetItem(item, {
                    dimension_label});
                child->setData(0, Qt::UserRole, QString::fromStdString(dimension.id));
                child->setData(0, Qt::UserRole + 3, "part-sketch-dimension");
                child->setData(0, Qt::UserRole + 4, QString::fromStdString(sketch.id));
            }
            item->setExpanded(true);
        }
        root->setExpanded(true);
        viewer_->set_selection_contract(sketch_coincident_active_
            ? std::vector{zima::viewer::CandidateKind::SketchPoint}
            : sketch_segment_pair_active_
                ? std::vector{zima::viewer::CandidateKind::SketchSegment}
            : active_sketch_id_.empty()
            ? std::vector{zima::viewer::CandidateKind::Container}
            : std::vector{zima::viewer::CandidateKind::SketchSegment,
                          zima::viewer::CandidateKind::SketchPoint,
                          zima::viewer::CandidateKind::SketchDimension,
                          zima::viewer::CandidateKind::SketchCurve});
        const auto& calculated = part->session.calculated_boundaries();
        if (part_rollback_ &&
            part_rollback_->part_document_id == document.document_id) {
            viewer_->set_mesh(part_rollback_->history_limit == 0 ||
                    calculated.size() < part_rollback_->history_limit
                ? zima::kernel::ViewerMesh{}
                : calculated[part_rollback_->history_limit - 1].mesh);
        } else {
            zima::kernel::ViewerMesh display = calculated.empty()
                ? zima::kernel::ViewerMesh{} : calculated.back().mesh;
            for (const auto& sketch : document.sketches) {
                append_mesh(display, sketch.viewer_mesh());
            }
            viewer_->set_mesh(std::move(display), !preserve_view_on_refresh_);
            preserve_view_on_refresh_ = false;
        }
        state_->setText(tr("Zobrazený Part: %1").arg(
            QString::fromStdString(document.name)));
        insert_action_->setEnabled(false);
        regenerate_action_->setEnabled(false);
        plane_mate_action_->setEnabled(false);
        axis_mate_action_->setEnabled(false);
        save_action_->setEnabled(true);
        box_action_->setEnabled(true);
        cylinder_action_->setEnabled(true);
        extrusion_action_->setEnabled(!active_sketch_id_.empty());
        revolution_action_->setEnabled(!active_sketch_id_.empty());
        sketch_action_->setEnabled(true);
        sketch_segment_action_->setEnabled(!active_sketch_id_.empty());
        sketch_rectangle_action_->setEnabled(!active_sketch_id_.empty());
        sketch_circle_action_->setEnabled(!active_sketch_id_.empty());
        sketch_arc_action_->setEnabled(!active_sketch_id_.empty());
        sketch_ellipse_action_->setEnabled(!active_sketch_id_.empty());
        sketch_bspline_action_->setEnabled(!active_sketch_id_.empty());
        sketch_horizontal_action_->setEnabled(!selected_sketch_segment_id_.empty());
        sketch_vertical_action_->setEnabled(!selected_sketch_segment_id_.empty());
        sketch_coincident_action_->setEnabled(!active_sketch_id_.empty());
        sketch_parallel_action_->setEnabled(!active_sketch_id_.empty());
        sketch_perpendicular_action_->setEnabled(!active_sketch_id_.empty());
        sketch_equal_length_action_->setEnabled(!active_sketch_id_.empty());
        sketch_dimension_action_->setEnabled(!selected_sketch_segment_id_.empty());
        sketch_dimension_x_action_->setEnabled(!selected_sketch_segment_id_.empty());
        sketch_dimension_y_action_->setEnabled(!selected_sketch_segment_id_.empty());
        sketch_angle_dimension_action_->setEnabled(!selected_sketch_segment_id_.empty());
        sketch_radius_dimension_action_->setEnabled(
            !selected_sketch_circle_id_.empty() || !selected_sketch_arc_id_.empty());
        sketch_diameter_dimension_action_->setEnabled(
            !selected_sketch_circle_id_.empty());
        sketch_ellipse_major_dimension_action_->setEnabled(
            !selected_sketch_ellipse_id_.empty());
        sketch_ellipse_minor_dimension_action_->setEnabled(
            !selected_sketch_ellipse_id_.empty());
        sketch_ellipse_rotation_dimension_action_->setEnabled(
            !selected_sketch_ellipse_id_.empty());
        sketch_fix_point_action_->setEnabled(!selected_sketch_point_id_.empty());
        regenerate_part_action_->setEnabled(true);
        undo_action_->setEnabled(part->session.can_undo());
        redo_action_->setEnabled(part->session.can_redo());
        return;
    }
    const auto& document = assembly->session.document();
    auto* root = new QTreeWidgetItem(tree_, {QString::fromStdString(document.name)});
    add_assembly_tree_children(root, document.document_id, {});
    if (document.document_id == workspace_.active_document_id()) {
        add_mate_tree_children(root, document.document_id);
    }
    root->setExpanded(true);
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Occurrence});
    if (part_rollback_ && !part_rollback_->instance_path.empty()) {
        const auto* active_part =
            workspace_.open_part(part_rollback_->part_document_id);
        if (active_part != nullptr) {
            const auto& boundaries = active_part->session.calculated_boundaries();
            zima::kernel::BodyResult boundary = part_rollback_->history_limit == 0 ||
                    boundaries.size() < part_rollback_->history_limit
                ? zima::kernel::BodyResult{}
                : boundaries[part_rollback_->history_limit - 1];
            viewer_->set_mesh(workspace_.build_scene_with_part_override(
                document.document_id,
                zima::assembly::InstancePath::decode(part_rollback_->instance_path),
                std::move(boundary)));
        } else {
            viewer_->set_mesh(document.build_scene());
        }
    } else {
        viewer_->set_mesh(document.build_scene());
    }
    state_->setText(tr("Zobrazená sestava: %1\nAktivní dokument: %2")
        .arg(QString::fromStdString(document.name),
             QString::fromStdString(workspace_.active_document_id())));
    insert_action_->setEnabled(
        workspace_.active_document_id() != workspace_.displayed_document_id() &&
        workspace_.find(workspace_.active_document_id()) != nullptr);
    regenerate_action_->setEnabled(true);
    plane_mate_action_->setEnabled(
        workspace_.open_assembly(workspace_.active_document_id()) != nullptr);
    axis_mate_action_->setEnabled(
        workspace_.open_assembly(workspace_.active_document_id()) != nullptr);
    save_action_->setEnabled(true);
    const auto* active_part = workspace_.open_part(workspace_.active_document_id());
    box_action_->setEnabled(active_part != nullptr);
    cylinder_action_->setEnabled(active_part != nullptr);
    extrusion_action_->setEnabled(active_part != nullptr && !active_sketch_id_.empty());
    revolution_action_->setEnabled(active_part != nullptr && !active_sketch_id_.empty());
    sketch_action_->setEnabled(active_part != nullptr);
    sketch_segment_action_->setEnabled(false);
    sketch_rectangle_action_->setEnabled(false);
    sketch_circle_action_->setEnabled(false);
    sketch_arc_action_->setEnabled(false);
    sketch_ellipse_action_->setEnabled(false);
    sketch_bspline_action_->setEnabled(false);
    sketch_horizontal_action_->setEnabled(false);
    sketch_vertical_action_->setEnabled(false);
    sketch_coincident_action_->setEnabled(false);
    sketch_parallel_action_->setEnabled(false);
    sketch_perpendicular_action_->setEnabled(false);
    sketch_equal_length_action_->setEnabled(false);
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
    regenerate_part_action_->setEnabled(active_part != nullptr);
    if (active_part != nullptr) {
        undo_action_->setEnabled(active_part->session.can_undo());
        redo_action_->setEnabled(active_part->session.can_redo());
    } else {
        const auto* active_assembly =
            workspace_.open_assembly(workspace_.active_document_id());
        undo_action_->setEnabled(active_assembly != nullptr &&
                                 active_assembly->session.can_undo());
        redo_action_->setEnabled(active_assembly != nullptr &&
                                 active_assembly->session.can_redo());
    }
}

void AssemblyWorkspaceWindow::add_assembly_tree_children(
    QTreeWidgetItem* parent,
    const std::string& assembly_document_id,
    const zima::assembly::InstancePath& parent_path,
    bool ancestor_suppressed) {
    const auto* assembly = workspace_.open_assembly(assembly_document_id);
    if (assembly == nullptr) return;
    add_snapshot_tree_children(
        parent, assembly->session.document().occurrence_snapshot(),
        assembly_document_id, parent_path, ancestor_suppressed);
}

void AssemblyWorkspaceWindow::add_snapshot_tree_children(
    QTreeWidgetItem* parent,
    const std::vector<zima::assembly::OccurrenceSnapshot>& snapshots,
    const std::string& owner_assembly_document_id,
    const zima::assembly::InstancePath& parent_path,
    bool ancestor_suppressed) {
    for (const auto& component : snapshots) {
        const bool suppressed = ancestor_suppressed ||
            component.manually_suppressed || component.dependency_suppressed;
        QString label = QString::fromStdString(component.name);
        if (component.manually_suppressed) label += tr(" [potlačeno]");
        else if (suppressed) {
            label += tr(" [potlačeno závislostí]");
        }
        else if (!component.visible) label += tr(" [skryto]");
        if (component.grounded) label += tr(" [uzemněno]");
        auto* item = new QTreeWidgetItem(parent, {label});
        const auto path = parent_path.child(component.occurrence_id);
        item->setData(0, Qt::UserRole, QString::fromStdString(component.occurrence_id));
        item->setData(0, Qt::UserRole + 1, QString::fromStdString(path.encoded()));
        item->setData(0, Qt::UserRole + 2,
                      QString::fromStdString(component.source_document_id));
        item->setData(0, Qt::UserRole + 3,
            component.source_kind == zima::assembly::ComponentSourceKind::Assembly
                ? "assembly-occurrence" : "part-occurrence");
        item->setData(0, Qt::UserRole + 4,
                      QString::fromStdString(owner_assembly_document_id));
        const bool active_assembly_occurrence =
            component.source_kind == zima::assembly::ComponentSourceKind::Assembly &&
            path.encoded() == active_occurrence_path_ &&
            component.source_document_id == workspace_.active_document_id();
        if ((part_rollback_ && path.encoded() == part_rollback_->instance_path) ||
            active_assembly_occurrence) {
            item->setForeground(0, QBrush(QColor(70, 190, 95)));
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
        } else if (suppressed || !component.visible) {
            item->setForeground(0, QBrush(QColor(125, 125, 125)));
        }
        if (component.source_kind == zima::assembly::ComponentSourceKind::Assembly) {
            const auto* active_source = active_assembly_occurrence
                ? workspace_.open_assembly(component.source_document_id) : nullptr;
            if (active_source != nullptr) {
                add_snapshot_tree_children(
                    item, active_source->session.document().occurrence_snapshot(),
                    component.source_document_id, path,
                    suppressed || !component.visible);
                add_mate_tree_children(item, component.source_document_id);
            } else {
                add_snapshot_tree_children(
                    item, component.children, component.source_document_id, path,
                    suppressed || !component.visible);
            }
            item->setExpanded(true);
        }
    }
}

void AssemblyWorkspaceWindow::add_mate_tree_children(
    QTreeWidgetItem* parent,
    const std::string& owner_assembly_document_id) {
    const auto* assembly = workspace_.open_assembly(owner_assembly_document_id);
    if (assembly == nullptr || assembly->session.document().mates.empty()) return;
    auto* mates_root = new QTreeWidgetItem(parent, {tr("Vazby")});
    for (const auto& mate : assembly->session.document().mates) {
        QString label = QString::fromStdString(mate.name);
        if (mate.suppressed) {
            label += tr(" [potlačeno]");
        } else if (mate.status == zima::assembly::MateStatus::Uncalculated) {
            label += tr(" [nevypočtená]");
        } else if (mate.status == zima::assembly::MateStatus::MissingReference) {
            label += tr(" [chybí reference]");
        } else if (mate.status == zima::assembly::MateStatus::UnsupportedGeometry) {
            label += tr(" [nepodporovaná geometrie]");
        }
        auto* item = new QTreeWidgetItem(mates_root, {label});
        item->setData(0, Qt::UserRole, QString::fromStdString(mate.mate_id));
        item->setData(0, Qt::UserRole + 3, "assembly-mate");
        item->setData(0, Qt::UserRole + 4,
                      QString::fromStdString(owner_assembly_document_id));
        if (mate.suppressed) {
            item->setForeground(0, QBrush(QColor(125, 125, 125)));
        } else if (mate.status == zima::assembly::MateStatus::MissingReference ||
                   mate.status == zima::assembly::MateStatus::UnsupportedGeometry) {
            item->setForeground(0, QBrush(QColor(205, 65, 65)));
        } else if (mate.status == zima::assembly::MateStatus::Uncalculated) {
            item->setForeground(0, QBrush(QColor(155, 105, 55)));
        }
    }
    mates_root->setExpanded(true);
}

void AssemblyWorkspaceWindow::select_container(const std::string& container_id) {
    auto* root = tree_->topLevelItem(0);
    if (root == nullptr) return;
    for (int index = 0; index < root->childCount(); ++index) {
        auto* item = root->child(index);
        if (item->data(0, Qt::UserRole).toString().toStdString() == container_id) {
            tree_->setCurrentItem(item);
            return;
        }
    }
}

void AssemblyWorkspaceWindow::select_occurrence(const std::string& instance_path) {
    auto* root = tree_->topLevelItem(0);
    if (root == nullptr) return;
    std::vector<QTreeWidgetItem*> pending{root};
    while (!pending.empty()) {
        auto* item = pending.back();
        pending.pop_back();
        if (item->data(0, Qt::UserRole + 1).toString().toStdString() == instance_path) {
            tree_->setCurrentItem(item);
            return;
        }
        for (int index = 0; index < item->childCount(); ++index) {
            pending.push_back(item->child(index));
        }
    }
}

void AssemblyWorkspaceWindow::show_component_properties(
    const std::string& instance_path) {
    if (properties_dialog_ != nullptr) return;
    std::optional<zima::workspace::OccurrenceAddress> address;
    try {
        address = workspace_.resolve_occurrence(
            workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(instance_path));
    } catch (const std::invalid_argument&) {
        return;
    }
    if (!address) return;
    auto* assembly = workspace_.open_assembly(address->owner_assembly_document_id);
    if (assembly == nullptr) return;
    const auto* occurrence = assembly->session.document().find_occurrence(
        address->occurrence_id);
    if (occurrence == nullptr) return;
    auto* dialog = new ComponentPropertiesDialog(
        *occurrence,
        [this, assembly_id = address->owner_assembly_document_id]
        (zima::assembly::PartOccurrence committed) {
            auto* assembly = workspace_.open_assembly(assembly_id);
            if (assembly == nullptr) {
                throw std::runtime_error("Assembly is no longer open");
            }
            auto next = assembly->session.document();
            auto found = std::find_if(next.components.begin(), next.components.end(),
                [&](const auto& item) { return item.occurrence_id == committed.occurrence_id; });
            if (found == next.components.end()) {
                throw std::runtime_error("Assembly occurrence no longer exists");
            }
            *found = std::move(committed);
            assembly->session.commit(std::move(next));
        }, this);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        refresh_scene();
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::show_component_context_menu(
    const std::string& instance_path, const QPoint& global_position) {
    std::optional<zima::workspace::OccurrenceAddress> address;
    try {
        address = workspace_.resolve_occurrence(
            workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(instance_path));
    } catch (const std::invalid_argument&) {
        return;
    }
    if (!address) return;
    auto* assembly = workspace_.open_assembly(address->owner_assembly_document_id);
    if (assembly == nullptr || properties_dialog_ != nullptr) return;
    const auto* occurrence = assembly->session.document().find_occurrence(
        address->occurrence_id);
    if (occurrence == nullptr) return;
    QMenu menu(this);
    auto* properties = menu.addAction(tr("Vlastnosti"));
    auto* visibility = menu.addAction(
        occurrence->visible ? tr("Skrýt") : tr("Zobrazit"));
    auto* suppression = menu.addAction(
        occurrence->suppressed ? tr("Obnovit") : tr("Potlačit"));
    auto* grounding = menu.addAction(
        occurrence->grounded ? tr("Uvolnit") : tr("Uzemnit"));
    const QAction* selected = menu.exec(global_position);
    if (selected == properties) {
        show_component_properties(instance_path);
        return;
    }
    if (selected != visibility && selected != suppression && selected != grounding) return;
    auto next = assembly->session.document();
    auto found = std::find_if(next.components.begin(), next.components.end(),
        [&](const auto& item) { return item.occurrence_id == address->occurrence_id; });
    if (found == next.components.end()) return;
    if (selected == visibility) found->visible = !found->visible;
    if (selected == suppression) found->suppressed = !found->suppressed;
    if (selected == grounding) found->grounded = !found->grounded;
    next.calculate_mates();
    assembly->session.commit(std::move(next));
    refresh_tabs();
    refresh_scene();
}

void AssemblyWorkspaceWindow::show_mate_properties(
    const std::string& assembly_document_id,
    const std::string& mate_id) {
    if (properties_dialog_ != nullptr ||
        assembly_document_id != workspace_.active_document_id()) return;
    auto* assembly = workspace_.open_assembly(assembly_document_id);
    const auto* mate = assembly == nullptr
        ? nullptr : assembly->session.document().find_mate(mate_id);
    if (mate == nullptr) return;
    auto* dialog = new MatePropertiesDialog(
        *mate,
        [this, assembly_document_id](zima::assembly::AssemblyMate committed) {
            auto* target = workspace_.open_assembly(assembly_document_id);
            if (target == nullptr) throw std::runtime_error("Assembly is no longer open");
            auto next = target->session.document();
            next.replace_mate(std::move(committed));
            next.calculate_mates();
            target->session.commit(std::move(next));
        }, this);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        refresh_tabs();
        refresh_scene();
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::show_mate_context_menu(
    const std::string& assembly_document_id,
    const std::string& mate_id,
    const QPoint& global_position) {
    if (properties_dialog_ != nullptr ||
        assembly_document_id != workspace_.active_document_id()) return;
    auto* assembly = workspace_.open_assembly(assembly_document_id);
    const auto* mate = assembly == nullptr
        ? nullptr : assembly->session.document().find_mate(mate_id);
    if (mate == nullptr) return;
    QMenu menu(this);
    auto* properties = menu.addAction(tr("Vlastnosti"));
    auto* suppression = menu.addAction(
        mate->suppressed ? tr("Obnovit") : tr("Potlačit"));
    auto* remove = menu.addAction(tr("Smazat"));
    const QAction* selected = menu.exec(global_position);
    if (selected == properties) {
        show_mate_properties(assembly_document_id, mate_id);
        return;
    }
    if (selected != suppression && selected != remove) return;
    if (selected == remove && QMessageBox::question(
            this, tr("Smazat vazbu"), tr("Opravdu chcete vazbu smazat?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    auto next = assembly->session.document();
    if (selected == remove) {
        next.remove_mate(mate_id);
    } else {
        auto* edited = next.find_mate(mate_id);
        if (edited == nullptr) return;
        edited->suppressed = !edited->suppressed;
        next.calculate_mates();
    }
    assembly->session.commit(std::move(next));
    refresh_tabs();
    refresh_scene();
}

}  // namespace zima::app
