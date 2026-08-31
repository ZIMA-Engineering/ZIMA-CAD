#include "primitive_properties_dialog.hpp"
#include "construction_properties_dialog.hpp"
#include "component_properties_dialog.hpp"
#include "sketch_properties_dialog.hpp"
#include "sketch_dimension_properties_dialog.hpp"
#include "sketch_text_properties_dialog.hpp"
#include "file_dialog.hpp"
#include "document_tools_dialogs.hpp"
#include "construction_reference_candidate_policy.hpp"
#include "extrusion_dimension_policy.hpp"

#include <zima/viewer/mesh_view.hpp>

#include <QAction>
#include <QApplication>
#include <QAbstractProxyModel>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QIcon>
#include <QMouseEvent>
#include <QListWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTimer>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QWidget>
#include <QWheelEvent>
#include <QVBoxLayout>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QWidget parent;
    parent.resize(900, 650);
    parent.show();
    const auto initial = zima::document::PartDocument::create_box_container();

    try {
        bool cancel_committed = false;
        auto* cancel_dialog = new zima::app::PrimitivePropertiesDialog(
            initial, false, false,
            [&](zima::document::HistoryContainer) { cancel_committed = true; },
            &parent);
        cancel_dialog->show();
        application.processEvents();
        require(cancel_dialog->isSizeGripEnabled(),
                "Shared internal Properties windows must be mouse-resizable");
        cancel_dialog->buttons()->button(QDialogButtonBox::Cancel)->click();
        application.processEvents();
        require(!cancel_committed, "Cancel committed pending Box changes");

        int ok_commits = 0;
        zima::document::HistoryContainer committed;
        auto* ok_dialog = new zima::app::PrimitivePropertiesDialog(
            initial, false, false,
            [&](zima::document::HistoryContainer value) {
                ++ok_commits;
                committed = std::move(value);
            },
            &parent);
        ok_dialog->show();
        application.processEvents();
        require(ok_dialog->windowTitle() == QStringLiteral("Kvádr"),
                "Create and edit must share the feature-only dialog title");
        auto* length = ok_dialog->findChild<QDoubleSpinBox*>("boxLength");
        require(length != nullptr, "Box dialog must expose its length");
        int box_preview_updates = 0;
        double previewed_box_length = 0.0;
        ok_dialog->set_preview_callback(
            [&](const zima::document::HistoryContainer& preview) {
                ++box_preview_updates;
                previewed_box_length = preview.box.length;
            });
        length->setValue(125.0);
        require(box_preview_updates >= 2 && previewed_box_length == 125.0,
                "Changing a Box dimension did not update its wire preview");
        const auto translations =
            ok_dialog->findChildren<QDoubleSpinBox*>("primitiveTranslation");
        require(translations.size() == 3,
                "Box dialog must expose three translation coordinates");
        translations.front()->setValue(42.0);
        ok_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(ok_commits == 1, "OK must commit exactly once");
        require(committed.box.length == 125.0, "OK did not commit edited length");
        require(committed.placement.x == 42.0,
                "OK did not commit container placement");

        auto fillet = initial;
        fillet.feature_kind = zima::document::FeatureKind::Fillet;
        fillet.name = "Zaoblení";
        fillet.edge_treatment.edges.clear();
        zima::document::HistoryContainer committed_fillet;
        auto* fillet_dialog = new zima::app::PrimitivePropertiesDialog(
            fillet, false, false,
            [&](zima::document::HistoryContainer value) {
                committed_fillet = std::move(value);
            }, &parent);
        fillet_dialog->show();
        fillet_dialog->set_edge_references({{"body-owner", "edge:stable", {}}});
        require(fillet_dialog->windowTitle() == QStringLiteral("Zaoblení"),
                "Fillet create/edit dialog title is not feature-only");
        auto* fillet_routes = fillet_dialog->findChild<QTreeWidget*>(
            "edgeTreatmentEdges");
        require(fillet_routes != nullptr && fillet_routes->columnCount() == 2 &&
                    fillet_routes->topLevelItemCount() == 1 &&
                    fillet_routes->topLevelItem(0)->childCount() == 1,
                "Fillet dialog did not expose the Python-style route/object tree");
        require(fillet_dialog->findChild<QLineEdit*>()->isHidden(),
                "Fillet dialog retained the unrelated container-name editor");
        fillet_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(committed_fillet.edge_treatment.edges.size() == 1,
                "Fillet dialog did not retain edges selected while it was open");

        int middle_commits = 0;
        zima::document::CombineMode middle_operation =
            zima::document::CombineMode::Add;
        auto* middle_dialog = new zima::app::PrimitivePropertiesDialog(
            initial, true, true,
            [&](zima::document::HistoryContainer value) {
                ++middle_commits;
                middle_operation = value.combine_mode;
            },
            &parent);
        middle_dialog->show();
        application.processEvents();
        auto* operation = middle_dialog->findChild<QComboBox*>();
        require(operation != nullptr, "Box dialog has no operation selector");
        operation->setCurrentIndex(operation->findData("subtract"));
        QMouseEvent middle_press(
            QEvent::MouseButtonPress,
            QPointF(50.0, 50.0),
            QPointF(50.0, 50.0),
            QPointF(50.0, 50.0),
            Qt::MiddleButton,
            Qt::MiddleButton,
            Qt::NoModifier);
        QApplication::sendEvent(&parent, &middle_press);
        QMouseEvent middle_release(
            QEvent::MouseButtonRelease,
            QPointF(50.0, 50.0),
            QPointF(50.0, 50.0),
            QPointF(50.0, 50.0),
            Qt::MiddleButton,
            Qt::NoButton,
            Qt::NoModifier);
        QApplication::sendEvent(&parent, &middle_release);
        application.processEvents();
        require(middle_commits == 0,
                "A short middle-button click confirmed Properties");
        QMouseEvent middle_double_click(
            QEvent::MouseButtonDblClick,
            QPointF(50.0, 50.0),
            QPointF(50.0, 50.0),
            QPointF(50.0, 50.0),
            Qt::MiddleButton,
            Qt::MiddleButton,
            Qt::NoModifier);
        QApplication::sendEvent(&parent, &middle_double_click);
        application.processEvents();
        require(middle_commits == 1,
                "Middle-button double-click outside dialog did not invoke OK");
        require(middle_operation == zima::document::CombineMode::Subtract,
                "Properties dialog did not commit subtract mode");

        int view_middle_commits = 0;
        auto* view_middle_dialog = new zima::app::PrimitivePropertiesDialog(
            initial, false, false,
            [&](zima::document::HistoryContainer) { ++view_middle_commits; },
            &parent);
        zima::viewer::MeshView view(&parent);
        view.setGeometry(0, 0, 500, 360);
        view.show();
        zima::kernel::ViewerMesh empty_document_mesh;
        empty_document_mesh.axes.push_back({
            {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 8.0,
            {"part-origin", "origin:axis:x", {}}});
        view.set_mesh(std::move(empty_document_mesh));
        const auto startup_camera = view.camera_state();
        require(startup_camera[4] >= 50.0F && startup_camera[7] < startup_camera[4],
                "Empty document camera does not expose a useful metric working area "
                "independently of the screen-constant Origin size");
        view_middle_dialog->show();
        application.processEvents();
        QMouseEvent view_middle_double_click(
            QEvent::MouseButtonDblClick,
            QPointF(250.0, 180.0),
            QPointF(250.0, 180.0),
            QPointF(250.0, 180.0),
            Qt::MiddleButton,
            Qt::MiddleButton,
            Qt::NoModifier);
        QApplication::sendEvent(&view, &view_middle_double_click);
        application.processEvents();
        require(view_middle_commits == 1,
                "Middle-button double-click over the owning view did not invoke OK");

        zima::kernel::ViewerMesh selection_mesh;
        selection_mesh.vertices = {
            {-0.5, -0.5, 0.0}, {0.5, -0.5, 0.0}, {0.0, 0.5, 0.0}};
        selection_mesh.triangles = {0, 1, 2};
        selection_mesh.triangle_references.push_back(
            {"selection-owner", "face", {}});
        zima::viewer::MeshView selection_view(&parent);
        selection_view.setGeometry(0, 0, 500, 360);
        selection_view.set_mesh(std::move(selection_mesh));
        selection_view.show();
        application.processEvents();
        selection_view.confirm_container("selection-owner");
        require(selection_view.confirmed_candidate().has_value(),
                "Selection fixture did not confirm its candidate");
        int empty_selection_callbacks = 0;
        selection_view.set_empty_confirmation_callback(
            [&] { ++empty_selection_callbacks; });
        QMouseEvent empty_left_click(
            QEvent::MouseButtonPress,
            QPointF(5.0, 5.0),
            QPointF(5.0, 5.0),
            QPointF(5.0, 5.0),
            Qt::LeftButton,
            Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&selection_view, &empty_left_click);
        application.processEvents();
        require(!selection_view.confirmed_candidate().has_value(),
                "Empty LMB click left stale view selection confirmed");
        require(empty_selection_callbacks == 1,
                "Empty LMB click did not notify tree/view selection clearing");

        zima::kernel::ViewerMesh face_cycle_mesh;
        face_cycle_mesh.vertices = {
            {-1.0, -1.0, 1.0}, {1.0, -1.0, 1.0},
            {1.0, 1.0, 1.0}, {-1.0, 1.0, 1.0},
            {-1.0, -1.0, -1.0}, {1.0, -1.0, -1.0},
            {1.0, 1.0, -1.0}, {-1.0, 1.0, -1.0}};
        face_cycle_mesh.triangles = {
            0, 1, 2, 0, 2, 3,
            4, 5, 6, 4, 6, 7};
        face_cycle_mesh.triangle_references = {
            {"front-owner", "front-face", {}},
            {"front-owner", "front-face", {}},
            {"back-owner", "back-face", {}},
            {"back-owner", "back-face", {}}};
        face_cycle_mesh.original_references.vertices =
            face_cycle_mesh.vertices;
        face_cycle_mesh.original_references.triangles =
            face_cycle_mesh.triangles;
        face_cycle_mesh.original_references.triangle_references = {
            {"front-owner", "front-face", {}},
            {"front-owner", "front-face", {}},
            {"back-owner", "back-face", {}},
            {"back-owner", "back-face", {}}};
        zima::viewer::MeshView face_cycle_view(&parent);
        face_cycle_view.setGeometry(0, 0, 500, 360);
        face_cycle_view.set_mesh(std::move(face_cycle_mesh));
        face_cycle_view.set_selection_contract(
            {zima::viewer::CandidateKind::Face});
        face_cycle_view.set_candidate_filter([](const auto& candidate) {
            return candidate.geometry ==
                zima::viewer::CandidateGeometry::Display;
        });
        face_cycle_view.show();
        face_cycle_view.fit_all();
        application.processEvents();
        const QPointF face_pointer(250.0, 180.0);
        const auto face_candidates =
            face_cycle_view.selection_candidates_at(face_pointer);
        require(face_candidates.size() == 2,
                "Face command did not retain front and hidden back faces in "
                "one ordered candidate list");
        QMouseEvent face_hover(
            QEvent::MouseMove, face_pointer, face_pointer, face_pointer,
            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&face_cycle_view, &face_hover);
        const auto first_face = face_cycle_view.hovered_candidate();
        QMouseEvent next_face(
            QEvent::MouseButtonPress, face_pointer, face_pointer, face_pointer,
            Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(&face_cycle_view, &next_face);
        const auto second_face = face_cycle_view.hovered_candidate();
        require(first_face && second_face &&
                    first_face->owner_id != second_face->owner_id &&
                    second_face->owner_id == face_candidates[1].owner_id,
                "RMB did not advance hover to the face behind the front face");

        zima::kernel::ViewerMesh zero_dimension_mesh;
        zero_dimension_mesh.axes.push_back({
            {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 20.0,
            {"sketch", "sketch_axis:y", {}}});
        zero_dimension_mesh.points.push_back({
            {0.0, 0.0, 0.0}, {"sketch", "point:dimension-owner", {}}});
        zero_dimension_mesh.constraint_markers.push_back({
            {0.0, 0.0, 0.0}, "C", {"sketch", "constraint:on-axis", {}},
            {"point:dimension-owner", "sketch_axis:y"}});
        zero_dimension_mesh.dimensions.push_back({
            {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0},
            {15.0, 0.0, 0.0}, {15.0, 0.0, 0.0}, 0.0,
            {"sketch", "dimension:zero-y", {}}, "Y "});
        zima::viewer::MeshView zero_dimension_view(&parent);
        zero_dimension_view.set_dimension_decimal_places(5);
        require(zero_dimension_view.dimension_decimal_places() == 5,
                "MeshView did not retain the document dimension precision");
        zero_dimension_view.set_dimension_decimal_places(99);
        require(zero_dimension_view.dimension_decimal_places() == 12,
                "MeshView did not clamp unsafe dimension precision");
        zero_dimension_view.set_dimension_decimal_places(2);
        zero_dimension_view.setGeometry(0, 0, 500, 360);
        zero_dimension_view.set_mesh(std::move(zero_dimension_mesh));
        zero_dimension_view.show();
        application.processEvents();
        zero_dimension_view.confirm_reference("sketch", "dimension:zero-y", {},
            zima::viewer::CandidateKind::Dimension);
        const auto zero_dimension_candidate =
            zero_dimension_view.confirmed_candidate();
        const auto zero_dimension_label = zero_dimension_candidate
            ? zero_dimension_view.candidate_dimension_label_position(
                  *zero_dimension_candidate)
            : std::nullopt;
        require(zero_dimension_label.has_value(),
                "Zero-valued point dimension lost its numeric label position");
        // Keep a Dimension confirmed while beginning the next gesture. This
        // reproduces dragging its owning point after selecting the Dimension
        // (including a Dimension reached through RMB candidate cycling).
        zero_dimension_view.confirm_reference("sketch", "dimension:zero-y", {},
            zima::viewer::CandidateKind::Dimension);
        zero_dimension_view.set_selection_contract(
            {zima::viewer::CandidateKind::Dimension});
        QMouseEvent zero_dimension_select(
            QEvent::MouseButtonPress, QPointF(*zero_dimension_label),
            QPointF(*zero_dimension_label), QPointF(*zero_dimension_label),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&zero_dimension_view, &zero_dimension_select);
        application.processEvents();
        const auto selected_zero_dimension =
            zero_dimension_view.confirmed_candidate();
        require(selected_zero_dimension.has_value() &&
                    selected_zero_dimension->kind ==
                        zima::viewer::CandidateKind::Dimension &&
                    selected_zero_dimension->semantic_key == "dimension:zero-y",
                "LMB on a dimension annotation did not confirm the exact dimension");
        int dimension_context_requests{};
        zero_dimension_view.set_context_menu_callback(
            [&](const auto& candidate, const QPoint&) {
                if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
                    candidate.semantic_key == "dimension:zero-y") {
                    ++dimension_context_requests;
                }
            });
        QMouseEvent zero_dimension_context(
            QEvent::MouseButtonPress, QPointF(*zero_dimension_label),
            QPointF(*zero_dimension_label), QPointF(*zero_dimension_label),
            Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(&zero_dimension_view, &zero_dimension_context);
        application.processEvents();
        require(dimension_context_requests == 1,
                "RMB on a confirmed dimension did not request its context menu");
        int zero_dimension_edits{};
        zero_dimension_view.set_double_confirmation_callback(
            [&](const auto& candidate) {
                if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
                    candidate.semantic_key == "dimension:zero-y") {
                    ++zero_dimension_edits;
                }
            });
        QMouseEvent zero_dimension_double_click(
            QEvent::MouseButtonDblClick, QPointF(*zero_dimension_label),
            QPointF(*zero_dimension_label), QPointF(*zero_dimension_label),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(
            &zero_dimension_view, &zero_dimension_double_click);
        application.processEvents();
        require(zero_dimension_edits == 1,
                "Zero-valued point dimension label could not be double-click edited");
        zero_dimension_view.clear_selection();
        zero_dimension_view.set_selection_contract({
            zima::viewer::CandidateKind::SketchPoint,
            zima::viewer::CandidateKind::Dimension,
            zima::viewer::CandidateKind::SketchConstraint,
            zima::viewer::CandidateKind::SketchAxis});
        std::optional<QPointF> point_dimension_overlap;
        for (int y = 0; y < zero_dimension_view.height() && !point_dimension_overlap;
             ++y) {
            for (int x = 0; x < zero_dimension_view.width(); ++x) {
                const auto candidates = zero_dimension_view.selection_candidates_at(
                    QPointF(x, y));
                const bool point = std::ranges::any_of(candidates,
                    [](const auto& candidate) {
                        return candidate.kind ==
                                zima::viewer::CandidateKind::SketchPoint &&
                            candidate.semantic_key == "point:dimension-owner";
                    });
                const bool dimension = std::ranges::any_of(candidates,
                    [](const auto& candidate) {
                        return candidate.kind ==
                                zima::viewer::CandidateKind::Dimension &&
                            candidate.semantic_key == "dimension:zero-y";
                    });
                if (point && dimension) {
                    point_dimension_overlap = QPointF(x, y);
                    break;
                }
            }
        }
        require(point_dimension_overlap.has_value(),
                "Dimension witness did not overlap its owning Sketch point in the test view");
        std::optional<zima::viewer::ViewerCandidate> overlap_drag;
        zero_dimension_view.set_candidate_drag_callbacks(
            [&](const auto& candidate, const auto&, const auto&) {
                overlap_drag = candidate;
                return true;
            }, [](const auto&, const auto&) {}, [] {});
        QMouseEvent overlap_press(QEvent::MouseButtonPress,
            *point_dimension_overlap, *point_dimension_overlap,
            *point_dimension_overlap, Qt::LeftButton, Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&zero_dimension_view, &overlap_press);
        require(overlap_drag && overlap_drag->kind ==
                    zima::viewer::CandidateKind::SketchPoint,
                "Overlapping Dimension stole the owning Sketch point drag");
        QMouseEvent overlap_release(QEvent::MouseButtonRelease,
            *point_dimension_overlap, *point_dimension_overlap,
            *point_dimension_overlap, Qt::LeftButton, Qt::NoButton,
            Qt::NoModifier);
        QApplication::sendEvent(&zero_dimension_view, &overlap_release);

        auto cylinder_initial =
            zima::document::PartDocument::create_cylinder_container();
        int cylinder_commits = 0;
        zima::document::HistoryContainer committed_cylinder;
        auto* cylinder_dialog = new zima::app::PrimitivePropertiesDialog(
            cylinder_initial, false, false,
            [&](zima::document::HistoryContainer value) {
                ++cylinder_commits;
                committed_cylinder = std::move(value);
            }, &parent);
        cylinder_dialog->show();
        application.processEvents();
        auto* radius = cylinder_dialog->findChild<QDoubleSpinBox*>("cylinderRadius");
        auto* height = cylinder_dialog->findChild<QDoubleSpinBox*>("cylinderHeight");
        require(radius != nullptr && height != nullptr,
                "Cylinder dialog does not expose its parameters");
        require(cylinder_dialog->findChild<QTableWidget*>(
                    "primitiveReferenceTable") != nullptr &&
                    cylinder_dialog->findChild<QTableWidget*>(
                        "primitiveOrientationTable") != nullptr,
                "Cylinder Properties did not receive the universal placement tables");
        require(cylinder_dialog->set_reference(
                    0, {{}, "part-origin", "origin:point"}, "Počátek dílu"),
                "Cylinder Properties rejected its placement reference");
        radius->setValue(17.0);
        height->setValue(63.0);
        cylinder_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(cylinder_commits == 1 &&
                    committed_cylinder.feature_kind ==
                        zima::document::FeatureKind::Cylinder &&
                    committed_cylinder.cylinder.radius == 17.0 &&
                    committed_cylinder.cylinder.height == 63.0 &&
                    committed_cylinder.placement.references.size() == 1 &&
                    committed_cylinder.placement.references[0].owner_id ==
                        "part-origin",
                "Cylinder Properties did not commit exact parameters or its "
                "universal placement reference");

        auto sphere_initial = zima::document::PartDocument::create_sphere_container();
        zima::document::HistoryContainer committed_sphere;
        auto* sphere_dialog = new zima::app::PrimitivePropertiesDialog(
            sphere_initial, false, false,
            [&](zima::document::HistoryContainer value) {
                committed_sphere = std::move(value);
            }, &parent);
        sphere_dialog->show();
        application.processEvents();
        auto* sphere_radius = sphere_dialog->findChild<QDoubleSpinBox*>("sphereRadius");
        require(sphere_radius != nullptr, "Sphere dialog does not expose its radius");
        require(sphere_dialog->findChild<QTableWidget*>("primitiveReferenceTable") !=
                    nullptr,
                "Sphere Properties did not receive the universal placement table");
        auto* front_button = sphere_dialog->findChild<QPushButton*>(
            "containerOrientationFront");
        auto* back_button = sphere_dialog->findChild<QPushButton*>(
            "containerOrientationBack");
        auto* rotate_button = sphere_dialog->findChild<QPushButton*>(
            "containerOrientationRotate");
        require(front_button == nullptr && back_button == nullptr &&
                    rotate_button == nullptr &&
                    std::ranges::none_of(sphere_dialog->findChildren<QLabel*>(),
                        [](const auto* label) {
                            return label->isVisible() &&
                                label->text() == QStringLiteral("Pohled na skicu");
                        }) &&
                    sphere_dialog->findChild<QTableWidget*>(
                        "primitiveOrientationTable")->isHidden(),
                "Primitive Properties exposes Sketch-only view controls");
        auto* sphere_absolute_rx = sphere_dialog->findChild<QDoubleSpinBox*>(
            "containerRotationX");
        const auto sphere_corrections = sphere_dialog->findChildren<QDoubleSpinBox*>(
            "primitiveRotation");
        require(sphere_absolute_rx != nullptr && sphere_absolute_rx->isEnabled() &&
                    sphere_corrections.size() == 3 &&
                    std::ranges::none_of(sphere_corrections,
                        [](const auto* field) { return field->isEnabled(); }),
                "Reference-free primitive enables correction instead of absolute rotation");
        sphere_dialog->set_orientation_base_rotation({10.0, 20.0, 30.0}, true);
        require(!sphere_absolute_rx->isEnabled() &&
                    std::ranges::all_of(sphere_corrections,
                        [](const auto* field) { return field->isEnabled(); }),
                "Referenced primitive did not lock its base rotation and enable correction");
        sphere_dialog->set_orientation_base_rotation({10.0, 20.0, 30.0}, false);
        sphere_radius->setValue(27.0);
        sphere_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(committed_sphere.feature_kind == zima::document::FeatureKind::Sphere &&
                    committed_sphere.sphere.radius == 27.0,
                "Sphere Properties did not commit its radius");
        const std::vector<zima::kernel::EdgeReference> treatment_edges{
            {"source-feature", "generated:source-edge", {}}};
        for (const auto kind : {zima::document::FeatureKind::Fillet,
                                zima::document::FeatureKind::Chamfer}) {
            auto treatment_initial = kind == zima::document::FeatureKind::Fillet
                ? zima::document::PartDocument::create_fillet_container(treatment_edges)
                : zima::document::PartDocument::create_chamfer_container(treatment_edges);
            zima::document::HistoryContainer committed_treatment;
            auto* treatment_dialog = new zima::app::PrimitivePropertiesDialog(
                treatment_initial, true, false,
                [&](zima::document::HistoryContainer value) {
                    committed_treatment = std::move(value);
                }, &parent);
            treatment_dialog->show();
            application.processEvents();
            auto* treatment_size = treatment_dialog->findChild<QDoubleSpinBox*>(
                "edgeTreatmentSize");
            require(treatment_size != nullptr,
                    "Fillet/Chamfer edit did not reuse Primitive Properties");
            treatment_size->setValue(kind == zima::document::FeatureKind::Fillet
                ? 2.5 : 3.5);
            treatment_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
            application.processEvents();
            require(committed_treatment.feature_kind == kind &&
                        committed_treatment.edge_treatment.edges == treatment_edges &&
                        committed_treatment.edge_treatment.size ==
                            (kind == zima::document::FeatureKind::Fillet ? 2.5 : 3.5),
                    "Fillet/Chamfer Properties lost its input edge identity or size");
        }
        auto cone_initial = zima::document::PartDocument::create_cone_container();
        zima::document::HistoryContainer committed_cone;
        auto* cone_dialog = new zima::app::PrimitivePropertiesDialog(
            cone_initial, false, false,
            [&](zima::document::HistoryContainer value) {
                committed_cone = std::move(value);
            }, &parent);
        cone_dialog->show();
        application.processEvents();
        require(cone_dialog->findChild<QTableWidget*>("primitiveReferenceTable") !=
                    nullptr,
                "Cone Properties did not receive the universal placement table");
        cone_dialog->findChild<QDoubleSpinBox*>("coneBottomRadius")->setValue(25.0);
        cone_dialog->findChild<QDoubleSpinBox*>("coneTopRadius")->setValue(5.0);
        cone_dialog->findChild<QDoubleSpinBox*>("coneHeight")->setValue(70.0);
        cone_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(committed_cone.feature_kind == zima::document::FeatureKind::Cone &&
                    committed_cone.cone.bottom_radius == 25.0 &&
                    committed_cone.cone.top_radius == 5.0 &&
                    committed_cone.cone.height == 70.0,
                "Cone Properties did not commit exact parameters");

        auto pyramid_initial = zima::document::PartDocument::create_pyramid_container();
        zima::document::HistoryContainer committed_pyramid;
        auto* pyramid_dialog = new zima::app::PrimitivePropertiesDialog(
            pyramid_initial, false, false,
            [&](zima::document::HistoryContainer value) {
                committed_pyramid = std::move(value);
            }, &parent);
        pyramid_dialog->show();
        application.processEvents();
        require(pyramid_dialog->findChild<QTableWidget*>("primitiveReferenceTable") !=
                    nullptr,
                "Pyramid Properties did not receive the universal placement table");
        pyramid_dialog->findChild<QDoubleSpinBox*>("pyramidLength")->setValue(30.0);
        pyramid_dialog->findChild<QDoubleSpinBox*>("pyramidWidth")->setValue(20.0);
        pyramid_dialog->findChild<QDoubleSpinBox*>("pyramidHeight")->setValue(40.0);
        pyramid_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(committed_pyramid.feature_kind == zima::document::FeatureKind::Pyramid &&
                    committed_pyramid.pyramid.length == 30.0 &&
                    committed_pyramid.pyramid.width == 20.0 &&
                    committed_pyramid.pyramid.height == 40.0,
                "Pyramid Properties did not commit exact parameters");

        auto wedge_initial = zima::document::PartDocument::create_wedge_container();
        zima::document::HistoryContainer committed_wedge;
        auto* wedge_dialog = new zima::app::PrimitivePropertiesDialog(
            wedge_initial, false, false,
            [&](zima::document::HistoryContainer value) {
                committed_wedge = std::move(value);
            }, &parent);
        wedge_dialog->show();
        application.processEvents();
        require(wedge_dialog->findChild<QTableWidget*>("primitiveReferenceTable") !=
                    nullptr,
                "Wedge Properties did not receive the universal placement table");
        wedge_dialog->findChild<QDoubleSpinBox*>("wedgeLength")->setValue(60.0);
        wedge_dialog->findChild<QDoubleSpinBox*>("wedgeWidth")->setValue(25.0);
        wedge_dialog->findChild<QDoubleSpinBox*>("wedgeHeight")->setValue(35.0);
        wedge_dialog->findChild<QDoubleSpinBox*>("wedgeTopOffset")->setValue(18.0);
        wedge_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(committed_wedge.feature_kind == zima::document::FeatureKind::Wedge &&
                    committed_wedge.wedge.length == 60.0 &&
                    committed_wedge.wedge.width == 25.0 &&
                    committed_wedge.wedge.height == 35.0 &&
                    committed_wedge.wedge.top_offset == 18.0,
                "Wedge Properties did not commit exact parameters");

        auto imported_step_initial =
            zima::document::PartDocument::create_imported_step_container(
                "/tmp/example.step");
        int imported_step_commits = 0;
        zima::document::HistoryContainer committed_imported_step;
        auto* imported_step_dialog = new zima::app::PrimitivePropertiesDialog(
            imported_step_initial, true, false,
            [&](zima::document::HistoryContainer value) {
                ++imported_step_commits;
                committed_imported_step = std::move(value);
            }, &parent);
        imported_step_dialog->show();
        application.processEvents();
        require(imported_step_dialog->findChild<QTableWidget*>(
                    "primitiveReferenceTable") != nullptr &&
                    imported_step_dialog->findChild<QTableWidget*>(
                        "primitiveOrientationTable") != nullptr,
                "ImportedStep Properties did not receive the universal "
                "placement tables");
        require(imported_step_dialog->set_reference(
                    0, {{}, "part-origin", "origin:point"}, "Počátek dílu"),
                "ImportedStep Properties rejected its placement reference");
        imported_step_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(imported_step_commits == 1 &&
                    committed_imported_step.feature_kind ==
                        zima::document::FeatureKind::ImportedStep &&
                    committed_imported_step.placement.references.size() == 1 &&
                    committed_imported_step.placement.references[0].owner_id ==
                        "part-origin",
                "ImportedStep Properties did not commit its universal "
                "placement reference");

        // Universal container placement PoC: Box exposes the same
        // reference/orientation table contract as ConstructionPropertiesDialog,
        // and a manual RZ correction becomes rotation_offset_z once a
        // reference is present (never overwriting the resolved rotation_z).
        zima::document::HistoryContainer committed_placed_box;
        std::optional<std::size_t> requested_box_reference;
        auto* box_reference_dialog = new zima::app::PrimitivePropertiesDialog(
            initial, false, false,
            [&](zima::document::HistoryContainer value) {
                committed_placed_box = std::move(value);
            }, &parent);
        box_reference_dialog->set_reference_request_callback(
            [&](std::size_t index) { requested_box_reference = index; });
        box_reference_dialog->show();
        application.processEvents();
        auto* box_reference_table = box_reference_dialog->findChild<QTableWidget*>(
            "primitiveReferenceTable");
        auto* box_orientation_table = box_reference_dialog->findChild<QTableWidget*>(
            "primitiveOrientationTable");
        require(box_reference_table != nullptr && box_orientation_table != nullptr &&
                    box_orientation_table->rowCount() == 2,
                "Box Properties does not expose the universal placement tables");
        auto* box_reference_item = box_reference_table->item(0, 1);
        require(box_reference_item != nullptr,
                "Box Properties has no explicit viewer-reference control");
        emit box_reference_table->cellClicked(0, 1);
        require(requested_box_reference == 0,
                "Box Properties did not request viewer reference selection");
        require(box_reference_dialog->set_reference(
                    0, {{}, "part-origin", "origin:point"}, "Počátek dílu"),
                "Box Properties rejected its first placement reference");
        box_reference_dialog->set_translation_constraint_state(
            {0, {true, true, true}}, {10.0, 20.0, 30.0});
        require(box_reference_dialog->owns_reference_owner(initial.id) &&
                    box_reference_dialog->owns_reference_owner(
                        initial.container_origin.id) &&
                    !box_reference_dialog->owns_reference_owner("part-origin"),
                "Box Properties does not recognize every identity of its own Container");
        auto* box_rotation_z = box_reference_dialog->findChildren<QDoubleSpinBox*>(
            "primitiveRotation").at(2);
        box_rotation_z->setValue(30.0);
        box_reference_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(committed_placed_box.placement.references.size() == 1 &&
                    committed_placed_box.placement.references[0].owner_id ==
                        "part-origin" &&
                    committed_placed_box.placement.rotation_offset_z == 30.0,
                "Box Properties lost its universal placement reference or manual "
                "rotation correction");

        auto axis_initial = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Axis);
        zima::document::ConstructionObject committed_axis;
        auto* construction_axis_dialog = new zima::app::ConstructionPropertiesDialog(
            axis_initial, false,
            [&](zima::document::ConstructionObject value) {
                committed_axis = std::move(value);
            }, &parent);
        construction_axis_dialog->show();
        application.processEvents();
        require(construction_axis_dialog->width() <= 360,
                "Axis Properties is wider than the compact feature dialogs");
        construction_axis_dialog->findChild<QDoubleSpinBox*>("constructionX")->setValue(12.0);
        auto* construction_axis_direction =
            construction_axis_dialog->findChild<QComboBox*>("constructionDirection");
        require(construction_axis_direction != nullptr,
                "Axis Properties does not expose the Python direction selector");
        construction_axis_direction->setCurrentIndex(
            construction_axis_direction->findData("x"));
        construction_axis_dialog->findChild<QDoubleSpinBox*>("constructionDisplaySize")->setValue(80.0);
        construction_axis_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(committed_axis.kind == zima::document::ConstructionKind::Axis &&
                    committed_axis.origin.x == 12.0 &&
                    committed_axis.direction.x == 1.0 &&
                    committed_axis.display_size == 80.0,
                "Construction Properties did not commit exact parameters");
        auto referenced_axis_initial =
            zima::document::PartDocument::create_construction(
                zima::document::ConstructionKind::Axis);
        zima::document::ConstructionObject committed_referenced_axis;
        std::optional<std::size_t> requested_reference;
        int construction_previews = 0;
        auto* referenced_axis_dialog = new zima::app::ConstructionPropertiesDialog(
            referenced_axis_initial, false,
            [&](zima::document::ConstructionObject value) {
                committed_referenced_axis = std::move(value);
            }, &parent);
        referenced_axis_dialog->set_reference_request_callback(
            [&](std::size_t index) { requested_reference = index; });
        referenced_axis_dialog->set_preview_callback(
            [&](zima::document::ConstructionObject) { ++construction_previews; });
        referenced_axis_dialog->show();
        application.processEvents();
        auto* definition = referenced_axis_dialog->findChild<QComboBox*>(
            "constructionDefinition");
        definition->setCurrentIndex(definition->findData(static_cast<int>(
            zima::document::ConstructionDefinition::TwoPointAxis)));
        auto* reference_table = referenced_axis_dialog->findChild<QTableWidget*>(
            "constructionReferenceTable");
        require(reference_table != nullptr,
                "Construction Properties has no Python-parity reference table");
        auto* reference_item = reference_table->item(0, 1);
        require(reference_item != nullptr,
                "Construction Properties has no explicit viewer-reference control");
        emit reference_table->cellClicked(0, 1);
        require(requested_reference == 0,
                "Construction Properties did not request viewer reference selection");
        referenced_axis_dialog->set_reference(
            0, {{}, "point-a", "point"}, "Bod A");
        referenced_axis_dialog->set_reference(
            1, {{}, "point-b", "point"}, "Bod B");
        referenced_axis_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(committed_referenced_axis.definition ==
                    zima::document::ConstructionDefinition::TwoPointAxis &&
                    committed_referenced_axis.references.size() == 2 &&
                    committed_referenced_axis.references[1].owner_id == "point-b" &&
                    construction_previews >= 3,
                "Construction Properties lost associative datum references");

        auto plane_initial = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Plane);
        zima::document::ConstructionObject committed_plane;
        std::optional<std::size_t> requested_orientation_reference;
        auto* plane_dialog = new zima::app::ConstructionPropertiesDialog(
            plane_initial, false,
            [&](zima::document::ConstructionObject value) {
                committed_plane = std::move(value);
            }, &parent);
        plane_dialog->set_reference_request_callback(
            [&](std::size_t index) { requested_orientation_reference = index; });
        plane_dialog->show();
        application.processEvents();
        require(plane_dialog->width() <= 360,
                "Plane Properties is wider than the compact feature dialogs");
        auto* plane_reference_table = plane_dialog->findChild<QTableWidget*>(
            "constructionReferenceTable");
        auto* orientation_table = plane_dialog->findChild<QTableWidget*>(
            "constructionOrientationTable");
        require(plane_reference_table != nullptr &&
                    orientation_table != nullptr && orientation_table->rowCount() == 2 &&
                    orientation_table->item(0, 1) != nullptr,
                "Plane Properties does not expose its position and FRONT/TOP tables");
        auto* plane_offset = plane_dialog->findChild<QDoubleSpinBox*>(
            "constructionOffset");
        require(plane_offset != nullptr && plane_offset->isEnabled(),
                "Plane work-plane offset was not editable without a position reference");
        require(plane_dialog->set_inline_parameter_value("offset", 6.25) &&
                    std::abs(plane_dialog->plane_offset() - 6.25) < 1.0e-9,
                "Plane drag/inline offset path did not update the live pending value");
        plane_dialog->set_reference(0,
            {{}, "plane-source", "plane", 0.0, true},
            "Zdrojová rovina", zima::document::ConstructionDefinition::PointReference);
        application.processEvents();
        auto* base_plane = plane_dialog->findChild<QComboBox*>(
            "constructionBasePlane");
        require(base_plane != nullptr &&
                    base_plane->currentData() == QStringLiteral("xz") &&
                    base_plane->currentText().contains(QStringLiteral("Zdrojová rovina")),
                "First planar reference did not become the Plane offset base");
        auto* first_position_item = plane_reference_table->item(0, 1);
        require(first_position_item != nullptr &&
                    first_position_item->text() == QStringLiteral("1. Zdrojová rovina"),
                "Plane-container first reference did not stay in the position table");
        require(plane_offset->isEnabled(),
                "Plane work-plane offset did not enable after the first position reference");
        plane_dialog->set_orientation_inherited_from_reference(true);
        application.processEvents();
        // The table stays enabled/clickable even while it shows the
        // automatic default (matching Python's
        // `_container_orientation_references`, which is never disabled) --
        // only the placeholder label communicates the default source.
        require(orientation_table->isEnabled(),
                "Plane FRONT/TOP table disabled itself while showing the "
                "automatic default orientation");
        auto* locked_orientation_item = orientation_table->item(0, 1);
        require(locked_orientation_item != nullptr &&
                    locked_orientation_item->text().contains("Zdrojová rovina"),
                "Default Plane orientation row did not name its source reference");
        plane_dialog->set_orientation_inherited_from_reference(false);
        application.processEvents();
        require(orientation_table->isEnabled(),
                "Plane FRONT/TOP table did not unlock once orientation is no "
                "longer inherited");
        // A non-planar second position reference stays positional and does
        // not occupy the TOP plane-mapping slot.
        plane_dialog->set_reference(1,
            {{}, "axis-top", "axis"}, "Osa TOP",
            zima::document::ConstructionDefinition::PointReference);
        require(base_plane != nullptr &&
                    base_plane->findData(QStringLiteral("xy")) >= 0 &&
                    base_plane->findData(QStringLiteral("yz")) >= 0 &&
                    base_plane->findData(QStringLiteral("xz")) >= 0,
                "Plane Properties does not offer all Container Origin planes");
        base_plane->setCurrentIndex(base_plane->findData(QStringLiteral("xy")));
        plane_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(committed_plane.references.size() == 3 &&
                    committed_plane.base_plane ==
                        zima::document::LocalDatumPlane::XY &&
                    !committed_plane.references[0].orientation_drives_rotation &&
                    !committed_plane.references[0].orientation_only &&
                    committed_plane.references[0].owner_id == "plane-source" &&
                    !committed_plane.references[1].orientation_only &&
                    committed_plane.references[1].owner_id == "axis-top" &&
                    committed_plane.references[2].orientation_drives_rotation &&
                    committed_plane.references[2].orientation_only &&
                    committed_plane.references[2].orientation_role == "front" &&
                    committed_plane.references[2].owner_id == "plane-source",
                "Plane Properties did not mirror its first planar position "
                "reference into the independent FRONT slot");

        auto* plane_origin_first_dialog = new zima::app::ConstructionPropertiesDialog(
            plane_initial, false, [](zima::document::ConstructionObject) {}, &parent);
        plane_origin_first_dialog->show();
        application.processEvents();
        auto* origin_first_position_table =
            plane_origin_first_dialog->findChild<QTableWidget*>(
                "constructionReferenceTable");
        auto* origin_first_orientation_table =
            plane_origin_first_dialog->findChild<QTableWidget*>(
                "constructionOrientationTable");
        require(origin_first_position_table != nullptr &&
                    origin_first_orientation_table != nullptr,
                "Plane origin-first dialog does not expose its reference tables");
        plane_origin_first_dialog->set_reference(0,
            {{}, "part-origin", "origin:plane:xz", 0.0, true},
            "Počátek dílu — Rovina XZ",
            zima::document::ConstructionDefinition::PointReference);
        application.processEvents();
        require(origin_first_position_table->item(0, 1) != nullptr &&
                    origin_first_position_table->item(0, 1)->text() ==
                        QStringLiteral("1. Počátek dílu — Rovina XZ") &&
                    origin_first_orientation_table->item(0, 1) != nullptr &&
                    origin_first_orientation_table->item(0, 1)->text().contains(
                        "Rovina XZ"),
                "Built-in origin plane did not mirror into Plane FRONT");
        plane_origin_first_dialog->buttons()->button(QDialogButtonBox::Cancel)->click();

        auto* plane_axis_first_dialog = new zima::app::ConstructionPropertiesDialog(
            plane_initial, false, [](zima::document::ConstructionObject) {}, &parent);
        plane_axis_first_dialog->show();
        application.processEvents();
        auto* axis_first_position_table = plane_axis_first_dialog->findChild<QTableWidget*>(
            "constructionReferenceTable");
        auto* axis_first_orientation_table = plane_axis_first_dialog->findChild<QTableWidget*>(
            "constructionOrientationTable");
        require(axis_first_position_table != nullptr &&
                    axis_first_orientation_table != nullptr,
                "Plane axis-first dialog does not expose its reference tables");
        plane_axis_first_dialog->set_reference(0,
            {{}, "axis-front", "axis", 0.0, false, "none", true}, "Osa FRONT",
            zima::document::ConstructionDefinition::PointReference);
        application.processEvents();
        require(axis_first_position_table->item(0, 1) != nullptr &&
                    axis_first_position_table->item(0, 1)->text() ==
                        QStringLiteral("1. Osa FRONT") &&
                    axis_first_orientation_table->item(0, 1) != nullptr &&
                    axis_first_orientation_table->item(0, 1)->text().contains("Vybrat"),
                "Non-plane first reference still auto-filled Plane FRONT/TOP");
        plane_axis_first_dialog->buttons()->button(QDialogButtonBox::Cancel)->click();

        auto* plane_position_only_dialog = new zima::app::ConstructionPropertiesDialog(
            plane_initial, false, [](zima::document::ConstructionObject) {}, &parent);
        plane_position_only_dialog->show();
        application.processEvents();
        auto* position_only_position_table =
            plane_position_only_dialog->findChild<QTableWidget*>(
                "constructionReferenceTable");
        auto* position_only_orientation_table =
            plane_position_only_dialog->findChild<QTableWidget*>(
                "constructionOrientationTable");
        require(position_only_position_table != nullptr &&
                    position_only_orientation_table != nullptr,
                "Plane position-only dialog does not expose its reference tables");
        plane_position_only_dialog->set_reference(1,
            {{}, "plane-second", "origin:plane:xy", 0.0, true},
            "Počátek dílu — Rovina XY",
            zima::document::ConstructionDefinition::PointReference);
        plane_position_only_dialog->set_reference(2,
            {{}, "plane-third", "origin:plane:yz", 0.0, true},
            "Počátek dílu — Rovina YZ",
            zima::document::ConstructionDefinition::PointReference);
        application.processEvents();
        require(position_only_position_table->item(1, 1) != nullptr &&
                    position_only_position_table->item(2, 1) != nullptr &&
                    position_only_orientation_table->item(0, 1) != nullptr &&
                    position_only_orientation_table->item(1, 1) != nullptr &&
                    position_only_orientation_table->item(0, 1)->text().contains("Rovina XY") &&
                    position_only_orientation_table->item(1, 1)->text().contains("Rovina YZ") &&
                    qobject_cast<QComboBox*>(position_only_orientation_table->cellWidget(
                        0, 2)) != nullptr &&
                    qobject_cast<QComboBox*>(position_only_orientation_table->cellWidget(
                        1, 2)) != nullptr &&
                    qobject_cast<QComboBox*>(position_only_orientation_table->cellWidget(
                        0, 2))->findData(QStringLiteral("back")) >= 0 &&
                    qobject_cast<QComboBox*>(position_only_orientation_table->cellWidget(
                        1, 2))->findData(QStringLiteral("right")) >= 0,
                "Plane position references did not auto-fill the editable "
                "FRONT/BACK and TOP/BOTTOM/LEFT/RIGHT mappings");
        plane_position_only_dialog->buttons()->button(QDialogButtonBox::Cancel)->click();

        {
            const zima::viewer::ViewerCandidate document_origin_plane{
                zima::viewer::CandidateKind::Plane, 0.0, 0u,
                "part-origin", "origin:plane:xz", {},
                zima::viewer::CandidateGeometry::Display};
            require(zima::app::construction_reference_candidate_passes_static_filters(
                        document_origin_plane, false, false, false),
                    "Position rows unexpectedly reject origin-plane candidates");
            require(zima::app::construction_reference_candidate_passes_static_filters(
                        document_origin_plane, true, false, false),
                    "FRONT/TOP reject stable document-origin plane candidates");
            const zima::viewer::ViewerCandidate edited_plane{
                zima::viewer::CandidateKind::Plane, 0.0, 0u,
                plane_initial.entity_id, "plane", {},
                zima::viewer::CandidateGeometry::OriginalReference};
            require(!zima::app::construction_reference_candidate_passes_static_filters(
                        edited_plane, false, true, false) &&
                    !zima::app::construction_reference_candidate_passes_static_filters(
                        edited_plane, true, true, false),
                "Edited construction still offers itself as a reference candidate");
            const zima::viewer::ViewerCandidate body_face{
                zima::viewer::CandidateKind::Face, 0.0, 0u,
                "box", "z_max", {},
                zima::viewer::CandidateGeometry::OriginalReference};
            const zima::viewer::ViewerCandidate visible_body_face{
                zima::viewer::CandidateKind::Face, 0.0, 0u,
                "box", "z_max", {},
                zima::viewer::CandidateGeometry::Display};
            const zima::viewer::ViewerCandidate visible_body_vertex{
                zima::viewer::CandidateKind::Vertex, 0.0, 0u,
                "box", "x_min:y_min:z_max", {},
                zima::viewer::CandidateGeometry::Display};
            const zima::viewer::ViewerCandidate body_edge{
                zima::viewer::CandidateKind::Edge, 0.0, 0u,
                "box", "edge", {},
                zima::viewer::CandidateGeometry::OriginalReference};
            require(zima::app::placement_reference_candidate_has_stable_geometry(
                        body_face) &&
                    zima::app::placement_reference_candidate_has_stable_geometry(
                        visible_body_face) &&
                    zima::app::placement_reference_candidate_has_stable_geometry(
                        visible_body_vertex) &&
                    zima::app::placement_reference_candidate_has_stable_geometry(
                        body_edge),
                "Placement contract rejected a persisted original Edge");
        }

        auto point_initial = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point);
        zima::document::ConstructionObject committed_point;
        std::optional<std::size_t> requested_point_reference;
        auto* construction_point_dialog = new zima::app::ConstructionPropertiesDialog(
            point_initial, false,
            [&](zima::document::ConstructionObject value) {
                committed_point = std::move(value);
            }, &parent, 2);
        construction_point_dialog->set_reference_request_callback(
            [&](std::size_t index) { requested_point_reference = index; });
        construction_point_dialog->show();
        application.processEvents();
        require(construction_point_dialog->width() <= 360,
                "Point Properties is wider than the compact feature dialogs");
        auto* point_reference_item = construction_point_dialog->findChild<QTableWidget*>(
            "constructionReferenceTable")->item(0, 1);
        require(point_reference_item != nullptr,
                "Point Properties has no explicit viewer-reference control");
        emit construction_point_dialog->findChild<QTableWidget*>(
            "constructionReferenceTable")->cellClicked(0, 1);
        require(requested_point_reference == 0,
                "Point Properties did not arm its first viewer reference");
        construction_point_dialog->set_reference(0,
            {{}, "part-origin", "origin:plane:xy", 0.0, true}, "XY Plane",
            zima::document::ConstructionDefinition::PointReference);
        construction_point_dialog->set_translation_constraint_state(
            {2, {false, false, true}}, {4.0, 5.0, 17.0});
        require(construction_point_dialog->owns_reference_owner(point_initial.id) &&
                    construction_point_dialog->owns_reference_owner(
                        point_initial.entity_id) &&
                    construction_point_dialog->owns_reference_owner(
                        point_initial.container_origin.id) &&
                    !construction_point_dialog->owns_reference_owner("part-origin"),
                "Point Properties does not recognize every identity of its own Container");
        auto* point_x = construction_point_dialog->findChild<QDoubleSpinBox*>(
            "constructionX");
        auto* point_y = construction_point_dialog->findChild<QDoubleSpinBox*>(
            "constructionY");
        auto* point_z = construction_point_dialog->findChild<QDoubleSpinBox*>(
            "constructionZ");
        require(point_x != nullptr && point_y != nullptr && point_z != nullptr &&
                    point_x->isEnabled() && point_y->isEnabled() &&
                    !point_z->isEnabled() && point_z->value() == 17.0,
                "Point Properties did not disable and solve the constrained Z axis");
        auto* construction_point_offset = qobject_cast<QDoubleSpinBox*>(
            construction_point_dialog->findChild<QTableWidget*>(
                "constructionReferenceTable")->cellWidget(0, 2));
        require(construction_point_offset != nullptr &&
                    construction_point_offset->isEnabled() &&
                    point_x->decimals() == 2 &&
                    construction_point_offset->decimals() == 2,
                "Point Properties did not apply document precision to its fields");
        std::optional<zima::document::ConstructionObject> inline_point_preview;
        construction_point_dialog->set_preview_callback(
            [&](zima::document::ConstructionObject value) {
                inline_point_preview = std::move(value);
            });
        require(construction_point_dialog->set_inline_parameter_value(
                    "x", 4.126) &&
                    std::abs(point_x->value() - 4.13) < 1.0e-9 &&
                    inline_point_preview.has_value() &&
                    std::abs(inline_point_preview->origin.x - 4.13) < 1.0e-9 &&
                    construction_point_dialog->set_inline_parameter_value(
                        "reference_offset:0", 12.346) &&
                    std::abs(construction_point_offset->value() - 12.35) < 1.0e-9 &&
                    inline_point_preview.has_value() &&
                    std::abs(inline_point_preview->references.front().offset -
                        12.35) < 1.0e-9 &&
                    !construction_point_dialog->set_inline_parameter_value(
                        "z", 99.0),
                "Inline Point dimension edit did not update the matching live "
                "dialog value and preview at document precision");
        construction_point_offset->setValue(17.0);
        require(construction_point_dialog->findChild<QTableWidget*>(
                    "constructionReferenceTable")->rowCount() == 2,
                "Under-constrained Point Properties did not offer the next reference");
        construction_point_dialog->set_translation_constraint_state(
            {0, {true, true, true}}, {4.0, 5.0, 17.0});
        const auto* fully_constrained_table =
            construction_point_dialog->findChild<QTableWidget*>(
                "constructionReferenceTable");
        require(fully_constrained_table->rowCount() == 1 &&
                    fully_constrained_table->item(0, 1) != nullptr,
                "Fully constrained Point Properties still offered another reference");
        construction_point_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(committed_point.kind == zima::document::ConstructionKind::Point &&
                    committed_point.definition ==
                        zima::document::ConstructionDefinition::PointReference &&
                    committed_point.references.size() == 2 &&
                    committed_point.references.front().owner_id == "part-origin" &&
                    committed_point.references.front().semantic_key == "origin:plane:xy" &&
                    committed_point.references.front().offset == 17.0 &&
                    committed_point.references.front().supports_offset &&
                    committed_point.references.back().owner_id == "part-origin" &&
                    committed_point.references.back().semantic_key ==
                        "origin:plane:xy" &&
                    committed_point.references.back().orientation_only &&
                    committed_point.references.back().orientation_role == "front",
                "Point Properties did not commit its selected Origin reference "
                "and mirrored container orientation");

        int cancelled_point_commits = 0;
        auto* cancelled_point_dialog = new zima::app::ConstructionPropertiesDialog(
            committed_point, true,
            [&](zima::document::ConstructionObject) { ++cancelled_point_commits; },
            &parent);
        cancelled_point_dialog->show();
        application.processEvents();
        auto* replacement_item = cancelled_point_dialog->findChild<QTableWidget*>(
            "constructionReferenceTable")->item(0, 1);
        require(replacement_item != nullptr,
                "Edited Point Properties cannot replace its reference");
        cancelled_point_dialog->set_reference(
            0, {{}, "other-point", "point"}, "Jiný bod");
        cancelled_point_dialog->buttons()->button(QDialogButtonBox::Cancel)->click();
        require(cancelled_point_commits == 0,
                "Cancel committed a pending Point Properties reference change");

        auto extrusion_initial =
            zima::document::PartDocument::create_extrusion_container("sketch-profile");
        int extrusion_commits = 0;
        zima::document::HistoryContainer committed_extrusion;
        auto* extrusion_dialog = new zima::app::PrimitivePropertiesDialog(
            extrusion_initial, false, false,
            [&](zima::document::HistoryContainer value) {
                ++extrusion_commits;
                committed_extrusion = std::move(value);
            }, &parent);
        extrusion_dialog->show();
        application.processEvents();
        auto* extrusion_height =
            extrusion_dialog->findChild<QDoubleSpinBox*>("extrusionHeight");
        auto* extrusion_direction =
            extrusion_dialog->findChild<QComboBox*>("extrusionDirection");
        auto* extrusion_extent =
            extrusion_dialog->findChild<QComboBox*>("extrusionExtentMode");
        auto* forward_end = extrusion_dialog->findChild<QComboBox*>(
            "extrusionForwardEndCondition");
        auto* extrusion_plane_offset = extrusion_dialog->findChild<QDoubleSpinBox*>(
            "profilePlaneOffset");
        require(extrusion_height != nullptr,
                "Extrusion dialog does not expose its height");
        require(extrusion_direction != nullptr,
                "Extrusion dialog does not expose its direction");
        require(extrusion_extent != nullptr && forward_end != nullptr &&
                    extrusion_plane_offset != nullptr,
                "Extrusion dialog does not expose its extent");
        require(!extrusion_dialog->findChildren<QDoubleSpinBox*>(
                    "primitiveTranslation").empty() &&
                    extrusion_dialog->findChild<QTableWidget*>(
                        "primitiveReferenceTable") != nullptr,
                "Extrusion dialog does not share the universal container "
                "placement UI");
        require(extrusion_dialog->findChild<QPushButton*>(
                    "containerOrientationFront") == nullptr &&
                    extrusion_dialog->findChild<QPushButton*>(
                    "containerOrientationBack") == nullptr &&
                    extrusion_dialog->findChild<QPushButton*>(
                    "containerOrientationRotate") == nullptr,
                "Extrusion Properties still exposes Sketcher camera controls");
        require(extrusion_dialog->set_reference(
                    0, {{}, "part-origin", "origin:point"}, "Počátek dílu"),
                "Extrusion Properties rejected its placement reference");
        extrusion_height->setValue(48.0);
        extrusion_plane_offset->setValue(-7.5);
        extrusion_extent->setCurrentIndex(
            extrusion_extent->findData("symmetric"));
        int preview_updates = 0;
        zima::document::HistoryContainer extrusion_preview;
        extrusion_dialog->set_preview_callback(
            [&](const auto& value) {
                ++preview_updates;
                extrusion_preview = value;
            });
        auto* extrusion_flip = extrusion_dialog->findChild<QPushButton*>(
            "containerOrientationFlipButton");
        auto* extrusion_rotate = extrusion_dialog->findChild<QPushButton*>(
            "containerOrientationRotateButton");
        require(extrusion_flip != nullptr && extrusion_rotate != nullptr,
                "Extrusion placement orientation controls are missing");
        extrusion_plane_offset->setValue(6.25);
        extrusion_flip->click();
        extrusion_rotate->click();
        require(extrusion_preview.extrusion.profile_plane_offset == 6.25 &&
                    extrusion_preview.placement.orientation_back &&
                    extrusion_preview.placement.orientation_quarter_turns == 1,
                "Extrusion offset/FRONT/rotation changes did not reach the "
                "live preview callback");
        const int updates_before_atomic_extent = preview_updates;
        extrusion_dialog->set_profile_offset_and_forward_length(4.0, 37.0);
        require(extrusion_dialog->profile_plane_offset() == 4.0 &&
                    extrusion_dialog->forward_extent_length() == 37.0 &&
                    preview_updates == updates_before_atomic_extent + 1,
                "Extrusion two-end manipulator update did not atomically "
                "change offset and length with one preview rebuild");
        extrusion_dialog->set_profile_offset_and_forward_length(-7.5, 48.0);
        forward_end->setCurrentIndex(forward_end->findData("up_to"));
        require(!zima::app::extrusion_length_dimension_value(
                    extrusion_preview.extrusion, false) &&
                    !extrusion_height->isVisible(),
                "Up-to still exposes its stored fallback length as an active "
                "numeric dimension");
        auto dimension_policy = extrusion_preview.extrusion;
        dimension_policy.end_condition_forward =
            zima::document::EndCondition::Length;
        dimension_policy.extent_mode =
            zima::document::ProfileExtentMode::OneSide;
        dimension_policy.length_forward = 0.0;
        require(!zima::app::extrusion_length_dimension_value(
                    dimension_policy, false),
                "A zero Container Properties value still created a dimension");
        dimension_policy.length_forward = 25.0;
        dimension_policy.extent_mode =
            zima::document::ProfileExtentMode::Symmetric;
        require(zima::app::extrusion_length_dimension_value(
                    dimension_policy, false) == 25.0 &&
                    zima::app::extrusion_length_dimension_value(
                        dimension_policy, true) == 25.0,
                "Symmetric Container Properties dimensions do not share the "
                "active forward value");
        dimension_policy.extent =
            zima::document::ExtrusionExtent::ThroughAll;
        require(!zima::app::extrusion_length_dimension_value(
                    dimension_policy, false) &&
                    !zima::app::extrusion_length_dimension_value(
                        dimension_policy, true),
                "Through-all still exposes a stored fallback length as an "
                "active numeric dimension");
        dimension_policy.extent = zima::document::ExtrusionExtent::Blind;
        dimension_policy.extent_mode =
            zima::document::ProfileExtentMode::TwoSides;
        dimension_policy.length_reverse = 12.0;
        dimension_policy.end_condition_reverse =
            zima::document::EndCondition::UpTo;
        require(!zima::app::extrusion_length_dimension_value(
                    dimension_policy, true),
                "Two-sided Up-to still exposes its reverse fallback length");
        int target_requests = 0;
        int target_highlight_updates = 0;
        extrusion_dialog->set_extrusion_target_request(
            [&] { ++target_requests; });
        extrusion_dialog->set_reference_highlights_changed_callback(
            [&] { ++target_highlight_updates; });
        extrusion_dialog->set_extrusion_target(
            {"datum-plane", "plane", {}}, {0.0, 0.0, 30.0}, {0.0, 0.0, 1.0});
        auto* forward_target = extrusion_dialog->findChild<QLineEdit*>(
            "extrusionForwardEndTarget");
        auto* clear_forward_target = extrusion_dialog->findChild<QAction*>(
            "extrusionForwardEndTargetClear");
        require(forward_target != nullptr && clear_forward_target != nullptr &&
                    clear_forward_target->isEnabled(),
                "Up-to target does not expose its reference field and clear action");
        QMouseEvent highlight_target(
            QEvent::MouseButtonPress, QPointF(8.0, 8.0),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(forward_target, &highlight_target);
        const auto highlighted_targets =
            extrusion_dialog->highlighted_reference_entries();
        require(target_highlight_updates == 1 &&
                    forward_target->styleSheet().contains("#00d1ff") &&
                    std::any_of(highlighted_targets.begin(),
                        highlighted_targets.end(), [](const auto& reference) {
                            return reference.owner_id == "datum-plane" &&
                                reference.semantic_key == "plane";
                        }),
                "Clicking a populated Up-to field did not highlight the exact "
                "stored target reference");
        clear_forward_target->trigger();
        require(forward_target->text().isEmpty() &&
                    !clear_forward_target->isEnabled() &&
                    extrusion_preview.extrusion.end_targets_forward.empty(),
                "Up-to context action did not clear its stored target");
        QMouseEvent request_target(
            QEvent::MouseButtonPress, QPointF(8.0, 8.0),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(forward_target, &request_target);
        require(target_requests == 1 &&
                    forward_target->styleSheet().contains("#00d1ff"),
                "Clicking an empty Up-to field did not arm explicit face picking");
        extrusion_dialog->set_extrusion_target(
            {"datum-plane", "plane", {}}, {0.0, 0.0, 30.0}, {0.0, 0.0, 1.0});
        extrusion_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(extrusion_commits == 1 &&
                    committed_extrusion.feature_kind ==
                        zima::document::FeatureKind::Extrusion &&
                    committed_extrusion.extrusion.sketch_id == "sketch-profile" &&
                    committed_extrusion.extrusion.length_forward == 48.0 &&
                    committed_extrusion.extrusion.length_reverse == 48.0 &&
                    committed_extrusion.extrusion.profile_plane_offset == -7.5 &&
                    committed_extrusion.extrusion.extent_mode ==
                        zima::document::ProfileExtentMode::Symmetric &&
                    committed_extrusion.extrusion.end_condition_forward ==
                        zima::document::EndCondition::UpTo &&
                    !committed_extrusion.extrusion.end_targets_forward.empty() &&
                    committed_extrusion.extrusion.end_targets_forward.front()
                        .reference.owner_id == "datum-plane" &&
                    committed_extrusion.placement.references.size() == 1 &&
                    committed_extrusion.placement.references[0].owner_id ==
                        "part-origin" &&
                    preview_updates >= 2,
                "Extrusion Properties did not preserve its Sketch or height");

        int profile_completion_commits = 0;
        auto* completed_profile_dialog =
            new zima::app::PrimitivePropertiesDialog(
                committed_extrusion, true, false,
                [&](zima::document::HistoryContainer) {
                    ++profile_completion_commits;
                }, &parent);
        completed_profile_dialog->set_commit_required(true);
        completed_profile_dialog->show();
        application.processEvents();
        completed_profile_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(profile_completion_commits == 1,
                "Unchanged Extrusion parameters skipped calculation after its "
                "owned Sketch profile changed");

        auto revolution_initial =
            zima::document::PartDocument::create_revolution_container(
                "sketch-revolution");
        int revolution_commits = 0;
        zima::document::HistoryContainer committed_revolution;
        auto* revolution_dialog = new zima::app::PrimitivePropertiesDialog(
            revolution_initial, false, false,
            [&](zima::document::HistoryContainer value) {
                ++revolution_commits;
                committed_revolution = std::move(value);
            }, &parent);
        revolution_dialog->show();
        application.processEvents();
        auto* revolution_axis_hint =
            revolution_dialog->findChild<QLabel*>("revolutionAxisHint");
        auto* revolution_angle =
            revolution_dialog->findChild<QDoubleSpinBox*>("revolutionAngle");
        auto* revolution_plane_offset =
            revolution_dialog->findChild<QDoubleSpinBox*>("profilePlaneOffset");
        require(revolution_axis_hint != nullptr &&
                    revolution_axis_hint->text().contains(
                        QStringLiteral("konstrukční osa")) &&
                    revolution_angle != nullptr &&
                    revolution_plane_offset != nullptr,
                "Revolution dialog does not explain its Sketch centerline axis");
        require(revolution_dialog->set_inline_parameter_value("angle", 210.0) &&
                    revolution_angle->value() == 210.0,
                "Inline Revolution angle edit did not reach the live angle field");
        require(!revolution_dialog->findChildren<QDoubleSpinBox*>(
                    "primitiveTranslation").empty() &&
                    revolution_dialog->findChild<QTableWidget*>(
                        "primitiveReferenceTable") != nullptr,
                "Revolution dialog does not share the universal container "
                "placement UI");
        require(revolution_dialog->set_reference(
                    0, {{}, "part-origin", "origin:point"}, "Počátek dílu"),
                "Revolution Properties rejected its placement reference");
        revolution_angle->setValue(225.0);
        revolution_plane_offset->setValue(12.25);
        revolution_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(revolution_commits == 1 &&
                    committed_revolution.feature_kind ==
                        zima::document::FeatureKind::Revolution &&
                    committed_revolution.revolution.sketch_id ==
                        "sketch-revolution" &&
                    committed_revolution.revolution.axis_segment_id.empty() &&
                    committed_revolution.revolution.angle_degrees == 225.0 &&
                    committed_revolution.revolution.profile_plane_offset == 12.25 &&
                    committed_revolution.placement.references.size() == 1 &&
                    committed_revolution.placement.references[0].owner_id ==
                        "part-origin",
                "Revolution Properties did not preserve exact parameters or "
                "its universal placement reference");

        zima::kernel::BodyResult component_body;
        auto component = zima::assembly::AssemblyDocument::create_part_occurrence(
            "Komponenta", "source-part", "source.prtz", component_body);
        const std::string source_id = component.source_document_id;
        int component_commits = 0;
        zima::assembly::PartOccurrence committed_component;
        auto* component_dialog = new zima::app::ComponentPropertiesDialog(
            component,
            [&](zima::assembly::PartOccurrence value) {
                ++component_commits;
                committed_component = std::move(value);
            }, &parent);
        component_dialog->show();
        application.processEvents();
        const auto component_translations =
            component_dialog->findChildren<QDoubleSpinBox*>("componentTranslation");
        require(component_translations.size() == 3,
                "Component Properties does not expose Assembly-owned placement");
        component_translations.front()->setValue(88.0);
        component_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(component_commits == 1 && committed_component.placement.x == 88.0 &&
                    committed_component.source_document_id == source_id,
                "Component Properties changed source ownership or lost placement");

        // Embedded placement-reference table (redesign matching Python's
        // AssemblyComponentPropertiesDialog): the dialog itself requests
        // viewer picks per row/side and stores committed rows in
        // placement_references(), which submit() persists onto the
        // committed PartOccurrence.
        std::vector<std::pair<std::size_t, bool>> placement_reference_requests;
        int placement_component_commits = 0;
        zima::assembly::PartOccurrence committed_placement_component;
        auto* placement_dialog = new zima::app::ComponentPropertiesDialog(
            component,
            [&](zima::assembly::PartOccurrence value) {
                ++placement_component_commits;
                committed_placement_component = std::move(value);
            }, &parent);
        placement_dialog->set_reference_request_callback(
            [&](std::size_t index, bool component_side) {
                placement_reference_requests.emplace_back(index, component_side);
            });
        placement_dialog->show();
        application.processEvents();
        require(placement_dialog->placement_references().empty(),
                "New Component Properties dialog started with stale placement references");
        placement_dialog->set_placement_reference(0, true,
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child("dependent"), "box-a", "z_min"},
            "Plocha");
        placement_dialog->set_placement_reference(0, false,
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child("prerequisite"), "box-b", "z_max"},
            "Plocha");
        require(placement_dialog->placement_references().size() == 1 &&
                    placement_dialog->placement_references()[0].component_reference
                            .owner_id == "box-a" &&
                    placement_dialog->placement_references()[0].target_reference
                            .owner_id == "box-b",
                "Component Properties did not store the picked placement-reference row");
        auto* mate_table = placement_dialog->findChild<QTableWidget*>(
            "componentPlacementTable");
        auto* limits_cell = mate_table == nullptr
            ? nullptr : mate_table->cellWidget(0, 6);
        auto* limits_button = limits_cell == nullptr
            ? nullptr : limits_cell->findChild<QToolButton*>();
        require(limits_button != nullptr && limits_button->isEnabled(),
                "Mate row does not expose its end-of-row limits icon");
        limits_button->click();
        application.processEvents();
        auto* limits_dialog = parent.findChild<QDialog*>("mateLimitsDialog");
        require(limits_dialog != nullptr &&
                    limits_dialog->windowFlags().testFlag(Qt::SubWindow),
                "Mate limits did not open as an internal Properties SubWindow");
        auto* mate_lower_enabled =
            limits_dialog->findChild<QCheckBox*>("mateLowerEnabled");
        auto* mate_upper_enabled =
            limits_dialog->findChild<QCheckBox*>("mateUpperEnabled");
        auto* lower =
            limits_dialog->findChild<QDoubleSpinBox*>("mateLowerLimit");
        auto* upper =
            limits_dialog->findChild<QDoubleSpinBox*>("mateUpperLimit");
        auto* current =
            limits_dialog->findChild<QDoubleSpinBox*>("mateCurrentValue");
        require(mate_lower_enabled && mate_upper_enabled && lower && upper && current &&
                    current->isReadOnly() && current->value() == 0.0,
                "Mate limits window does not show current/lower/upper values");
        mate_lower_enabled->setChecked(true);
        mate_upper_enabled->setChecked(true);
        lower->setValue(-5.0);
        upper->setValue(10.0);
        auto* limits_properties = dynamic_cast<zima::ui::PropertiesSubWindow*>(
            limits_dialog);
        require(limits_properties != nullptr,
                "Mate limits window does not use the shared Properties contract");
        limits_properties->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(placement_dialog->placement_references()[0].lower_limit == -5.0 &&
                    placement_dialog->placement_references()[0].upper_limit == 10.0,
                "Mate limits were not written back into their mate row");
        placement_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(placement_component_commits == 1 &&
                    committed_placement_component.placement_references.size() == 1 &&
                    committed_placement_component.placement_references[0].mate_type ==
                        zima::assembly::MateKind::PlaneCoincident &&
                    committed_placement_component.placement_references[0].lower_limit ==
                        -5.0 &&
                    committed_placement_component.placement_references[0].upper_limit ==
                        10.0,
                "Component Properties did not persist its embedded placement "
                "reference onto the committed occurrence");

        // A row with only one side picked is transient (not yet a valid
        // constraint) and must be discarded on commit rather than persisted
        // half-filled, matching Python's row-cap/discard-incomplete behavior.
        auto* partial_dialog = new zima::app::ComponentPropertiesDialog(
            component, [](zima::assembly::PartOccurrence) {}, &parent);
        partial_dialog->show();
        application.processEvents();
        partial_dialog->set_placement_reference(0, true,
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child("dependent"), "box-a", "z_min"},
            "Plocha");
        require(partial_dialog->placement_references().size() == 1,
                "Partial placement-reference row was not tracked while incomplete");
        zima::assembly::PartOccurrence committed_partial_component;
        int partial_component_commits = 0;
        auto* partial_dialog_committing = new zima::app::ComponentPropertiesDialog(
            component,
            [&](zima::assembly::PartOccurrence value) {
                ++partial_component_commits;
                committed_partial_component = std::move(value);
            }, &parent);
        partial_dialog_committing->show();
        application.processEvents();
        partial_dialog_committing->set_placement_reference(0, true,
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child("dependent"), "box-a", "z_min"},
            "Plocha");
        partial_dialog_committing->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(partial_component_commits == 1 &&
                    committed_partial_component.placement_references.empty(),
                "Component Properties persisted an incomplete (one-sided) "
                "placement-reference row");
        partial_dialog->close();

        auto sketch = zima::sketcher::Sketch::create_default();
        zima::document::Placement sketch_placement;
        int sketch_commits = 0;
        bool sketch_entry_requested = true;
        auto* sketch_dialog = new zima::app::SketchPropertiesDialog(
            sketch, sketch_placement, false, {},
            [&](zima::sketcher::Sketch committed,
                zima::document::Placement committed_placement,
                bool enter_sketch) {
                ++sketch_commits;
                sketch = std::move(committed);
                sketch_placement = std::move(committed_placement);
                sketch_entry_requested = enter_sketch;
            }, &parent);
        sketch_dialog->show();
        require(sketch_dialog->windowFlags().testFlag(Qt::SubWindow),
                "Sketch Properties is not an internal SubWindow");
        auto* sketch_offset =
            sketch_dialog->findChild<QDoubleSpinBox*>("sketchPlaneOffset");
        require(sketch_offset != nullptr,
                "Sketch Properties does not expose its plane offset");
        require(sketch_dialog->findChild<QTableWidget*>("sketchReferenceTable") != nullptr,
                "Sketch Properties does not reuse container placement UI");
        auto* sketch_reference_table =
            sketch_dialog->findChild<QTableWidget*>("sketchReferenceTable");
        require(sketch_reference_table->horizontalHeader()->sectionSize(1) >
                    sketch_reference_table->horizontalHeader()->sectionSize(2),
                "Offset column still steals space from placement reference text");
        auto* sketch_dof_label = sketch_dialog->findChild<QLabel*>(
            "containerPlacementDofLabel");
        require(sketch_dof_label != nullptr && sketch_dof_label->isVisible() &&
                    sketch_dialog->content_layout()->indexOf(sketch_dof_label) >= 0,
                "Sketch placement DOF label is floating outside the dialog layout");
        require(sketch_dialog->set_reference(0,
                    {{}, "source-plane", "plane", 0.0, true},
                    "První rovina"),
                "Sketch Properties rejected its first planar reference");
        auto* sketch_plane = sketch_dialog->findChild<QComboBox*>("sketchPlane");
        require(sketch_plane != nullptr &&
                    sketch_plane->currentData().toInt() == static_cast<int>(
                        zima::sketcher::SketchPlane::XZ) &&
                    sketch_plane->currentText().contains(QStringLiteral("První rovina")),
                "First planar reference did not become the Sketch work plane");
        auto* sketch_front = sketch_dialog->findChild<QPushButton*>(
            "containerOrientationFront");
        auto* sketch_back = sketch_dialog->findChild<QPushButton*>(
            "containerOrientationBack");
        auto* sketch_rotate = sketch_dialog->findChild<QPushButton*>(
            "containerOrientationRotate");
        require(sketch_front == nullptr && sketch_back == nullptr &&
                    sketch_rotate == nullptr,
                "Sketch Properties still exposes Sketcher camera controls");
        auto* container_flip = sketch_dialog->findChild<QPushButton*>(
            "containerOrientationFlipButton");
        auto* container_rotate = sketch_dialog->findChild<QPushButton*>(
            "containerOrientationRotateButton");
        require(container_flip != nullptr && container_rotate != nullptr &&
                    container_flip->text() == QStringLiteral("PŘEDNÍ") &&
                    container_rotate->text().contains(QStringLiteral("0°")),
                "Shared placement FRONT/rotation controls are missing");
        container_flip->click();
        container_rotate->click();
        sketch_offset->setValue(12.5);
        sketch_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(sketch_commits == 1 && sketch.plane_offset == 12.5 &&
                    sketch.plane == zima::sketcher::SketchPlane::XZ &&
                    !sketch_placement.references.empty() &&
                    sketch_placement.references.front().owner_id == "source-plane" &&
                    sketch_placement.orientation_back &&
                    sketch_placement.orientation_quarter_turns == 1 &&
                    !sketch_entry_requested,
                "Sketch Properties OK committed incorrectly or entered Sketcher");
        application.processEvents();

        bool explicit_sketch_entry_requested = false;
        auto* sketch_entry_dialog = new zima::app::SketchPropertiesDialog(
            sketch, sketch_placement, true, {},
            [&](zima::sketcher::Sketch,
                zima::document::Placement, bool enter_sketch) {
                explicit_sketch_entry_requested = enter_sketch;
            }, &parent);
        sketch_entry_dialog->show();
        application.processEvents();
        auto* open_sketch_button = sketch_entry_dialog->findChild<QPushButton*>(
            "sketchOpenButton");
        require(open_sketch_button != nullptr,
                "Sketch Properties does not expose its Sketcher entry action");
        open_sketch_button->click();
        require(explicit_sketch_entry_requested,
                "SKETCH did not request entry into Sketcher");
        application.processEvents();

        auto initial_text = zima::sketcher::Sketch::create_text();
        initial_text.value = "O";
        int text_commits = 0;
        zima::sketcher::SketchText committed_text;
        auto* text_dialog = new zima::app::SketchTextPropertiesDialog(
            initial_text, std::array{5.0, 7.0},
            [](const std::optional<zima::sketcher::SketchText>&) {},
            [&](zima::sketcher::SketchText value) {
                ++text_commits;
                committed_text = std::move(value);
            }, &parent);
        text_dialog->show();
        application.processEvents();
        auto* text_value =
            text_dialog->findChild<QPlainTextEdit*>("sketchTextValue");
        require(text_value && text_value->toPlainText() == QStringLiteral("O") &&
                    text_dialog->windowFlags().testFlag(Qt::SubWindow),
                "Editable Sketch Text did not reopen as a semantic internal dialog");
        text_value->setPlainText(QStringLiteral("OI"));
        text_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        double text_min_y = std::numeric_limits<double>::infinity();
        double text_max_y = -std::numeric_limits<double>::infinity();
        for (const auto& contour : committed_text.contours) {
            for (const auto& point : contour) {
                text_min_y = std::min(text_min_y, point[1]);
                text_max_y = std::max(text_max_y, point[1]);
            }
        }
        require(text_commits == 1 && committed_text.value == "OI" &&
                    !committed_text.contours.empty() &&
                    text_min_y < committed_text.anchor_y &&
                    text_max_y <= committed_text.anchor_y + 1.0e-6,
                "Sketch Text OK did not retain upright bottom-aligned profile contours");

        auto* cancel_text_dialog = new zima::app::SketchTextPropertiesDialog(
            committed_text, std::nullopt,
            [](const std::optional<zima::sketcher::SketchText>&) {},
            [&](zima::sketcher::SketchText) { ++text_commits; }, &parent);
        cancel_text_dialog->show();
        application.processEvents();
        cancel_text_dialog->findChild<QPlainTextEdit*>("sketchTextValue")
            ->setPlainText(QStringLiteral("discarded"));
        cancel_text_dialog->buttons()->button(QDialogButtonBox::Cancel)->click();
        application.processEvents();
        require(text_commits == 1,
                "Cancel committed an edited semantic Sketch Text");

        zima::sketcher::SketchDimension dimension{
            "dimension", zima::sketcher::DimensionKind::Distance,
            "first", "second", 20.0};
        int dimension_commits = 0;
        zima::sketcher::SketchDimension committed_dimension;
        auto* dimension_dialog = new zima::app::SketchDimensionPropertiesDialog(
            dimension, false, [&](zima::sketcher::SketchDimension committed) {
                ++dimension_commits;
                committed_dimension = std::move(committed);
            }, &parent);
        dimension_dialog->show();
        auto* dimension_value =
            dimension_dialog->findChild<QDoubleSpinBox*>("sketchDimensionValue");
        auto* dimension_driving =
            dimension_dialog->findChild<QCheckBox*>("sketchDimensionDriving");
        auto* dimension_locked =
            dimension_dialog->findChild<QCheckBox*>("sketchDimensionLocked");
        auto* dimension_prefix =
            dimension_dialog->findChild<QLineEdit*>("sketchDimensionPrefix");
        auto* tolerance_mode =
            dimension_dialog->findChild<QComboBox*>("sketchDimensionToleranceMode");
        auto* symmetric_tolerance =
            dimension_dialog->findChild<QLineEdit*>("sketchSymmetricTolerance");
        const auto dimension_labels = dimension_dialog->findChildren<QLabel*>();
        const auto has_dimension_label = [&](const QString& text) {
            return std::any_of(dimension_labels.cbegin(), dimension_labels.cend(),
                [&](const QLabel* label) { return label->text() == text; });
        };
        require(dimension_value && dimension_driving && dimension_locked &&
                    dimension_prefix &&
                    tolerance_mode && symmetric_tolerance &&
                    dimension_dialog->findChild<QCheckBox*>("sketchLowerEnabled") == nullptr &&
                    has_dimension_label(QStringLiteral("Text před hodnotou")) &&
                    has_dimension_label(QStringLiteral("Text za hodnotou")) &&
                    !has_dimension_label(QStringLiteral("Prefix")) &&
                    !has_dimension_label(QStringLiteral("Suffix")),
                "Sketch dimension Properties is missing style/tolerance fields or retained limits");
        dimension_value->setValue(35.0);
        dimension_driving->setChecked(false);
        require(!dimension_value->isEnabled() && dimension_value->value() == 20.0,
                "Reference dimension remained editable or retained a pending driver value");
        dimension_driving->setChecked(true);
        dimension_locked->setChecked(true);
        require(dimension_value->isEnabled(),
                "Locked driving dimension disabled intentional numeric editing");
        dimension_value->setValue(25.0);
        dimension_prefix->setText(QStringLiteral("⌀"));
        tolerance_mode->setCurrentIndex(
            tolerance_mode->findData(QStringLiteral("symmetric")));
        symmetric_tolerance->setText(QStringLiteral("0.1"));
        dimension_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(dimension_commits == 1 && committed_dimension.locked &&
                    std::abs(committed_dimension.value - 25.0) < 1.0e-9 &&
                    committed_dimension.prefix == "⌀" &&
                    committed_dimension.tolerance_mode == "symmetric" &&
                    committed_dimension.symmetric_tolerance == "0.1",
                "Sketch dimension Properties did not commit display/tolerance style");

        auto assembly_extrusion =
            zima::document::PartDocument::create_extrusion_container("assembly-sketch");
        bool assembly_cut_committed = false;
        zima::document::HistoryContainer committed_assembly_cut;
        std::vector<std::string> committed_targets;
        auto* assembly_cut_dialog = new zima::app::PrimitivePropertiesDialog(
            assembly_extrusion, false, true,
            [&](zima::document::HistoryContainer value,
                std::vector<std::string> targets) {
                assembly_cut_committed = true;
                committed_assembly_cut = std::move(value);
                committed_targets = std::move(targets);
            }, &parent,
            {{"occurrence-a", "Díl A"}, {"occurrence-b", "Díl B"}},
            {"occurrence-b"}, true);
        auto* assembly_targets =
            assembly_cut_dialog->findChild<QListWidget*>("assemblyCutTargets");
        require(assembly_targets != nullptr &&
                    assembly_cut_dialog->findChild<QTableWidget*>(
                        "primitiveReferenceTable") != nullptr &&
                    assembly_cut_dialog->findChild<QPushButton*>(
                        "primitiveAddOperation") != nullptr &&
                    assembly_cut_dialog->findChild<QPushButton*>(
                        "primitiveOwnSketchButton") != nullptr &&
                    assembly_targets->count() == 2 &&
                    assembly_targets->item(0)->checkState() == Qt::Unchecked &&
                    assembly_targets->item(1)->checkState() == Qt::Checked,
                "Assembly cut did not expose the shared dialog target selection");
        assembly_cut_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(assembly_cut_committed &&
                    committed_assembly_cut.combine_mode ==
                        zima::document::CombineMode::Subtract &&
                    committed_targets == std::vector<std::string>{"occurrence-b"},
                "Assembly cut did not commit subtract with exact targets");

        zima::app::ApplicationSettings tool_settings;
        bool parameters_committed = false;
        zima::app::UserParameterData parameter_data;
        parameter_data.flat = {{"name", "Bracket"}};
        parameter_data.order = {"name"};
        parameter_data.labels["name"] = {{"cs", "Název"}, {"en", "Name"}};
        parameter_data.values["name"] = {{"", "Bracket"}};
        auto* user_parameters_dialog = new zima::app::UserParametersDialog(
            parameter_data, "cs", [&](zima::app::UserParameterData value) {
                parameters_committed = value.order == std::vector<std::string>{"name"} &&
                    value.labels["name"]["cs"] == "Název" &&
                    value.flat["name"] == "Bracket";
            }, tool_settings, &parent);
        require(user_parameters_dialog->findChild<QTableWidget*>(
                    "documentParametersTable")->columnCount() == 4 &&
                    user_parameters_dialog->findChild<QComboBox*>(
                        "parameterLanguage") != nullptr,
                "User Parameters does not expose the Python language/table contract");
        user_parameters_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(parameters_committed,
                "User Parameters OK did not preserve order, labels and shared values");

        bool file_settings_committed = false;
        zima::app::DocumentToolData file_data;
        file_data.units = {{"Length", "mm"}, {"Angle", "deg"}, {"Mass", "kg"},
            {"Time", "s"}, {"Temperature", "C"}, {"Stress", "MPa"}};
        file_data.precision = {{"linear_tolerance", "0.001"},
            {"angular_tolerance", "0.001"}, {"mesh_deflection", "0.1"},
            {"decimal_places", "3"}};
        auto* file_settings_dialog = new zima::app::FileSettingsDialog(
            file_data, [&](zima::app::DocumentToolData value) {
                file_settings_committed = value.units["Length"] == "cm";
            }, tool_settings, &parent);
        file_settings_dialog->findChild<QComboBox*>("fileUnitLength")->setCurrentText("cm");
        file_settings_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(file_settings_committed, "File Settings OK did not commit units");

        bool relation_committed = false;
        auto* relations_dialog = new zima::app::RelationsDialog(
            {{"x", "2"}}, {{"result", "x * 3"}},
            [&](auto parameters, auto relations) {
                relation_committed = parameters["result"] == "6.000" &&
                    relations.size() == 1;
            }, tool_settings, &parent);
        relations_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(relation_committed, "Relations OK did not evaluate and commit");

        bool family_committed = false;
        zima::app::DocumentToolData family_data;
        family_data.family_table = R"({"columns":["length"],"instances":[{"name":"LONG","values":{"length":"20"}}]})";
        auto* family_dialog = new zima::app::FamilyTableDialog(
            "GENERIC", family_data, [&](zima::app::DocumentToolData value) {
                family_committed = value.family_table.find("LONG") != std::string::npos;
            }, tool_settings, &parent);
        family_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(family_committed, "Family Table OK did not commit table data");

        bool material_committed = false;
        zima::app::DocumentToolData material_data;
        material_data.physical_parameters = {{"MASS_DENSITY", "7.85e-6"}};
        material_data.physical_parameter_units = {{"MASS_DENSITY", "kg/mm^3"}};
        auto* material_dialog = new zima::app::MaterialDialog(
            material_data, [&](zima::app::DocumentToolData value) {
                material_committed = value.physical_parameters["MASS_DENSITY"] ==
                        "7.85e-6" &&
                    value.physical_parameter_units["MASS_DENSITY"] == "kg/mm^3";
            }, tool_settings, &parent);
        material_dialog->show();
        application.processEvents();
        require(material_dialog->width() >= 620 &&
                    material_dialog->height() >= 380 &&
                    material_dialog->isSizeGripEnabled(),
                "Material dialog did not preserve its resizable large-window contract");
        auto* material_unit = material_dialog->findChild<QComboBox*>();
        require(material_unit != nullptr, "Material unit selector is missing");
        const int material_unit_index = material_unit->currentIndex();
        QWheelEvent unit_wheel(QPointF(4, 4), QPointF(4, 4), QPoint(), QPoint(0, 120),
            Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QApplication::sendEvent(material_unit, &unit_wheel);
        require(material_unit->currentIndex() == material_unit_index,
                "Mouse wheel changed a Material unit selector");
        material_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(material_committed, "Material OK did not commit material data");

        const auto file_dialog_directory = std::filesystem::temp_directory_path() /
            ("zima-cad-file-dialog-contract-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(file_dialog_directory / "0000-index");
        std::filesystem::create_directories(file_dialog_directory / "visible-folder");
        std::ofstream(file_dialog_directory / "visible.prtz") << "{}";
        std::ofstream(file_dialog_directory / "0000-index.prtz") << "{}";
        bool file_dialog_inspected = false;
        bool file_dialog_contents_valid = false;
        int file_dialog_attempts = 0;
        QTimer file_dialog_probe;
        file_dialog_probe.setInterval(10);
        QObject::connect(&file_dialog_probe, &QTimer::timeout, [&] {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (dialog == nullptr) return;
            if (++file_dialog_attempts > 200) {
                file_dialog_inspected = true;
                dialog->reject();
                return;
            }
            auto* proxy = dialog->proxyModel();
            auto* files = proxy == nullptr
                ? nullptr : qobject_cast<QFileSystemModel*>(proxy->sourceModel());
            if (files == nullptr) return;
            const auto source_root = files->index(
                QString::fromStdString(file_dialog_directory.string()));
            if (!source_root.isValid()) return;
            const auto root = proxy->mapFromSource(source_root);
            QStringList names;
            for (int row = 0; row < proxy->rowCount(root); ++row)
                names.push_back(proxy->index(row, 0, root).data().toString());
            if (!names.contains("visible-folder") || !names.contains("visible.prtz") ||
                !names.contains("0000-index.prtz")) return;
            const bool hidden_index = !names.contains("0000-index");
            proxy->sort(0, Qt::DescendingOrder);
            const auto sorted_root = proxy->mapFromSource(source_root);
            bool file_seen = false;
            bool directories_first = true;
            bool document_icon_visible = false;
            for (int row = 0; row < proxy->rowCount(sorted_root); ++row) {
                const auto index = proxy->index(row, 0, sorted_root);
                const auto source_index = proxy->mapToSource(index);
                const bool directory = files->fileInfo(source_index).isDir();
                if (!directory) file_seen = true;
                else if (file_seen) directories_first = false;
                if (index.data().toString() == QStringLiteral("visible.prtz")) {
                    const auto icon = qvariant_cast<QIcon>(
                        index.data(Qt::DecorationRole));
                    document_icon_visible = !icon.isNull() &&
                        !icon.pixmap(QSize(20, 20)).isNull();
                }
            }
            file_dialog_contents_valid = hidden_index && directories_first &&
                document_icon_visible;
            file_dialog_inspected = true;
            dialog->reject();
        });
        file_dialog_probe.start();
        QTimer file_dialog_timeout;
        file_dialog_timeout.setSingleShot(true);
        QObject::connect(&file_dialog_timeout, &QTimer::timeout, [] {
            if (auto* dialog = qobject_cast<QFileDialog*>(
                    QApplication::activeModalWidget())) dialog->reject();
        });
        file_dialog_timeout.start(3000);
        const auto selected_file = zima::app::open_file(
            &parent, "File dialog contract",
            QString::fromStdString(file_dialog_directory.string()),
            "ZIMA-CAD Part (*.prtz)");
        file_dialog_probe.stop();
        file_dialog_timeout.stop();
        std::filesystem::remove_all(file_dialog_directory);
        require(selected_file.isEmpty(),
                "Rejected file dialog unexpectedly returned a selection");
        require(file_dialog_inspected && file_dialog_contents_valid,
                "File dialog did not show its document icon, hide 0000-index, "
                "or keep directories first");

        zima::viewer::MeshView gesture_view(&parent);
        gesture_view.resize(320, 240);
        gesture_view.set_mesh({});
        const auto base_revision = gesture_view.base_mesh_revision();
        zima::kernel::ViewerDimension live_dimension;
        live_dimension.reference = {
            "preview-container", "parameter:length_forward", {}};
        live_dimension.value = 25.0;
        gesture_view.set_transient_dimensions({live_dimension});
        require(gesture_view.base_mesh_revision() == base_revision,
                "Live feature dimension invalidated the base Viewer mesh");
        gesture_view.set_selection_contract({});
        require(gesture_view.selection_candidates_at({160.0, 120.0}).empty(),
                "Idle Properties selection contract still invoked 3D picking");
        int short_middle_confirmations{};
        int double_middle_finishes{};
        gesture_view.set_short_middle_click_callback([&] {
            ++short_middle_confirmations;
            return true;
        });
        gesture_view.set_double_middle_click_callback([&] {
            ++double_middle_finishes;
            return true;
        });
        int pointer_preview_refreshes{};
        gesture_view.set_world_pointer_callback([&](const auto&, const auto&) {
            ++pointer_preview_refreshes;
        });
        require(gesture_view.refresh_current_pointer_preview() &&
                    pointer_preview_refreshes == 1,
                "RMB inference cycling could not refresh the current View preview");
        require(gesture_view.confirm_current_pointer() &&
                    short_middle_confirmations == 1,
                "View-focused confirmation did not share the short-middle path");
        QMouseEvent middle_double(QEvent::MouseButtonDblClick,
            QPointF(20.0, 20.0), QPointF(20.0, 20.0), QPointF(20.0, 20.0),
            Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(&gesture_view, &middle_double);
        QMouseEvent gesture_middle_release(QEvent::MouseButtonRelease,
            QPointF(20.0, 20.0), QPointF(20.0, 20.0), QPointF(20.0, 20.0),
            Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&gesture_view, &gesture_middle_release);
        require(double_middle_finishes == 1 && short_middle_confirmations == 1,
                "Middle double-click did not finish exactly once or leaked a short click");

        zima::viewer::MeshView box_selection_view(&parent);
        box_selection_view.resize(320, 240);
        box_selection_view.show();
        zima::kernel::ViewerMesh box_selection_mesh;
        box_selection_mesh.edges.push_back({
            {{-10.0, -5.0, 0.0}, {0.0, 0.0, 0.0}},
            {"box-selection-sketch", "segment:box-selection-first"},
            false, true});
        box_selection_mesh.edges.push_back({
            {{0.0, 0.0, 0.0}, {10.0, -5.0, 0.0}},
            {"box-selection-sketch", "segment:box-selection-second"},
            false, true});
        box_selection_mesh.points.push_back({{0.0, 0.0, 0.0},
            {"box-selection-sketch", "point:shared-corner"}});
        box_selection_mesh.constraint_markers.push_back({{0.0, 0.0, 0.0},
            "C", {"box-selection-sketch", "constraint:first"}});
        box_selection_mesh.constraint_markers.push_back({{0.0, 0.0, 0.0},
            "T", {"box-selection-sketch", "constraint:second"}});
        // A passive outer edge establishes a larger camera extent, leaving
        // the active Sketch segment wholly inside the drag rectangle.
        box_selection_mesh.edges.push_back({
            {{-100.0, -60.0, 0.0}, {100.0, 60.0, 0.0}},
            {"passive-context", "segment:passive-context"}, false, true});
        box_selection_view.set_mesh(std::move(box_selection_mesh));
        box_selection_view.set_active_sketch_owner("box-selection-sketch");
        box_selection_view.set_selection_contract(
            {zima::viewer::CandidateKind::SketchSegment});
        std::vector<zima::viewer::ViewerCandidate> box_selected;
        bool box_callback_called = false;
        box_selection_view.set_sketch_box_selection(true,
            [&](auto candidates, bool) {
                box_callback_called = true;
                box_selected = std::move(candidates);
            });
        application.processEvents();
        QMouseEvent box_press(QEvent::MouseButtonPress,
            QPointF(4.0, 4.0), QPointF(4.0, 4.0), QPointF(4.0, 4.0),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&box_selection_view, &box_press);
        QMouseEvent box_move(QEvent::MouseMove,
            QPointF(316.0, 236.0), QPointF(316.0, 236.0),
            QPointF(316.0, 236.0), Qt::NoButton, Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&box_selection_view, &box_move);
        QMouseEvent box_release(QEvent::MouseButtonRelease,
            QPointF(316.0, 236.0), QPointF(316.0, 236.0),
            QPointF(316.0, 236.0), Qt::LeftButton, Qt::NoButton,
            Qt::NoModifier);
        QApplication::sendEvent(&box_selection_view, &box_release);
        require(box_callback_called,
                "Sketch View did not complete the drag rectangle gesture");
        require(box_selected.size() == 3 &&
                    std::count_if(box_selected.begin(), box_selected.end(),
                        [](const auto& candidate) {
                            return candidate.kind ==
                                zima::viewer::CandidateKind::SketchSegment;
                        }) == 2,
                "Sketch View drag rectangle did not select its enclosed geometry");
        box_selection_view.set_selection_contract({
            zima::viewer::CandidateKind::SketchSegment,
            zima::viewer::CandidateKind::SketchPoint});
        std::optional<QPointF> shared_corner_position;
        for (int y = 0; y < box_selection_view.height() && !shared_corner_position;
             ++y) {
            for (int x = 0; x < box_selection_view.width(); ++x) {
                const QPointF position(x, y);
                const auto candidates =
                    box_selection_view.selection_candidates_at(position);
                if (std::any_of(candidates.begin(), candidates.end(),
                        [](const auto& candidate) {
                            return candidate.kind ==
                                    zima::viewer::CandidateKind::SketchPoint &&
                                candidate.semantic_key == "point:shared-corner";
                        })) {
                    shared_corner_position = position;
                    break;
                }
            }
        }
        require(shared_corner_position.has_value(),
                "Shared Sketch corner was not a View candidate");
        box_selection_view.set_selection_contract({
            zima::viewer::CandidateKind::SketchConstraint});
        const auto second_relation_candidates =
            box_selection_view.selection_candidates_at(
                *shared_corner_position + QPointF(25.0, -12.0));
        require(!second_relation_candidates.empty() &&
                    second_relation_candidates.front().semantic_key ==
                        "constraint:second",
                "Second visible relation glyph was not directly selectable");
        box_selection_view.set_selection_contract({
            zima::viewer::CandidateKind::SketchSegment,
            zima::viewer::CandidateKind::SketchPoint});
        std::optional<zima::viewer::ViewerCandidate> drag_candidate;
        box_selection_view.set_candidate_drag_callbacks(
            [&](const auto& candidate, const auto&, const auto&) {
                drag_candidate = candidate;
                return true;
            }, [](const auto&, const auto&) {}, [] {});
        QMouseEvent corner_press(QEvent::MouseButtonPress,
            *shared_corner_position, *shared_corner_position,
            *shared_corner_position, Qt::LeftButton, Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(&box_selection_view, &corner_press);
        require(drag_candidate && drag_candidate->kind ==
                    zima::viewer::CandidateKind::SketchPoint &&
                    drag_candidate->semantic_key == "point:shared-corner",
                "Two selected segments did not prioritize their shared corner drag");
        QMouseEvent corner_release(QEvent::MouseButtonRelease,
            *shared_corner_position, *shared_corner_position,
            *shared_corner_position, Qt::LeftButton, Qt::NoButton,
            Qt::NoModifier);
        QApplication::sendEvent(&box_selection_view, &corner_release);

        std::cout << "C++ properties-window contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
