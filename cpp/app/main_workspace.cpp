#include "assembly_workspace_window.hpp"

#include <zima/viewer/mesh_view.hpp>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
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
    auto* delete_working_directory_menu =
        window.findChild<QMenu*>("deleteWorkingDirectoryMenu");
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
    construction_point->trigger();
    application.processEvents();
    auto* point_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    auto* point_references = point_dialog == nullptr
        ? nullptr : point_dialog->findChild<QTableWidget*>(
            "constructionReferenceTable");
    auto* point_viewer = dynamic_cast<zima::viewer::MeshView*>(
        window.findChild<QOpenGLWidget*>("modelWorkspace"));
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
                    point_hover->geometry ==
                        zima::viewer::CandidateGeometry::OriginalReference,
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
                    selected_reference->property("text").toString().contains('/'),
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
                child->text(0) == QStringLiteral("Kvádr")) {
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
                child->text(0) == QStringLiteral("Vytažení")) {
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
                    candidate->text(0) == QStringLiteral("Vytažení")) {
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
            if (item->text(0) == QStringLiteral("Vytažení")) {
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

    if (!create_document(QStringLiteral("drawing"), drawing_name)) {
        return 1;
    }
    auto* stack = window.findChild<QStackedWidget*>("workspaceStack");
    auto* drawing_canvas = window.findChild<QWidget*>("drawingCanvas");
    auto* drawing_toolbar = window.findChild<QToolBar*>("drawingToolbar");
    auto* insert_view = window.findChild<QAction*>("insertDrawingViewAction");
    if (!verify(tabs->count() == 3 && stack != nullptr &&
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
    zima::app::AssemblyWorkspaceWindow window;
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
