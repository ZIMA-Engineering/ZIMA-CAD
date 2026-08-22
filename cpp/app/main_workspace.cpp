#include "assembly_workspace_window.hpp"
#include "construction_properties_dialog.hpp"
#include "drawing_window.hpp"

#include <zima/viewer/mesh_view.hpp>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPalette>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QSurfaceFormat>
#include <QTabBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUuid>
#include <QWidget>

#include <iostream>
#include <filesystem>
#include <tuple>

namespace {

bool verify(bool condition, const char* message) {
    if (!condition) std::cerr << "Startup contract failed: " << message << '\n';
    return condition;
}

bool contains_rendered_geometry(const QImage& image) {
    if (image.isNull() || image.width() < 2 || image.height() < 2) return false;
    const QColor background = image.pixelColor(0, 0);
    std::size_t sampled{};
    std::size_t changed{};
    for (int y = 0; y < image.height(); y += 4) {
        for (int x = 0; x < image.width(); x += 4) {
            const QColor pixel = image.pixelColor(x, y);
            const int distance = std::abs(pixel.red() - background.red()) +
                std::abs(pixel.green() - background.green()) +
                std::abs(pixel.blue() - background.blue());
            ++sampled;
            if (distance > 24) ++changed;
        }
    }
    return sampled != 0 && changed * 20 > sampled;
}

bool contains_orange_hover(const QImage& image) {
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.red() >= 225 && pixel.green() >= 80 &&
                pixel.green() <= 175 && pixel.blue() <= 55) return true;
        }
    }
    return false;
}

bool contains_cyan_selection(const QImage& image) {
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.red() <= 80 && pixel.green() >= 175 &&
                pixel.blue() >= 190) return true;
        }
    }
    return false;
}

int verify_startup_contract(
    QApplication& application, zima::app::AssemblyWorkspaceWindow& window,
    const QString& part_capture_path = {}, const QString& drawing_capture_path = {}) {
    window.show();
    application.processEvents();

    auto* tabs = window.findChild<QTabBar*>("documentTabs");
    auto* tree = window.findChild<QTreeWidget*>("documentTree");
    auto* splitter = window.findChild<QSplitter*>("documentSplitter");
    auto* main_toolbar = window.findChild<QToolBar*>("mainToolbar");
    auto* view_toolbar = window.findChild<QToolBar*>("viewToolbar");
    auto* tools_toolbar = window.findChild<QToolBar*>("toolsToolbar");
    auto* box = window.findChild<QAction*>("boxAction");
    auto* construction_point = window.findChild<QAction*>("constructionPointAction");
    auto* construction_axis = window.findChild<QAction*>("constructionAxisAction");
    auto* construction_plane = window.findChild<QAction*>("constructionPlaneAction");
    auto* sketch = window.findChild<QAction*>("sketchAction");
    auto* sketch_normal = window.findChild<QAction*>("sketchNormalViewAction");
    auto* sketch_point = window.findChild<QAction*>("sketchPointAction");
    auto* sketch_construction = window.findChild<QAction*>("sketchConstructionAction");
    auto* sketch_segment = window.findChild<QAction*>("sketchSegmentAction");
    auto* sketch_polyline = window.findChild<QAction*>("sketchPolylineAction");
    auto* sketch_rectangle = window.findChild<QAction*>("sketchRectangleAction");
    auto* sketch_polygon = window.findChild<QAction*>("sketchPolygonAction");
    auto* sketch_trim = window.findChild<QAction*>("sketchTrimAction");
    auto* sketch_mirror = window.findChild<QAction*>("sketchMirrorAction");
    auto* sketch_elliptical_arc =
        window.findChild<QAction*>("sketchEllipticalArcAction");
    auto* sketch_midpoint = window.findChild<QAction*>("sketchMidpointAction");
    auto* sketch_symmetric = window.findChild<QAction*>("sketchSymmetricAction");
    auto* sketch_concentric = window.findChild<QAction*>("sketchConcentricAction");
    auto* sketch_tangent = window.findChild<QAction*>("sketchTangentAction");
    auto* sketch_equal = window.findChild<QAction*>("sketchEqualAction");
    auto* sketch_text = window.findChild<QAction*>("sketchTextAction");
    auto* sketch_external_reference =
        window.findChild<QAction*>("sketchExternalReferenceAction");
    auto* sketch_constraints =
        window.findChild<QAction*>("sketchConstraintsAction");
    auto* sketch_dimensions =
        window.findChild<QAction*>("sketchDimensionsAction");
    auto* finish_sketch = window.findChild<QAction*>("finishSketchAction");
    auto* extrusion = window.findChild<QAction*>("extrusionAction");
    auto* about = window.findChild<QAction*>("aboutAction");
    auto* save_as = window.findChild<QAction*>("saveDocumentAsAction");
    auto* rename_document = window.findChild<QAction*>("renameDocumentAction");
    auto* delete_file_menu = window.findChild<QMenu*>("deleteFileMenu");
    auto* delete_current_file = window.findChild<QAction*>("deleteCurrentFileAction");
    auto* delete_all_versions = window.findChild<QAction*>("deleteAllVersionsAction");
    auto* delete_old_versions = window.findChild<QAction*>("deleteOldVersionsAction");
    auto* delete_old_versions_keep_latest =
        window.findChild<QAction*>("deleteOldVersionsKeepLatestAction");
    auto* delete_working_directory_menu =
        window.findChild<QMenu*>("deleteWorkingDirectoryMenu");
    auto* delete_working_directory_old_versions =
        window.findChild<QAction*>("deleteWorkingDirectoryOldVersionsAction");
    auto* delete_working_directory_keep_latest =
        window.findChild<QAction*>("deleteWorkingDirectoryKeepLatestAction");
    auto* working_directory = window.findChild<QAction*>("workingDirectoryAction");
    auto* new_document = window.findChild<QAction*>("newDocumentAction");
    auto* undo = window.findChild<QAction*>("undoAction");
    auto* redo = window.findChild<QAction*>("redoAction");
    auto* save = window.findChild<QAction*>("saveDocumentAction");
    auto* close = window.findChild<QAction*>("closeDocumentAction");
    auto* parameters = window.findChild<QAction*>("documentParametersAction");
    auto* export_document = window.findChild<QAction*>("exportDocumentAction");
    auto* global_settings = window.findChild<QAction*>("globalSettingsAction");
    auto* standard_views = window.findChild<QMenu*>("standardViewsMenu");
    auto* colors_menu = window.findChild<QMenu*>("colorsMenu");
    auto* fit_view = window.findChild<QAction*>("fitViewAction");
    auto* view_selection = window.findChild<QAction*>("viewSelectionAction");
    auto* import_document = window.findChild<QAction*>("importDocumentAction");
    auto* material = window.findChild<QAction*>("materialAction");
    auto* relations = window.findChild<QAction*>("relationsAction");
    auto* family_table = window.findChild<QAction*>("familyTableAction");
    auto* file_settings = window.findChild<QAction*>("fileSettingsAction");
    auto* window_menu = window.findChild<QMenu*>("windowMenu");
    if (!verify(tabs != nullptr && tree != nullptr, "document navigation is missing") ||
        !verify(splitter != nullptr && main_toolbar != nullptr &&
                    view_toolbar != nullptr && tools_toolbar != nullptr,
                "Python-compatible workspace shell is missing") ||
        !verify(box != nullptr && sketch != nullptr && sketch_normal != nullptr &&
                    sketch_point != nullptr && sketch_construction != nullptr &&
                    sketch_segment != nullptr && sketch_polyline != nullptr &&
                    sketch_rectangle != nullptr &&
                    sketch_polygon != nullptr && sketch_polygon->menu() != nullptr &&
                    sketch_polygon->menu()->actions().size() == 3 &&
                    sketch_trim != nullptr &&
                    sketch_mirror != nullptr &&
                    sketch_elliptical_arc != nullptr &&
                    sketch_midpoint != nullptr &&
                    sketch_symmetric != nullptr &&
                    sketch_concentric != nullptr &&
                    sketch_tangent != nullptr &&
                    sketch_equal != nullptr &&
                    sketch_text != nullptr &&
                    sketch_external_reference != nullptr &&
                    sketch_constraints != nullptr &&
                    sketch_constraints->menu() != nullptr &&
                    sketch_constraints->menu()->actions().size() == 12 &&
                    sketch_constraints->menu()->actions().contains(sketch_equal) &&
                    sketch_dimensions != nullptr &&
                    sketch_dimensions->menu() != nullptr &&
                    sketch_dimensions->menu()->actions().size() == 9 &&
                    finish_sketch != nullptr &&
                    extrusion != nullptr && about != nullptr && save_as != nullptr &&
                    rename_document != nullptr && delete_file_menu != nullptr &&
                    delete_working_directory_menu != nullptr &&
                    working_directory != nullptr && new_document != nullptr &&
                    undo != nullptr && redo != nullptr &&
                    save != nullptr && close != nullptr && parameters != nullptr,
                "primary actions are missing") ||
        !verify(tabs->count() == 0 && !splitter->isVisible() &&
                    window.windowTitle() == QStringLiteral("ZIMA-CAD — Bez dokumentu"),
                "application must start without a document") ||
        !verify(main_toolbar->isVisible() && !save_as->isEnabled() &&
                    working_directory->isEnabled(),
                "startup file-command state is invalid")) {
        return 1;
    }
    if (!verify(export_document != nullptr && !export_document->isEnabled() &&
                    parameters != nullptr && !parameters->isEnabled(),
                "document-only commands must be disabled without a document") ||
        !verify(import_document != nullptr && import_document->isEnabled() &&
                    working_directory->isEnabled() && about->isEnabled(),
                "document-independent commands must remain available") ||
        !verify(standard_views != nullptr &&
                    !standard_views->menuAction()->isEnabled() &&
                    colors_menu != nullptr &&
                    !colors_menu->menuAction()->isEnabled() &&
                    fit_view != nullptr && !fit_view->isEnabled() &&
                    view_selection != nullptr && !view_selection->isEnabled(),
                "viewer commands must be disabled without a visible document") ||
        !verify(global_settings != nullptr && global_settings->isEnabled(),
                "Global Settings must remain functional without a document")) {
        return 1;
    }
    if (!verify(material != nullptr && !material->isEnabled() &&
                    relations != nullptr && !relations->isEnabled() &&
                    family_table != nullptr && !family_table->isEnabled() &&
                    file_settings != nullptr && !file_settings->isEnabled(),
                "unavailable document tools must remain visibly disabled")) {
        return 1;
    }
    for (int index = 0; index < 6; ++index) {
        auto* action = window.findChild<QAction*>(
            QStringLiteral("applicationModeAction%1").arg(index));
        if (!verify(action != nullptr && !action->isEnabled(),
                    "application modes must be disabled without a document")) {
            return 1;
        }
    }
    if (!verify(window_menu != nullptr &&
                    QMetaObject::invokeMethod(window_menu, "aboutToShow",
                                              Qt::DirectConnection),
                "Window menu cannot be refreshed")) {
        return 1;
    }
    auto* new_window = window.findChild<QAction*>("newWindowAction");
    if (!verify(new_window != nullptr && new_window->isEnabled(),
                "New Window must remain functional without a document")) {
        return 1;
    }
    global_settings->trigger();
    application.processEvents();
    auto* global_dialog = window.findChild<QDialog*>("globalSettingsDialog");
    auto* global_language = global_dialog == nullptr
        ? nullptr : global_dialog->findChild<QComboBox*>("globalSettingsLanguage");
    auto* global_buttons = global_dialog == nullptr
        ? nullptr : global_dialog->findChild<QDialogButtonBox*>();
    if (!verify(global_dialog != nullptr &&
                    global_dialog->windowFlags().testFlag(Qt::SubWindow) &&
                    global_language != nullptr && global_language->count() == 4 &&
                    global_language->findText("cs") >= 0 &&
                    global_language->findText("de") >= 0 &&
                    global_language->findText("en") >= 0 &&
                    global_language->findText("fr") >= 0 &&
                    global_dialog->findChildren<QLineEdit*>().size() == 4 &&
                    global_buttons != nullptr &&
                    global_buttons->buttons().size() == 2,
                "Global Settings must implement the Python startup contract")) {
        return 1;
    }
    global_buttons->button(QDialogButtonBox::Cancel)->click();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    bool new_dialog_validation_checked = false;
    const auto create_document = [&](const QString& type, const QString& name) {
        new_document->trigger();
        application.processEvents();
        auto* dialog = window.findChild<QDialog*>("newDocumentDialog");
        auto* name_field = dialog == nullptr
            ? nullptr : dialog->findChild<QLineEdit*>("newDocumentFileName");
        auto* buttons = dialog == nullptr
            ? nullptr : dialog->findChild<QDialogButtonBox*>();
        const auto type_radios = dialog == nullptr
            ? QList<QRadioButton*>{} : dialog->findChildren<QRadioButton*>();
        if (!verify(dialog != nullptr && name_field != nullptr && buttons != nullptr,
                    "New must open the shared in-application document dialog") ||
            !verify(dialog->windowFlags().testFlag(Qt::SubWindow),
                    "new document dialog must be an internal SubWindow") ||
            !verify(type_radios.size() == 5 &&
                        std::all_of(type_radios.begin(), type_radios.end(),
                            [](const auto* radio) { return radio->isEnabled(); }),
                    "New must expose all five Python document types")) {
            return false;
        }
        if (!new_dialog_validation_checked) {
            name_field->clear();
            buttons->button(QDialogButtonBox::Ok)->click();
            application.processEvents();
            auto* validation_error = dialog->findChild<QLabel*>("newDocumentError");
            if (!verify(dialog->isVisible() && validation_error != nullptr &&
                            validation_error->isVisible() &&
                            !validation_error->text().isEmpty(),
                        "New must reject an empty file name inside the dialog")) {
                return false;
            }
            new_dialog_validation_checked = true;
        }
        name_field->setText(name);
        for (auto* radio : dialog->findChildren<QRadioButton*>()) {
            radio->setChecked(radio->property("documentType").toString() == type);
        }
        buttons->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        return true;
    };

    const QString identity = QUuid::createUuid().toString(QUuid::Id128);
    const QString part_name = QStringLiteral("DIL-STARTUP-") + identity;
    const QString assembly_name = QStringLiteral("SESTAVA-STARTUP-") + identity;
    const QString nested_assembly_name = QStringLiteral("NADSESTAVA-STARTUP-") + identity;
    const QString drawing_name = QStringLiteral("VYKRES-STARTUP-") + identity;
    if (!create_document(QStringLiteral("part"),
                         part_name + QStringLiteral(".prtz")) ||
        !verify(tabs->count() == 1 &&
                    tabs->tabText(0) == part_name + QStringLiteral(".prtz"),
                "new Part must open in the common document tabs") ||
        !verify(splitter->isVisible() && box->isEnabled() && tools_toolbar->isVisible(),
                "Part workspace and Modeling commands must become visible") ||
        !verify(save_as->isEnabled(), "Save As must be available for an open document")) {
        return 1;
    }

    parameters->trigger();
    application.processEvents();
    auto* parameters_dialog =
        window.findChild<QDialog*>("documentParametersDialog");
    auto* parameters_table = parameters_dialog == nullptr ? nullptr :
        parameters_dialog->findChild<QTableWidget*>("documentParametersTable");
    auto* parameter_language = parameters_dialog == nullptr ? nullptr :
        parameters_dialog->findChild<QComboBox*>("parameterLanguage");
    if (!verify(parameters->isEnabled() && parameters_dialog != nullptr &&
                    parameters_dialog->windowFlags().testFlag(Qt::SubWindow) &&
                    parameters_table != nullptr && parameters_table->rowCount() >= 12 &&
                    parameters_table->columnCount() == 4 &&
                    parameter_language != nullptr && parameter_language->count() >= 4 &&
                    parameters_dialog->findChild<QTableWidget*>(
                        "documentRelationsTable") == nullptr,
                "Parameters must match the localized Python table contract")) {
        return 1;
    }
    parameters_dialog->reject();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();

    const std::array tool_dialogs{
        std::pair{material, QStringLiteral("materialDialog")},
        std::pair{relations, QStringLiteral("relationsDialog")},
        std::pair{family_table, QStringLiteral("familyTableDialog")},
        std::pair{file_settings, QStringLiteral("fileSettingsDialog")}};
    for (const auto& [action, object_name] : tool_dialogs) {
        if (!verify(action != nullptr && action->isEnabled(),
                    "document tool must be enabled for an open Part")) return 1;
        action->trigger(); application.processEvents();
        auto* tool_dialog = window.findChild<QDialog*>(object_name);
        auto* tool_buttons = tool_dialog == nullptr
            ? nullptr : tool_dialog->findChild<QDialogButtonBox*>();
        if (!verify(tool_dialog != nullptr &&
                        tool_dialog->windowFlags().testFlag(Qt::SubWindow) &&
                        tool_buttons != nullptr && tool_buttons->buttons().size() == 2,
                    "document tool must use the shared OK/Cancel SubWindow")) {
            return 1;
        }
        tool_buttons->button(QDialogButtonBox::Cancel)->click();
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    new_document->trigger();
    application.processEvents();
    auto* duplicate_dialog = window.findChild<QDialog*>("newDocumentDialog");
    auto* duplicate_name = duplicate_dialog == nullptr
        ? nullptr : duplicate_dialog->findChild<QLineEdit*>("newDocumentFileName");
    auto* duplicate_buttons = duplicate_dialog == nullptr
        ? nullptr : duplicate_dialog->findChild<QDialogButtonBox*>();
    auto* duplicate_error = duplicate_dialog == nullptr
        ? nullptr : duplicate_dialog->findChild<QLabel*>("newDocumentError");
    if (!verify(duplicate_name != nullptr && duplicate_buttons != nullptr &&
                    duplicate_error != nullptr,
                "new document validation controls are missing")) {
        return 1;
    }
    duplicate_name->setText(part_name);
    duplicate_buttons->button(QDialogButtonBox::Ok)->click();
    application.processEvents();
    if (!verify(duplicate_dialog->isVisible() && duplicate_error->isVisible() &&
                    tabs->count() == 1,
                "duplicate document path must keep the New dialog transactional")) {
        return 1;
    }
    duplicate_buttons->button(QDialogButtonBox::Cancel)->click();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();

    box->trigger();
    application.processEvents();
    auto* properties = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    auto* buttons = properties == nullptr
        ? nullptr : properties->findChild<QDialogButtonBox*>();
    if (!verify(buttons != nullptr, "Box must open the shared Properties window")) {
        return 1;
    }
    buttons->button(QDialogButtonBox::Ok)->click();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    QTreeWidgetItem* box_tree_item{};
    if (tree->topLevelItemCount() == 1) {
        auto* root = tree->topLevelItem(0);
        for (int index = 0; index < root->childCount(); ++index) {
            if (root->child(index)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-container")) {
                box_tree_item = root->child(index);
                break;
            }
        }
    }
    if (!verify(box_tree_item != nullptr,
                "confirming Box must create the first Part history item")) {
        return 1;
    }
    auto* selection_viewer = dynamic_cast<zima::viewer::MeshView*>(
        window.findChild<QOpenGLWidget*>("modelWorkspace"));
    tree->setCurrentItem(box_tree_item);
    application.processEvents();
    const auto tree_confirmed = selection_viewer == nullptr
        ? std::optional<zima::viewer::ViewerCandidate>{}
        : selection_viewer->confirmed_candidate();
    if (!verify(tree_confirmed && tree_confirmed->kind ==
                    zima::viewer::CandidateKind::Container &&
                    tree_confirmed->owner_id ==
                        box_tree_item->data(0, Qt::UserRole).toString().toStdString(),
                "Tree selectionChanged did not synchronize the View candidate")) {
        return 1;
    }
    std::optional<QPointF> empty_view_position;
    for (int y = 4; y < selection_viewer->height() && !empty_view_position; y += 8) {
        for (int x = 4; x < selection_viewer->width(); x += 8) {
            const QPointF position{static_cast<qreal>(x), static_cast<qreal>(y)};
            if (selection_viewer->selection_candidates_at(position).empty()) {
                empty_view_position = position;
                break;
            }
        }
    }
    if (!verify(empty_view_position.has_value(),
                "Viewer selection contract exposed no empty click position")) {
        return 1;
    }
    QMouseEvent empty_view_press(QEvent::MouseButtonPress, *empty_view_position,
        selection_viewer->mapToGlobal(empty_view_position->toPoint()),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(selection_viewer, &empty_view_press);
    application.processEvents();
    if (!verify(!selection_viewer->confirmed_candidate() &&
                    tree->selectedItems().empty() && tree->currentItem() == nullptr,
                "Empty View click did not clear the shared View and Tree selection")) {
        return 1;
    }
    {
        // RMB cycling must consume the exact same ordered candidate list as
        // hover/LMB and remain active only until an LMB confirms one of the
        // offered candidates. The default (Container-only) selection filter
        // offers exactly one whole-body candidate per Box, so switch to the
        // Face filter to expose genuinely overlapping Face candidates near
        // a shared edge, matching real multi-candidate cycling.
        auto* selection_filter_combo =
            window.findChild<QComboBox*>("selectionFilterCombo");
        if (!verify(selection_filter_combo != nullptr,
                    "the real workspace has no selectionFilterCombo")) {
            return 1;
        }
        selection_filter_combo->setCurrentIndex(1);
        application.processEvents();
        std::optional<QPointF> multi_candidate_position;
        std::vector<zima::viewer::ViewerCandidate> multi_candidates;
        for (int y = 4; y < selection_viewer->height() && !multi_candidate_position;
             y += 4) {
            for (int x = 4; x < selection_viewer->width(); x += 4) {
                const QPointF position{static_cast<qreal>(x), static_cast<qreal>(y)};
                auto candidates = selection_viewer->selection_candidates_at(position);
                if (candidates.size() > 1) {
                    multi_candidate_position = position;
                    multi_candidates = std::move(candidates);
                    break;
                }
            }
        }
        if (!verify(multi_candidate_position.has_value(),
                    "Box scene offered no overlapping candidates for RMB cycling")) {
            return 1;
        }
        QMouseEvent hover_move(QEvent::MouseMove, *multi_candidate_position,
            selection_viewer->mapToGlobal(multi_candidate_position->toPoint()),
            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(selection_viewer, &hover_move);
        application.processEvents();
        const auto initial_hover = selection_viewer->hovered_candidate();
        if (!verify(initial_hover.has_value(),
                    "Overlapping position did not offer an initial hover candidate")) {
            return 1;
        }
        const auto send_rmb_press = [&] {
            QMouseEvent press(QEvent::MouseButtonPress, *multi_candidate_position,
                selection_viewer->mapToGlobal(multi_candidate_position->toPoint()),
                Qt::RightButton, Qt::RightButton, Qt::NoModifier);
            QApplication::sendEvent(selection_viewer, &press);
            application.processEvents();
        };
        send_rmb_press();
        const auto after_first_cycle = selection_viewer->hovered_candidate();
        if (!verify(after_first_cycle.has_value() &&
                        !selection_viewer->confirmed_candidate() &&
                        (after_first_cycle->kind != initial_hover->kind ||
                            after_first_cycle->semantic_key !=
                                initial_hover->semantic_key ||
                            after_first_cycle->owner_id != initial_hover->owner_id),
                    "RMB before LMB confirmation did not cycle to the next "
                    "candidate in the shared ordered list")) {
            return 1;
        }
        // Cycling all the way back to the original candidate (once per
        // remaining entry) must reproduce the exact same candidate, proving
        // RMB consumes one fixed ordered list rather than recomputing a
        // different candidate on each press.
        for (std::size_t step = 1; step < multi_candidates.size(); ++step) {
            send_rmb_press();
        }
        const auto cycled_back = selection_viewer->hovered_candidate();
        if (!verify(cycled_back.has_value() &&
                        cycled_back->kind == initial_hover->kind &&
                        cycled_back->semantic_key == initial_hover->semantic_key &&
                        cycled_back->owner_id == initial_hover->owner_id,
                    "RMB cycling did not return to the original candidate after "
                    "visiting the complete ordered list")) {
            return 1;
        }
        QMouseEvent lmb_press(QEvent::MouseButtonPress, *multi_candidate_position,
            selection_viewer->mapToGlobal(multi_candidate_position->toPoint()),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(selection_viewer, &lmb_press);
        application.processEvents();
        const auto confirmed_after_cycle = selection_viewer->confirmed_candidate();
        if (!verify(confirmed_after_cycle.has_value() &&
                        confirmed_after_cycle->kind == initial_hover->kind &&
                        confirmed_after_cycle->semantic_key ==
                            initial_hover->semantic_key &&
                        confirmed_after_cycle->owner_id == initial_hover->owner_id,
                    "LMB did not confirm the exact RMB-cycled candidate")) {
            return 1;
        }
        QMouseEvent clear_press(QEvent::MouseButtonPress, *empty_view_position,
            selection_viewer->mapToGlobal(empty_view_position->toPoint()),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(selection_viewer, &clear_press);
        application.processEvents();
        selection_filter_combo->setCurrentIndex(0);
        application.processEvents();
        if (!verify(!selection_viewer->confirmed_candidate(),
                    "Clearing selection after RMB cycling left a stale candidate")) {
            return 1;
        }
        // Leave the pointer over empty space so no stale hover candidate
        // carries into the next scenario's own hover search.
        QMouseEvent settle_move(QEvent::MouseMove, *empty_view_position,
            selection_viewer->mapToGlobal(empty_view_position->toPoint()),
            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(selection_viewer, &settle_move);
        application.processEvents();
        selection_viewer->repaint();
        application.processEvents();
    }
    construction_point->trigger();
    application.processEvents();
    auto* point_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    auto* point_references = point_dialog == nullptr
        ? nullptr : point_dialog->findChild<QTableWidget*>(
            "constructionReferenceTable");
    auto* point_viewer = dynamic_cast<zima::viewer::MeshView*>(
        window.findChild<QOpenGLWidget*>("modelWorkspace"));
    auto* point_properties =
        dynamic_cast<zima::app::ConstructionPropertiesDialog*>(point_dialog);
    std::optional<zima::viewer::ViewerCandidate> own_point_candidate;
    if (point_viewer != nullptr && point_properties != nullptr) {
        for (int y = 2; y < point_viewer->height() && !own_point_candidate; y += 2) {
            for (int x = 2; x < point_viewer->width(); x += 2) {
                for (const auto& candidate : point_viewer->selection_candidates_at(
                         {static_cast<qreal>(x), static_cast<qreal>(y)})) {
                    if (point_properties->owns_reference_owner(candidate.owner_id)) {
                        own_point_candidate = candidate;
                        break;
                    }
                }
                if (own_point_candidate) break;
            }
        }
    }
    if (!verify(point_properties != nullptr && !own_point_candidate,
                "Point placement offered its own Container Origin geometry")) {
        if (own_point_candidate) {
            std::cerr << "  owner='" << own_point_candidate->owner_id
                      << "' key='" << own_point_candidate->semantic_key
                      << "' geometry=" << static_cast<int>(
                             own_point_candidate->geometry) << '\n';
        }
        return 1;
    }
    std::optional<QPointF> origin_axis_hover_position;
    if (point_viewer != nullptr) {
        for (int y = 2; y < point_viewer->height() && !origin_axis_hover_position; y += 4) {
            for (int x = 2; x < point_viewer->width(); x += 4) {
                const QPointF local{static_cast<qreal>(x), static_cast<qreal>(y)};
                const auto candidates = point_viewer->selection_candidates_at(local);
                if (!candidates.empty() &&
                    candidates.front().kind == zima::viewer::CandidateKind::Axis &&
                    candidates.front().semantic_key.starts_with("origin:axis:")) {
                    origin_axis_hover_position = local;
                    break;
                }
            }
        }
    }
    if (!verify(origin_axis_hover_position.has_value(),
                "Point placement did not offer an Origin axis in View")) return 1;
    const auto origin_axis_candidates =
        point_viewer->selection_candidates_at(*origin_axis_hover_position);
    if (!verify(!origin_axis_candidates.empty() &&
                    !point_properties->owns_reference_owner(
                        origin_axis_candidates.front().owner_id) &&
                    origin_axis_candidates.front().owner_id.ends_with(":origin"),
                "Overlapped Point placement axis was not owned by the document Origin")) {
        return 1;
    }
    QMouseEvent origin_axis_move(QEvent::MouseMove, *origin_axis_hover_position,
        point_viewer->mapToGlobal(origin_axis_hover_position->toPoint()),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(point_viewer, &origin_axis_move);
    application.processEvents();
    point_viewer->repaint();
    if (!verify(point_viewer->hovered_candidate() &&
                    point_viewer->hovered_candidate()->semantic_key.starts_with(
                        "origin:axis:") &&
                    contains_orange_hover(point_viewer->grabFramebuffer()),
                "Origin axis hover was not rendered orange during placement")) return 1;
    std::optional<zima::viewer::ViewerCandidate> point_hover;
    QPointF point_hover_position;
    if (point_viewer != nullptr) {
        for (int y = 8; y < point_viewer->height() - 8 && !point_hover; y += 8) {
            for (int x = 8; x < point_viewer->width() - 8 && !point_hover; x += 8) {
                const QPointF local{static_cast<qreal>(x), static_cast<qreal>(y)};
                QMouseEvent move(QEvent::MouseMove, local,
                    point_viewer->mapToGlobal(local.toPoint()), Qt::NoButton,
                    Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(point_viewer, &move);
                point_hover = point_viewer->hovered_candidate();
                if (point_hover) point_hover_position = local;
            }
        }
    }
    if (!verify(point_dialog != nullptr && point_references != nullptr &&
                    point_viewer != nullptr && point_hover.has_value() &&
                    (point_hover->geometry ==
                         zima::viewer::CandidateGeometry::OriginalReference ||
                     point_hover->semantic_key.starts_with("origin:")),
                "Point command did not offer a persisted viewer candidate on hover")) {
        return 1;
    }
    QMouseEvent point_press(QEvent::MouseButtonPress, point_hover_position,
        point_viewer->mapToGlobal(point_hover_position.toPoint()), Qt::LeftButton,
        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(point_viewer, &point_press);
    application.processEvents();
    auto* selected_reference = point_references->cellWidget(0, 1);
    if (!verify(selected_reference != nullptr &&
                    !selected_reference->property("text").toString().contains(
                        QString::fromStdString(point_hover->owner_id)),
                "Point command did not pass the confirmed viewer candidate to its dialog")) {
        return 1;
    }
    if (auto* point_buttons = point_dialog->findChild<QDialogButtonBox*>()) {
        point_buttons->button(QDialogButtonBox::Ok)->click();
    }
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    QTreeWidgetItem* point_tree_item{};
    box_tree_item = nullptr;
    if (tree->topLevelItemCount() == 1) {
        auto* root = tree->topLevelItem(0);
        for (int index = 0; index < root->childCount(); ++index) {
            auto* child = root->child(index);
            const auto item_kind = child->data(0, Qt::UserRole + 3).toString();
            if (item_kind == QStringLiteral("part-container") &&
                child->text(0) == QStringLiteral("+ Kvádr")) {
                box_tree_item = child;
            } else if (item_kind ==
                    QStringLiteral("part-construction")) {
                point_tree_item = child;
            }
        }
    }
    if (!verify(box_tree_item != nullptr && point_tree_item != nullptr,
                "Point refresh did not preserve the Box and Point history items")) {
        return 1;
    }
    const auto click_tree_item = [&](QTreeWidgetItem* item) {
        tree->scrollToItem(item);
        application.processEvents();
        const QPoint position = tree->visualItemRect(item).center();
        QMouseEvent press(QEvent::MouseButtonPress, position,
            tree->viewport()->mapToGlobal(position), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, position,
            tree->viewport()->mapToGlobal(position), Qt::LeftButton,
            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &release);
        application.processEvents();
    };
    auto* document_origin = tree->topLevelItem(0)->child(0);
    auto* document_x_axis = document_origin == nullptr ||
            document_origin->childCount() < 2
        ? nullptr : document_origin->child(1);
    if (!verify(document_x_axis != nullptr &&
                    document_origin->child(0)->text(0) == QStringLiteral("Point"),
                "Part Origin tree does not use the Python Point/axis structure")) {
        return 1;
    }
    tree->expandItem(document_origin);
    application.processEvents();
    click_tree_item(document_x_axis);
    const auto selected_origin_axis = point_viewer->confirmed_candidate();
    if (!verify(selected_origin_axis &&
                    selected_origin_axis->kind ==
                        zima::viewer::CandidateKind::Axis &&
                    selected_origin_axis->semantic_key == "origin:axis:x" &&
                    contains_cyan_selection(point_viewer->grabFramebuffer()),
                "Tree Origin axis did not select its complete View presentation")) {
        return 1;
    }
    if (!part_capture_path.isEmpty() &&
        !verify(point_viewer->grabFramebuffer().save(
                    part_capture_path + QStringLiteral(".origin-axis.png")),
                "Origin axis selection framebuffer save failed")) return 1;
    auto* container_origin_point = point_tree_item->child(0)->child(0);
    tree->expandItem(point_tree_item);
    tree->expandItem(point_tree_item->child(0));
    application.processEvents();
    click_tree_item(container_origin_point);
    const auto selected_container_origin_point = point_viewer->confirmed_candidate();
    if (!part_capture_path.isEmpty()) {
        point_viewer->grabFramebuffer().save(
            part_capture_path + QStringLiteral(".container-origin-point.png"));
    }
    if (!verify(selected_container_origin_point &&
                    selected_container_origin_point->kind ==
                        zima::viewer::CandidateKind::Vertex &&
                    selected_container_origin_point->semantic_key == "point" &&
                    contains_cyan_selection(point_viewer->grabFramebuffer()),
                "Point container Origin Point did not select its View marker")) {
        return 1;
    }
    click_tree_item(box_tree_item);
    click_tree_item(point_tree_item);
    application.processEvents();
    const auto confirmed_point_container = point_viewer->confirmed_candidate();
    if (!verify(confirmed_point_container &&
                    confirmed_point_container->kind ==
                        zima::viewer::CandidateKind::Container &&
                    confirmed_point_container->owner_id ==
                        point_tree_item->data(0, Qt::UserRole)
                            .toString().toStdString(),
                "Tree Point selection did not confirm its container marker in View")) {
        return 1;
    }
    point_viewer->clear_selection();
    click_tree_item(point_tree_item);
    const auto repeated_point_confirmation = point_viewer->confirmed_candidate();
    if (!verify(repeated_point_confirmation &&
                    repeated_point_confirmation->kind ==
                        zima::viewer::CandidateKind::Container &&
                    repeated_point_confirmation->owner_id ==
                        point_tree_item->data(0, Qt::UserRole)
                            .toString().toStdString(),
                "repeated LMB on the current Tree Point did not confirm it in View")) {
        return 1;
    }
    const auto point_container_id =
        point_tree_item->data(0, Qt::UserRole).toString().toStdString();
    point_viewer->clear_selection();
    tree->clearSelection();
    tree->setCurrentItem(nullptr);
    std::optional<QPointF> point_offer_position;
    for (int y = 2; y < point_viewer->height() && !point_offer_position; y += 4) {
        for (int x = 2; x < point_viewer->width(); x += 4) {
            const QPointF position{static_cast<qreal>(x), static_cast<qreal>(y)};
            const auto candidates = point_viewer->selection_candidates_at(position);
            if (!candidates.empty() &&
                candidates.front().kind == zima::viewer::CandidateKind::Container &&
                candidates.front().owner_id == point_container_id) {
                point_offer_position = position;
                break;
            }
        }
    }
    if (!verify(point_offer_position.has_value(),
                "ordinary View hover did not offer the Point container")) {
        return 1;
    }
    QMouseEvent point_container_move(QEvent::MouseMove, *point_offer_position,
        point_viewer->mapToGlobal(point_offer_position->toPoint()),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(point_viewer, &point_container_move);
    const auto hovered_point_container = point_viewer->hovered_candidate();
    if (!verify(hovered_point_container &&
                    hovered_point_container->kind ==
                        zima::viewer::CandidateKind::Container &&
                    hovered_point_container->owner_id == point_container_id,
                "Point marker did not become the orange View hover candidate")) {
        return 1;
    }
    application.processEvents();
    point_viewer->repaint();
    application.processEvents();
    if (!part_capture_path.isEmpty()) {
        point_viewer->grabFramebuffer().save(
            part_capture_path + QStringLiteral(".point-hover.png"));
    }
    if (!verify(contains_orange_hover(point_viewer->grabFramebuffer()),
                "Point hover candidate was not rendered orange")) return 1;
    QMouseEvent point_container_press(QEvent::MouseButtonPress,
        *point_offer_position,
        point_viewer->mapToGlobal(point_offer_position->toPoint()),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(point_viewer, &point_container_press);
    application.processEvents();
    if (!verify(contains_cyan_selection(point_viewer->grabFramebuffer()),
                "Point LMB confirmation was not rendered cyan")) return 1;
    const auto valid_origin_branch = [](QTreeWidgetItem* container) {
        if (container == nullptr || container->childCount() < 1) return false;
        auto* origin = container->child(0);
        return origin->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("construction-origin") &&
            origin->childCount() == 7;
    };
    if (!verify(valid_origin_branch(box_tree_item) &&
                    box_tree_item->childCount() == 2 &&
                    box_tree_item->child(1)->data(0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("part-container-entity") &&
                    valid_origin_branch(point_tree_item) &&
                    point_tree_item->childCount() == 1,
                "Box and Point do not expose the Python container hierarchy")) {
        return 1;
    }
    const auto create_construction = [&](QAction* action) {
        action->trigger();
        application.processEvents();
        auto* dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
        auto* dialog_buttons = dialog == nullptr
            ? nullptr : dialog->findChild<QDialogButtonBox*>();
        if (dialog_buttons == nullptr) return false;
        dialog_buttons->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        return true;
    };
    if (!verify(create_construction(construction_axis) &&
                    create_construction(construction_plane),
                "Axis or Plane container creation failed")) return 1;
    int datum_container_count = 0;
    if (tree->topLevelItemCount() == 1) {
        auto* root = tree->topLevelItem(0);
        for (int index = 0; index < root->childCount(); ++index) {
            auto* child = root->child(index);
            if (child->data(0, Qt::UserRole + 3).toString() !=
                    QStringLiteral("part-construction")) continue;
            if (valid_origin_branch(child) && child->childCount() == 2 &&
                child->child(1)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("construction-entity")) {
                ++datum_container_count;
            }
        }
    }
    if (!verify(datum_container_count == 2,
                "Axis and Plane do not expose Origin plus their datum entity")) {
        return 1;
    }
    point_viewer->clear_selection();
    tree->clearSelection();
    tree->setCurrentItem(nullptr);
    QApplication::sendEvent(point_viewer, &point_container_move);
    application.processEvents();
    if (!part_capture_path.isEmpty() &&
        !verify(window.grab().save(part_capture_path),
                "native Qt window capture failed")) {
        return 1;
    }
    {
        QOpenGLWidget* model_viewer{};
        for (auto* child : window.findChildren<QObject*>()) {
            if (child->objectName() == QStringLiteral("modelWorkspace")) {
                model_viewer = dynamic_cast<QOpenGLWidget*>(child);
                break;
            }
        }
        if (!verify(model_viewer != nullptr, "model viewer widget is missing")) {
            return 1;
        }
        const QImage framebuffer = model_viewer->grabFramebuffer();
        if (!part_capture_path.isEmpty() &&
            !verify(framebuffer.save(
                        part_capture_path + QStringLiteral(".viewer.png")),
                    "native Qt viewer framebuffer save failed")) return 1;
        if (!verify(!framebuffer.isNull(),
                    "Wayland OpenGL viewer framebuffer is null") ||
            !verify(contains_rendered_geometry(framebuffer),
                    "Wayland OpenGL viewer framebuffer contains no body geometry") ||
            !verify(contains_orange_hover(framebuffer),
                    "Wayland OpenGL framebuffer contains no orange Point hover")) {
            return 1;
        }
    }

    sketch->trigger();
    application.processEvents();
    properties = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    buttons = properties == nullptr
        ? nullptr : properties->findChild<QDialogButtonBox*>();
    if (!verify(buttons != nullptr,
                "Sketch must use the shared in-application Properties window")) {
        return 1;
    }
    buttons->button(QDialogButtonBox::Ok)->click();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    if (!verify(tree->headerItem()->text(0).startsWith("SKETCHER") &&
                    tree->topLevelItemCount() >= 4,
                "confirming Sketch must add it to the Part tree") ||
        !verify(sketch_normal->isEnabled() && sketch_point->isEnabled() &&
                    sketch_construction->isEnabled() && sketch_segment->isEnabled() &&
                    sketch_polyline->isEnabled() && sketch_rectangle->isEnabled() &&
                    sketch_polygon->isEnabled() &&
                    sketch_trim->isEnabled() &&
                    sketch_mirror->isEnabled() &&
                    sketch_elliptical_arc->isEnabled() &&
                    sketch_midpoint->isEnabled() &&
                    sketch_symmetric->isEnabled() &&
                    sketch_concentric->isEnabled() &&
                    sketch_tangent->isEnabled() &&
                    sketch_text->isEnabled() &&
                    sketch_external_reference->isEnabled() &&
                    sketch_constraints->isEnabled() &&
                    sketch_dimensions->isEnabled() &&
                    finish_sketch->isEnabled(),
                "active Sketch is missing its basic editing command set") ||
        !verify(tools_toolbar->actions().contains(finish_sketch) &&
                    tools_toolbar->actions().contains(sketch_rectangle) &&
                    tools_toolbar->actions().contains(sketch_polygon) &&
                    tools_toolbar->actions().contains(sketch_trim) &&
                    tools_toolbar->actions().contains(sketch_mirror) &&
                    tools_toolbar->actions().contains(sketch_elliptical_arc) &&
                    tools_toolbar->actions().contains(sketch_text) &&
                    tools_toolbar->actions().contains(sketch_external_reference) &&
                    tools_toolbar->actions().contains(sketch_constraints) &&
                    tools_toolbar->actions().contains(sketch_dimensions) &&
                    sketch_constraints->menu()->actions().contains(sketch_midpoint) &&
                    sketch_constraints->menu()->actions().contains(sketch_symmetric) &&
                    sketch_constraints->menu()->actions().contains(sketch_concentric) &&
                    sketch_constraints->menu()->actions().contains(sketch_tangent),
                "Sketch commands must be exposed in the shared right toolbar")) {
        return 1;
    }
    sketch_external_reference->trigger();
    application.processEvents();
    if (!verify(sketch_external_reference->isChecked(),
                "Sketch external-reference command must enter its viewer mode")) {
        return 1;
    }
    sketch_external_reference->trigger();
    application.processEvents();
    if (!verify(!sketch_external_reference->isChecked(),
                "Sketch external-reference command must leave its viewer mode")) {
        return 1;
    }
    sketch_text->trigger();
    application.processEvents();
    auto* text_value = window.findChild<QPlainTextEdit*>("sketchTextValue");
    auto* text_dialog = text_value == nullptr
        ? nullptr : qobject_cast<QDialog*>(text_value->parentWidget());
    auto* text_buttons = text_dialog == nullptr
        ? nullptr : text_dialog->findChild<QDialogButtonBox*>();
    if (!verify(text_dialog != nullptr && text_buttons != nullptr &&
                    text_dialog->windowFlags().testFlag(Qt::SubWindow) &&
                    text_buttons->button(QDialogButtonBox::Ok) != nullptr &&
                    text_buttons->button(QDialogButtonBox::Cancel) != nullptr &&
                    text_buttons->button(QDialogButtonBox::Apply) == nullptr,
                "Sketch Text must use the shared internal OK/Cancel properties contract")) {
        return 1;
    }
    text_buttons->button(QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    auto* model_viewer = window.findChild<QOpenGLWidget*>("modelWorkspace");
    if (!verify(model_viewer != nullptr,
                "Sketch parity workflow is missing the model viewer")) {
        return 1;
    }
    const auto sketch_click = [&](double x_ratio, double y_ratio) {
        const QPointF local{
            model_viewer->width() * x_ratio,
            model_viewer->height() * y_ratio};
        QMouseEvent press(QEvent::MouseButtonPress, local,
            model_viewer->mapToGlobal(local.toPoint()), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(model_viewer, &press);
        application.processEvents();
    };
    sketch_rectangle->trigger();
    application.processEvents();
    sketch_click(0.42, 0.42);
    sketch_click(0.62, 0.62);
    finish_sketch->trigger();
    application.processEvents();
    if (!verify(!tools_toolbar->actions().contains(finish_sketch) &&
                    !sketch_segment->isEnabled() && extrusion->isEnabled(),
                "finishing Sketch must leave editing while retaining its model source")) {
        return 1;
    }
    extrusion->trigger();
    application.processEvents();
    properties = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    buttons = properties == nullptr
        ? nullptr : properties->findChild<QDialogButtonBox*>();
    if (!verify(buttons != nullptr,
                "profile Sketch must open Extrusion Properties")) {
        return 1;
    }
    buttons->button(QDialogButtonBox::Ok)->click();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    bool extrusion_in_tree = false;
    if (tree->topLevelItemCount() == 1) {
        const auto* root = tree->topLevelItem(0);
        for (int index = 0; index < root->childCount(); ++index) {
            const auto* child = root->child(index);
            if (child->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-container") &&
                child->text(0).endsWith(QStringLiteral("Vytažení"))) {
                extrusion_in_tree = true;
                break;
            }
        }
    }
    if (!verify(extrusion_in_tree,
                "Sketch rectangle must produce a committed Extrusion history item")) {
        return 1;
    }
    auto open_extrusion_properties = [&]() {
        auto* root = tree->topLevelItem(0);
        QTreeWidgetItem* item{};
        if (root != nullptr) {
            for (int index = 0; index < root->childCount(); ++index) {
                auto* candidate = root->child(index);
                if (candidate->data(0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("part-container") &&
                    candidate->text(0).endsWith(QStringLiteral("Vytažení"))) {
                    item = candidate;
                    break;
                }
            }
        }
        if (item != nullptr) window.show_tree_item_properties(item);
        application.processEvents();
        auto* dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
        return std::pair{dialog, dialog == nullptr
            ? nullptr : dialog->findChild<QDoubleSpinBox*>("extrusionHeight")};
    };
    auto [edit_dialog, edit_height] = open_extrusion_properties();
    if (!verify(edit_dialog != nullptr && edit_height != nullptr &&
                    edit_height->value() == 10.0,
                "existing Extrusion did not reopen with its persisted value")) {
        return 1;
    }
    edit_height->setValue(37.0);
    edit_dialog->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    std::tie(edit_dialog, edit_height) = open_extrusion_properties();
    if (!verify(edit_height != nullptr && edit_height->value() == 10.0,
                "Cancel changed the persisted Extrusion value")) {
        return 1;
    }
    edit_dialog->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Ok)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    std::tie(edit_dialog, edit_height) = open_extrusion_properties();
    edit_height->setValue(18.0);
    edit_dialog->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Ok)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    undo->trigger();
    application.processEvents();
    std::tie(edit_dialog, edit_height) = open_extrusion_properties();
    if (!verify(edit_height != nullptr && edit_height->value() == 10.0,
                "Undo did not restore the previous Extrusion value")) {
        return 1;
    }
    edit_dialog->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    undo->trigger();
    application.processEvents();
    int remaining_history_items = 0;
    if (tree->topLevelItemCount() == 1) {
        const auto* root = tree->topLevelItem(0);
        for (int index = 0; index < root->childCount(); ++index) {
            if (root->child(index)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-container")) ++remaining_history_items;
        }
    }
    if (!verify(remaining_history_items == 1,
                "unchanged Extrusion OK created an extra Undo revision")) {
        return 1;
    }
    redo->trigger();
    redo->trigger();
    application.processEvents();
    std::tie(edit_dialog, edit_height) = open_extrusion_properties();
    if (!verify(edit_height != nullptr && edit_height->value() == 18.0,
                "Redo did not restore the edited Extrusion value")) {
        return 1;
    }
    {
        // A middle-button double-click over the owning document view must
        // commit the open Properties dialog exactly like clicking OK,
        // whether or not the pointer is over the dialog itself.
        auto* model_workspace = window.findChild<QOpenGLWidget*>("modelWorkspace");
        if (!verify(model_workspace != nullptr,
                    "the real workspace has no modelWorkspace view")) {
            return 1;
        }
        const QPoint middle = model_workspace->rect().center();
        QMouseEvent middle_double_click(
            QEvent::MouseButtonDblClick, middle, middle,
            model_workspace->mapToGlobal(middle),
            Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(model_workspace, &middle_double_click);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        if (!verify(window.findChild<QDialog*>("zimaPropertiesSubWindow") == nullptr,
                    "middle-button double-click over the view did not commit "
                    "and close the open Extrusion Properties dialog")) {
            return 1;
        }
        std::tie(edit_dialog, edit_height) = open_extrusion_properties();
        if (!verify(edit_height != nullptr && edit_height->value() == 18.0,
                    "middle-button double-click OK changed the committed "
                    "Extrusion value")) {
            return 1;
        }
    }
    edit_dialog->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    box->trigger();
    application.processEvents();
    properties = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    buttons = properties == nullptr
        ? nullptr : properties->findChild<QDialogButtonBox*>();
    if (!verify(buttons != nullptr,
                "downstream Box did not open its shared Properties window")) {
        return 1;
    }
    buttons->button(QDialogButtonBox::Ok)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    std::tie(edit_dialog, edit_height) = open_extrusion_properties();
    auto* rollback_root = tree->topLevelItem(0);
    QTreeWidgetItem* rollback_extrusion{};
    QTreeWidgetItem* downstream_box{};
    if (rollback_root != nullptr) {
        for (int index = 0; index < rollback_root->childCount(); ++index) {
            auto* item = rollback_root->child(index);
            if (item->text(0).endsWith(QStringLiteral("Vytažení"))) {
                rollback_extrusion = item;
            } else if (rollback_extrusion != nullptr &&
                       item->data(0, Qt::UserRole + 3).toString() ==
                           QStringLiteral("part-container")) {
                downstream_box = item;
                break;
            }
        }
    }
    if (!verify(edit_dialog != nullptr && rollback_extrusion != nullptr &&
                    downstream_box != nullptr &&
                    rollback_extrusion->foreground(0).color() ==
                        QColor(70, 190, 95) &&
                    downstream_box->foreground(0).color() ==
                        QColor(125, 125, 125),
                "history edit did not keep the active item green and suppress downstream items")) {
        return 1;
    }
    edit_dialog->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    const auto saved_part_path = std::filesystem::current_path() /
        (part_name.toStdString() + ".prtz");
    save->trigger();
    application.processEvents();
    if (!verify(std::filesystem::exists(saved_part_path),
                "Save did not persist the edited Part")) {
        return 1;
    }
    close->trigger();
    application.processEvents();
    const bool reopened = tabs->count() == 0 && window.open_document_path(
        QString::fromStdString(saved_part_path.string()));
    int reopened_history_items = 0;
    if (reopened && tree->topLevelItemCount() == 1) {
        const auto* root = tree->topLevelItem(0);
        for (int index = 0; index < root->childCount(); ++index) {
            if (root->child(index)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-container")) ++reopened_history_items;
        }
    }
    if (!verify(reopened && tabs->count() == 1 && reopened_history_items == 3,
                "saved Part did not close and reopen through the application")) {
        return 1;
    }
    const bool reopened_existing = window.open_document_path(
        QString::fromStdString(saved_part_path.string()));
    if (!verify(reopened_existing && tabs->count() == 1,
                "opening an already open path created a duplicate document tab")) {
        return 1;
    }
    std::tie(edit_dialog, edit_height) = open_extrusion_properties();
    if (!verify(edit_height != nullptr && edit_height->value() == 18.0,
                "reopened Part lost the edited Extrusion value")) {
        return 1;
    }
    edit_dialog->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    std::filesystem::remove(saved_part_path);

    if (!create_document(QStringLiteral("assembly"), assembly_name)) {
        return 1;
    }
    auto* insert = window.findChild<QAction*>("insertComponentAction");
    auto* insert_menu = window.findChild<QMenu*>("insertComponentMenu");
    if (!verify(tabs->count() == 2 &&
                    tabs->tabText(tabs->currentIndex()) ==
                        assembly_name + QStringLiteral(".asmz"),
                "New Assembly must become a visible second document") ||
        !verify(insert != nullptr && insert->isEnabled() && insert_menu != nullptr,
                "calculated open Part must be insertable into the Assembly") ||
        !verify(construction_point != nullptr && construction_point->isEnabled() &&
                    construction_axis != nullptr && construction_axis->isEnabled() &&
                    construction_plane != nullptr && construction_plane->isEnabled() &&
                    tools_toolbar->actions().contains(construction_point) &&
                    tools_toolbar->actions().contains(construction_axis) &&
                    tools_toolbar->actions().contains(construction_plane),
                "Assembly toolbar is missing its Point/Axis/Plane commands")) {
        return 1;
    }
    if (!verify(sketch->isEnabled(),
                "Assembly must expose the shared Sketch command")) return 1;
    sketch->trigger();
    application.processEvents();
    auto* assembly_sketch_name = window.findChild<QLineEdit*>("sketchName");
    QDialog* assembly_sketch_dialog{};
    for (auto* candidate : window.findChildren<QDialog*>()) {
        if (candidate->findChild<QLineEdit*>("sketchName") != nullptr) {
            assembly_sketch_dialog = candidate;
            break;
        }
    }
    if (!verify(assembly_sketch_dialog != nullptr &&
                    assembly_sketch_dialog->windowFlags().testFlag(Qt::SubWindow),
                "Assembly Sketch must use the shared internal Sketch dialog")) {
        return 1;
    }
    assembly_sketch_dialog->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Ok)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    sketch_rectangle->trigger();
    application.processEvents();
    sketch_click(0.44, 0.44);
    sketch_click(0.58, 0.58);
    finish_sketch->trigger();
    application.processEvents();
    bool assembly_sketch_in_tree = false;
    if (tree->topLevelItemCount() == 1) {
        const auto* root = tree->topLevelItem(0);
        for (int index = 0; index < root->childCount(); ++index) {
            if (root->child(index)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("assembly-sketch")) {
                assembly_sketch_in_tree = true;
                break;
            }
        }
    }
    if (!verify(assembly_sketch_in_tree,
                "confirming Assembly Sketch must create an Assembly-owned sketch")) {
        return 1;
    }
    QAction* source_action{};
    for (auto* action : insert_menu->actions()) {
        if (action->objectName() == QStringLiteral("insertSourceAction") &&
            action->isEnabled()) {
            source_action = action;
            break;
        }
    }
    if (!verify(source_action != nullptr, "Assembly insertion has no Part source")) {
        return 1;
    }
    source_action->trigger();
    application.processEvents();
    bool inserted_occurrence_in_tree = false;
    if (tree->topLevelItemCount() == 1) {
        const auto* root = tree->topLevelItem(0);
        for (int index = 0; index < root->childCount(); ++index) {
            if (root->child(index)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-occurrence")) {
                inserted_occurrence_in_tree = true;
                break;
            }
        }
    }
    if (!verify(inserted_occurrence_in_tree,
                "inserting the open Part must create an Assembly occurrence")) {
        return 1;
    }
    if (!verify(extrusion->isEnabled(),
                "Assembly Sketch must enable the shared Extrusion command")) {
        return 1;
    }
    extrusion->trigger();
    application.processEvents();
    auto* assembly_cut_dialog = window.findChild<QDialog*>(
        "zimaPropertiesSubWindow");
    auto* assembly_cut_targets = assembly_cut_dialog == nullptr
        ? nullptr : assembly_cut_dialog->findChild<QListWidget*>(
            "assemblyCutTargets");
    auto* assembly_cut_buttons = assembly_cut_dialog == nullptr
        ? nullptr : assembly_cut_dialog->findChild<QDialogButtonBox*>();
    if (!verify(assembly_cut_targets != nullptr &&
                    assembly_cut_targets->count() == 1 &&
                    assembly_cut_targets->item(0)->checkState() == Qt::Checked &&
                    assembly_cut_buttons != nullptr,
                "Assembly Extrusion must use the shared dialog with one exact target")) {
        return 1;
    }
    assembly_cut_buttons->button(QDialogButtonBox::Ok)->click();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    bool cut_in_tree = false;
    for (int index = 0; index < tree->topLevelItem(0)->childCount(); ++index) {
        if (tree->topLevelItem(0)->child(index)->data(
                0, Qt::UserRole + 3).toString() == QStringLiteral("assembly-cut")) {
            cut_in_tree = true;
            break;
        }
    }
    if (!verify(cut_in_tree,
                "Assembly Extrusion OK must calculate and create one cut")) {
        return 1;
    }
    QTreeWidgetItem* assembly_cut_item{};
    for (int index = 0; index < tree->topLevelItem(0)->childCount(); ++index) {
        auto* candidate = tree->topLevelItem(0)->child(index);
        if (candidate->data(0, Qt::UserRole + 3).toString() ==
                QStringLiteral("assembly-cut")) {
            assembly_cut_item = candidate;
            break;
        }
    }
    if (!verify(assembly_cut_item != nullptr,
                "Calculated Assembly cut is missing from the tree")) return 1;
    window.show_tree_item_properties(assembly_cut_item);
    application.processEvents();
    auto* rollback_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    QTreeWidgetItem* rollback_cut_item{};
    for (int index = 0; index < tree->topLevelItem(0)->childCount(); ++index) {
        auto* candidate = tree->topLevelItem(0)->child(index);
        if (candidate->data(0, Qt::UserRole + 3).toString() ==
                QStringLiteral("assembly-cut")) {
            rollback_cut_item = candidate;
            break;
        }
    }
    if (!verify(rollback_dialog != nullptr &&
                    rollback_dialog->findChild<QListWidget*>(
                        "assemblyCutTargets") != nullptr &&
                    rollback_cut_item != nullptr &&
                    rollback_cut_item->foreground(0).color() == QColor(70, 190, 95),
                "Assembly cut Properties did not enter persisted rollback")) {
        return 1;
    }
    rollback_dialog->findChild<QDialogButtonBox*>()->button(
        QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();

    {
        // Nested Assembly occurrence activation must be reachable through the
        // real window (context-menu "Aktivovat komponentu"/"Aktivovat
        // podsestavu"), not only through the underlying Workspace unit
        // tests. Exercise the same public entry point the context menu uses.
        QTreeWidgetItem* occurrence_item{};
        for (int index = 0; index < tree->topLevelItem(0)->childCount(); ++index) {
            auto* candidate = tree->topLevelItem(0)->child(index);
            if (candidate->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-occurrence")) {
                occurrence_item = candidate;
                break;
            }
        }
        if (!verify(occurrence_item != nullptr,
                    "Assembly tree lost its inserted Part occurrence")) {
            return 1;
        }
        const std::string instance_path =
            occurrence_item->data(0, Qt::UserRole + 1).toString().toStdString();
        if (!verify(!instance_path.empty(),
                    "Assembly occurrence tree item has no instance path")) {
            return 1;
        }
        if (!verify(window.activate_occurrence_for_test(instance_path),
                    "activating an Assembly occurrence through the real window failed")) {
            return 1;
        }
        if (!verify(window.active_occurrence_path_for_test() == instance_path,
                    "activation did not record the exact activated instance path")) {
            return 1;
        }
        bool has_return_to_assembly = false;
        for (auto* action : window.findChildren<QAction*>()) {
            if (action->text() == QStringLiteral("Zpět do sestavy")) {
                has_return_to_assembly = true;
                break;
            }
        }
        if (!verify(has_return_to_assembly,
                    "activated occurrence did not expose the Zpět do sestavy toolbar action")) {
            return 1;
        }
        window.deactivate_active_occurrence_for_test();
        if (!verify(window.active_occurrence_path_for_test().empty(),
                    "returning to the Assembly did not clear the active occurrence path")) {
            return 1;
        }
    }

    {
        // Multi-level (nested-within-nested) occurrence activation must also
        // be reachable through the real window: create a second, outer
        // Assembly, insert the already-populated Assembly above as one of
        // its own components (a subassembly occurrence), then activate the
        // leaf Part occurrence two levels deep through the exact same
        // public entry point the context menu uses.
        if (!create_document(QStringLiteral("assembly"), nested_assembly_name)) {
            return 1;
        }
        if (!verify(tabs->count() == 3 &&
                        tabs->tabText(tabs->currentIndex()) ==
                            nested_assembly_name + QStringLiteral(".asmz"),
                    "New outer Assembly must become a visible third document")) {
            return 1;
        }
        QAction* subassembly_source_action{};
        for (auto* action : insert_menu->actions()) {
            if (action->objectName() == QStringLiteral("insertSourceAction") &&
                action->isEnabled() &&
                action->text().startsWith(assembly_name)) {
                subassembly_source_action = action;
                break;
            }
        }
        if (!verify(subassembly_source_action != nullptr,
                    "outer Assembly insertion has no inner Assembly source")) {
            return 1;
        }
        subassembly_source_action->trigger();
        application.processEvents();
        QTreeWidgetItem* subassembly_item{};
        if (tree->topLevelItemCount() == 1) {
            const auto* root = tree->topLevelItem(0);
            for (int index = 0; index < root->childCount(); ++index) {
                auto* candidate = root->child(index);
                if (candidate->data(0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("assembly-occurrence")) {
                    subassembly_item = candidate;
                    break;
                }
            }
        }
        if (!verify(subassembly_item != nullptr,
                    "inserting the inner Assembly must create a subassembly occurrence")) {
            return 1;
        }
        subassembly_item->setExpanded(true);
        application.processEvents();
        QTreeWidgetItem* nested_leaf_item{};
        for (int index = 0; index < subassembly_item->childCount(); ++index) {
            auto* candidate = subassembly_item->child(index);
            if (candidate->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-occurrence")) {
                nested_leaf_item = candidate;
                break;
            }
        }
        if (!verify(nested_leaf_item != nullptr,
                    "outer Assembly did not expose the inner Assembly's leaf Part occurrence")) {
            return 1;
        }
        const std::string nested_instance_path =
            nested_leaf_item->data(0, Qt::UserRole + 1).toString().toStdString();
        if (!verify(!nested_instance_path.empty() &&
                        nested_instance_path.find(':') != std::string::npos,
                    "multi-level occurrence tree item has no composed instance path")) {
            return 1;
        }
        if (!verify(window.activate_occurrence_for_test(nested_instance_path),
                    "activating a two-level-deep Assembly occurrence through the real window failed")) {
            return 1;
        }
        if (!verify(window.active_occurrence_path_for_test() == nested_instance_path,
                    "multi-level activation did not record the exact composed instance path")) {
            return 1;
        }
        bool has_nested_return_to_assembly = false;
        for (auto* action : window.findChildren<QAction*>()) {
            if (action->text() == QStringLiteral("Zpět do sestavy")) {
                has_nested_return_to_assembly = true;
                break;
            }
        }
        if (!verify(has_nested_return_to_assembly,
                    "activated two-level-deep occurrence did not expose the Zpět do sestavy toolbar action")) {
            return 1;
        }
        window.deactivate_active_occurrence_for_test();
        if (!verify(window.active_occurrence_path_for_test().empty(),
                    "returning from a two-level-deep occurrence did not clear the active occurrence path")) {
            return 1;
        }

        // Save/close/reopen must preserve the outer Assembly's nested
        // subassembly structure through the real window, matching the
        // existing Part save/reopen coverage above.
        const auto saved_nested_assembly_path = std::filesystem::current_path() /
            (nested_assembly_name.toStdString() + ".asmz");
        save->trigger();
        application.processEvents();
        if (!verify(std::filesystem::exists(saved_nested_assembly_path),
                    "Save did not persist the outer Assembly")) {
            return 1;
        }
        close->trigger();
        application.processEvents();
        const bool reopened_nested_assembly = window.open_document_path(
            QString::fromStdString(saved_nested_assembly_path.string()));
        bool reopened_subassembly_in_tree = false;
        bool reopened_nested_leaf_in_tree = false;
        if (reopened_nested_assembly && tree->topLevelItemCount() == 1) {
            const auto* root = tree->topLevelItem(0);
            for (int index = 0; index < root->childCount(); ++index) {
                auto* candidate = root->child(index);
                if (candidate->data(0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("assembly-occurrence")) {
                    reopened_subassembly_in_tree = true;
                    candidate->setExpanded(true);
                    application.processEvents();
                    for (int nested_index = 0;
                         nested_index < candidate->childCount(); ++nested_index) {
                        if (candidate->child(nested_index)->data(
                                0, Qt::UserRole + 3).toString() ==
                                QStringLiteral("part-occurrence")) {
                            reopened_nested_leaf_in_tree = true;
                            break;
                        }
                    }
                    break;
                }
            }
        }
        if (!verify(reopened_nested_assembly && reopened_subassembly_in_tree &&
                        reopened_nested_leaf_in_tree,
                    "saved outer Assembly did not close and reopen its nested subassembly structure")) {
            return 1;
        }
    }

    {
        // Deep Assembly mechanisms: mate creation, DOF display and mate
        // dimension drag must all be reachable through the real window and
        // the real viewer selection contract, not only through the module
        // unit tests. Switch back to the inner Assembly document (already
        // open in its own tab), which already contains one Part occurrence
        // with a calculated Box, insert a second occurrence of the same
        // Part (a repeated-occurrence fixture), mate their opposing planar
        // faces, then drag the mate's dimension handle to change its offset
        // within its limits.
        int inner_assembly_tab_index = -1;
        for (int index = 0; index < tabs->count(); ++index) {
            if (tabs->tabText(index).startsWith(
                    assembly_name + QStringLiteral(".asmz"))) {
                inner_assembly_tab_index = index;
                break;
            }
        }
        if (!verify(inner_assembly_tab_index >= 0,
                    "inner Assembly tab is no longer open for mate coverage")) {
            return 1;
        }
        tabs->setCurrentIndex(inner_assembly_tab_index);
        application.processEvents();
        auto* local_mate_tree = window.findChild<QTreeWidget*>("documentTree");
        if (!verify(local_mate_tree != nullptr && local_mate_tree->topLevelItemCount() == 1,
                    "reopened inner Assembly is missing its tree root")) {
            return 1;
        }
        int part_occurrence_count_before = 0;
        std::string first_occurrence_instance_path;
        for (int index = 0; index < local_mate_tree->topLevelItem(0)->childCount(); ++index) {
            auto* candidate_child = local_mate_tree->topLevelItem(0)->child(index);
            if (candidate_child->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-occurrence")) {
                ++part_occurrence_count_before;
                first_occurrence_instance_path =
                    candidate_child->data(0, Qt::UserRole + 1).toString().toStdString();
            }
        }
        if (!verify(part_occurrence_count_before == 1 && !first_occurrence_instance_path.empty(),
                    "reopened inner Assembly did not have exactly one Part occurrence")) {
            return 1;
        }
        auto* mate_insert_menu = window.findChild<QMenu*>("insertComponentMenu");
        auto* mate_insert = window.findChild<QAction*>("insertComponentAction");
        if (!verify(mate_insert != nullptr && mate_insert->isEnabled() &&
                        mate_insert_menu != nullptr,
                    "reopened inner Assembly cannot insert a second Part occurrence")) {
            return 1;
        }
        QAction* second_source_action{};
        for (auto* action : mate_insert_menu->actions()) {
            if (action->objectName() == QStringLiteral("insertSourceAction") &&
                action->isEnabled()) {
                second_source_action = action;
                break;
            }
        }
        if (!verify(second_source_action != nullptr,
                    "reopened inner Assembly has no Part source for a second occurrence")) {
            return 1;
        }
        second_source_action->trigger();
        application.processEvents();
        int part_occurrence_count_after = 0;
        std::string second_occurrence_instance_path;
        if (local_mate_tree->topLevelItemCount() == 1) {
            const auto* root = local_mate_tree->topLevelItem(0);
            for (int index = 0; index < root->childCount(); ++index) {
                auto* child = root->child(index);
                if (child->data(0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("part-occurrence")) {
                    ++part_occurrence_count_after;
                    second_occurrence_instance_path =
                        child->data(0, Qt::UserRole + 1).toString().toStdString();
                }
            }
        }
        if (!verify(part_occurrence_count_after == 2 &&
                        !second_occurrence_instance_path.empty(),
                    "inserting the same Part again did not create a second repeated occurrence")) {
            return 1;
        }
        // Both copies currently share the exact same placement (fully
        // coincident), which makes their opposing faces indistinguishable
        // to the pixel-scan picker (only the frontmost occurrence's faces
        // are ever hit). Move the second occurrence 100 mm along X through
        // its real Properties dialog so its own z_min/z_max faces become
        // separately pickable, matching the intended repeated-occurrence
        // mate scenario.
        QTreeWidgetItem* second_occurrence_item{};
        for (int index = 0; index < local_mate_tree->topLevelItem(0)->childCount();
             ++index) {
            auto* child = local_mate_tree->topLevelItem(0)->child(index);
            if (child->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-occurrence") &&
                child->data(0, Qt::UserRole + 1).toString().toStdString() ==
                    second_occurrence_instance_path) {
                second_occurrence_item = child;
                break;
            }
        }
        if (!verify(second_occurrence_item != nullptr,
                    "could not locate the second occurrence's tree item to reposition it")) {
            return 1;
        }
        window.show_tree_item_properties(second_occurrence_item);
        application.processEvents();
        auto* reposition_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
        auto* reposition_translation_x = reposition_dialog == nullptr
            ? nullptr : reposition_dialog->findChild<QDoubleSpinBox*>("componentTranslation");
        if (!verify(reposition_dialog != nullptr && reposition_translation_x != nullptr,
                    "second occurrence's Properties dialog is missing its translation field")) {
            return 1;
        }
        reposition_translation_x->setValue(100.0);
        reposition_dialog->findChild<QDialogButtonBox*>()
            ->button(QDialogButtonBox::Ok)->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        auto* mate_viewer = dynamic_cast<zima::viewer::MeshView*>(
            window.findChild<QOpenGLWidget*>("modelWorkspace"));
        auto* plane_mate_action = window.findChild<QAction*>("planeMateAction");
        if (!verify(mate_viewer != nullptr && plane_mate_action != nullptr &&
                        plane_mate_action->isEnabled(),
                    "reopened inner Assembly is missing the viewer or plane-mate action")) {
            return 1;
        }
        // The second occurrence was moved 100 mm along X above, so its
        // z_min/z_max faces are now distinct real Face candidates from the
        // first occurrence's, matching a real plane-plane mate pick.
        auto* mate_filter_combo =
            window.findChild<QComboBox*>("selectionFilterCombo");
        if (!verify(mate_filter_combo != nullptr,
                    "mate coverage requires the shared selectionFilterCombo")) {
            return 1;
        }
        mate_filter_combo->setCurrentIndex(1);  // Plochy (Faces)
        application.processEvents();
        const auto find_face_candidate = [&](
            const std::string& semantic_key,
            const std::string& required_instance_path) -> std::optional<QPointF> {
            for (int y = 4; y < mate_viewer->height(); y += 4) {
                for (int x = 4; x < mate_viewer->width(); x += 4) {
                    const QPointF position{
                        static_cast<qreal>(x), static_cast<qreal>(y)};
                    for (const auto& candidate :
                             mate_viewer->selection_candidates_at(position)) {
                        if (candidate.kind == zima::viewer::CandidateKind::Face &&
                            candidate.geometry ==
                                zima::viewer::CandidateGeometry::OriginalReference &&
                            candidate.semantic_key == semantic_key &&
                            candidate.instance_path == required_instance_path) {
                            return position;
                        }
                    }
                }
            }
            return std::nullopt;
        };
        const auto click_at = [&](const QPointF& position) {
            QMouseEvent press(QEvent::MouseButtonPress, position,
                mate_viewer->mapToGlobal(position.toPoint()), Qt::LeftButton,
                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(mate_viewer, &press);
            application.processEvents();
        };
        plane_mate_action->trigger();
        application.processEvents();
        const auto dependent_face_position =
            find_face_candidate("z_min", second_occurrence_instance_path);
        if (!verify(dependent_face_position.has_value(),
                    "plane-mate selection did not offer a z_min Face candidate on the moved occurrence")) {
            return 1;
        }
        click_at(*dependent_face_position);
        const auto prerequisite_face_position =
            find_face_candidate("z_max", first_occurrence_instance_path);
        if (!verify(prerequisite_face_position.has_value(),
                    "plane-mate selection did not offer a z_max Face candidate on the original occurrence")) {
            return 1;
        }
        click_at(*prerequisite_face_position);
        auto* mate_properties_dialog =
            window.findChild<QDialog*>("zimaPropertiesSubWindow");
        auto* mate_offset = mate_properties_dialog == nullptr
            ? nullptr : mate_properties_dialog->findChild<QDoubleSpinBox*>("mateOffset");
        auto* mate_lower_enabled = mate_properties_dialog == nullptr
            ? nullptr : mate_properties_dialog->findChild<QCheckBox*>("mateLowerEnabled");
        auto* mate_lower_limit = mate_properties_dialog == nullptr
            ? nullptr : mate_properties_dialog->findChild<QDoubleSpinBox*>("mateLowerLimit");
        auto* mate_upper_enabled = mate_properties_dialog == nullptr
            ? nullptr : mate_properties_dialog->findChild<QCheckBox*>("mateUpperEnabled");
        auto* mate_upper_limit = mate_properties_dialog == nullptr
            ? nullptr : mate_properties_dialog->findChild<QDoubleSpinBox*>("mateUpperLimit");
        if (!verify(mate_properties_dialog != nullptr &&
                        mate_properties_dialog->windowFlags().testFlag(Qt::SubWindow) &&
                        mate_offset != nullptr && mate_offset->isEnabled() &&
                        mate_lower_enabled != nullptr && mate_lower_limit != nullptr &&
                        mate_upper_enabled != nullptr && mate_upper_limit != nullptr,
                    "real plane-plane Face picks did not open the shared Mate Properties dialog")) {
            return 1;
        }
        mate_offset->setValue(5.0);
        mate_lower_enabled->setChecked(true);
        mate_lower_limit->setValue(0.0);
        mate_upper_enabled->setChecked(true);
        mate_upper_limit->setValue(20.0);
        mate_properties_dialog->findChild<QDialogButtonBox*>()
            ->button(QDialogButtonBox::Ok)->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        bool mate_in_tree = false;
        QTreeWidgetItem* local_mate_tree_item{};
        if (local_mate_tree->topLevelItemCount() == 1) {
            const auto* root = local_mate_tree->topLevelItem(0);
            for (int index = 0; index < root->childCount() && !mate_in_tree; ++index) {
                auto* child = root->child(index);
                if (child->text(0) == QStringLiteral("Vazby")) {
                    for (int mate_index = 0; mate_index < child->childCount();
                         ++mate_index) {
                        if (child->child(mate_index)->data(0, Qt::UserRole + 3)
                                .toString() == QStringLiteral("assembly-mate")) {
                            mate_in_tree = true;
                            local_mate_tree_item = child->child(mate_index);
                            break;
                        }
                    }
                }
            }
        }
        if (!verify(mate_in_tree && local_mate_tree_item != nullptr,
                    "confirming the plane-mate Properties dialog did not create a mate")) {
            return 1;
        }
        bool second_occurrence_shows_dof = false;
        for (int index = 0; index < local_mate_tree->topLevelItem(0)->childCount(); ++index) {
            auto* child = local_mate_tree->topLevelItem(0)->child(index);
            if (child->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-occurrence") &&
                child->data(0, Qt::UserRole + 1).toString().toStdString() ==
                    second_occurrence_instance_path &&
                child->text(0).contains(QStringLiteral("[3 DOF]"))) {
                second_occurrence_shows_dof = true;
                break;
            }
        }
        if (!verify(second_occurrence_shows_dof,
                    "mated occurrence did not display its remaining 3 DOF in the tree")) {
            return 1;
        }
        std::optional<QPointF> mate_handle_position;
        mate_filter_combo->setCurrentIndex(0);
        application.processEvents();
        for (int y = 4; y < mate_viewer->height() && !mate_handle_position; y += 2) {
            for (int x = 4; x < mate_viewer->width() && !mate_handle_position; x += 2) {
                const QPointF position{static_cast<qreal>(x), static_cast<qreal>(y)};
                for (const auto& candidate :
                         mate_viewer->selection_candidates_at(position)) {
                    if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
                        candidate.semantic_key.starts_with("mate:")) {
                        mate_handle_position = position;
                        break;
                    }
                }
            }
        }
        if (!verify(mate_handle_position.has_value(),
                    "plane mate did not offer a draggable Dimension handle in the viewer")) {
            return 1;
        }
        click_at(*mate_handle_position);
        const QPointF drag_target{
            mate_handle_position->x() + 12.0, mate_handle_position->y() + 12.0};
        QMouseEvent drag_press(QEvent::MouseButtonPress, *mate_handle_position,
            mate_viewer->mapToGlobal(mate_handle_position->toPoint()),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(mate_viewer, &drag_press);
        application.processEvents();
        QMouseEvent drag_move(QEvent::MouseMove, drag_target,
            mate_viewer->mapToGlobal(drag_target.toPoint()), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(mate_viewer, &drag_move);
        application.processEvents();
        QMouseEvent drag_release(QEvent::MouseButtonRelease, drag_target,
            mate_viewer->mapToGlobal(drag_target.toPoint()), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(mate_viewer, &drag_release);
        application.processEvents();
        QTreeWidgetItem* refreshed_mate_item{};
        if (local_mate_tree->topLevelItemCount() == 1) {
            const auto* root = local_mate_tree->topLevelItem(0);
            for (int index = 0; index < root->childCount() && !refreshed_mate_item;
                 ++index) {
                auto* child = root->child(index);
                if (child->text(0) == QStringLiteral("Vazby")) {
                    for (int mate_index = 0; mate_index < child->childCount();
                         ++mate_index) {
                        if (child->child(mate_index)->data(0, Qt::UserRole + 3)
                                .toString() == QStringLiteral("assembly-mate")) {
                            refreshed_mate_item = child->child(mate_index);
                            break;
                        }
                    }
                }
            }
        }
        if (!verify(refreshed_mate_item != nullptr,
                    "mate tree item no longer present in the tree after the drag")) {
            return 1;
        }
        window.show_tree_item_properties(refreshed_mate_item);
        application.processEvents();
        auto* dragged_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
        auto* dragged_offset = dragged_dialog == nullptr
            ? nullptr : dragged_dialog->findChild<QDoubleSpinBox*>("mateOffset");
        if (!verify(dragged_offset != nullptr &&
                        dragged_offset->value() >= 0.0 &&
                        dragged_offset->value() <= 20.0,
                    "dragging the mate dimension handle did not keep the offset "
                    "within its persisted limits")) {
            return 1;
        }
        if (auto* dragged_buttons =
                dragged_dialog->findChild<QDialogButtonBox*>()) {
            dragged_buttons->button(QDialogButtonBox::Cancel)->click();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
    }

    if (!create_document(QStringLiteral("drawing"), drawing_name)) {
        return 1;
    }
    auto* stack = window.findChild<QStackedWidget*>("workspaceStack");
    auto* drawing_canvas = window.findChild<QWidget*>("drawingCanvas");
    auto* drawing_toolbar = window.findChild<QToolBar*>("drawingToolbar");
    auto* insert_view = window.findChild<QAction*>("insertDrawingViewAction");
    if (!verify(tabs->count() == 4 && stack != nullptr &&
                    stack->currentWidget()->objectName() == QStringLiteral("drawingWorkspace"),
                "New Drawing must open inside the common workspace") ||
        !verify(drawing_toolbar != nullptr && drawing_toolbar->isHidden() &&
                    tools_toolbar->isVisible() && insert_view != nullptr &&
                    insert_view->isEnabled(),
                "Drawing commands must move into the shared right toolbar")) {
        return 1;
    }
    application.processEvents();
    const QImage drawing_snapshot = drawing_canvas == nullptr
        ? QImage{} : drawing_canvas->grab().toImage();
    if (!verify(!drawing_snapshot.isNull(), "Drawing canvas cannot be rendered") ||
        !verify(drawing_canvas->palette().color(QPalette::Window) == QColor("#000000"),
                "Drawing workspace palette must be black") ||
        !verify(drawing_snapshot.pixelColor(drawing_snapshot.width() / 2,
                                            drawing_snapshot.height() / 2) ==
                    QColor("#000000"),
                "Drawing paper must retain the black workspace background")) {
        return 1;
    }
    if (!drawing_capture_path.isEmpty() &&
        !verify(window.grab().save(drawing_capture_path),
                "native Qt Drawing capture failed")) {
        return 1;
    }

    // Save/close/reopen must preserve the Drawing's sheet structure through
    // the real window, matching the existing Part/Assembly save/reopen
    // coverage above.
    const int sheet_count_before_reopen = tree->topLevelItemCount() == 1
        ? tree->topLevelItem(0)->childCount() : -1;
    const auto saved_drawing_path = std::filesystem::current_path() /
        (drawing_name.toStdString() + ".drwz");
    save->trigger();
    application.processEvents();
    if (!verify(std::filesystem::exists(saved_drawing_path),
                "Save did not persist the Drawing")) {
        return 1;
    }
    close->trigger();
    application.processEvents();
    const bool reopened_drawing = tabs->count() == 3 && window.open_document_path(
        QString::fromStdString(saved_drawing_path.string()));
    if (!verify(reopened_drawing && tabs->count() == 4 &&
                    tree->topLevelItemCount() == 1 &&
                    tree->topLevelItem(0)->childCount() ==
                        sheet_count_before_reopen &&
                    sheet_count_before_reopen > 0,
                "saved Drawing did not close and reopen its sheet structure")) {
        return 1;
    }

    {
        // Advanced Drawing parity: a Drawing view inserted from an
        // Assembly source must seed the sheet's BOM from that Assembly's
        // components, and later regenerating the view (an explicit user
        // action) must re-derive the BOM from the Assembly's *current*
        // component list rather than leaving it frozen at insertion time.
        auto* drawing_window = static_cast<zima::app::DrawingWindow*>(
            window.findChild<QWidget*>("drawingWorkspace"));
        if (!verify(drawing_window != nullptr,
                    "Drawing document has no embedded DrawingWindow for BOM coverage")) {
            return 1;
        }
        insert_view->trigger();
        application.processEvents();
        auto* source_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
        auto* source_combo = source_dialog == nullptr
            ? nullptr : source_dialog->findChild<QComboBox*>();
        if (!verify(source_dialog != nullptr && source_combo != nullptr,
                    "inserting a Drawing view did not open the source-selection dialog")) {
            return 1;
        }
        int assembly_source_index = -1;
        for (int index = 0; index < source_combo->count(); ++index) {
            if (source_combo->itemText(index).startsWith(assembly_name)) {
                assembly_source_index = index;
                break;
            }
        }
        if (!verify(assembly_source_index >= 0,
                    "Drawing view source dialog did not offer the inner Assembly")) {
            return 1;
        }
        source_combo->setCurrentIndex(assembly_source_index);
        source_dialog->findChild<QDialogButtonBox*>()
            ->button(QDialogButtonBox::Ok)->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        auto* view_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
        if (!verify(view_dialog != nullptr,
                    "selecting the Assembly source did not open the view Properties dialog")) {
            return 1;
        }
        view_dialog->findChild<QDialogButtonBox*>()
            ->button(QDialogButtonBox::Ok)->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        const int bom_quantity_after_insertion =
            drawing_window->document_for_test().sheets.empty() ||
                    drawing_window->document_for_test().sheets.front().bom_rows.empty()
                ? 0
                : drawing_window->document_for_test()
                      .sheets.front().bom_rows.front().quantity;
        if (!verify(bom_quantity_after_insertion == 2,
                    "inserting an Assembly view did not seed the BOM from its two Part occurrences")) {
            return 1;
        }
        // Insert a third occurrence into the Assembly directly through the
        // workspace model (not the GUI, since the Assembly tab is not the
                // Insert a third occurrence into the Assembly through the real GUI
        // insertion flow (switch to its tab, trigger Insert, switch back
        // to the Drawing tab), so the source Assembly's component list
        // genuinely changes between view insertion and regeneration.
        const std::string bom_assembly_document_id =
            drawing_window->document_for_test().sheets.front()
                .views.front().source_document_id;
        int bom_assembly_tab_index = -1;
        for (int index = 0; index < tabs->count(); ++index) {
            if (tabs->tabText(index).startsWith(
                    assembly_name + QStringLiteral(".asmz"))) {
                bom_assembly_tab_index = index;
                break;
            }
        }
        const int drawing_tab_index = tabs->currentIndex();
        if (!verify(bom_assembly_tab_index >= 0,
                    "inner Assembly tab is no longer open for BOM regeneration coverage")) {
            return 1;
        }
        tabs->setCurrentIndex(bom_assembly_tab_index);
        application.processEvents();
        auto* bom_insert_menu = window.findChild<QMenu*>("insertComponentMenu");
        QAction* bom_source_action{};
        if (bom_insert_menu != nullptr) {
            for (auto* action : bom_insert_menu->actions()) {
                if (action->objectName() == QStringLiteral("insertSourceAction") &&
                    action->isEnabled()) {
                    bom_source_action = action;
                    break;
                }
            }
        }
        if (!verify(bom_source_action != nullptr,
                    "inner Assembly has no Part source for a third occurrence")) {
            return 1;
        }
        bom_source_action->trigger();
        application.processEvents();
        tabs->setCurrentIndex(drawing_tab_index);
        application.processEvents();
        auto* regenerate_view = window.findChild<QAction*>("regenerateDrawingViewAction");
        if (!verify(regenerate_view != nullptr,
                    "Drawing regeneration coverage requires its action")) {
            return 1;
        }
        // Select the sole inserted view directly (production code exposes
        // this only through the canvas' internal hit-testing, which the
        // GUI test drives via a dedicated test-only accessor instead of
        // guessing pixel coordinates).
        const auto& inserted_view_id = drawing_window->document_for_test()
            .sheets.front().views.front().id;
        drawing_window->select_view_for_test(inserted_view_id);
        application.processEvents();
        if (!verify(regenerate_view->isEnabled(),
                    "selecting the inserted Drawing view did not enable regeneration")) {
            return 1;
        }
        regenerate_view->trigger();
        application.processEvents();
        const int bom_quantity_after_regeneration =
            drawing_window->document_for_test().sheets.empty() ||
                    drawing_window->document_for_test().sheets.front().bom_rows.empty()
                ? 0
                : drawing_window->document_for_test()
                      .sheets.front().bom_rows.front().quantity;
        if (!verify(bom_quantity_after_regeneration == 3,
                    "regenerating the Drawing view did not re-derive the BOM from the "
                    "Assembly's updated component list")) {
            return 1;
        }
    }

    {
        // Frame/title-block template load and remove actions (Python's
        // remove_format/remove_title_block, previously missing in C++).
        auto* drawing_window_for_template = static_cast<zima::app::DrawingWindow*>(
            window.findChild<QWidget*>("drawingWorkspace"));
        auto* remove_frame_action = window.findChild<QAction*>("removeDrawingFrameAction");
        auto* remove_title_block_action =
            window.findChild<QAction*>("removeDrawingTitleBlockAction");
        if (!verify(drawing_window_for_template != nullptr &&
                        remove_frame_action != nullptr &&
                        remove_title_block_action != nullptr,
                    "Drawing template removal coverage requires the window and actions")) {
            return 1;
        }
        drawing_window_for_template->load_frame_for_test("config/formats/ZE-A4.frmz");
        drawing_window_for_template->load_title_block_for_test(
            "config/formats/ZE-TITLE-BLOCK.tblz");
        application.processEvents();
        if (!verify(!drawing_window_for_template->document_for_test()
                            .sheets.front().frame_lines.empty() &&
                        !drawing_window_for_template->document_for_test()
                             .sheets.front().title_block_fields.empty(),
                    "loading a frame/title-block template did not populate the sheet")) {
            return 1;
        }
        remove_frame_action->trigger();
        application.processEvents();
        remove_title_block_action->trigger();
        application.processEvents();
        if (!verify(drawing_window_for_template->document_for_test()
                            .sheets.front().frame_lines.empty() &&
                        drawing_window_for_template->document_for_test()
                            .sheets.front().title_block_fields.empty(),
                    "removing the frame/title-block did not clear the sheet's template geometry")) {
            return 1;
        }
    }

    // File-management parity: rename, delete-current-file, delete old
    // versions (with and without keep-latest), and the working-directory
    // wide equivalents must all be wired to real handlers operating on the
    // reopened Drawing document, mirroring the Python reference workflow.
    {
        if (!verify(rename_document != nullptr && rename_document->isEnabled() &&
                        delete_current_file != nullptr && delete_current_file->isEnabled() &&
                        delete_all_versions != nullptr && delete_all_versions->isEnabled() &&
                        delete_working_directory_old_versions != nullptr &&
                        delete_working_directory_old_versions->isEnabled() &&
                        delete_working_directory_keep_latest != nullptr &&
                        delete_working_directory_keep_latest->isEnabled(),
                    "file-management actions must enable once a saved Drawing is active")) {
            return 1;
        }
        if (!verify(!delete_old_versions->isEnabled() &&
                        !delete_old_versions_keep_latest->isEnabled(),
                    "delete-old-versions actions must stay disabled without archive files")) {
            return 1;
        }

        // Create two versioned archive files ("<name>.1", "<name>.2") next
        // to the saved Drawing, as ZIMA-CAD's save/version rotation would.
        const auto archive_one = std::filesystem::path(
            saved_drawing_path.string() + ".1");
        const auto archive_two = std::filesystem::path(
            saved_drawing_path.string() + ".2");
        std::filesystem::copy_file(saved_drawing_path, archive_one,
            std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(saved_drawing_path, archive_two,
            std::filesystem::copy_options::overwrite_existing);
        // Re-trigger any refresh path that recomputes action enablement
        // (closing and reopening is the simplest reliable trigger already
        // exercised above).
        close->trigger();
        application.processEvents();
        const bool reopened_for_versions = window.open_document_path(
            QString::fromStdString(saved_drawing_path.string()));
        application.processEvents();
        if (!verify(reopened_for_versions && delete_old_versions->isEnabled() &&
                        delete_old_versions_keep_latest->isEnabled(),
                    "delete-old-versions actions must enable once archive files exist")) {
            return 1;
        }

        // "Staré verze kromě nejnovější" must remove only the older archive
        // and keep the newest one (archive_two).
        QTimer::singleShot(0, &window, [] {
            if (auto* box = qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget())) {
                if (auto* yes_button = box->button(QMessageBox::Yes)) {
                    yes_button->click();
                } else {
                    box->accept();
                }
            }
        });
        delete_old_versions_keep_latest->trigger();
        application.processEvents();
        if (!verify(!std::filesystem::exists(archive_one) &&
                        std::filesystem::exists(archive_two),
                    "delete-old-versions-keep-latest must remove only older archives")) {
            return 1;
        }

        // "Staré verze" must remove every remaining archive but keep the
        // primary saved Drawing file itself.
        QTimer::singleShot(0, &window, [] {
            if (auto* box = qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget())) {
                if (auto* yes_button = box->button(QMessageBox::Yes)) {
                    yes_button->click();
                } else {
                    box->accept();
                }
            }
        });
        delete_old_versions->trigger();
        application.processEvents();
        if (!verify(!std::filesystem::exists(archive_two) &&
                        std::filesystem::exists(saved_drawing_path),
                    "delete-old-versions must remove all archives but keep the current file")) {
            return 1;
        }

        // Rename must move the file on disk and keep the document open
        // under its new name/path.
        const auto renamed_path = saved_drawing_path.parent_path() /
            (drawing_name.toStdString() + "-renamed.drwz");
        rename_document->trigger();
        application.processEvents();
        auto* rename_dialog = window.findChild<QDialog*>("renameDocumentDialog");
        if (!verify(rename_dialog != nullptr,
                    "rename must open the shared in-application dialog")) {
            return 1;
        }
        auto* rename_field = rename_dialog->findChild<QLineEdit*>("renameDocumentName");
        rename_field->setText(QString::fromStdString(renamed_path.filename().string()));
        auto* rename_buttons = rename_dialog->findChild<QDialogButtonBox*>();
        rename_buttons->button(QDialogButtonBox::Ok)->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        if (!verify(!std::filesystem::exists(saved_drawing_path) &&
                        std::filesystem::exists(renamed_path),
                    "rename must move the document file on disk")) {
            return 1;
        }

        // The rename must also rewrite references in documents saved on
        // disk but not currently open, matching Python's
        // _rename_document_file_to (which scans the file's directory and
        // the working directory for other documents referencing the
        // renamed path). Build a throwaway closed Assembly fixture whose
        // component references the Drawing's current (already-renamed-once)
        // path (an artificial but sufficient stand-in, since AssemblyDocument
        // components reference any source_path uniformly) and confirm the
        // rename rewrote it even though it was never opened in the workspace.
        const auto closed_reference_assembly_path = saved_drawing_path.parent_path() /
            (QStringLiteral("REFERENCE-STARTUP-") + identity).toStdString().append(".asmz");
        {
            auto reference_document = zima::assembly::AssemblyDocument::create_default();
            reference_document.components.push_back(
                zima::assembly::AssemblyDocument::create_part_occurrence(
                    "closed-reference", "unused-source-id", renamed_path, {}));
            reference_document.save(closed_reference_assembly_path);
        }
        const auto renamed_again_path = renamed_path.parent_path() /
            (drawing_name.toStdString() + "-renamed-again.drwz");
        rename_document->trigger();
        application.processEvents();
        auto* second_rename_dialog = window.findChild<QDialog*>("renameDocumentDialog");
        if (!verify(second_rename_dialog != nullptr,
                    "rename must open the shared in-application dialog a second time")) {
            return 1;
        }
        second_rename_dialog->findChild<QLineEdit*>("renameDocumentName")
            ->setText(QString::fromStdString(renamed_again_path.filename().string()));
        second_rename_dialog->findChild<QDialogButtonBox*>()
            ->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        bool closed_reference_rewritten = false;
        try {
            const auto reloaded_reference_document =
                zima::assembly::AssemblyDocument::load(closed_reference_assembly_path);
            for (const auto& component : reloaded_reference_document.components) {
                if (std::filesystem::absolute(component.source_path).lexically_normal() ==
                        std::filesystem::absolute(renamed_again_path).lexically_normal()) {
                    closed_reference_rewritten = true;
                    break;
                }
            }
        } catch (const std::exception&) {
        }
        if (!verify(std::filesystem::exists(renamed_again_path) &&
                        closed_reference_rewritten,
                    "rename must rewrite references in Assembly documents saved on disk "
                    "but not currently open")) {
            return 1;
        }
        std::filesystem::remove(closed_reference_assembly_path);

        // Working-directory wide deletion: create archives for both the
        // renamed Drawing and an unrelated saved Part, then verify
        // "keep latest" removes only the older ones across all documents.
        const auto renamed_archive_one = std::filesystem::path(
            renamed_again_path.string() + ".1");
        const auto renamed_archive_two = std::filesystem::path(
            renamed_again_path.string() + ".2");
        std::filesystem::copy_file(renamed_again_path, renamed_archive_one,
            std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(renamed_again_path, renamed_archive_two,
            std::filesystem::copy_options::overwrite_existing);
        const auto part_saved_path = std::filesystem::current_path() /
            (part_name.toStdString() + ".prtz");
        std::filesystem::path part_archive_one;
        std::filesystem::path part_archive_two;
        const bool part_saved_exists = std::filesystem::exists(part_saved_path);
        if (part_saved_exists) {
            part_archive_one = std::filesystem::path(part_saved_path.string() + ".1");
            part_archive_two = std::filesystem::path(part_saved_path.string() + ".2");
            std::filesystem::copy_file(part_saved_path, part_archive_one,
                std::filesystem::copy_options::overwrite_existing);
            std::filesystem::copy_file(part_saved_path, part_archive_two,
                std::filesystem::copy_options::overwrite_existing);
        }
        QTimer::singleShot(0, &window, [] {
            if (auto* box = qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget())) {
                if (auto* yes_button = box->button(QMessageBox::Yes)) {
                    yes_button->click();
                } else {
                    box->accept();
                }
            }
        });
        delete_working_directory_keep_latest->trigger();
        application.processEvents();
        const bool working_directory_keep_latest_ok =
            !std::filesystem::exists(renamed_archive_one) &&
            std::filesystem::exists(renamed_archive_two) &&
            (!part_saved_exists ||
             (!std::filesystem::exists(part_archive_one) &&
              std::filesystem::exists(part_archive_two)));
        if (!verify(working_directory_keep_latest_ok,
                    "working-directory keep-latest delete must remove only older "
                    "archives across every document")) {
            return 1;
        }
        if (part_saved_exists && std::filesystem::exists(part_archive_two)) {
            std::filesystem::remove(part_archive_two);
        }
    }

    about->trigger();
    application.processEvents();
    auto* about_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    auto* about_buttons = about_dialog == nullptr
        ? nullptr : about_dialog->findChild<QDialogButtonBox*>();
    if (!verify(about_buttons != nullptr &&
                    about_dialog->windowFlags().testFlag(Qt::SubWindow),
                "About must use the shared in-application SubWindow contract")) {
        return 1;
    }
    about_buttons->button(QDialogButtonBox::Ok)->click();
    application.processEvents();
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication application(argc, argv);
    application.setApplicationName("ZIMA-CAD");
    QString startup_directory = QDir::currentPath();
    const auto arguments = application.arguments();
    for (int index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (argument == QStringLiteral("--working-directory") ||
            argument == QStringLiteral("-w")) {
            if (index + 1 < arguments.size()) {
                startup_directory = QFileInfo(arguments.at(++index))
                    .absoluteFilePath();
            }
            continue;
        }
        if (argument.startsWith(QStringLiteral("--working-directory="))) {
            startup_directory = QFileInfo(
                argument.section('=', 1)).absoluteFilePath();
            continue;
        }
        if (argument.startsWith('-')) continue;
        const QFileInfo candidate(argument);
        if (candidate.isDir()) {
            startup_directory = candidate.absoluteFilePath();
            continue;
        }
        if (argument.endsWith(".prtz", Qt::CaseInsensitive) ||
            argument.endsWith(".asmz", Qt::CaseInsensitive) ||
            argument.endsWith(".drwz", Qt::CaseInsensitive)) {
            startup_directory = candidate.absolutePath();
            break;
        }
    }
    zima::app::AssemblyWorkspaceWindow window(startup_directory);
    QString part_capture_path;
    QString drawing_capture_path;
    const QString part_capture_prefix = QStringLiteral("--capture-part=");
    const QString drawing_capture_prefix = QStringLiteral("--capture-drawing=");
    for (const auto& argument : application.arguments()) {
        if (argument.startsWith(part_capture_prefix)) {
            part_capture_path = argument.mid(part_capture_prefix.size());
        } else if (argument.startsWith(drawing_capture_prefix)) {
            drawing_capture_path = argument.mid(drawing_capture_prefix.size());
        }
    }
    if (application.arguments().contains("--verify-startup") ||
        !part_capture_path.isEmpty() || !drawing_capture_path.isEmpty()) {
        return verify_startup_contract(
            application, window, part_capture_path, drawing_capture_path);
    }
    for (const auto& argument : application.arguments().mid(1)) {
        if (argument.startsWith('-')) continue;
        if (argument.endsWith(".prtz", Qt::CaseInsensitive) ||
            argument.endsWith(".asmz", Qt::CaseInsensitive) ||
            argument.endsWith(".drwz", Qt::CaseInsensitive)) {
            if (!window.open_document_path(argument)) return 1;
        }
    }
    window.showMaximized();
    return application.exec();
}
