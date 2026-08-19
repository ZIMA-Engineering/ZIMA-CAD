#include "assembly_workspace_window.hpp"
#include "component_properties_dialog.hpp"
#include "mate_properties_dialog.hpp"
#include "primitive_properties_dialog.hpp"
#include "construction_properties_dialog.hpp"
#include "sketch_properties_dialog.hpp"
#include "sketch_bspline_properties_dialog.hpp"
#include "sketch_text_properties_dialog.hpp"
#include "sketch_dimension_properties_dialog.hpp"
#include "drawing_window.hpp"
#include "file_dialog.hpp"
#include "resource_icon.hpp"

#include <zima/viewer/mesh_view.hpp>
#include <zima/interchange/interchange.hpp>
#include <zima/interchange/dxf.hpp>
#include <zima/interchange/step.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QBrush>
#include <QComboBox>
#include <QColor>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMenu>
#include <QKeySequence>
#include <QPixmap>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <set>
#include <string_view>
#include <unordered_map>
#include <type_traits>

namespace zima::app {
namespace {

void append_reference_geometry(
    zima::kernel::ViewerReferenceGeometry& target,
    zima::kernel::ViewerReferenceGeometry source) {
    const auto vertex_offset = static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(
        target.vertices.end(), source.vertices.begin(), source.vertices.end());
    for (const auto index : source.triangles) {
        target.triangles.push_back(index + vertex_offset);
    }
    target.triangle_references.insert(target.triangle_references.end(),
        source.triangle_references.begin(), source.triangle_references.end());
    target.edges.insert(target.edges.end(), source.edges.begin(), source.edges.end());
    target.points.insert(target.points.end(), source.points.begin(), source.points.end());
    target.axes.insert(target.axes.end(), source.axes.begin(), source.axes.end());
}

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
    append_reference_geometry(
        target.original_references, std::move(source.original_references));
}

using SketchPosition = std::array<double, 2>;

std::optional<std::string> sketch_text_id_from_key(const std::string& key) {
    constexpr std::string_view prefix{"text:"};
    if (!key.starts_with(prefix)) return std::nullopt;
    const auto color_separator = key.find(':', prefix.size());
    if (color_separator == std::string::npos || color_separator == prefix.size()) {
        return std::nullopt;
    }
    return key.substr(prefix.size(), color_separator - prefix.size());
}

std::optional<std::string> sketch_external_reference_id_from_key(
    const std::string& key) {
    const std::string_view edge_prefix{"external_edge:"};
    const std::string_view point_prefix{"external_point:"};
    const std::string_view axis_prefix{"external_axis:"};
    const std::string_view face_prefix{"external_face:"};
    const auto prefix = key.starts_with(edge_prefix) ? edge_prefix
        : key.starts_with(point_prefix) ? point_prefix
        : key.starts_with(axis_prefix) ? axis_prefix
        : key.starts_with(face_prefix) ? face_prefix : std::string_view{};
    if (prefix.empty() || key.size() == prefix.size()) return std::nullopt;
    const auto suffix = key.find(':', prefix.size());
    return key.substr(prefix.size(), suffix == std::string::npos
        ? std::string::npos : suffix - prefix.size());
}

std::vector<zima::kernel::ViewerEdge> sketch_text_preview_edges(
    const zima::sketcher::Sketch& sketch,
    const zima::sketcher::SketchText& text) {
    std::vector<zima::kernel::ViewerEdge> edges;
    edges.reserve(text.contours.size());
    for (const auto& contour : text.contours) {
        if (contour.size() < 3) continue;
        zima::kernel::ViewerEdge edge;
        edge.points.reserve(contour.size() + 1);
        for (const auto& point : contour) {
            edge.points.push_back(sketch.world_point(point[0], point[1]));
        }
        edge.points.push_back(edge.points.front());
        edges.push_back(std::move(edge));
    }
    return edges;
}

std::set<std::string> sketch_external_reference_source_owners(
    const zima::document::PartDocument& document,
    const std::string& sketch_id) {
    std::size_t first_consumer = document.history.size();
    for (std::size_t index = 0; index < document.history.size(); ++index) {
        const auto& container = document.history[index];
        const bool extrusion_consumer =
            container.feature_kind == zima::document::FeatureKind::Extrusion &&
            container.extrusion.sketch_id == sketch_id;
        const bool revolution_consumer =
            container.feature_kind == zima::document::FeatureKind::Revolution &&
            container.revolution.sketch_id == sketch_id;
        if (extrusion_consumer || revolution_consumer) {
            first_consumer = std::min(first_consumer, index);
        }
    }
    std::set<std::string> owners;
    for (std::size_t index = 0; index < first_consumer; ++index) {
        owners.insert(document.history[index].id);
    }
    for (const auto& construction : document.constructions) {
        owners.insert(construction.id);
    }
    return owners;
}

zima::kernel::ViewerReferenceGeometry sketch_external_reference_source_geometry(
    const zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries) {
    zima::kernel::ViewerReferenceGeometry source;
    if (!calculated_boundaries.empty()) {
        source = calculated_boundaries.back().mesh.original_references;
    }
    append_reference_geometry(source,
        document.construction_viewer_mesh().original_references);
    return source;
}

bool refresh_sketch_external_references(
    zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries) {
    const auto source = sketch_external_reference_source_geometry(
        document, calculated_boundaries);
    bool changed = false;
    for (auto& sketch : document.sketches) {
        const auto allowed_owners = sketch_external_reference_source_owners(
            document, sketch.id);
        zima::kernel::ViewerReferenceGeometry allowed_source;
        for (const auto& edge : source.edges) {
            if (allowed_owners.contains(edge.reference.owner_id)) {
                allowed_source.edges.push_back(edge);
            }
        }
        for (const auto& point : source.points) {
            if (allowed_owners.contains(point.reference.owner_id)) {
                allowed_source.points.push_back(point);
            }
        }
        for (const auto& axis : source.axes) {
            if (allowed_owners.contains(axis.reference.owner_id)) {
                allowed_source.axes.push_back(axis);
            }
        }
        allowed_source.vertices = source.vertices;
        for (std::size_t triangle = 0;
             triangle < source.triangle_references.size(); ++triangle) {
            const auto& face = source.triangle_references[triangle];
            if (!allowed_owners.contains(face.owner_id)) continue;
            allowed_source.triangle_references.push_back(face);
            allowed_source.triangles.insert(allowed_source.triangles.end(), {
                source.triangles[triangle * 3],
                source.triangles[triangle * 3 + 1],
                source.triangles[triangle * 3 + 2]});
        }
        if (sketch.refresh_external_references(
                document.document_id, allowed_source)) {
            changed = true;
        }
    }
    return changed;
}

void populate_external_reference_cache(
    const zima::sketcher::Sketch& sketch,
    zima::sketcher::SketchExternalReference& reference,
    const zima::kernel::ViewerReferenceGeometry& source) {
    const auto matches = [&](const auto& candidate) {
        return candidate.owner_id == reference.source_owner_id &&
            candidate.semantic_key == reference.source_semantic_key &&
            candidate.instance_path == reference.source_instance_path;
    };
    if (reference.kind == zima::sketcher::ExternalReferenceKind::Edge) {
        const auto edge = std::find_if(source.edges.begin(), source.edges.end(),
            [&](const auto& candidate) { return matches(candidate.reference); });
        if (edge == source.edges.end()) {
            throw std::runtime_error("Persisted source edge geometry is unavailable");
        }
        for (const auto& point : edge->points) {
            const auto local = sketch.local_point(point);
            if (reference.cached_points.empty() || std::hypot(
                    local[0] - reference.cached_points.back()[0],
                    local[1] - reference.cached_points.back()[1]) > 1.0e-9) {
                reference.cached_points.push_back(local);
            }
        }
    } else if (reference.kind == zima::sketcher::ExternalReferenceKind::Point) {
        const auto point = std::find_if(source.points.begin(), source.points.end(),
            [&](const auto& candidate) { return matches(candidate.reference); });
        if (point == source.points.end()) {
            throw std::runtime_error("Persisted source point geometry is unavailable");
        }
        reference.cached_points.push_back(sketch.local_point(point->position));
    } else if (reference.kind == zima::sketcher::ExternalReferenceKind::Axis) {
        const auto axis = std::find_if(source.axes.begin(), source.axes.end(),
            [&](const auto& candidate) { return matches(candidate.reference); });
        if (axis == source.axes.end()) {
            throw std::runtime_error("Persisted source axis geometry is unavailable");
        }
        const auto projected = sketch.project_external_axis(*axis);
        if (!projected) {
            throw std::runtime_error(
                "Source axis cannot be projected into the Sketch plane");
        }
        reference.cached_points = *projected;
    } else {
        const auto projected = sketch.project_external_face(source,
            {reference.source_owner_id, reference.source_semantic_key,
             reference.source_instance_path});
        if (!projected) {
            throw std::runtime_error(
                "Source face cannot be projected into closed Sketch contours");
        }
        reference.cached_paths = *projected;
    }
    reference.broken = false;
}

std::optional<SketchPosition> projected_ellipse_minor(
    const SketchPosition& center, const SketchPosition& major,
    const SketchPosition& cursor) {
    const double axis_x = major[0] - center[0];
    const double axis_y = major[1] - center[1];
    const double axis_length = std::hypot(axis_x, axis_y);
    if (axis_length <= 1.0e-9) return std::nullopt;
    const double normal_x = -axis_y / axis_length;
    const double normal_y = axis_x / axis_length;
    const double signed_length =
        (cursor[0] - center[0]) * normal_x +
        (cursor[1] - center[1]) * normal_y;
    if (std::abs(signed_length) <= 1.0e-9) return std::nullopt;
    return SketchPosition{
        center[0] + signed_length * normal_x,
        center[1] + signed_length * normal_y};
}

struct ProjectedEllipsePosition {
    SketchPosition position;
    double parameter{};
};

std::optional<ProjectedEllipsePosition> projected_ellipse_position(
    const SketchPosition& center, const SketchPosition& major,
    const SketchPosition& minor, const SketchPosition& cursor) {
    const double major_x = major[0] - center[0];
    const double major_y = major[1] - center[1];
    const double minor_x = minor[0] - center[0];
    const double minor_y = minor[1] - center[1];
    const double determinant = major_x * minor_y - major_y * minor_x;
    if (std::abs(determinant) <= 1.0e-12) return std::nullopt;
    const double cursor_x = cursor[0] - center[0];
    const double cursor_y = cursor[1] - center[1];
    double cosine =
        (cursor_x * minor_y - cursor_y * minor_x) / determinant;
    double sine =
        (major_x * cursor_y - major_y * cursor_x) / determinant;
    const double scale = std::hypot(cosine, sine);
    if (scale <= 1.0e-12) return std::nullopt;
    cosine /= scale;
    sine /= scale;
    return ProjectedEllipsePosition{{
        center[0] + major_x * cosine + minor_x * sine,
        center[1] + major_y * cosine + minor_y * sine},
        std::atan2(sine, cosine)};
}

zima::kernel::ViewerEdge ellipse_preview_edge(
    const zima::sketcher::Sketch& sketch, const SketchPosition& center,
    const SketchPosition& major, const SketchPosition& minor) {
    zima::kernel::ViewerEdge edge;
    constexpr std::size_t samples = 192;
    constexpr double full_turn = 2.0 * 3.14159265358979323846;
    edge.points.reserve(samples + 1);
    for (std::size_t sample = 0; sample <= samples; ++sample) {
        const double parameter = full_turn * static_cast<double>(sample) /
            static_cast<double>(samples);
        const double cosine = std::cos(parameter);
        const double sine = std::sin(parameter);
        edge.points.push_back(sketch.world_point(
            center[0] + (major[0] - center[0]) * cosine +
                (minor[0] - center[0]) * sine,
            center[1] + (major[1] - center[1]) * cosine +
                (minor[1] - center[1]) * sine));
    }
    return edge;
}

class NewDocumentDialog final : public zima::ui::PropertiesSubWindow {
public:
    using Accepted = std::function<QString(QString, QString)>;

    NewDocumentDialog(Accepted accepted, QMainWindow* parent)
        : PropertiesSubWindow(QObject::tr("Nový dokument"), parent),
          accepted_(std::move(accepted)) {
        setObjectName("newDocumentDialog");
        setMinimumWidth(420);
        auto* content = new QWidget(this);
        auto* layout = new QVBoxLayout(content);
        auto* form = new QFormLayout;
        name_ = new QLineEdit(QStringLiteral("part"), content);
        name_->setObjectName("newDocumentFileName");
        form->addRow(QObject::tr("Název souboru"), name_);
        layout->addLayout(form);
        layout->addWidget(new QLabel(QObject::tr("Typ dokumentu"), content));
        part_ = add_type(layout, QObject::tr("Díl"), "part", "part", true);
        add_type(layout, QObject::tr("Sestava"), "assembly", "assembly", true);
        add_type(layout, QObject::tr("Výkres"), "drawing", "drawing", true);
        auto* drawing_format = add_type(
            layout, QObject::tr("Formát výkresu"), "drawing_format",
            "drawing-format", false);
        auto* title_block = add_type(
            layout, QObject::tr("Razítko výkresu"), "title_block",
            "title-block", false);
        const auto pending = QObject::tr("Editor tohoto typu dokumentu ještě není přenesen.");
        drawing_format->setToolTip(pending);
        title_block->setToolTip(pending);
        part_->setChecked(true);
        error_ = new QLabel(content);
        error_->setObjectName("newDocumentError");
        error_->setWordWrap(true);
        error_->setStyleSheet(QStringLiteral("color:#F08A85;"));
        error_->hide();
        layout->addWidget(error_);
        content_layout()->addWidget(content);
        setAttribute(Qt::WA_DeleteOnClose);
    }

private:
    QLineEdit* name_{};
    QRadioButton* part_{};
    QLabel* error_{};
    Accepted accepted_;

    QRadioButton* add_type(QVBoxLayout* layout, const QString& label,
                           const QString& type, const QString& icon,
                           bool enabled) {
        auto* radio = new QRadioButton(label, this);
        radio->setIcon(resource_icon(icon));
        radio->setProperty("documentType", type);
        radio->setEnabled(enabled);
        layout->addWidget(radio);
        return radio;
    }

    bool submit() override {
        const QString stem = name_->text().trimmed();
        if (stem.isEmpty()) return false;
        const auto radios = findChildren<QRadioButton*>();
        const auto selected = std::find_if(radios.begin(), radios.end(),
            [](const auto* radio) { return radio->isChecked(); });
        if (selected == radios.end()) return false;
        const QString error = accepted_(
            (*selected)->property("documentType").toString(), stem);
        if (!error.isEmpty()) {
            error_->setText(error);
            error_->show();
            return false;
        }
        return true;
    }
};

class AboutSubWindow final : public zima::ui::PropertiesSubWindow {
public:
    explicit AboutSubWindow(QMainWindow* parent)
        : PropertiesSubWindow(QObject::tr("O aplikaci ZIMA-CAD"), parent) {
        setMinimumWidth(520);
        auto* artwork = new QLabel(this);
        artwork->setAlignment(Qt::AlignCenter);
        const QPixmap pixmap(QStringLiteral(":/zima/branding/about.svg"));
        if (!pixmap.isNull()) {
            artwork->setPixmap(pixmap.scaled(
                480, 260, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            content_layout()->addWidget(artwork);
        }
        auto* description = new QLabel(
            QObject::tr(
                "ZIMA-CAD\nNativní CAD aplikace pro parametrické modelování, "
                "sestavy a technické výkresy."),
            this);
        description->setAlignment(Qt::AlignCenter);
        description->setWordWrap(true);
        content_layout()->addWidget(description);
        setAttribute(Qt::WA_DeleteOnClose);
    }

private:
    bool submit() override { return true; }
};

}  // namespace

AssemblyWorkspaceWindow::AssemblyWorkspaceWindow() {
    setWindowTitle(tr("ZIMA-CAD"));
    setWindowIcon(application_icon());
    resize(1200, 800);
    create_actions();
    create_layout();
    refresh_tabs();
    refresh_scene();
}

void AssemblyWorkspaceWindow::create_actions() {
    const auto make_action = [this](const QString& text, const char* icon = nullptr) {
        auto* action = new QAction(text, this);
        if (icon != nullptr) action->setIcon(resource_icon(QString::fromLatin1(icon)));
        return action;
    };

    auto* file = menuBar()->addMenu(tr("Soubor"));
    new_document_action_ = make_action(tr("Nový"), "new");
    new_document_action_->setObjectName("newDocumentAction");
    new_document_action_->setShortcut(QKeySequence::New);
    connect(new_document_action_, &QAction::triggered, this,
        [this] { new_document(); });
    file->addAction(new_document_action_);
    open_document_action_ = make_action(tr("Otevřít..."), "open");
    open_document_action_->setObjectName("openDocumentAction");
    open_document_action_->setShortcut(QKeySequence::Open);
    connect(open_document_action_, &QAction::triggered, this,
        [this] { open_document(); });
    file->addAction(open_document_action_);
    auto* import_action = make_action(tr("Importovat…"), "open");
    connect(import_action, &QAction::triggered, this, [this] { import_file(); });
    file->addAction(import_action);
    auto* export_action = make_action(tr("Exportovat…"), "save");
    connect(export_action, &QAction::triggered, this, [this] { export_file(); });
    file->addAction(export_action);
    close_document_action_ = make_action(tr("Zavřít"));
    close_document_action_->setObjectName("closeDocumentAction");
    close_document_action_->setShortcut(QKeySequence(QStringLiteral("F2")));
    connect(close_document_action_, &QAction::triggered, this,
        [this] { close_document(); });
    file->addAction(close_document_action_);
    save_action_ = make_action(tr("Uložit"), "save");
    save_action_->setObjectName("saveDocumentAction");
    save_action_->setShortcut(QKeySequence::Save);
    connect(save_action_, &QAction::triggered, this,
        [this] { save_active_document(); });
    file->addAction(save_action_);
    save_as_action_ = make_action(tr("Uložit jako..."));
    save_as_action_->setObjectName("saveDocumentAsAction");
    save_as_action_->setShortcut(QKeySequence::SaveAs);
    save_as_action_->setEnabled(false);
    connect(save_as_action_, &QAction::triggered, this,
        [this] { save_active_document_as(); });
    file->addAction(save_as_action_);
    file->addSeparator();
    working_directory_action_ = make_action(tr("Nastavit pracovní adresář..."));
    working_directory_action_->setObjectName("workingDirectoryAction");
    connect(working_directory_action_, &QAction::triggered, this,
        [this] { set_working_directory(); });
    file->addAction(working_directory_action_);

    auto* edit = menuBar()->addMenu(tr("Upravit"));
    regenerate_document_action_ = make_action(tr("⟳ Regenerovat model"));
    regenerate_document_action_->setObjectName("regenerateDocumentAction");
    regenerate_document_action_->setShortcut(QKeySequence(QStringLiteral("F5")));
    regenerate_document_action_->setToolTip(
        tr("Přepočítá aktivní dokument a jeho otevřené závislosti (F5)"));
    connect(regenerate_document_action_, &QAction::triggered, this,
        [this] { regenerate_active_document(); });
    edit->addAction(regenerate_document_action_);
    edit->addSeparator();
    undo_action_ = make_action(tr("Zpět"), "undo");
    redo_action_ = make_action(tr("Znovu"), "redo");
    undo_action_->setObjectName("undoAction");
    redo_action_->setObjectName("redoAction");
    connect(undo_action_, &QAction::triggered, this, [this] { undo(); });
    connect(redo_action_, &QAction::triggered, this, [this] { redo(); });
    edit->addAction(undo_action_);
    edit->addAction(redo_action_);

    fit_view_action_ = make_action(tr("Obnovit pohled"), "view-fit");
    fit_view_action_->setObjectName("fitViewAction");
    connect(fit_view_action_, &QAction::triggered, this, [this] {
        if (viewer_ != nullptr) viewer_->fit_all();
    });
    auto* normal_view_action = make_action(tr("Orientace"), "view-normal");
    normal_view_action->setToolTip(tr("Nastavit výchozí izometrickou orientaci"));
    connect(normal_view_action, &QAction::triggered, this, [this] {
        if (viewer_ != nullptr) {
            viewer_->set_standard_view(zima::viewer::StandardView::Isometric);
        }
    });
    selection_action_ = make_action(tr("Výběr"), "select");
    selection_action_->setObjectName("viewSelectionAction");
    selection_action_->setCheckable(true);
    selection_action_->setChecked(true);
    connect(selection_action_, &QAction::toggled, this, [this](bool enabled) {
        if (viewer_ == nullptr) return;
        if (enabled) refresh_scene();
        else viewer_->set_selection_contract({});
    });

    display_mode_group_ = new QActionGroup(this);
    display_mode_group_->setExclusive(true);
    const auto display_action = [this, &make_action](
        const QString& text, zima::viewer::DisplayMode mode) {
        auto* action = make_action(text);
        action->setCheckable(true);
        display_mode_group_->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode] {
            if (viewer_ != nullptr) viewer_->set_display_mode(mode);
        });
        return action;
    };
    wire_action_ = display_action(tr("Drátový"), zima::viewer::DisplayMode::Wire);
    hidden_edges_action_ = display_action(
        tr("Skryté hrany"), zima::viewer::DisplayMode::HiddenEdges);
    no_hidden_edges_action_ = display_action(
        tr("Bez skrytých hran"), zima::viewer::DisplayMode::NoHiddenEdges);
    shaded_edges_action_ = display_action(
        tr("Stínovaný s hranami"), zima::viewer::DisplayMode::ShadedWithEdges);
    shaded_action_ = display_action(
        tr("Stínovaný"), zima::viewer::DisplayMode::Shaded);
    wire_action_->setObjectName("wireDisplayAction");
    hidden_edges_action_->setObjectName("hiddenEdgesDisplayAction");
    no_hidden_edges_action_->setObjectName("noHiddenEdgesDisplayAction");
    shaded_edges_action_->setObjectName("shadedEdgesDisplayAction");
    shaded_action_->setObjectName("shadedDisplayAction");
    shaded_edges_action_->setChecked(true);

    const auto reference_action = [this, &make_action](
        const QString& text, const char* icon,
        zima::viewer::ReferenceVisibility reference) {
        auto* action = make_action(text, icon);
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::toggled, this, [this, reference](bool visible) {
            if (viewer_ != nullptr) {
                viewer_->set_reference_visibility(reference, visible);
            }
        });
        return action;
    };
    show_origins_action_ = reference_action(
        tr("Počátky"), "origin", zima::viewer::ReferenceVisibility::Origins);
    show_points_action_ = reference_action(
        tr("Body"), "point", zima::viewer::ReferenceVisibility::Points);
    show_axes_action_ = reference_action(
        tr("Osy"), "axis", zima::viewer::ReferenceVisibility::Axes);
    show_planes_action_ = reference_action(
        tr("Roviny"), "plane", zima::viewer::ReferenceVisibility::Planes);
    show_sketches_action_ = reference_action(
        tr("Skici"), "sketch", zima::viewer::ReferenceVisibility::Sketches);
    show_origins_action_->setObjectName("showOriginsAction");
    show_points_action_->setObjectName("showPointsAction");
    show_axes_action_->setObjectName("showAxesAction");
    show_planes_action_->setObjectName("showPlanesAction");
    show_sketches_action_->setObjectName("showSketchesAction");

    auto* view = menuBar()->addMenu(tr("Zobrazení"));
    view->addAction(fit_view_action_);
    auto* standard_views = view->addMenu(tr("Základní pohledy"));
    const auto add_standard_view = [this, standard_views](
        const QString& text, zima::viewer::StandardView standard_view) {
        auto* action = standard_views->addAction(text);
        connect(action, &QAction::triggered, this, [this, standard_view] {
            if (viewer_ != nullptr) viewer_->set_standard_view(standard_view);
        });
    };
    add_standard_view(tr("Výchozí – izometrický"), zima::viewer::StandardView::Isometric);
    add_standard_view(tr("Front – XZ"), zima::viewer::StandardView::Front);
    add_standard_view(tr("Back – XZ opačně"), zima::viewer::StandardView::Back);
    add_standard_view(tr("Left – YZ"), zima::viewer::StandardView::Left);
    add_standard_view(tr("Right – YZ opačně"), zima::viewer::StandardView::Right);
    add_standard_view(tr("Top – XY"), zima::viewer::StandardView::Top);
    add_standard_view(tr("Bottom – XY opačně"), zima::viewer::StandardView::Bottom);
    view->addSeparator();
    view->addAction(selection_action_);
    view->addSeparator();
    for (auto* action : {wire_action_, hidden_edges_action_, no_hidden_edges_action_,
                         shaded_edges_action_, shaded_action_}) {
        view->addAction(action);
    }
    view->addSeparator();
    for (auto* action : {show_origins_action_, show_points_action_, show_axes_action_,
                         show_planes_action_, show_sketches_action_}) {
        view->addAction(action);
    }

    auto* applications = menuBar()->addMenu(tr("Aplikace"));
    application_group_ = new QActionGroup(this);
    application_group_->setExclusive(true);
    const std::array<QString, 6> application_names{
        tr("Modelování"), tr("Sestava"), tr("Plech"), tr("Plochy"),
        tr("Potrubí"), tr("Výkres")};
    for (std::size_t index = 0; index < application_actions_.size(); ++index) {
        auto* action = applications->addAction(application_names[index]);
        action->setCheckable(true);
        action->setData(static_cast<int>(index));
        application_group_->addAction(action);
        connect(action, &QAction::triggered, this, [this, index] {
            set_active_application(static_cast<ApplicationMode>(index));
        });
        application_actions_[index] = action;
    }
    application_actions_[0]->setChecked(true);

    auto* tools = menuBar()->addMenu(tr("Nástroje"));
    for (const auto& label : {tr("Materiál..."), tr("Parametry..."),
                              tr("Relace..."), tr("Family Table...")}) {
        auto* action = tools->addAction(label);
        action->setEnabled(false);
    }
    tools->addSeparator();
    auto* file_settings_action = tools->addAction(tr("Nastavení souboru..."));
    file_settings_action->setEnabled(false);
    auto* settings_action = make_action(tr("Globální nastavení..."), "settings");
    settings_action->setEnabled(false);
    tools->addAction(settings_action);
    auto* window_menu = menuBar()->addMenu(tr("Okno"));
    window_menu->setObjectName("windowMenu");
    connect(window_menu, &QMenu::aboutToShow, this, [this, window_menu] {
        window_menu->clear();
        if (tabs_ == nullptr || tabs_->count() == 0) {
            auto* empty = window_menu->addAction(tr("Není otevřen žádný dokument"));
            empty->setEnabled(false);
            return;
        }
        for (int index = 0; index < tabs_->count(); ++index) {
            auto* action = window_menu->addAction(tabs_->tabIcon(index), tabs_->tabText(index));
            action->setCheckable(true);
            action->setChecked(index == tabs_->currentIndex());
            connect(action, &QAction::triggered, this, [this, index] {
                if (tabs_ != nullptr && index >= 0 && index < tabs_->count()) {
                    tabs_->setCurrentIndex(index);
                }
            });
        }
    });
    auto* help = menuBar()->addMenu(tr("Nápověda"));
    auto* about_action = help->addAction(tr("O aplikaci ZIMA-CAD"));
    about_action->setObjectName("aboutAction");
    connect(about_action, &QAction::triggered, this, [this] { show_about(); });

    box_action_ = make_action(tr("Kvádr"), "box");
    box_action_->setObjectName("boxAction");
    cylinder_action_ = make_action(tr("Válec"), "cylinder");
    sphere_action_ = make_action(tr("Koule"), "sphere");
    cone_action_ = make_action(tr("Kužel"), "cone");
    pyramid_action_ = make_action(tr("Jehlan"), "pyramid");
    wedge_action_ = make_action(tr("Klín"), "wedge");
    construction_point_action_ = make_action(tr("Bod"), "point");
    construction_axis_action_ = make_action(tr("Osa"), "axis");
    construction_plane_action_ = make_action(tr("Rovina"), "plane");
    extrusion_action_ = make_action(tr("Vytažení"), "protrusion");
    extrusion_action_->setObjectName("extrusionAction");
    revolution_action_ = make_action(tr("Rotace"), "revolve");
    revolution_action_->setObjectName("revolutionAction");
    fillet_action_ = make_action(tr("Zaoblení"), "fillet");
    chamfer_action_ = make_action(tr("Sražení"), "chamfer");
    sketch_action_ = make_action(tr("Vytvořit skicu"), "sketch");
    sketch_action_->setObjectName("sketchAction");
    sketch_normal_view_action_ = make_action(tr("Pohled kolmo"), "view-normal");
    sketch_normal_view_action_->setObjectName("sketchNormalViewAction");
    sketch_normal_view_action_->setEnabled(false);
    sketch_external_reference_action_ = make_action(
        tr("Externí reference"), "sketch-reference");
    sketch_external_reference_action_->setObjectName(
        "sketchExternalReferenceAction");
    sketch_external_reference_action_->setCheckable(true);
    sketch_external_reference_action_->setEnabled(false);
    sketch_point_action_ = make_action(tr("Bod"), "point");
    sketch_point_action_->setObjectName("sketchPointAction");
    sketch_point_action_->setEnabled(false);
    sketch_construction_action_ = make_action(
        tr("Konstrukční čára"), "sketch-construction");
    sketch_construction_action_->setObjectName("sketchConstructionAction");
    sketch_construction_action_->setEnabled(false);
    sketch_segment_action_ = make_action(tr("Úsečka"), "sketch-segment");
    sketch_segment_action_->setObjectName("sketchSegmentAction");
    sketch_polyline_action_ = make_action(tr("Lomená čára"), "sketch-polyline");
    sketch_polyline_action_->setObjectName("sketchPolylineAction");
    sketch_polyline_action_->setEnabled(false);
    sketch_rectangle_action_ = make_action(tr("Obdélník"), "sketch-rectangle");
    sketch_rectangle_action_->setObjectName("sketchRectangleAction");
    sketch_polygon_menu_ = new QMenu(tr("Mnohoúhelník"), this);
    sketch_polygon_action_ = sketch_polygon_menu_->menuAction();
    sketch_polygon_action_->setIcon(resource_icon("sketch-hexagon"));
    sketch_polygon_action_->setObjectName("sketchPolygonAction");
    sketch_polygon_action_->setEnabled(false);
    for (const auto [sides, label] : std::array{
             std::pair{4U, tr("Čtverec (4 strany)")},
             std::pair{6U, tr("Šestiúhelník (6 stran)")},
             std::pair{8U, tr("Osmiúhelník (8 stran)")}}) {
        auto* action = sketch_polygon_menu_->addAction(label);
        connect(action, &QAction::triggered, this,
            [this, sides] { start_sketch_polygon(sides); });
    }
    sketch_trim_action_ = make_action(tr("Ořezat"), "sketch-trim");
    sketch_trim_action_->setObjectName("sketchTrimAction");
    sketch_trim_action_->setEnabled(false);
    sketch_mirror_action_ = make_action(tr("Zrcadlit"), "sketch-mirror");
    sketch_mirror_action_->setObjectName("sketchMirrorAction");
    sketch_mirror_action_->setEnabled(false);
    sketch_circle_action_ = make_action(tr("Kružnice"), "sketch-circle");
    sketch_arc_action_ = make_action(tr("Oblouk"), "sketch-arc");
    sketch_ellipse_action_ = make_action(tr("Elipsa"), "sketch-ellipse");
    sketch_elliptical_arc_action_ = make_action(
        tr("Eliptický oblouk"), "sketch-elliptical-arc");
    sketch_elliptical_arc_action_->setObjectName("sketchEllipticalArcAction");
    sketch_bspline_action_ = make_action(tr("B-spline"), "sketch-spline");
    sketch_text_action_ = make_action(tr("Text"), "sketch-text");
    sketch_text_action_->setObjectName("sketchTextAction");
    sketch_text_action_->setEnabled(false);
    sketch_horizontal_action_ = make_action(tr("Vodorovná úsečka"));
    sketch_vertical_action_ = make_action(tr("Svislá úsečka"));
    sketch_coincident_action_ = make_action(tr("Shodnost bodů"));
    sketch_midpoint_action_ = make_action(tr("Bod ve středu"));
    sketch_midpoint_action_->setObjectName("sketchMidpointAction");
    sketch_symmetric_action_ = make_action(tr("Symetrická"));
    sketch_symmetric_action_->setObjectName("sketchSymmetricAction");
    sketch_concentric_action_ = make_action(tr("Soustředná"));
    sketch_concentric_action_->setObjectName("sketchConcentricAction");
    sketch_tangent_action_ = make_action(tr("Tečná"));
    sketch_tangent_action_->setObjectName("sketchTangentAction");
    sketch_parallel_action_ = make_action(tr("Rovnoběžnost úseček"));
    sketch_perpendicular_action_ = make_action(tr("Kolmost úseček"));
    sketch_equal_length_action_ = make_action(tr("Stejné"));
    sketch_equal_length_action_->setObjectName("sketchEqualAction");
    sketch_fix_point_action_ = make_action(tr("Fixovat/uvolnit bod"));
    sketch_constraints_menu_ = new QMenu(tr("Vazby"), this);
    sketch_constraints_menu_->setObjectName("sketchConstraintsMenu");
    for (auto* action : {sketch_horizontal_action_, sketch_vertical_action_,
                         sketch_parallel_action_, sketch_equal_length_action_,
                         sketch_perpendicular_action_, sketch_coincident_action_,
                         sketch_midpoint_action_, sketch_symmetric_action_,
                         sketch_tangent_action_, sketch_concentric_action_}) {
        sketch_constraints_menu_->addAction(action);
    }
    sketch_constraints_menu_->addSeparator();
    sketch_constraints_menu_->addAction(sketch_fix_point_action_);
    sketch_constraints_action_ = sketch_constraints_menu_->menuAction();
    sketch_constraints_action_->setText(tr("Vazby"));
    sketch_constraints_action_->setIcon(resource_icon("sketch-constraints"));
    sketch_constraints_action_->setObjectName("sketchConstraintsAction");
    sketch_constraints_action_->setEnabled(false);
    sketch_dimension_action_ = make_action(tr("Kóta délky úsečky…"), "sketch-dimensions");
    sketch_dimension_x_action_ = make_action(tr("Vodorovná kóta úsečky…"));
    sketch_dimension_y_action_ = make_action(tr("Svislá kóta úsečky…"));
    sketch_angle_dimension_action_ = make_action(tr("Úhlová kóta úsečky…"));
    sketch_radius_dimension_action_ = make_action(tr("Kóta poloměru…"));
    sketch_diameter_dimension_action_ = make_action(tr("Kóta průměru…"));
    sketch_ellipse_major_dimension_action_ = make_action(tr("Kóta hlavní poloosy elipsy…"));
    sketch_ellipse_minor_dimension_action_ = make_action(tr("Kóta vedlejší poloosy elipsy…"));
    sketch_ellipse_rotation_dimension_action_ = make_action(tr("Kóta natočení elipsy…"));
    sketch_dimensions_menu_ = new QMenu(tr("Kóty"), this);
    sketch_dimensions_menu_->setObjectName("sketchDimensionsMenu");
    for (auto* action : {sketch_dimension_action_, sketch_dimension_x_action_,
                         sketch_dimension_y_action_, sketch_angle_dimension_action_,
                         sketch_radius_dimension_action_,
                         sketch_diameter_dimension_action_,
                         sketch_ellipse_major_dimension_action_,
                         sketch_ellipse_minor_dimension_action_,
                         sketch_ellipse_rotation_dimension_action_}) {
        sketch_dimensions_menu_->addAction(action);
    }
    sketch_dimensions_action_ = sketch_dimensions_menu_->menuAction();
    sketch_dimensions_action_->setText(tr("Kóty"));
    sketch_dimensions_action_->setIcon(resource_icon("sketch-dimensions"));
    sketch_dimensions_action_->setObjectName("sketchDimensionsAction");
    sketch_dimensions_action_->setEnabled(false);
    finish_sketch_action_ = make_action(tr("Dokončit skicu"), "sketch");
    finish_sketch_action_->setObjectName("finishSketchAction");
    finish_sketch_action_->setEnabled(false);
    regenerate_part_action_ = make_action(tr("Regenerovat díl"));
    regenerate_part_action_->setObjectName("regeneratePartAction");

    connect(box_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Box); });
    connect(cylinder_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Cylinder); });
    connect(sphere_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Sphere); });
    connect(cone_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Cone); });
    connect(pyramid_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Pyramid); });
    connect(wedge_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Wedge); });
    connect(construction_point_action_, &QAction::triggered, this, [this] {
        show_construction_properties(zima::document::ConstructionKind::Point); });
    connect(construction_axis_action_, &QAction::triggered, this, [this] {
        show_construction_properties(zima::document::ConstructionKind::Axis); });
    connect(construction_plane_action_, &QAction::triggered, this, [this] {
        show_construction_properties(zima::document::ConstructionKind::Plane); });
    connect(extrusion_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Extrusion); });
    connect(revolution_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Revolution); });
    connect(fillet_action_, &QAction::triggered, this, [this] {
        start_edge_treatment(zima::document::FeatureKind::Fillet); });
    connect(chamfer_action_, &QAction::triggered, this, [this] {
        start_edge_treatment(zima::document::FeatureKind::Chamfer); });
    connect(sketch_action_, &QAction::triggered, this, [this] { show_sketch_properties(); });
    connect(sketch_normal_view_action_, &QAction::triggered, this,
        [this] { align_active_sketch_view(); });
    connect(sketch_external_reference_action_, &QAction::toggled, this,
        [this](bool enabled) { set_sketch_external_reference_mode(enabled); });
    connect(sketch_point_action_, &QAction::triggered, this,
        [this] { start_sketch_point(); });
    connect(sketch_construction_action_, &QAction::triggered, this,
        [this] { start_sketch_segment(true); });
    connect(sketch_segment_action_, &QAction::triggered, this, [this] { start_sketch_segment(); });
    connect(sketch_polyline_action_, &QAction::triggered, this,
        [this] { start_sketch_polyline(); });
    connect(sketch_rectangle_action_, &QAction::triggered, this, [this] { start_sketch_rectangle(); });
    connect(sketch_trim_action_, &QAction::triggered, this,
        [this] { start_sketch_trim(); });
    connect(sketch_mirror_action_, &QAction::triggered, this,
        [this] { start_sketch_mirror(); });
    connect(sketch_circle_action_, &QAction::triggered, this, [this] { start_sketch_circle(); });
    connect(sketch_arc_action_, &QAction::triggered, this, [this] { start_sketch_arc(); });
    connect(sketch_ellipse_action_, &QAction::triggered, this, [this] { start_sketch_ellipse(); });
    connect(sketch_elliptical_arc_action_, &QAction::triggered, this,
        [this] { start_sketch_elliptical_arc(); });
    connect(sketch_bspline_action_, &QAction::triggered, this, [this] { start_sketch_bspline(); });
    connect(sketch_text_action_, &QAction::triggered, this,
        [this] { show_sketch_text_properties(active_sketch_id_); });
    connect(sketch_horizontal_action_, &QAction::triggered, this, [this] {
        constrain_selected_segment(zima::sketcher::ConstraintKind::Horizontal); });
    connect(sketch_vertical_action_, &QAction::triggered, this, [this] {
        constrain_selected_segment(zima::sketcher::ConstraintKind::Vertical); });
    connect(sketch_coincident_action_, &QAction::triggered, this, [this] { start_sketch_coincident(); });
    connect(sketch_midpoint_action_, &QAction::triggered, this,
        [this] { start_sketch_midpoint(); });
    connect(sketch_symmetric_action_, &QAction::triggered, this,
        [this] { start_sketch_symmetric(); });
    connect(sketch_concentric_action_, &QAction::triggered, this,
        [this] { start_sketch_concentric(); });
    connect(sketch_tangent_action_, &QAction::triggered, this,
        [this] { start_sketch_tangent(); });
    connect(sketch_parallel_action_, &QAction::triggered, this, [this] {
        start_sketch_segment_pair(zima::sketcher::ConstraintKind::Parallel); });
    connect(sketch_perpendicular_action_, &QAction::triggered, this, [this] {
        start_sketch_segment_pair(zima::sketcher::ConstraintKind::Perpendicular); });
    connect(sketch_equal_length_action_, &QAction::triggered, this, [this] {
        start_sketch_segment_pair(zima::sketcher::ConstraintKind::EqualLength); });
    connect(sketch_fix_point_action_, &QAction::triggered, this,
        [this] { toggle_selected_sketch_point_fixed(); });
    connect(sketch_dimension_action_, &QAction::triggered, this,
        [this] { show_sketch_dimension_properties(active_sketch_id_); });
    connect(sketch_dimension_x_action_, &QAction::triggered, this, [this] {
        show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::DistanceX); });
    connect(sketch_dimension_y_action_, &QAction::triggered, this, [this] {
        show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::DistanceY); });
    connect(sketch_angle_dimension_action_, &QAction::triggered, this, [this] {
        show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::Angle); });
    connect(sketch_radius_dimension_action_, &QAction::triggered, this,
        [this] { show_sketch_dimension_properties(active_sketch_id_); });
    connect(sketch_diameter_dimension_action_, &QAction::triggered, this, [this] {
        show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::Diameter); });
    connect(sketch_ellipse_major_dimension_action_, &QAction::triggered, this, [this] {
        show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::EllipseMajorRadius); });
    connect(sketch_ellipse_minor_dimension_action_, &QAction::triggered, this, [this] {
        show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::EllipseMinorRadius); });
    connect(sketch_ellipse_rotation_dimension_action_, &QAction::triggered, this, [this] {
        show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::EllipseRotation); });
    connect(finish_sketch_action_, &QAction::triggered, this,
        [this] { finish_active_sketch(); });
    connect(regenerate_part_action_, &QAction::triggered, this,
        [this] { regenerate_active_part(); });

    insert_menu_ = new QMenu(tr("Vložit otevřený dokument"), this);
    insert_menu_->setObjectName("insertComponentMenu");
    insert_action_ = insert_menu_->menuAction();
    insert_action_->setIcon(resource_icon("assembly"));
    insert_action_->setText(tr("Vložit komponentu"));
    insert_action_->setObjectName("insertComponentAction");
    connect(insert_menu_, &QMenu::aboutToShow, this,
        [this] { rebuild_insert_menu(); });
    regenerate_action_ = make_action(tr("Regenerovat sestavu"));
    regenerate_action_->setObjectName("regenerateAssemblyAction");
    plane_mate_action_ = make_action(tr("Vazba plocha–plocha…"), "plane");
    axis_mate_action_ = make_action(tr("Vazba osa–osa…"), "axis");
    point_mate_action_ = make_action(tr("Vazba bod–bod…"), "point");
    angle_mate_action_ = make_action(tr("Úhel os…"));
    plane_angle_mate_action_ = make_action(tr("Úhel ploch…"));
    connect(regenerate_action_, &QAction::triggered, this, [this] { regenerate_assembly(); });
    connect(plane_mate_action_, &QAction::triggered, this, [this] { start_plane_mate(); });
    connect(axis_mate_action_, &QAction::triggered, this, [this] { start_axis_mate(); });
    connect(point_mate_action_, &QAction::triggered, this, [this] { start_point_mate(); });
    connect(angle_mate_action_, &QAction::triggered, this, [this] { start_angle_mate(); });
    connect(plane_angle_mate_action_, &QAction::triggered, this,
        [this] { start_plane_angle_mate(); });

    main_toolbar_ = new QToolBar(tr("Dokument"), this);
    main_toolbar_->setObjectName("mainToolbar");
    main_toolbar_->setMovable(false);
    main_toolbar_->setIconSize(QSize(24, 24));
    main_toolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    main_toolbar_->addAction(new_document_action_);
    main_toolbar_->addAction(open_document_action_);
    main_toolbar_->addAction(save_action_);
    main_toolbar_->addSeparator();
    main_toolbar_->addAction(settings_action);
    auto* toolbar_spacer = new QWidget(main_toolbar_);
    toolbar_spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    main_toolbar_->addWidget(toolbar_spacer);
    auto* logo = new QLabel(QStringLiteral(
        "<span style=\"color:#80AA1A\">ZIMA</span>-CAD"), main_toolbar_);
    auto logo_font = logo->font();
    logo_font.setBold(true);
    logo_font.setPointSizeF(std::max(11.0, logo_font.pointSizeF()));
    logo->setFont(logo_font);
    logo->setContentsMargins(8, 2, 12, 2);
    main_toolbar_->addWidget(logo);
    addToolBar(Qt::TopToolBarArea, main_toolbar_);

    view_toolbar_ = new QToolBar(tr("Pohled"), this);
    view_toolbar_->setObjectName("viewToolbar");
    view_toolbar_->setMovable(false);
    view_toolbar_->setIconSize(QSize(16, 16));
    view_toolbar_->setStyleSheet(
        "QToolButton:hover:enabled { background-color: rgba(255,255,255,32);"
        " color:#fff; border:none; border-radius:4px; }"
        "QToolButton:checked { background-color:rgba(77,216,17,125);"
        " color:#fff; border:none; border-radius:4px; }"
        "QToolButton:pressed { background-color:rgba(77,216,17,165);"
        " color:#fff; border:none; border-radius:4px; }");
    view_toolbar_->addAction(regenerate_document_action_);
    view_toolbar_->addSeparator();
    view_toolbar_->addAction(fit_view_action_);
    view_toolbar_->addAction(normal_view_action);
    standard_view_combo_ = new QComboBox(view_toolbar_);
    standard_view_combo_->setObjectName("standardViewCombo");
    const std::array<std::pair<QString, zima::viewer::StandardView>, 8> standard_views_data{{
        {tr("Základní pohledy"), zima::viewer::StandardView::Isometric},
        {tr("Výchozí – izometrický"), zima::viewer::StandardView::Isometric},
        {tr("Front – XZ"), zima::viewer::StandardView::Front},
        {tr("Back – XZ opačně"), zima::viewer::StandardView::Back},
        {tr("Left – YZ"), zima::viewer::StandardView::Left},
        {tr("Right – YZ opačně"), zima::viewer::StandardView::Right},
        {tr("Top – XY"), zima::viewer::StandardView::Top},
        {tr("Bottom – XY opačně"), zima::viewer::StandardView::Bottom},
    }};
    for (const auto& [text, mode] : standard_views_data) {
        standard_view_combo_->addItem(text, static_cast<int>(mode));
    }
    connect(standard_view_combo_, &QComboBox::currentIndexChanged, this,
        [this](int index) {
            if (index <= 0 || viewer_ == nullptr) return;
            viewer_->set_standard_view(static_cast<zima::viewer::StandardView>(
                standard_view_combo_->itemData(index).toInt()));
            standard_view_combo_->setCurrentIndex(0);
        });
    view_toolbar_->addWidget(standard_view_combo_);
    view_toolbar_->addAction(selection_action_);
    if (auto* selection_button = qobject_cast<QToolButton*>(
            view_toolbar_->widgetForAction(selection_action_))) {
        selection_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
    selection_filter_combo_ = new QComboBox(view_toolbar_);
    selection_filter_combo_->setObjectName("selectionFilterCombo");
    selection_filter_combo_->setToolTip(tr("Filtr prvků vybíraných ve 3D pohledu"));
    for (const auto& filter : {tr("Vše"), tr("Plochy"), tr("Body"),
                               tr("Osy"), tr("Roviny")}) {
        selection_filter_combo_->addItem(filter);
    }
    connect(selection_filter_combo_, &QComboBox::currentIndexChanged, this,
        [this] { if (viewer_ != nullptr && workspace_.size() != 0) refresh_scene(); });
    view_toolbar_->addWidget(selection_filter_combo_);
    view_toolbar_->addSeparator();
    for (auto* action : {wire_action_, hidden_edges_action_, no_hidden_edges_action_,
                         shaded_edges_action_, shaded_action_}) {
        view_toolbar_->addAction(action);
    }
    view_toolbar_->addSeparator();
    for (auto* action : {show_origins_action_, show_points_action_, show_axes_action_,
                         show_planes_action_, show_sketches_action_}) {
        view_toolbar_->addAction(action);
    }

    tools_toolbar_ = new QToolBar(tr("Nástroje"), this);
    tools_toolbar_->setObjectName("toolsToolbar");
    tools_toolbar_->setMovable(false);
    tools_toolbar_->setOrientation(Qt::Vertical);
    tools_toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tools_toolbar_->setMinimumWidth(158);
    tools_toolbar_->setIconSize(QSize(16, 16));
    tools_toolbar_->setStyleSheet(
        "QToolButton { padding:3px 6px; text-align:left; }"
        "QToolButton:checked { background-color:rgba(77,216,17,125);"
        " color:#fff; border:none; border-radius:4px; }"
        "QToolButton#applicationCommandButton:hover:enabled {"
        " background-color:rgba(77,216,17,90); color:#fff; border:none;"
        " border-radius:4px; }"
        "QToolButton#applicationCommandButton:pressed:enabled {"
        " background-color:rgba(77,216,17,165); color:#fff; border:none;"
        " border-radius:4px; }");
}

void AssemblyWorkspaceWindow::create_layout() {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    tabs_ = new QTabBar(central);
    tabs_->setObjectName("documentTabs");
    tabs_->setTabsClosable(true);
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
    tree_ = new QTreeWidget(document_splitter_);
    tree_->setObjectName("documentTree");
    auto tree_font = tree_->font();
    tree_font.setPixelSize(11);
    tree_->setFont(tree_font);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setColumnCount(1);
    tree_->setHeaderLabels({tr("DÍL")});
    tree_->setMinimumWidth(280);
    tree_->header()->setMinimumHeight(38);
    tree_->setStyleSheet(
        "QTreeWidget::item:selected, QTreeWidget::item:selected:active,"
        " QTreeWidget::item:selected:!active { background-color:#356E22;"
        " color:#fff; } QTreeWidget::item:hover { background-color:transparent; }");
    viewer_ = new zima::viewer::MeshView;
    viewer_->setObjectName("modelViewer");
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Dimension,
                                     zima::viewer::CandidateKind::Occurrence});
    viewer_->set_confirmation_callback([this](const auto& candidate) {
        if (extrusion_target_dialog_ != nullptr) {
            accept_extrusion_target(candidate);
            return;
        }
        if (edge_treatment_selection_) {
            accept_edge_treatment(candidate);
            return;
        }
        if (mate_selection_active_) {
            accept_mate_reference(candidate);
            return;
        }
        if (sketch_external_reference_active_) {
            accept_sketch_external_reference(candidate);
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
        if (sketch_segment_pair_active_) {
            accept_sketch_segment_pair(candidate);
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
        sketch_ellipse_major_dimension_action_->setEnabled(false);
        sketch_ellipse_minor_dimension_action_->setEnabled(false);
        sketch_ellipse_rotation_dimension_action_->setEnabled(false);
        selected_sketch_text_id_.clear();
        selected_sketch_external_reference_id_.clear();
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
            selected_sketch_elliptical_arc_id_.clear();
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
            selected_sketch_elliptical_arc_id_.clear();
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
            sketch_dimension_action_->setEnabled(false);
            sketch_dimension_x_action_->setEnabled(false);
            sketch_dimension_y_action_->setEnabled(false);
            sketch_angle_dimension_action_->setEnabled(false);
            sketch_radius_dimension_action_->setEnabled(false);
            sketch_diameter_dimension_action_->setEnabled(false);
            sketch_fix_point_action_->setEnabled(true);
            state_->setText(tr("Vybrán bod skici."));
        }
        sketch_trim_action_->setEnabled(
            !active_sketch_id_.empty() && !sketch_trim_active_);
        sketch_mirror_action_->setEnabled(
            !active_sketch_id_.empty() && !sketch_mirror_active_ &&
            !sketch_trim_active_);
    });
    viewer_->set_context_menu_callback(
        [this](const auto& candidate, const QPoint& global_position) {
            if (mate_selection_active_ || edge_treatment_selection_ ||
                extrusion_target_dialog_ != nullptr ||
                sketch_external_reference_active_ || sketch_trim_active_ ||
                sketch_mirror_active_ || sketch_coincident_active_ ||
                sketch_midpoint_active_ || sketch_symmetric_active_ ||
                sketch_concentric_active_ || sketch_tangent_active_ ||
                sketch_segment_pair_active_) return;
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
            if (candidate.kind != zima::viewer::CandidateKind::Occurrence) return;
            show_component_context_menu(candidate.instance_path, global_position);
    });
    viewer_->set_world_click_callback([this](const auto& origin, const auto& direction) {
        const auto [local_origin, local_direction] =
            active_part_local_ray(origin, direction);
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
        const auto [local_origin, local_direction] =
            active_part_local_ray(origin, direction);
        preview_sketch_segment_ray(local_origin, local_direction);
        preview_sketch_rectangle_ray(local_origin, local_direction);
        preview_sketch_polygon_ray(local_origin, local_direction);
        preview_sketch_circle_ray(local_origin, local_direction);
        preview_sketch_arc_ray(local_origin, local_direction);
        preview_sketch_ellipse_ray(local_origin, local_direction);
        preview_sketch_elliptical_arc_ray(local_origin, local_direction);
        preview_sketch_bspline_ray(local_origin, local_direction);
    });
    viewer_->set_command_gesture_callbacks(
        [this](const auto& candidate, const auto& origin, const auto& direction) {
            const auto [local_origin, local_direction] =
                active_part_local_ray(origin, direction);
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
        return finish_edge_treatment_selection() || finish_sketch_bspline() ||
            finish_sketch_polyline() || finish_sketch_mirror() ||
            finish_sketch_trim();
    });
    viewer_->set_double_confirmation_callback([this](const auto& candidate) {
        if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
            candidate.owner_id == active_sketch_id_ &&
            candidate.semantic_key.starts_with("dimension:")) {
            show_sketch_dimension_properties(
                active_sketch_id_, candidate.semantic_key.substr(10));
        } else if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
                   candidate.semantic_key.starts_with("mate:") &&
                   workspace_.open_assembly(candidate.owner_id) != nullptr) {
            show_mate_properties(candidate.owner_id, candidate.semantic_key.substr(5));
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
        }
    });
    viewer_->set_candidate_drag_callbacks(
        [this](const auto& candidate) {
            return begin_assembly_mate_drag(candidate) ||
                   begin_sketch_point_drag(candidate);
        },
        [this](const auto& origin, const auto& direction) {
            if (assembly_drag_document_) {
                update_assembly_mate_drag(origin, direction);
            } else {
                const auto [local_origin, local_direction] =
                    active_part_local_ray(origin, direction);
                update_sketch_point_drag(local_origin, local_direction);
            }
        },
        [this] {
            if (assembly_drag_document_) end_assembly_mate_drag();
            else end_sketch_point_drag();
        });
    model_workspace_ = viewer_;
    model_workspace_->setObjectName("modelWorkspace");
    workspace_stack_ = new QStackedWidget;
    workspace_stack_->setObjectName("workspaceStack");
    workspace_stack_->addWidget(model_workspace_);
    drawing_workspace_ = new DrawingWindow(&workspace_, false);
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
    connect(tabs_, &QTabBar::tabCloseRequested, this,
        [this](int index) { close_document(index); });
    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (index < 0) return;
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
    });
    connect(tree_, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item) {
            if (item == nullptr || item->parent() == nullptr) return;
            if (item->data(0, Qt::UserRole + 3).toString() == "assembly-mate") return;
            if (item->data(0, Qt::UserRole + 3).toString() == "part-sketch-dimension") return;
            if (item->data(0, Qt::UserRole + 3).toString() == "part-sketch") {
                active_sketch_id_ = item->data(0, Qt::UserRole).toString().toStdString();
                selected_sketch_id_ = active_sketch_id_;
                selected_sketch_segment_id_.clear();
                selected_sketch_point_id_.clear();
                selected_sketch_circle_id_.clear();
                selected_sketch_arc_id_.clear();
                selected_sketch_ellipse_id_.clear();
                selected_sketch_elliptical_arc_id_.clear();
                selected_sketch_bspline_id_.clear();
                selected_sketch_text_id_.clear();
                cancel_sketch_segment();
                sketch_normal_view_action_->setEnabled(true);
                sketch_external_reference_action_->setEnabled(true);
                sketch_point_action_->setEnabled(true);
                sketch_construction_action_->setEnabled(true);
                sketch_segment_action_->setEnabled(true);
                sketch_polyline_action_->setEnabled(true);
                sketch_rectangle_action_->setEnabled(true);
                sketch_polygon_action_->setEnabled(true);
                sketch_trim_action_->setEnabled(true);
                sketch_mirror_action_->setEnabled(true);
                sketch_circle_action_->setEnabled(true);
                sketch_arc_action_->setEnabled(true);
                sketch_ellipse_action_->setEnabled(true);
                sketch_elliptical_arc_action_->setEnabled(true);
                sketch_bspline_action_->setEnabled(true);
                sketch_text_action_->setEnabled(true);
                sketch_constraints_action_->setEnabled(true);
                sketch_dimensions_action_->setEnabled(true);
                sketch_coincident_action_->setEnabled(true);
                sketch_midpoint_action_->setEnabled(true);
                sketch_symmetric_action_->setEnabled(true);
                sketch_concentric_action_->setEnabled(true);
                sketch_tangent_action_->setEnabled(true);
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
                finish_sketch_action_->setEnabled(true);
                rebuild_application_toolbar();
                align_active_sketch_view();
                return;
            }
            if (item->data(0, Qt::UserRole + 3).toString() == "part-construction") {
                const auto* part = workspace_.open_part(workspace_.active_document_id());
                const auto* object = part == nullptr ? nullptr
                    : part->session.document().find_construction(
                        item->data(0, Qt::UserRole).toString().toStdString());
                if (object != nullptr) show_construction_properties(
                    object->kind, object->id);
                return;
            }
            if (item->data(0, Qt::UserRole + 3).toString() == "part-container") {
                viewer_->confirm_container(
                    item->data(0, Qt::UserRole).toString().toStdString());
            } else if (item->data(0, Qt::UserRole + 3).toString() ==
                       "part-construction") {
                return;
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
                const std::string path =
                    item->data(0, Qt::UserRole + 1).toString().toStdString();
                try {
                    if (workspace_.activate_occurrence(
                            workspace_.displayed_document_id(),
                            zima::assembly::InstancePath::decode(path))) {
                        active_occurrence_path_ = path;
                        refresh_tabs();
                        refresh_scene();
                    }
                } catch (const std::invalid_argument&) {
                    state_->setText(tr("Výskyt už v sestavě neexistuje."));
                }
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
            } else if (item->data(0, Qt::UserRole + 3).toString() ==
                       "part-construction") {
                const std::string id = item->data(0, Qt::UserRole).toString().toStdString();
                const auto* part = workspace_.open_part(workspace_.active_document_id());
                const auto* object = part == nullptr
                    ? nullptr : part->session.document().find_construction(id);
                if (object != nullptr) show_construction_properties(object->kind, id);
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

void AssemblyWorkspaceWindow::new_document() {
    if (properties_dialog_ != nullptr) {
        properties_dialog_->raise();
        return;
    }
    auto* dialog = new NewDocumentDialog(
        [this](QString type, QString stem) {
            return create_document(type, stem);
        }, this);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (properties_dialog_ == dialog) properties_dialog_ = nullptr;
    });
    dialog->show();
}

QString AssemblyWorkspaceWindow::create_document(
    const QString& document_type, const QString& file_stem) {
    const std::string name = file_stem.trimmed().toStdString();
    if (name.empty()) return tr("Zadejte název souboru.");
    const QString suffix = document_type == QStringLiteral("part")
        ? QStringLiteral(".prtz")
        : document_type == QStringLiteral("assembly")
            ? QStringLiteral(".asmz")
            : document_type == QStringLiteral("drawing")
                ? QStringLiteral(".drwz") : QString{};
    if (suffix.isEmpty()) return tr("Tento typ dokumentu zatím není podporován.");
    const std::filesystem::path path = working_directory_ /
        (name + suffix.toStdString());
    if (std::filesystem::exists(path) || workspace_.document_id_for_path(path)) {
        return tr("Soubor %1 již existuje nebo je otevřený.").arg(
            QString::fromStdString(path.filename().string()));
    }
    std::string id;
    try {
        if (document_type == QStringLiteral("part")) {
            auto document = zima::document::PartDocument::create_default();
            document.name = name;
            id = document.document_id;
            workspace_.add_part(std::move(document), {}, path);
            active_application_ = ApplicationMode::Modeling;
        } else if (document_type == QStringLiteral("assembly")) {
            auto document = zima::assembly::AssemblyDocument::create_default();
            document.name = name;
            id = document.document_id;
            workspace_.add_assembly(std::move(document), path);
            active_application_ = ApplicationMode::Assembly;
        } else {
            auto document = zima::drawing::DrawingDocument::create_default();
            document.name = name;
            id = document.document_id;
            workspace_.add_drawing(std::move(document), path);
            active_application_ = ApplicationMode::Drawing;
        }
    } catch (const std::exception& error) {
        return tr("Dokument nelze vytvořit: %1").arg(error.what());
    }
    workspace_.activate(id);
    workspace_.display_top_level(id);
    active_occurrence_path_.clear();
    active_sketch_id_.clear();
    selected_sketch_id_.clear();
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_text_id_.clear();
    cancel_sketch_segment();
    refresh_tabs();
    refresh_scene();
    return {};
}

void AssemblyWorkspaceWindow::close_document(int tab_index) {
    if (properties_dialog_ != nullptr) {
        state_->setText(tr("Nejprve dokončete nebo zrušte otevřené vlastnosti."));
        properties_dialog_->raise();
        return;
    }
    if (tab_index < 0) tab_index = tabs_->currentIndex();
    if (tab_index < 0 || tab_index >= tabs_->count()) return;
    const std::string id = tabs_->tabData(tab_index).toString().toStdString();
    const auto* state = workspace_.find(id);
    if (state == nullptr) return;
    const bool dirty = std::visit([](const auto& document) {
        using State = std::decay_t<decltype(document)>;
        if constexpr (std::is_same_v<State, zima::workspace::PartState> ||
                      std::is_same_v<State, zima::workspace::AssemblyState>) {
            return document.session.is_dirty();
        }
        return false;
    }, *state);
    if (dirty) {
        const auto answer = QMessageBox::warning(
            this, tr("Neuložené změny"),
            tr("Dokument obsahuje neuložené změny. Chcete je uložit?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (answer == QMessageBox::Cancel) return;
        if (answer == QMessageBox::Save) {
            workspace_.activate(id);
            workspace_.display_top_level(id);
            save_active_document();
            const auto* saved = workspace_.find(id);
            const bool still_dirty = saved != nullptr && std::visit([](const auto& document) {
                using State = std::decay_t<decltype(document)>;
                if constexpr (std::is_same_v<State, zima::workspace::PartState> ||
                              std::is_same_v<State, zima::workspace::AssemblyState>) {
                    return document.session.is_dirty();
                }
                return false;
            }, *saved);
            if (still_dirty) return;
        }
    }
    if (!workspace_.remove(id)) return;
    active_occurrence_path_.clear();
    active_sketch_id_.clear();
    selected_sketch_id_.clear();
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_text_id_.clear();
    cancel_sketch_segment();
    refresh_tabs();
    refresh_scene();
}

void AssemblyWorkspaceWindow::update_document_area_visibility() {
    const bool has_document = workspace_.size() != 0;
    if (tabs_ != nullptr) tabs_->setVisible(has_document);
    if (document_splitter_ != nullptr) document_splitter_->setVisible(has_document);
    save_action_->setEnabled(has_document);
    close_document_action_->setEnabled(has_document);
    regenerate_document_action_->setEnabled(has_document);
    fit_view_action_->setEnabled(has_document);
    selection_action_->setEnabled(has_document);
    selection_filter_combo_->setEnabled(has_document);
    for (auto* action : {wire_action_, hidden_edges_action_, no_hidden_edges_action_,
                         shaded_edges_action_, shaded_action_, show_origins_action_,
                         show_points_action_, show_axes_action_, show_planes_action_,
                         show_sketches_action_}) {
        action->setEnabled(has_document);
    }
    update_application_actions();
    if (!has_document && state_ != nullptr) state_->setText(tr("Připraveno."));
}

void AssemblyWorkspaceWindow::update_application_actions() {
    for (auto* action : application_actions_) action->setEnabled(false);
    if (workspace_.size() == 0) return;
    if (workspace_.open_drawing(workspace_.displayed_document_id()) != nullptr) {
        application_actions_[static_cast<std::size_t>(ApplicationMode::Drawing)]
            ->setEnabled(true);
        active_application_ = ApplicationMode::Drawing;
    } else if (workspace_.open_part(workspace_.active_document_id()) != nullptr) {
        for (const auto mode : {ApplicationMode::Modeling, ApplicationMode::SheetMetal,
                                ApplicationMode::Surface, ApplicationMode::Piping}) {
            application_actions_[static_cast<std::size_t>(mode)]->setEnabled(true);
        }
        if (!application_actions_[static_cast<std::size_t>(active_application_)]
                 ->isEnabled()) {
            active_application_ = ApplicationMode::Modeling;
        }
    } else if (workspace_.open_assembly(workspace_.active_document_id()) != nullptr) {
        for (const auto mode : {ApplicationMode::Modeling, ApplicationMode::Assembly,
                                ApplicationMode::SheetMetal, ApplicationMode::Surface,
                                ApplicationMode::Piping}) {
            application_actions_[static_cast<std::size_t>(mode)]->setEnabled(true);
        }
        if (!application_actions_[static_cast<std::size_t>(active_application_)]
                 ->isEnabled()) {
            active_application_ = ApplicationMode::Assembly;
        }
    }
    application_actions_[static_cast<std::size_t>(active_application_)]->setChecked(true);
}

void AssemblyWorkspaceWindow::set_active_application(ApplicationMode mode) {
    const auto index = static_cast<std::size_t>(mode);
    if (index >= application_actions_.size() ||
        !application_actions_[index]->isEnabled()) {
        update_application_actions();
        return;
    }
    active_application_ = mode;
    application_actions_[index]->setChecked(true);
    rebuild_application_toolbar();
}

void AssemblyWorkspaceWindow::rebuild_application_toolbar() {
    if (tools_toolbar_ == nullptr) return;
    tools_toolbar_->clear();
    const bool drawing =
        workspace_.open_drawing(workspace_.displayed_document_id()) != nullptr;
    const QString heading_text = drawing ? tr("Výkres")
        : !active_sketch_id_.empty() ? tr("Skica")
        : active_application_ == ApplicationMode::Modeling ? tr("Modelování")
        : active_application_ == ApplicationMode::Assembly ? tr("Sestava")
        : active_application_ == ApplicationMode::SheetMetal ? tr("Plech")
        : active_application_ == ApplicationMode::Surface ? tr("Plochy")
        : active_application_ == ApplicationMode::Piping ? tr("Potrubí")
        : tr("Výkres");
    auto* heading = new QLabel(heading_text, tools_toolbar_);
    heading->setAlignment(Qt::AlignCenter);
    heading->setFont(tree_->font());
    heading->setStyleSheet(QStringLiteral("font-weight:600; padding:3px;"));
    tools_toolbar_->addWidget(heading);
    const auto add_green_separator = [this] {
        auto* separator = new QWidget(tools_toolbar_);
        separator->setObjectName("greenToolbarSeparator");
        separator->setFixedHeight(1);
        separator->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        separator->setStyleSheet(
            "QWidget#greenToolbarSeparator { background:#4DD811; border:none; }");
        tools_toolbar_->addWidget(separator);
    };
    const auto add_command = [this](QAction* action) {
        if (action == nullptr) return;
        tools_toolbar_->addAction(action);
        if (auto* button = qobject_cast<QToolButton*>(
                tools_toolbar_->widgetForAction(action))) {
            button->setObjectName("applicationCommandButton");
            if (action->menu() != nullptr) {
                button->setPopupMode(QToolButton::InstantPopup);
            }
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            button->setMinimumWidth(std::max(0, tools_toolbar_->minimumWidth() - 12));
        }
    };
    add_green_separator();
    auto* spacing = new QWidget(tools_toolbar_);
    spacing->setFixedHeight(5);
    tools_toolbar_->addWidget(spacing);

    if (workspace_.size() == 0) return;
    if (drawing) {
        if (auto* drawing_toolbar =
                drawing_workspace_->findChild<QToolBar*>("drawingToolbar")) {
            for (auto* action : drawing_toolbar->actions()) {
                if (action->isSeparator()) tools_toolbar_->addSeparator();
                else {
                    if (action->objectName() == "insertDrawingViewAction" ||
                        action->objectName() == "projectDrawingViewAction") {
                        action->setIcon(resource_icon("drawing"));
                    } else if (action->objectName() == "drawingDimensionAction") {
                        action->setIcon(resource_icon("sketch-dimensions"));
                    }
                    add_command(action);
                }
            }
        }
        return;
    }

    const bool editing_nested_document =
        workspace_.open_assembly(workspace_.displayed_document_id()) != nullptr &&
        workspace_.active_document_id() != workspace_.displayed_document_id();
    if (editing_nested_document) {
        auto* return_action = new QAction(
            resource_icon("assembly"), tr("Zpět do sestavy"), tools_toolbar_);
        connect(return_action, &QAction::triggered, this, [this] {
            const std::string displayed = workspace_.displayed_document_id();
            if (workspace_.open_assembly(displayed) == nullptr) return;
            workspace_.activate(displayed);
            active_occurrence_path_.clear();
            active_sketch_id_.clear();
            selected_sketch_id_.clear();
            active_application_ = ApplicationMode::Assembly;
            refresh_tabs();
            refresh_scene();
        });
        add_command(return_action);
        tools_toolbar_->addSeparator();
    }

    if (!active_sketch_id_.empty()) {
        add_command(sketch_normal_view_action_);
        add_command(selection_action_);
        add_command(sketch_external_reference_action_);
        tools_toolbar_->addSeparator();
        add_command(sketch_trim_action_);
        add_command(sketch_mirror_action_);
        add_green_separator();
        for (auto* action : {sketch_point_action_, sketch_construction_action_,
                             sketch_segment_action_, sketch_polyline_action_,
                             sketch_rectangle_action_, sketch_polygon_action_,
                             sketch_circle_action_, sketch_arc_action_,
                             sketch_ellipse_action_, sketch_elliptical_arc_action_,
                             sketch_bspline_action_}) {
            add_command(action);
        }
        add_green_separator();
        add_command(sketch_constraints_action_);
        add_green_separator();
        add_command(sketch_dimensions_action_);
        add_command(sketch_text_action_);
        add_green_separator();
        add_command(finish_sketch_action_);
        if (auto* button = qobject_cast<QToolButton*>(
                tools_toolbar_->widgetForAction(finish_sketch_action_))) {
            button->setObjectName("finishSketchButton");
            button->setStyleSheet(QStringLiteral(
                "QToolButton#finishSketchButton {"
                " background:#2F8F3A; color:white; border:1px solid #63C66D;"
                " border-radius:3px; padding:6px; font-weight:600; }"
                "QToolButton#finishSketchButton:hover { background:#3AA94A; }"
                "QToolButton#finishSketchButton:pressed { background:#267631; }"));
        }
        return;
    }

    if (active_application_ == ApplicationMode::Modeling) {
        add_command(selection_action_);
        tools_toolbar_->addSeparator();
        for (auto* action : {construction_point_action_, construction_axis_action_,
                             construction_plane_action_, sketch_action_}) {
            add_command(action);
        }
        add_green_separator();
        add_command(extrusion_action_);
        add_command(revolution_action_);
        add_green_separator();
        add_command(fillet_action_);
        add_command(chamfer_action_);
        add_green_separator();
        for (auto* action : {box_action_, sphere_action_, cylinder_action_, cone_action_,
                             pyramid_action_, wedge_action_}) {
            add_command(action);
        }
        return;
    }
    if (active_application_ == ApplicationMode::Assembly) {
        add_command(selection_action_);
        tools_toolbar_->addSeparator();
        add_command(insert_action_);
        add_green_separator();
        for (auto* action : {plane_mate_action_, axis_mate_action_, point_mate_action_,
                             angle_mate_action_, plane_angle_mate_action_}) {
            add_command(action);
        }
        tools_toolbar_->addSeparator();
        add_command(regenerate_action_);
        return;
    }
    auto* placeholder = new QAction(
        active_application_ == ApplicationMode::SheetMetal
            ? tr("Příkazy plechu – připravuje se")
        : active_application_ == ApplicationMode::Surface
            ? tr("Příkazy ploch – připravuje se")
            : tr("Příkazy potrubí – připravuje se"), tools_toolbar_);
    placeholder->setEnabled(false);
    add_command(placeholder);
}

void AssemblyWorkspaceWindow::regenerate_active_document() {
    if (workspace_.open_part(workspace_.active_document_id()) != nullptr) {
        regenerate_active_part();
    } else if (workspace_.open_assembly(workspace_.displayed_document_id()) != nullptr) {
        regenerate_assembly();
    } else if (workspace_.open_drawing(workspace_.displayed_document_id()) != nullptr) {
        if (auto* action = drawing_workspace_->findChild<QAction*>(
                "regenerateDrawingViewAction"); action != nullptr && action->isEnabled()) {
            action->trigger();
        }
    }
}

void AssemblyWorkspaceWindow::new_part() {
    static_cast<void>(create_document(QStringLiteral("part"), tr("Nový díl")));
}

void AssemblyWorkspaceWindow::new_assembly() {
    static_cast<void>(create_document(
        QStringLiteral("assembly"), tr("Nová sestava")));
}

void AssemblyWorkspaceWindow::new_drawing() {
    static_cast<void>(create_document(
        QStringLiteral("drawing"), tr("Nový výkres")));
}

void AssemblyWorkspaceWindow::open_document() {
    const QString path = open_file(this, tr("Otevřít dokument"),
        QString::fromStdString(working_directory_.string()),
        tr("Dokument ZIMA-CAD (*.prtz *.asmz *.drwz)"));
    if (path.isEmpty()) return;
    static_cast<void>(open_document_path(path));
}

bool AssemblyWorkspaceWindow::open_document_path(const QString& path) {
    try {
        std::string id;
        if (path.endsWith(".prtz", Qt::CaseInsensitive)) {
            std::vector<zima::kernel::BodyResult> calculated;
            auto document = zima::document::PartDocument::load(
                path.toStdString(), &calculated);
            id = document.document_id;
            workspace_.add_part(
                std::move(document), std::move(calculated), path.toStdString());
        } else if (path.endsWith(".asmz", Qt::CaseInsensitive)) {
            auto document = zima::assembly::AssemblyDocument::load(path.toStdString());
            id = document.document_id;
            workspace_.add_assembly(std::move(document), path.toStdString());
        } else if (path.endsWith(".drwz", Qt::CaseInsensitive)) {
            auto document = zima::drawing::DrawingDocument::load(path.toStdString());
            id = document.document_id;
            workspace_.add_drawing(std::move(document), path.toStdString());
        } else {
            throw std::runtime_error("Nepodporovaná přípona dokumentu ZIMA-CAD.");
        }
        workspace_.activate(id);
        workspace_.display_top_level(id);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Otevření dokumentu selhalo"), error.what());
        return false;
    }
    const std::filesystem::path opened_path = path.toStdString();
    if (!opened_path.parent_path().empty()) {
        working_directory_ = opened_path.parent_path();
    }
    active_occurrence_path_.clear();
    active_sketch_id_.clear();
    selected_sketch_id_.clear();
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_text_id_.clear();
    cancel_sketch_segment();
    refresh_tabs();
    refresh_scene();
    return true;
}

bool AssemblyWorkspaceWindow::has_insertable_component() const {
    const std::string owner_id = workspace_.displayed_document_id();
    if (workspace_.open_assembly(owner_id) == nullptr) return false;
    for (const auto& state : workspace_.documents()) {
        const bool available = std::visit([&](const auto& item) {
            using State = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<State, zima::workspace::PartState>) {
                return item.session.document().document_id != owner_id &&
                    !item.session.document().history.empty() &&
                    !item.session.calculated_boundaries().empty();
            } else if constexpr (
                std::is_same_v<State, zima::workspace::AssemblyState>) {
                return item.session.document().document_id != owner_id;
            }
            return false;
        }, state);
        if (available) return true;
    }
    return false;
}

void AssemblyWorkspaceWindow::rebuild_insert_menu() {
    insert_menu_->clear();
    const std::string owner_id = workspace_.displayed_document_id();
    if (workspace_.open_assembly(owner_id) == nullptr) return;
    for (const auto& state : workspace_.documents()) {
        std::visit([&](const auto& item) {
            using State = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<State, zima::workspace::PartState>) {
                const auto& document = item.session.document();
                if (document.document_id == owner_id) return;
                const bool calculated = !document.history.empty() &&
                    !item.session.calculated_boundaries().empty();
                auto* action = insert_menu_->addAction(
                    QString::fromStdString(document.name) +
                    (calculated ? tr(" — Part") : tr(" — Part (není vypočtený)")));
                action->setObjectName("insertSourceAction");
                action->setEnabled(calculated);
                if (!calculated) {
                    action->setToolTip(tr(
                        "Part musí obsahovat alespoň jeden potvrzený a vypočtený prvek."));
                }
                connect(action, &QAction::triggered, this,
                    [this, id = document.document_id] { insert_component(id); });
            } else if constexpr (
                std::is_same_v<State, zima::workspace::AssemblyState>) {
                const auto& document = item.session.document();
                if (document.document_id == owner_id) return;
                auto* action = insert_menu_->addAction(
                    QString::fromStdString(document.name) + tr(" — sestava"));
                action->setObjectName("insertSourceAction");
                connect(action, &QAction::triggered, this,
                    [this, id = document.document_id] { insert_component(id); });
            }
        }, state);
    }
    if (insert_menu_->actions().empty()) {
        auto* empty = insert_menu_->addAction(tr("Není otevřený žádný zdrojový dokument"));
        empty->setEnabled(false);
    }
}

void AssemblyWorkspaceWindow::insert_component(
    const std::string& source_document_id) {
    const std::string assembly_id = workspace_.displayed_document_id();
    if (workspace_.open_assembly(assembly_id) == nullptr) return;
    try {
        std::string occurrence_id;
        if (const auto* part = workspace_.open_part(source_document_id)) {
            occurrence_id = workspace_.insert_open_part(
                assembly_id, source_document_id, part->session.document().name);
        } else if (const auto* source = workspace_.open_assembly(source_document_id)) {
            occurrence_id = workspace_.insert_open_assembly(
                assembly_id, source_document_id, source->session.document().name);
        } else {
            return;
        }
        workspace_.activate(assembly_id);
        refresh_tabs();
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
        std::set<std::string> regenerated_parts;
        std::set<std::string> visiting_parts;
        const std::function<void(const std::string&)> regenerate_part_dependencies =
            [&](const std::string& part_id) {
                if (regenerated_parts.contains(part_id)) return;
                if (!visiting_parts.insert(part_id).second) {
                    throw std::runtime_error(
                        "External Sketch document dependency cycle detected");
                }
                auto* part = workspace_.open_part(part_id);
                if (part == nullptr) {
                    visiting_parts.erase(part_id);
                    return;
                }
                for (const auto& sketch : part->session.document().sketches) {
                    for (const auto& reference : sketch.external_references) {
                        if (reference.context_assembly_document_id == id &&
                            reference.source_document_id != part_id) {
                            regenerate_part_dependencies(
                                reference.source_document_id);
                        }
                    }
                }
                auto next = part->session.document();
                const bool has_context = std::any_of(
                    next.sketches.begin(), next.sketches.end(), [&](const auto& sketch) {
                        return std::any_of(sketch.external_references.begin(),
                            sketch.external_references.end(), [&](const auto& reference) {
                                return reference.context_assembly_document_id == id;
                            });
                    });
                if (has_context) {
                    auto calculated = calculate_part(next);
                    const bool references_changed =
                        refresh_sketch_external_references(next, calculated) |
                        workspace_.refresh_context_external_references(next);
                    if (references_changed) calculated = calculate_part(next);
                    if (references_changed) {
                        part->session.commit(std::move(next), std::move(calculated));
                    } else {
                        part->session.update_calculated_boundaries(
                            std::move(calculated));
                    }
                }
                visiting_parts.erase(part_id);
                regenerated_parts.insert(part_id);
            };
        for (const auto& state : workspace_.documents()) {
            const auto* part = std::get_if<zima::workspace::PartState>(&state);
            if (part != nullptr) {
                regenerate_part_dependencies(
                    part->session.document().document_id);
            }
        }
        workspace_.regenerate_assembly_from_open_dependencies(id);
        refresh_tabs();
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

void AssemblyWorkspaceWindow::start_point_mate() {
    if (properties_dialog_ != nullptr ||
        workspace_.open_assembly(workspace_.active_document_id()) == nullptr ||
        workspace_.open_assembly(workspace_.displayed_document_id()) == nullptr) return;
    pending_mate_reference_.reset();
    mate_selection_active_ = true;
    pending_mate_kind_ = zima::assembly::MateKind::PointCoincident;
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Vertex});
    state_->setText(tr("Vyberte pohyblivý bod původního solidu."));
}

void AssemblyWorkspaceWindow::start_angle_mate() {
    if (properties_dialog_ != nullptr ||
        workspace_.open_assembly(workspace_.active_document_id()) == nullptr ||
        workspace_.open_assembly(workspace_.displayed_document_id()) == nullptr) return;
    pending_mate_reference_.reset();
    mate_selection_active_ = true;
    pending_mate_kind_ = zima::assembly::MateKind::AxisAngle;
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Axis});
    state_->setText(tr("Vyberte pohyblivou osu pro úhlovou vazbu."));
}

void AssemblyWorkspaceWindow::start_plane_angle_mate() {
    if (properties_dialog_ != nullptr ||
        workspace_.open_assembly(workspace_.active_document_id()) == nullptr ||
        workspace_.open_assembly(workspace_.displayed_document_id()) == nullptr) return;
    pending_mate_reference_.reset();
    mate_selection_active_ = true;
    pending_mate_kind_ = zima::assembly::MateKind::PlaneAngle;
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Face});
    state_->setText(tr("Vyberte pohyblivou rovinnou plochu pro úhlovou vazbu."));
}

void AssemblyWorkspaceWindow::start_edge_treatment(
    zima::document::FeatureKind kind) {
    if (properties_dialog_ != nullptr ||
        (kind != zima::document::FeatureKind::Fillet &&
         kind != zima::document::FeatureKind::Chamfer)) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || part->session.document().history.empty()) return;
    if (workspace_.open_assembly(workspace_.displayed_document_id()) != nullptr &&
        !resolve_active_occurrence(part->session.document().document_id)) {
        state_->setText(tr(
            "Fillet/Chamfer vyžaduje přesně aktivovaný výskyt Partu."));
        return;
    }
    edge_treatment_selection_ = kind;
    pending_edge_treatment_edges_.clear();
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Edge});
    const auto active_occurrence = resolve_active_occurrence(
        part->session.document().document_id);
    viewer_->set_candidate_filter(
        [expected_path = active_occurrence.value_or(std::string{})](
            const auto& candidate) {
            return candidate.kind == zima::viewer::CandidateKind::Edge &&
                candidate.geometry ==
                    zima::viewer::CandidateGeometry::OriginalReference &&
                candidate.instance_path == expected_path;
        });
    state_->setText(kind == zima::document::FeatureKind::Fillet
        ? tr("Vyberte jednu nebo více původních hran. Krátké MMB výběr dokončí.")
        : tr("Vyberte jednu nebo více původních hran. Krátké MMB výběr dokončí."));
}

void AssemblyWorkspaceWindow::accept_edge_treatment(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!edge_treatment_selection_) return;
    if (candidate.kind != zima::viewer::CandidateKind::Edge ||
        candidate.geometry != zima::viewer::CandidateGeometry::OriginalReference ||
        candidate.owner_id.empty() || candidate.semantic_key.empty()) {
        state_->setText(tr("Vyberte hranu původního solidu aktivního Partu."));
        return;
    }
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    const auto* assembly =
        workspace_.open_assembly(workspace_.displayed_document_id());
    if (part == nullptr) return;
    if (assembly == nullptr) {
        if (!candidate.instance_path.empty()) {
            state_->setText(tr("Vyberte hranu aktivního Partu."));
            return;
        }
    } else {
        const auto active_occurrence = resolve_active_occurrence(
            part->session.document().document_id);
        if (!active_occurrence || candidate.instance_path != *active_occurrence) {
            state_->setText(tr(
                "Vyberte hranu přesného aktivního výskytu Partu."));
            return;
        }
    }
    const zima::kernel::EdgeReference edge{candidate.owner_id, candidate.semantic_key, {}};
    if (std::find(pending_edge_treatment_edges_.begin(),
                  pending_edge_treatment_edges_.end(), edge) ==
        pending_edge_treatment_edges_.end()) {
        pending_edge_treatment_edges_.push_back(edge);
    }
    state_->setText(tr("Vybrané hrany: %1. Krátké MMB otevře vlastnosti.")
        .arg(pending_edge_treatment_edges_.size()));
}

bool AssemblyWorkspaceWindow::finish_edge_treatment_selection() {
    if (!edge_treatment_selection_) return false;
    if (pending_edge_treatment_edges_.empty()) {
        state_->setText(tr("Nejprve vyberte alespoň jednu hranu."));
        return true;
    }
    const auto kind = *edge_treatment_selection_;
    edge_treatment_selection_.reset();
    auto initial = kind == zima::document::FeatureKind::Fillet
        ? zima::document::PartDocument::create_fillet_container(
            pending_edge_treatment_edges_)
        : zima::document::PartDocument::create_chamfer_container(
            pending_edge_treatment_edges_);
    pending_edge_treatment_edges_.clear();
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return true;
    const std::string part_id = part->session.document().document_id;
    auto* dialog = new PrimitivePropertiesDialog(
        initial, false, false,
        [this, part_id](zima::document::HistoryContainer committed) {
            auto* target = workspace_.open_part(part_id);
            if (target == nullptr) throw std::runtime_error("Part is no longer open");
            auto next = target->session.document();
            next.history.push_back(std::move(committed));
            auto calculated = calculate_part(next);
            static_cast<void>(refresh_sketch_external_references(next, calculated));
            target->session.commit(std::move(next), std::move(calculated));
        }, this);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        edge_treatment_selection_.reset();
        refresh_tabs();
        refresh_scene();
    });
    dialog->show();
    return true;
}

void AssemblyWorkspaceWindow::accept_extrusion_target(
    const zima::viewer::ViewerCandidate& candidate) {
    if (extrusion_target_dialog_ == nullptr ||
        candidate.kind != zima::viewer::CandidateKind::Face ||
        candidate.geometry != zima::viewer::CandidateGeometry::OriginalReference ||
        candidate.owner_id.empty() || candidate.semantic_key.empty()) {
        state_->setText(tr("Vyberte rovinnou plochu nebo konstrukční rovinu Partu."));
        return;
    }
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto active_occurrence = resolve_active_occurrence(
        part->session.document().document_id);
    if (!active_occurrence || candidate.instance_path != *active_occurrence) {
        state_->setText(tr(
            "Vyberte plochu přesného aktivního výskytu Partu."));
        return;
    }
    zima::kernel::Vec3 origin;
    zima::kernel::Vec3 normal;
    bool resolved = false;
    std::vector<zima::kernel::Vec3> surface_triangles;
    if (const auto* construction =
            part->session.document().find_construction(candidate.owner_id);
        construction != nullptr &&
        construction->kind == zima::document::ConstructionKind::Plane) {
        origin = construction->origin;
        normal = construction->direction;
        resolved = true;
    } else if (!part->session.calculated_boundaries().empty()) {
        const auto& references = part->session.calculated_boundaries().back()
            .mesh.original_references;
        for (std::size_t triangle = 0;
             triangle < references.triangle_references.size(); ++triangle) {
            const auto& reference = references.triangle_references[triangle];
            if (reference.owner_id != candidate.owner_id ||
                reference.semantic_key != candidate.semantic_key) continue;
            const auto first = references.vertices[
                references.triangles[triangle * 3]];
            const auto second = references.vertices[
                references.triangles[triangle * 3 + 1]];
            const auto third = references.vertices[
                references.triangles[triangle * 3 + 2]];
            const zima::kernel::Vec3 a{second.x - first.x, second.y - first.y,
                                       second.z - first.z};
            const zima::kernel::Vec3 b{third.x - first.x, third.y - first.y,
                                       third.z - first.z};
            normal = {a.y * b.z - a.z * b.y,
                      a.z * b.x - a.x * b.z,
                      a.x * b.y - a.y * b.x};
            const double length = std::sqrt(normal.x * normal.x +
                                            normal.y * normal.y +
                                            normal.z * normal.z);
            if (length <= 1e-12) continue;
            normal = {normal.x / length, normal.y / length, normal.z / length};
            origin = first;
            resolved = true;
            for (std::size_t other = 0;
                 other < references.triangle_references.size(); ++other) {
                if (references.triangle_references[other] != reference) continue;
                for (int corner = 0; corner < 3; ++corner) {
                    const auto& point = references.vertices[
                        references.triangles[other * 3 + corner]];
                    surface_triangles.push_back(point);
                    const double distance = (point.x - origin.x) * normal.x +
                        (point.y - origin.y) * normal.y +
                        (point.z - origin.z) * normal.z;
                    if (std::abs(distance) > 1e-6) resolved = false;
                }
            }
            break;
        }
    }
    if (!resolved && !surface_triangles.empty()) {
        extrusion_target_dialog_->set_extrusion_surface_target(
            {candidate.owner_id, candidate.semantic_key, {}},
            std::move(surface_triangles));
        extrusion_target_dialog_ = nullptr;
        state_->setText(tr("Zakřivená cílová plocha vytažení byla nastavena."));
        return;
    }
    if (!resolved) {
        state_->setText(tr("Vybraná plocha není rovinná."));
        return;
    }
    extrusion_target_dialog_->set_extrusion_target(
        {candidate.owner_id, candidate.semantic_key, {}}, origin, normal);
    extrusion_target_dialog_ = nullptr;
    state_->setText(tr("Cílová plocha vytažení byla nastavena."));
}

std::optional<zima::assembly::MateReference>
AssemblyWorkspaceWindow::local_mate_reference(
    const zima::viewer::ViewerCandidate& candidate) const {
    const bool face = candidate.kind == zima::viewer::CandidateKind::Face &&
        candidate.geometry == zima::viewer::CandidateGeometry::OriginalReference &&
        (pending_mate_kind_ == zima::assembly::MateKind::PlaneCoincident ||
         pending_mate_kind_ == zima::assembly::MateKind::PlaneAngle);
    const bool axis = candidate.kind == zima::viewer::CandidateKind::Axis &&
        candidate.geometry == zima::viewer::CandidateGeometry::OriginalReference &&
        (pending_mate_kind_ == zima::assembly::MateKind::AxisCoincident ||
         pending_mate_kind_ == zima::assembly::MateKind::AxisAngle);
    const bool point = candidate.kind == zima::viewer::CandidateKind::Vertex &&
        candidate.geometry == zima::viewer::CandidateGeometry::OriginalReference &&
        pending_mate_kind_ == zima::assembly::MateKind::PointCoincident;
    if ((!face && !axis && !point) ||
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
             : axis ? zima::assembly::MateReferenceKind::Axis
                    : zima::assembly::MateReferenceKind::Point,
        std::move(path),
        candidate.owner_id, candidate.semantic_key};
}

void AssemblyWorkspaceWindow::accept_mate_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    auto reference = local_mate_reference(candidate);
    if (!reference) {
        state_->setText(tr("Reference nepatří do aktivní sestavy."));
        return;
    }
    if (!pending_mate_reference_) {
        pending_mate_reference_ = std::move(reference);
        viewer_->clear_selection();
        state_->setText((pending_mate_kind_ == zima::assembly::MateKind::PlaneCoincident ||
                         pending_mate_kind_ == zima::assembly::MateKind::PlaneAngle)
            ? tr("Vyberte pevnou referenční rovinnou plochu.")
            : pending_mate_kind_ == zima::assembly::MateKind::AxisCoincident ||
              pending_mate_kind_ == zima::assembly::MateKind::AxisAngle
                ? tr("Vyberte pevnou referenční osu.")
                : tr("Vyberte pevný referenční bod."));
        return;
    }
    try {
        auto mate = zima::assembly::AssemblyDocument::create_mate(
            pending_mate_kind_ == zima::assembly::MateKind::PlaneCoincident
                ? "Plocha na plochu"
                : pending_mate_kind_ == zima::assembly::MateKind::PlaneAngle
                    ? "Úhel ploch"
                : pending_mate_kind_ == zima::assembly::MateKind::AxisCoincident
                    ? "Osa na osu"
                    : pending_mate_kind_ == zima::assembly::MateKind::AxisAngle
                        ? "Úhel os" : "Bod na bod",
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
        viewer_->set_selection_contract({zima::viewer::CandidateKind::Dimension,
                                         zima::viewer::CandidateKind::Occurrence});
        QMessageBox::warning(this, tr("Vazbu nelze vytvořit"), error.what());
        refresh_scene();
    }
}

void AssemblyWorkspaceWindow::save_active_assembly() {
    auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    if (assembly == nullptr) return;
    QString path = QString::fromStdString(assembly->path.string());
    if (path.isEmpty()) path = save_file(
        this, tr("Uložit sestavu"),
        QString::fromStdString((working_directory_ / "assembly.asmz").string()),
        tr("ZIMA-CAD sestava (*.asmz)"), "asmz");
    if (path.isEmpty()) return;
    if (!path.endsWith(".asmz", Qt::CaseInsensitive)) path += ".asmz";
    try {
        assembly->session.document().save(path.toStdString());
        assembly->path = path.toStdString();
        working_directory_ = assembly->path.parent_path();
        assembly->session.mark_saved();
        refresh_tabs();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Uložení sestavy selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::save_active_document() {
    if(auto* drawing=workspace_.open_drawing(workspace_.active_document_id())) {
        QString path=QString::fromStdString(drawing->path.string());
        if(path.isEmpty()) path=save_file(
            this,tr("Uložit výkres"),
            QString::fromStdString((working_directory_ / "drawing.drwz").string()),
            tr("ZIMA-CAD Výkres (*.drwz)"),"drwz");
        if(path.isEmpty()) return;
        if(!path.endsWith(".drwz", Qt::CaseInsensitive)) path += ".drwz";
        try { drawing->document.save(path.toStdString()); drawing->path=path.toStdString();
            working_directory_ = drawing->path.parent_path();
            drawing_workspace_->edit_workspace_document(drawing->document.document_id);
            refresh_tabs(); }
        catch(const std::exception& error) {
            QMessageBox::critical(this,tr("Uložení výkresu selhalo"),error.what()); }
        return;
    }
    if (workspace_.open_assembly(workspace_.active_document_id()) != nullptr) {
        save_active_assembly();
        return;
    }
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    QString path = QString::fromStdString(part->path.string());
    if (path.isEmpty()) path = save_file(
        this, tr("Uložit díl"),
        QString::fromStdString((working_directory_ / "part.prtz").string()),
        tr("ZIMA-CAD díl (*.prtz)"), "prtz");
    if (path.isEmpty()) return;
    if (!path.endsWith(".prtz", Qt::CaseInsensitive)) path += ".prtz";
    try {
        part->session.document().save(path.toStdString(),
                                      part->session.calculated_boundaries());
        part->path = path.toStdString();
        working_directory_ = part->path.parent_path();
        part->session.mark_saved();
        refresh_tabs();
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Uložení Partu selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::save_active_document_as() {
    const std::string document_id = workspace_.active_document_id();
    if (document_id.empty()) return;
    std::filesystem::path current_path;
    QString caption;
    QString fallback_name;
    QString filter;
    QString suffix;
    if (const auto* drawing = workspace_.open_drawing(document_id)) {
        current_path = drawing->path;
        caption = tr("Uložit výkres jako");
        fallback_name = QString::fromStdString(drawing->document.name) + ".drwz";
        filter = tr("ZIMA-CAD výkres (*.drwz)");
        suffix = "drwz";
    } else if (const auto* assembly = workspace_.open_assembly(document_id)) {
        current_path = assembly->path;
        caption = tr("Uložit sestavu jako");
        fallback_name = QString::fromStdString(assembly->session.document().name) + ".asmz";
        filter = tr("ZIMA-CAD sestava (*.asmz)");
        suffix = "asmz";
    } else if (const auto* part = workspace_.open_part(document_id)) {
        current_path = part->path;
        caption = tr("Uložit díl jako");
        fallback_name = QString::fromStdString(part->session.document().name) + ".prtz";
        filter = tr("ZIMA-CAD díl (*.prtz)");
        suffix = "prtz";
    } else {
        return;
    }
    const QString initial = current_path.empty()
        ? QString::fromStdString((working_directory_ /
              fallback_name.toStdString()).string())
        : QString::fromStdString(current_path.string());
    QString selected = save_file(this, caption, initial, filter, suffix);
    if (selected.isEmpty()) return;
    const QString dotted_suffix = QStringLiteral(".") + suffix;
    if (!selected.endsWith(dotted_suffix, Qt::CaseInsensitive)) {
        selected += dotted_suffix;
    }
    const std::filesystem::path target = selected.toStdString();
    if (const auto owner = workspace_.document_id_for_path(target);
        owner && *owner != document_id) {
        QMessageBox::warning(
            this, tr("Soubor je již otevřen"),
            tr("Cílový soubor již používá jiný otevřený dokument."));
        return;
    }
    try {
        if (auto* drawing = workspace_.open_drawing(document_id)) {
            drawing->document.save(target);
            drawing->path = target;
            drawing_workspace_->edit_workspace_document(document_id);
        } else if (auto* assembly = workspace_.open_assembly(document_id)) {
            assembly->session.document().save(target);
            assembly->path = target;
            assembly->session.mark_saved();
        } else if (auto* part = workspace_.open_part(document_id)) {
            part->session.document().save(
                target, part->session.calculated_boundaries());
            part->path = target;
            part->session.mark_saved();
        }
        if (!target.parent_path().empty()) working_directory_ = target.parent_path();
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("Dokument uložen jako %1").arg(selected));
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Uložení jako selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::set_working_directory() {
    const QString selected = choose_directory(
        this, tr("Nastavit pracovní adresář"),
        QString::fromStdString(working_directory_.string()));
    if (selected.isEmpty()) return;
    const std::filesystem::path target = selected.toStdString();
    if (!std::filesystem::is_directory(target)) {
        QMessageBox::warning(this, tr("Neplatný adresář"),
            tr("Vybraná cesta není existující adresář."));
        return;
    }
    working_directory_ = target;
    state_->setText(tr("Pracovní adresář: %1").arg(selected));
}

void AssemblyWorkspaceWindow::show_about() {
    if (properties_dialog_ != nullptr) {
        properties_dialog_->raise();
        return;
    }
    auto* dialog = new AboutSubWindow(this);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::import_file() {
    const QString path = open_file(this, tr("Importovat"),
        QString::fromStdString(working_directory_.string()),
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
    if (format == zima::interchange::Format::Dxf) {
        auto* part = workspace_.open_part(workspace_.active_document_id());
        if (part == nullptr || active_sketch_id_.empty()) {
            QMessageBox::information(this, tr("Umístění DXF"),
                tr("Nejprve vytvořte nebo aktivujte skicu, která určí rovinu DXF."));
            return;
        }
        try {
            auto next = part->session.document();
            auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
                [&](const auto& value) { return value.id == active_sketch_id_; });
            if (sketch == next.sketches.end()) return;
            const auto imported = zima::interchange::import_dxf(
                path.toStdString(), *sketch);
            part->session.commit(std::move(next), part->session.calculated_boundaries());
            refresh_tabs();
            refresh_scene();
            state_->setText(tr("DXF importováno: %1 entit, blok %2")
                .arg(imported.imported_entities)
                .arg(QString::fromStdString(imported.import_block_id)));
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Import DXF selhal"), error.what());
        }
        return;
    }
    if (format == zima::interchange::Format::Step) {
        auto* part = workspace_.open_part(workspace_.active_document_id());
        if (part == nullptr) {
            if (workspace_.open_assembly(workspace_.active_document_id()) != nullptr) {
                try { import_step_into_assembly(path.toStdString()); }
                catch (const std::exception& error) {
                    QMessageBox::warning(this, tr("Import STEP selhal"), error.what());
                }
            }
            return;
        }
        try {
            auto next = part->session.document();
            const auto absolute = std::filesystem::absolute(path.toStdString());
            const auto imported_parts = zima::interchange::inspect_step_parts(absolute);
            if (imported_parts.empty()) {
                throw std::runtime_error("STEP neobsahuje žádný samostatný díl");
            }
            std::size_t part_count{};
            for (const auto& imported : imported_parts) {
                if (imported.assembly) continue;
                auto container =
                    zima::document::PartDocument::create_imported_step_container(
                        absolute, imported.definition_id, imported.name);
                container.placement = {imported.global_x, imported.global_y,
                    imported.global_z, imported.global_rotation_x,
                    imported.global_rotation_y, imported.global_rotation_z};
                next.history.push_back(std::move(container));
                ++part_count;
            }
            if (part_count == 0) throw std::runtime_error("STEP neobsahuje žádný díl");
            auto calculated = calculate_part(next);
            static_cast<void>(refresh_sketch_external_references(next, calculated));
            part->session.commit(std::move(next), std::move(calculated));
            refresh_tabs();
            refresh_scene();
            state_->setText(tr("STEP importován: %1 samostatných kontejnerů")
                .arg(part_count));
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Import STEP selhal"), error.what());
        }
        return;
    }
    state_->setText(tr("Importní soubor připraven: %1").arg(path));
}

void AssemblyWorkspaceWindow::import_step_into_assembly(
    const std::filesystem::path& source_path) {
    const std::string target_id = workspace_.active_document_id();
    if (workspace_.open_assembly(target_id) == nullptr) {
        throw std::runtime_error("Aktivní dokument není sestava");
    }
    const auto source = std::filesystem::absolute(source_path);
    const auto nodes = zima::interchange::inspect_step_parts(source);
    if (nodes.empty()) throw std::runtime_error("STEP neobsahuje produktovou strukturu");
    const auto safe_name = [](std::string name) {
        for (auto& value : name) {
            const unsigned char byte = static_cast<unsigned char>(value);
            if (!std::isalnum(byte) && value != '-' && value != '_') value = '_';
        }
        return name.empty() ? std::string{"step"} : name;
    };
    auto* target_state = workspace_.open_assembly(target_id);
    const auto base = target_state->path.empty() ? source.parent_path()
        : target_state->path.parent_path();
    const auto output_directory = base / (safe_name(source.stem().string()) + "_zima");
    std::filesystem::create_directories(output_directory);

    std::vector<std::string> part_definitions;
    std::unordered_map<std::string, const zima::interchange::StepPart*> part_nodes;
    for (const auto& node : nodes) if (!node.assembly && !part_nodes.contains(node.definition_id)) {
        part_nodes.emplace(node.definition_id, &node);
        part_definitions.push_back(node.definition_id);
    }
    std::vector<zima::kernel::StepRequest> requests;
    for (const auto& definition : part_definitions) requests.push_back({source.string(), definition});
    const auto calculated = kernel_.import_step_components(requests);
    if (calculated.size() != part_definitions.size()) {
        throw std::runtime_error("STEP díly nebyly vypočteny kompletně");
    }
    std::unordered_map<std::string, std::string> source_documents;
    std::unordered_map<std::string, std::filesystem::path> source_paths;
    for (std::size_t index = 0; index < part_definitions.size(); ++index) {
        const auto* node = part_nodes.at(part_definitions[index]);
        auto document = zima::document::PartDocument::create_default();
        document.name = node->name;
        auto container = zima::document::PartDocument::create_imported_step_container(
            source, node->definition_id, node->name);
        container.id = "step-import:" + std::to_string(index);
        document.history.push_back(std::move(container));
        const auto path = output_directory /
            (safe_name(node->name) + "_" + std::to_string(index + 1) + ".prtz");
        document.save(path, {calculated[index]});
        source_documents[node->definition_id] = document.document_id;
        source_paths[node->definition_id] = path;
        workspace_.add_part(std::move(document), {calculated[index]}, path);
    }

    std::vector<const zima::interchange::StepPart*> assemblies;
    std::unordered_map<std::string, const zima::interchange::StepPart*> assembly_nodes;
    for (const auto& node : nodes) if (node.assembly && !assembly_nodes.contains(node.definition_id)) {
        assembly_nodes.emplace(node.definition_id, &node);
        assemblies.push_back(&node);
    }
    std::ranges::sort(assemblies, [](const auto* left, const auto* right) {
        return std::ranges::count(left->component_path, '/') >
               std::ranges::count(right->component_path, '/');
    });
    for (const auto* assembly_node : assemblies) {
        auto document = zima::assembly::AssemblyDocument::create_default();
        document.name = assembly_node->name;
        for (const auto& child : nodes) {
            if (child.parent_path != assembly_node->component_path) continue;
            const auto id = source_documents.find(child.definition_id);
            if (id == source_documents.end()) continue;
            zima::assembly::PartOccurrence occurrence;
            if (child.assembly) {
                const auto* child_document = workspace_.open_assembly(id->second);
                occurrence = zima::assembly::AssemblyDocument::create_assembly_occurrence(
                    child.name, id->second, source_paths.at(child.definition_id),
                    child_document->session.document());
            } else {
                const auto* child_document = workspace_.open_part(id->second);
                occurrence = zima::assembly::AssemblyDocument::create_part_occurrence(
                    child.name, id->second, source_paths.at(child.definition_id),
                    child_document->session.calculated_boundaries().back());
            }
            occurrence.placement = {child.x, child.y, child.z,
                child.rotation_x, child.rotation_y, child.rotation_z};
            document.components.push_back(std::move(occurrence));
        }
        const auto path = output_directory /
            (safe_name(assembly_node->name) + "_assembly_" +
             std::to_string(source_documents.size() + 1) + ".asmz");
        document.save(path);
        source_documents[assembly_node->definition_id] = document.document_id;
        source_paths[assembly_node->definition_id] = path;
        workspace_.add_assembly(std::move(document), path);
    }

    auto next = workspace_.open_assembly(target_id)->session.document();
    for (const auto& root : nodes) {
        if (!root.parent_path.empty()) continue;
        const auto id = source_documents.find(root.definition_id);
        if (id == source_documents.end()) continue;
        zima::assembly::PartOccurrence occurrence;
        if (root.assembly) {
            const auto* generated = workspace_.open_assembly(id->second);
            occurrence = zima::assembly::AssemblyDocument::create_assembly_occurrence(
                root.name, id->second, source_paths.at(root.definition_id),
                generated->session.document());
        } else {
            const auto* generated = workspace_.open_part(id->second);
            occurrence = zima::assembly::AssemblyDocument::create_part_occurrence(
                root.name, id->second, source_paths.at(root.definition_id),
                generated->session.calculated_boundaries().back());
        }
        occurrence.placement = {root.x, root.y, root.z,
            root.rotation_x, root.rotation_y, root.rotation_z};
        next.components.push_back(std::move(occurrence));
    }
    workspace_.open_assembly(target_id)->session.commit(std::move(next));
    refresh_tabs();
    refresh_scene();
    state_->setText(tr("STEP sestava importována: %1 unikátních Partů, %2 podsestav")
        .arg(part_definitions.size()).arg(assemblies.size()));
}

void AssemblyWorkspaceWindow::export_file() {
    const QString path = save_file(this, tr("Exportovat"),
        QString::fromStdString(working_directory_.string()),
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
    if (format == zima::interchange::Format::Dxf) {
        const auto* part = workspace_.open_part(workspace_.active_document_id());
        if (part == nullptr) return;
        const auto sketch = std::find_if(
            part->session.document().sketches.begin(),
            part->session.document().sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == part->session.document().sketches.end()) return;
        try {
            zima::interchange::export_dxf(path.toStdString(), *sketch);
            state_->setText(tr("Aktivní skica exportována do DXF: %1").arg(path));
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Export DXF selhal"), error.what());
        }
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
    const std::string source_sketch_id = !active_sketch_id_.empty()
        ? active_sketch_id_ : selected_sketch_id_;
    if (container_id.empty() &&
        (feature_kind == zima::document::FeatureKind::Extrusion ||
         feature_kind == zima::document::FeatureKind::Revolution) &&
        source_sketch_id.empty()) {
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
    const auto rollback_boundary = edit_mode
        ? part->session.rollback_boundary(container_id)
        : std::optional<zima::document::HistoryRollbackBoundary>{};
    if (edit_mode) {
        if (!rollback_boundary) {
            QMessageBox::warning(this, tr("Chybí vypočtený vstup"),
                tr("Prvek nelze editovat bez uložené geometrie jeho vstupu. "
                   "Nejprve explicitně regenerujte Part."));
            return;
        }
        rollback_occurrence = resolve_active_occurrence(document.document_id);
        if (!rollback_occurrence) {
            QMessageBox::warning(
                this, tr("Nejednoznačný výskyt"),
                tr("Nejprve aktivujte přesný výskyt Partu ve stromu sestavy."));
            return;
        }
    }
    if (!edit_mode && (feature_kind == zima::document::FeatureKind::Fillet ||
                       feature_kind == zima::document::FeatureKind::Chamfer ||
                       feature_kind == zima::document::FeatureKind::ImportedStep)) return;
    const auto initial = edit_mode ? *edited
        : feature_kind == zima::document::FeatureKind::Cylinder
            ? zima::document::PartDocument::create_cylinder_container()
        : feature_kind == zima::document::FeatureKind::Sphere
            ? zima::document::PartDocument::create_sphere_container()
        : feature_kind == zima::document::FeatureKind::Cone
            ? zima::document::PartDocument::create_cone_container()
        : feature_kind == zima::document::FeatureKind::Pyramid
            ? zima::document::PartDocument::create_pyramid_container()
        : feature_kind == zima::document::FeatureKind::Wedge
            ? zima::document::PartDocument::create_wedge_container()
        : feature_kind == zima::document::FeatureKind::Extrusion
            ? zima::document::PartDocument::create_extrusion_container(source_sketch_id)
        : feature_kind == zima::document::FeatureKind::Revolution
            ? zima::document::PartDocument::create_revolution_container(source_sketch_id)
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
                if (*target == committed) return;
                *target = std::move(committed);
            } else {
                next.history.push_back(std::move(committed));
            }
            auto calculated = calculate_part(next);
            static_cast<void>(refresh_sketch_external_references(next, calculated));
            target_part->session.commit(std::move(next), std::move(calculated));
        }, this);
    if (feature_kind == zima::document::FeatureKind::Extrusion) {
        dialog->set_extrusion_target_request([this, dialog] {
            extrusion_target_dialog_ = dialog;
            viewer_->set_selection_contract({zima::viewer::CandidateKind::Face});
            const auto* part =
                workspace_.open_part(workspace_.active_document_id());
            const auto expected_path = part == nullptr
                ? std::optional<std::string>{}
                : resolve_active_occurrence(part->session.document().document_id);
            viewer_->set_candidate_filter(
                [path = expected_path.value_or(std::string{})](const auto& candidate) {
                    return candidate.kind == zima::viewer::CandidateKind::Face &&
                        candidate.geometry ==
                            zima::viewer::CandidateGeometry::OriginalReference &&
                        candidate.instance_path == path;
                });
            state_->setText(tr("Vyberte cílovou rovinnou plochu ve view."));
        });
        dialog->set_preview_callback([this, part_id](const auto& preview) {
            const auto* preview_part = workspace_.open_part(part_id);
            if (preview_part == nullptr) return;
            try {
                viewer_->set_transient_edges(
                    preview_part->session.document().extrusion_preview_edges(preview));
                state_->setText(tr("Azurový drát zobrazuje náhled vytažení."));
            } catch (const std::exception& error) {
                viewer_->set_transient_edges({});
                state_->setText(QString::fromUtf8(error.what()));
            }
        });
    }
    properties_dialog_ = dialog;
    if (rollback_boundary) {
        part_rollback_ = PartRollbackContext{
            document.document_id, *rollback_occurrence,
            rollback_boundary->history_index, rollback_boundary->input_body};
        refresh_scene();
    }
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        extrusion_target_dialog_ = nullptr;
        viewer_->set_transient_edges({});
        part_rollback_.reset();
        refresh_tabs();
        refresh_scene();
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::show_construction_properties(
    zima::document::ConstructionKind kind, const std::string& object_id) {
    if (properties_dialog_ != nullptr) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto* edited = object_id.empty() ? nullptr
        : part->session.document().find_construction(object_id);
    if (!object_id.empty() && (edited == nullptr || edited->kind != kind)) return;
    const bool edit_mode = edited != nullptr;
    const auto initial = edit_mode ? *edited
        : zima::document::PartDocument::create_construction(kind);
    const std::string part_id = part->session.document().document_id;
    auto* dialog = new ConstructionPropertiesDialog(
        initial, edit_mode,
        [this, part_id, edit_mode](zima::document::ConstructionObject committed) {
            auto* target_part = workspace_.open_part(part_id);
            if (target_part == nullptr) throw std::runtime_error("Part is no longer open");
            auto next = target_part->session.document();
            if (edit_mode) {
                auto* target = next.find_construction(committed.id);
                if (target == nullptr) {
                    throw std::runtime_error("Construction object no longer exists");
                }
                *target = std::move(committed);
            } else {
                next.constructions.push_back(std::move(committed));
            }
            auto calculated = target_part->session.calculated_boundaries();
            static_cast<void>(refresh_sketch_external_references(next, calculated));
            target_part->session.commit(std::move(next), std::move(calculated));
        }, this);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
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
            selected_sketch_id_ = committed.id;
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
            auto calculated = target_part->session.calculated_boundaries();
            static_cast<void>(refresh_sketch_external_references(next, calculated));
            target_part->session.commit(std::move(next), std::move(calculated));
        }, this);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        refresh_tabs();
        refresh_scene();
        if (!active_sketch_id_.empty()) align_active_sketch_view();
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::show_sketch_bspline_properties(
    const std::string& sketch_id, const std::string& bspline_id) {
    if (properties_dialog_ != nullptr || sketch_bspline_active_) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
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

void AssemblyWorkspaceWindow::show_sketch_text_properties(
    const std::string& sketch_id, const std::string& text_id) {
    if (properties_dialog_ != nullptr || sketch_id.empty() ||
        sketch_id != active_sketch_id_) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == sketch_id; });
    if (sketch == part->session.document().sketches.end()) return;

    const bool edit_mode = !text_id.empty();
    zima::sketcher::SketchText initial;
    std::optional<std::array<double, 2>> anchor;
    if (edit_mode) {
        const auto text = std::find_if(sketch->texts.begin(), sketch->texts.end(),
            [&](const auto& value) { return value.id == text_id; });
        if (text == sketch->texts.end()) return;
        initial = *text;
        anchor = std::array{text->anchor_x, text->anchor_y};
    } else {
        initial = zima::sketcher::Sketch::create_text();
    }

    cancel_sketch_segment();
    sketch_text_active_ = true;
    editing_sketch_text_id_ = text_id;
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_text_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_text_id_ = text_id;

    const std::string part_id = part->session.document().document_id;
    auto* dialog = new SketchTextPropertiesDialog(
        std::move(initial), anchor,
        [this, part_id, sketch_id](
            const std::optional<zima::sketcher::SketchText>& preview) {
            if (!preview) {
                viewer_->set_transient_edges({});
                return;
            }
            const auto* target_part = workspace_.open_part(part_id);
            if (target_part == nullptr) return;
            const auto target_sketch = std::find_if(
                target_part->session.document().sketches.begin(),
                target_part->session.document().sketches.end(),
                [&](const auto& value) { return value.id == sketch_id; });
            if (target_sketch == target_part->session.document().sketches.end()) return;
            viewer_->set_transient_edges(
                sketch_text_preview_edges(*target_sketch, *preview));
        },
        [this, part_id, sketch_id, edit_mode](
            zima::sketcher::SketchText committed) {
            auto* target_part = workspace_.open_part(part_id);
            if (target_part == nullptr) {
                throw std::runtime_error("Part is no longer open");
            }
            auto next = target_part->session.document();
            const auto target_sketch = std::find_if(
                next.sketches.begin(), next.sketches.end(),
                [&](const auto& value) { return value.id == sketch_id; });
            if (target_sketch == next.sketches.end()) {
                throw std::runtime_error("Sketch no longer exists");
            }
            const std::string committed_id = committed.id;
            if (edit_mode) target_sketch->update_text(std::move(committed));
            else target_sketch->add_text(std::move(committed));
            target_part->session.commit(
                std::move(next), target_part->session.calculated_boundaries());
            selected_sketch_text_id_ = committed_id;
            state_->setText(edit_mode
                ? tr("Text skici byl upraven jako jedna Part revize.")
                : tr("Text skici byl vytvořen jako jedna Part revize."));
        }, this);
    properties_dialog_ = dialog;
    sketch_text_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (properties_dialog_ == dialog) properties_dialog_ = nullptr;
        if (sketch_text_dialog_ == dialog) sketch_text_dialog_ = nullptr;
        sketch_text_active_ = false;
        editing_sketch_text_id_.clear();
        viewer_->set_transient_edges({});
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
    });
    preserve_view_on_refresh_ = true;
    refresh_scene();
    dialog->show();
    state_->setText(edit_mode
        ? tr("Upravte text; OK změnu uloží a Cancel obnoví původní stav.")
        : tr("Text skici: klikněte na polohu, potom potvrďte OK."));
}

void AssemblyWorkspaceWindow::set_sketch_external_reference_mode(bool enabled) {
    const bool was_active = sketch_external_reference_active_;
    if (enabled) {
        if (properties_dialog_ != nullptr || active_sketch_id_.empty()) {
            enabled = false;
        } else {
            const auto* part = workspace_.open_part(workspace_.active_document_id());
            if (part == nullptr ||
                std::none_of(part->session.document().sketches.begin(),
                    part->session.document().sketches.end(), [&](const auto& sketch) {
                        return sketch.id == active_sketch_id_;
                    })) {
                enabled = false;
            }
        }
    }
    if (enabled) {
        cancel_sketch_segment();
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
        sketch_external_reference_action_->setChecked(enabled);
    }
    if (enabled || was_active) {
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(enabled
            ? tr("Externí reference: vyberte persistovanou původní plochu, "
                 "hranu, vrchol nebo osu. Pravým tlačítkem lze přepínat kandidáty.")
            : tr("Režim externích referencí byl ukončen."));
    }
}

void AssemblyWorkspaceWindow::accept_sketch_external_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_external_reference_active_ ||
        candidate.geometry != zima::viewer::CandidateGeometry::OriginalReference ||
        (candidate.kind != zima::viewer::CandidateKind::Edge &&
         candidate.kind != zima::viewer::CandidateKind::Vertex &&
         candidate.kind != zima::viewer::CandidateKind::Axis &&
         candidate.kind != zima::viewer::CandidateKind::Face)) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    try {
        auto next = part->session.document();
        const auto sketch = std::find_if(
            next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == next.sketches.end()) return;
        auto reference = zima::sketcher::Sketch::create_external_reference(
            candidate.kind == zima::viewer::CandidateKind::Edge
                ? zima::sketcher::ExternalReferenceKind::Edge
                : candidate.kind == zima::viewer::CandidateKind::Axis
                    ? zima::sketcher::ExternalReferenceKind::Axis
                    : candidate.kind == zima::viewer::CandidateKind::Face
                        ? zima::sketcher::ExternalReferenceKind::Face
                        : zima::sketcher::ExternalReferenceKind::Point);
        const auto* assembly =
            workspace_.open_assembly(workspace_.displayed_document_id());
        std::optional<zima::assembly::InstancePath> dependent_path;
        std::optional<zima::assembly::InstancePath> source_path;
        std::string source_document_id = next.document_id;
        zima::kernel::ViewerReferenceGeometry source_geometry;
        if (assembly == nullptr) {
            if (!candidate.instance_path.empty() ||
                !sketch_external_reference_source_owners(next, active_sketch_id_)
                    .contains(candidate.owner_id)) {
                throw std::invalid_argument(
                    "Geometry is not a valid source before the active Sketch");
            }
            source_geometry = sketch_external_reference_source_geometry(
                next, part->session.calculated_boundaries());
        } else {
            const auto dependent_encoded = resolve_active_occurrence(next.document_id);
            if (!dependent_encoded || dependent_encoded->empty() ||
                candidate.instance_path.empty()) {
                throw std::invalid_argument(
                    "In-context reference requires exact occurrence paths");
            }
            dependent_path = zima::assembly::InstancePath::decode(*dependent_encoded);
            source_path = zima::assembly::InstancePath::decode(
                candidate.instance_path);
            const auto source_address = workspace_.resolve_occurrence(
                assembly->session.document().document_id, *source_path);
            if (!source_address || source_address->source_kind !=
                    zima::assembly::ComponentSourceKind::Part) {
                throw std::invalid_argument(
                    "External reference source must be an exact Part occurrence");
            }
            if (*source_path == *dependent_path) {
                if (!sketch_external_reference_source_owners(next, active_sketch_id_)
                        .contains(candidate.owner_id)) {
                    throw std::invalid_argument(
                        "Geometry is not a valid source before the active Sketch");
                }
                source_geometry = sketch_external_reference_source_geometry(
                    next, part->session.calculated_boundaries());
                source_path.reset();
            } else {
                source_document_id = source_address->source_document_id;
                source_geometry =
                    workspace_.authoritative_external_reference_geometry(
                        assembly->session.document().document_id,
                        *dependent_path, source_document_id);
            }
        }
        reference.source_document_id = source_document_id;
        reference.source_owner_id = candidate.owner_id;
        reference.source_semantic_key = candidate.semantic_key;
        reference.source_instance_path = source_path
            ? source_path->encoded() : std::string{};
        if (assembly != nullptr && dependent_path && source_path) {
            for (const auto& existing_sketch : next.sketches) {
                for (const auto& existing : existing_sketch.external_references) {
                    if (existing.context_assembly_document_id.empty()) continue;
                    if (existing.context_assembly_document_id !=
                            assembly->session.document().document_id ||
                        existing.context_instance_path != dependent_path->encoded()) {
                        throw std::invalid_argument(
                            "Part already owns external references from another occurrence context");
                    }
                }
            }
            reference.context_assembly_document_id =
                assembly->session.document().document_id;
            reference.context_instance_path = dependent_path->encoded();
        }
        populate_external_reference_cache(*sketch, reference, source_geometry);
        sketch->add_external_reference(std::move(reference));
        if (assembly != nullptr && dependent_path && source_path) {
            workspace_.add_external_sketch_dependency(
                assembly->session.document().document_id,
                *dependent_path, *source_path);
        }
        part->session.commit(
            std::move(next), part->session.calculated_boundaries());
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr(
            "Externí reference byla uložena bez volání OCCT. Vyberte další zdroj."));
    } catch (const std::exception& error) {
        state_->setText(tr("Externí referenci nelze vytvořit: %1")
            .arg(QString::fromUtf8(error.what())));
    }
}

void AssemblyWorkspaceWindow::align_active_sketch_view() {
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (viewer_ == nullptr || part == nullptr || active_sketch_id_.empty()) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return;
    zima::kernel::Vec3 direction =
        sketch->plane == zima::sketcher::SketchPlane::XY
            ? zima::kernel::Vec3{0.0, 0.0, 1.0}
            : sketch->plane == zima::sketcher::SketchPlane::XZ
                ? zima::kernel::Vec3{0.0, -1.0, 0.0}
                : zima::kernel::Vec3{1.0, 0.0, 0.0};
    if (workspace_.open_assembly(workspace_.displayed_document_id()) != nullptr) {
        const auto occurrence = resolve_active_occurrence(
            part->session.document().document_id);
        if (!occurrence || occurrence->empty()) return;
        direction = workspace_.occurrence_direction_to_scene(
            workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(*occurrence), direction);
    }
    viewer_->set_view_direction(direction);
    viewer_->fit_all();
    state_->setText(tr("Pohled je kolmý k rovině aktivní skici."));
}

bool AssemblyWorkspaceWindow::accept_sketch_text_ray(
    const zima::kernel::Vec3& origin,
    const zima::kernel::Vec3& direction) {
    if (!sketch_text_active_ || sketch_text_dialog_ == nullptr ||
        !editing_sketch_text_id_.empty()) return false;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || active_sketch_id_.empty()) return false;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    sketch_text_dialog_->set_anchor((*position)[0], (*position)[1]);
    state_->setText(tr("Poloha textu určena. Upravte parametry a potvrďte OK."));
    return true;
}

void AssemblyWorkspaceWindow::finish_active_sketch() {
    if (active_sketch_id_.empty() || properties_dialog_ != nullptr) return;
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
    state_->setText(tr("Skica dokončena. Je vybrána jako zdroj pro modelovací operace."));
}

void AssemblyWorkspaceWindow::start_sketch_point() {
    if (properties_dialog_ != nullptr) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto found = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
    if (found == part->session.document().sketches.end()) return;
    cancel_sketch_segment();
    sketch_point_active_ = true;
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
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto found = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
    if (found == part->session.document().sketches.end()) return;
    cancel_sketch_segment();
    sketch_segment_active_ = true;
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
    state_->setText(tr(
        "Lomená čára: určete první bod. Prostřední tlačítko ukončí aktuální řetězec."));
}

void AssemblyWorkspaceWindow::cancel_sketch_segment() {
    sketch_external_reference_active_ = false;
    selected_sketch_external_reference_id_.clear();
    if (sketch_external_reference_action_ != nullptr) {
        const QSignalBlocker blocker(sketch_external_reference_action_);
        sketch_external_reference_action_->setChecked(false);
    }
    sketch_point_active_ = false;
    sketch_segment_active_ = false;
    sketch_segment_construction_ = false;
    sketch_polyline_active_ = false;
    sketch_rectangle_active_ = false;
    sketch_polygon_active_ = false;
    cancel_sketch_trim();
    sketch_mirror_active_ = false;
    sketch_mirror_selecting_sources_ = false;
    sketch_circle_active_ = false;
    sketch_arc_active_ = false;
    sketch_ellipse_active_ = false;
    sketch_elliptical_arc_active_ = false;
    sketch_bspline_active_ = false;
    sketch_coincident_active_ = false;
    sketch_midpoint_active_ = false;
    sketch_symmetric_active_ = false;
    sketch_concentric_active_ = false;
    sketch_tangent_active_ = false;
    sketch_segment_pair_active_ = false;
    pending_segment_start_.reset();
    pending_rectangle_corner_.reset();
    pending_polygon_center_.reset();
    pending_mirror_geometry_ids_.clear();
    pending_mirror_axis_id_.clear();
    pending_circle_center_.reset();
    pending_arc_center_.reset();
    pending_arc_start_.reset();
    pending_ellipse_center_.reset();
    pending_ellipse_major_.reset();
    pending_elliptical_arc_center_.reset();
    pending_elliptical_arc_major_.reset();
    pending_elliptical_arc_minor_.reset();
    pending_elliptical_arc_start_.reset();
    pending_elliptical_arc_reversed_ = false;
    pending_bspline_points_.clear();
    pending_coincident_point_id_.clear();
    pending_midpoint_point_id_.clear();
    pending_symmetric_point_ids_.clear();
    pending_concentric_geometry_id_.clear();
    pending_tangent_geometry_id_.clear();
    pending_tangent_reference_is_segment_ = false;
    pending_tangent_reference_supports_curve_pair_ = false;
    pending_pair_geometry_id_.clear();
    pending_pair_reference_is_circular_ = false;
    viewer_->set_transient_edges({});
}

bool AssemblyWorkspaceWindow::finish_sketch_polyline() {
    if (!sketch_polyline_active_) return false;
    pending_segment_start_.reset();
    viewer_->set_transient_edges({});
    state_->setText(tr(
        "Řetězec lomené čáry dokončen. Kliknutím začnete nový řetězec."));
    return true;
}

void AssemblyWorkspaceWindow::start_sketch_rectangle() {
    if (properties_dialog_ != nullptr) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
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
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    state_->setText(tr("Obdélník skici: určete první roh. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_rectangle() {
    sketch_rectangle_active_ = false;
    pending_rectangle_corner_.reset();
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_polygon(unsigned sides) {
    if (properties_dialog_ != nullptr ||
        (sides != 4 && sides != 6 && sides != 8)) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto found = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
    if (found == part->session.document().sketches.end()) return;
    cancel_sketch_segment();
    sketch_polygon_active_ = true;
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
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return;
    cancel_sketch_segment();
    sketch_trim_preview_ = *sketch;
    sketch_trim_topology_ = zima::sketcher::sketch_trim_topology(
        *sketch_trim_preview_, true);
    sketch_trim_active_ = true;
    sketch_trim_changed_ = false;
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_point_id_.clear();
    selection_action_->setChecked(true);
    viewer_->clear_selection();
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
            "Ořezání je pouze v náhledu. Pokračujte, prostřední klik vše "
            "potvrdí a Escape obnoví původní skicu."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
}

bool AssemblyWorkspaceWindow::finish_sketch_trim() {
    if (!sketch_trim_active_) return false;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || !sketch_trim_preview_) return true;
    if (!sketch_trim_changed_) {
        cancel_sketch_trim();
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(tr("Ořezání ukončeno beze změny."));
        return true;
    }
    try {
        auto next = part->session.document();
        const auto sketch = std::find_if(
            next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == next.sketches.end()) return true;
        *sketch = *sketch_trim_preview_;
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        cancel_sketch_trim();
        viewer_->clear_selection();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("Ořezání skici bylo potvrzeno jako jedna Part revize."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
    return true;
}

void AssemblyWorkspaceWindow::start_sketch_mirror() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const std::string source_id = !selected_sketch_segment_id_.empty()
        ? selected_sketch_segment_id_
        : !selected_sketch_circle_id_.empty() ? selected_sketch_circle_id_
        : !selected_sketch_arc_id_.empty() ? selected_sketch_arc_id_
        : !selected_sketch_ellipse_id_.empty() ? selected_sketch_ellipse_id_
        : !selected_sketch_elliptical_arc_id_.empty()
            ? selected_sketch_elliptical_arc_id_
        : !selected_sketch_bspline_id_.empty() ? selected_sketch_bspline_id_
        : selected_sketch_point_id_;
    cancel_sketch_segment();
    sketch_mirror_active_ = true;
    sketch_mirror_selecting_sources_ = source_id.empty();
    if (!source_id.empty()) pending_mirror_geometry_ids_ = {source_id};
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
    state_->setText(sketch_mirror_selecting_sources_
        ? tr("Zrcadlení: vybírejte geometrii a body; prostředním kliknutím výběr dokončete.")
        : tr("Zrcadlení: vyberte úsečku nebo osu X/Y skici. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_mirror() {
    sketch_mirror_active_ = false;
    sketch_mirror_selecting_sources_ = false;
    pending_mirror_geometry_ids_.clear();
    pending_mirror_axis_id_.clear();
}

void AssemblyWorkspaceWindow::accept_sketch_mirror_source(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_mirror_active_ || !sketch_mirror_selecting_sources_ ||
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
    if (!sketch_mirror_active_ || pending_mirror_geometry_ids_.empty() ||
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
    state_->setText(tr(
        "Osa zrcadlení vybrána. Krátkým prostředním kliknutím zrcadlení potvrďte."));
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
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return false;
    try {
        auto next = part->session.document();
        const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == next.sketches.end()) return false;
        static_cast<void>(sketch->mirror_geometry(
            pending_mirror_geometry_ids_, pending_mirror_axis_id_));
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        cancel_sketch_mirror();
        viewer_->clear_selection();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("Geometrie skici byla zrcadlena jako jedna Part revize."));
        return true;
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
        return true;
    }
}

void AssemblyWorkspaceWindow::start_sketch_circle() {
    if (properties_dialog_ != nullptr) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
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
    selected_sketch_elliptical_arc_id_.clear();
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
    if (part == nullptr) return;
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
    selected_sketch_elliptical_arc_id_.clear();
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
    if (part == nullptr) return;
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
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    state_->setText(tr("Elipsa skici: určete střed. Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::cancel_sketch_ellipse() {
    sketch_ellipse_active_ = false;
    pending_ellipse_center_.reset();
    pending_ellipse_major_.reset();
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_elliptical_arc() {
    if (properties_dialog_ != nullptr) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto found = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
    if (found == part->session.document().sketches.end()) return;
    cancel_sketch_segment();
    sketch_elliptical_arc_active_ = true;
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
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_bspline() {
    if (properties_dialog_ != nullptr) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
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
    selected_sketch_elliptical_arc_id_.clear();
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
    if (!sketch_bspline_active_ || part == nullptr || active_sketch_id_.empty()) {
        return false;
    }
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

bool AssemblyWorkspaceWindow::accept_sketch_point_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (!sketch_point_active_ || part == nullptr || active_sketch_id_.empty()) {
        return false;
    }
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    try {
        auto next = part->session.document();
        const auto target = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (target == next.sketches.end()) return true;
        const auto previous_size = target->points.size();
        static_cast<void>(target->add_point((*position)[0], (*position)[1]));
        if (target->points.size() == previous_size) {
            state_->setText(tr("V této poloze již bod skici existuje."));
            return true;
        }
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("Bod vytvořen. Kliknutím můžete vytvořit další."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
    return true;
}

bool AssemblyWorkspaceWindow::accept_sketch_segment_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (!sketch_segment_active_ || part == nullptr || active_sketch_id_.empty()) {
        return false;
    }
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_segment_start_) {
        pending_segment_start_ = *position;
        state_->setText(sketch_polyline_active_
            ? tr("Lomená čára: určete další bod.")
            : sketch_segment_construction_
                ? tr("Konstrukční čára: určete druhý bod.")
                : tr("Úsečka skici: určete druhý bod. Escape příkaz zruší."));
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
        (*position)[0], (*position)[1], 1.0e-6,
        sketch_segment_construction_));
    const auto calculated = part->session.calculated_boundaries();
    part->session.commit(std::move(next), calculated);
    preserve_view_on_refresh_ = true;
    if (sketch_polyline_active_) pending_segment_start_ = *position;
    else pending_segment_start_.reset();
    viewer_->set_transient_edges({});
    refresh_tabs();
    refresh_scene();
    state_->setText(sketch_polyline_active_
        ? tr("Úsek lomené čáry vytvořen. Určete další bod nebo řetězec ukončete prostředním tlačítkem.")
        : sketch_segment_construction_
            ? tr("Konstrukční čára vytvořena. Určete první bod další čáry.")
            : tr("Úsečka vytvořena. Kliknutím určete první bod další úsečky."));
    return true;
}

void AssemblyWorkspaceWindow::constrain_selected_segment(
    zima::sketcher::ConstraintKind kind) {
    if (sketch_segment_active_ || sketch_rectangle_active_ || sketch_polygon_active_ ||
        sketch_mirror_active_ || sketch_circle_active_ || sketch_arc_active_ ||
        sketch_ellipse_active_ || sketch_elliptical_arc_active_ ||
        sketch_bspline_active_ || sketch_coincident_active_ || sketch_midpoint_active_ ||
        sketch_symmetric_active_ || sketch_concentric_active_ ||
        sketch_tangent_active_ ||
        sketch_segment_pair_active_ ||
        selected_sketch_segment_id_.empty() ||
        active_sketch_id_.empty() || properties_dialog_ != nullptr) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
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
    if (part == nullptr) return;
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

void AssemblyWorkspaceWindow::start_sketch_midpoint() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return;
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
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    try {
        auto next = part->session.document();
        const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == next.sketches.end()) return;
        static_cast<void>(sketch->add_midpoint_constraint(
            pending_midpoint_point_id_, segment_id));
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        pending_midpoint_point_id_.clear();
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
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return;
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
    viewer_->set_selection_contract({zima::viewer::CandidateKind::SketchSegment});
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) {
        viewer_->set_candidate_filter([](const auto&) { return false; });
        return;
    }
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) {
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
            return candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
                candidate.owner_id == owner_id &&
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
    if (candidate.kind != zima::viewer::CandidateKind::SketchSegment ||
        !candidate.semantic_key.starts_with("segment:")) return;
    const auto axis_id = candidate.semantic_key.substr(8);
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    try {
        auto next = part->session.document();
        const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == next.sketches.end()) return;
        const auto axis = std::find_if(sketch->segments.begin(), sketch->segments.end(),
            [&](const auto& value) { return value.id == axis_id; });
        if (axis == sketch->segments.end() || !axis->construction) {
            state_->setText(tr(
                "Symetrická vazba: osou musí být konstrukční čára."));
            return;
        }
        static_cast<void>(sketch->add_symmetric_constraint(
            pending_symmetric_point_ids_[0], pending_symmetric_point_ids_[1],
            axis_id));
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        pending_symmetric_point_ids_.clear();
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
    viewer_->set_selection_contract({zima::viewer::CandidateKind::SketchCurve});
    const auto owner_id = active_sketch_id_;
    viewer_->set_candidate_filter([owner_id](const auto& candidate) {
        return candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
            candidate.owner_id == owner_id &&
            (candidate.semantic_key.starts_with("circle:") ||
             candidate.semantic_key.starts_with("arc:") ||
             candidate.semantic_key.starts_with("ellipse:") ||
             candidate.semantic_key.starts_with("elliptical_arc:"));
    });
}

void AssemblyWorkspaceWindow::start_sketch_concentric() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return;
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
        candidate.kind != zima::viewer::CandidateKind::SketchCurve ||
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
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    try {
        auto next = part->session.document();
        const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == next.sketches.end()) return;
        static_cast<void>(sketch->add_concentric_constraint(
            pending_concentric_geometry_id_, geometry_id));
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        pending_concentric_geometry_id_.clear();
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
             candidate.semantic_key.starts_with("elliptical_arc:"));
    };
    const auto segment_candidate = [](const auto& candidate) {
        return candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
            candidate.semantic_key.starts_with("segment:");
    };
    const auto circular_curve_candidate = [](const auto& candidate) {
        return candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
            (candidate.semantic_key.starts_with("circle:") ||
             candidate.semantic_key.starts_with("arc:"));
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
        [owner_id, first_pending, reference_is_segment,
         reference_supports_curve_pair, pending_geometry_id,
         curve_candidate, segment_candidate,
         circular_curve_candidate](const auto& candidate) {
            if (candidate.owner_id != owner_id) return false;
            if (!first_pending) {
                return segment_candidate(candidate) || curve_candidate(candidate);
            }
            const auto separator = candidate.semantic_key.find(':');
            if (separator != std::string::npos &&
                candidate.semantic_key.substr(separator + 1) == pending_geometry_id) {
                return false;
            }
            if (reference_is_segment) return curve_candidate(candidate);
            return segment_candidate(candidate) ||
                (reference_supports_curve_pair &&
                 circular_curve_candidate(candidate));
        });
}

void AssemblyWorkspaceWindow::start_sketch_tangent() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return;
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
        "Tečná vazba: vyberte referenční úsečku, kružnici, oblouk, elipsu "
        "nebo eliptický oblouk."));
}

void AssemblyWorkspaceWindow::accept_sketch_tangent_selection(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_tangent_active_ || candidate.owner_id != active_sketch_id_) return;
    std::string geometry_id;
    bool is_segment = false;
    bool is_circular_curve = false;
    if (candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
        candidate.semantic_key.starts_with("segment:")) {
        geometry_id = candidate.semantic_key.substr(8);
        is_segment = true;
    } else if (candidate.kind == zima::viewer::CandidateKind::SketchCurve) {
        for (const std::string_view prefix : {
                std::string_view{"circle:"}, std::string_view{"arc:"},
                std::string_view{"ellipse:"},
                std::string_view{"elliptical_arc:"}}) {
            if (candidate.semantic_key.starts_with(prefix)) {
                geometry_id = candidate.semantic_key.substr(prefix.size());
                is_circular_curve = prefix == "circle:" || prefix == "arc:";
                break;
            }
        }
    }
    if (geometry_id.empty()) return;
    if (pending_tangent_geometry_id_.empty()) {
        pending_tangent_geometry_id_ = geometry_id;
        pending_tangent_reference_is_segment_ = is_segment;
        pending_tangent_reference_supports_curve_pair_ = is_circular_curve;
        set_sketch_tangent_contract();
        state_->setText(is_segment
            ? tr("Tečná vazba: vyberte řízenou kružnici, oblouk, elipsu "
                 "nebo eliptický oblouk.")
            : is_circular_curve
                ? tr("Tečná vazba: vyberte řízenou úsečku, kružnici nebo oblouk.")
                : tr("Tečná vazba: vyberte řízenou úsečku."));
        return;
    }
    if (geometry_id == pending_tangent_geometry_id_) {
        state_->setText(tr("Tečná vazba: vyberte jinou druhou geometrii."));
        return;
    }
    const bool line_curve_pair =
        is_segment != pending_tangent_reference_is_segment_;
    const bool circular_curve_pair =
        !is_segment && !pending_tangent_reference_is_segment_ &&
        pending_tangent_reference_supports_curve_pair_ && is_circular_curve;
    if (!line_curve_pair && !circular_curve_pair) {
        state_->setText(tr(
            "Tuto dvojici geometrií tečná vazba zatím nepodporuje."));
        return;
    }
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    try {
        auto next = part->session.document();
        const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == next.sketches.end()) return;
        static_cast<void>(sketch->add_tangent_constraint(
            pending_tangent_geometry_id_, geometry_id));
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        pending_tangent_geometry_id_.clear();
        pending_tangent_reference_is_segment_ = false;
        pending_tangent_reference_supports_curve_pair_ = false;
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
    if (part == nullptr) return;
    cancel_sketch_segment();
    sketch_segment_pair_active_ = true;
    pending_pair_kind_ = kind;
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_point_id_.clear();
    set_sketch_pair_contract();
    state_->setText(kind == zima::sketcher::ConstraintKind::Parallel
        ? tr("Rovnoběžnost: vyberte referenční úsečku. Escape příkaz zruší.")
        : kind == zima::sketcher::ConstraintKind::Perpendicular
            ? tr("Kolmost: vyberte referenční úsečku. Escape příkaz zruší.")
            : tr("Stejné: vyberte referenční úsečku, kružnici nebo oblouk. "
                 "Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::set_sketch_pair_contract() {
    const bool equal = pending_pair_kind_ ==
        zima::sketcher::ConstraintKind::EqualLength;
    if (!equal || (!pending_pair_geometry_id_.empty() &&
                   !pending_pair_reference_is_circular_)) {
        viewer_->set_selection_contract({
            zima::viewer::CandidateKind::SketchSegment});
    } else if (!pending_pair_geometry_id_.empty()) {
        viewer_->set_selection_contract({
            zima::viewer::CandidateKind::SketchCurve});
    } else {
        viewer_->set_selection_contract({
            zima::viewer::CandidateKind::SketchSegment,
            zima::viewer::CandidateKind::SketchCurve});
    }
    const auto owner_id = active_sketch_id_;
    const auto pending_geometry_id = pending_pair_geometry_id_;
    const bool reference_is_circular = pending_pair_reference_is_circular_;
    viewer_->set_candidate_filter(
        [owner_id, pending_geometry_id, reference_is_circular, equal](
            const auto& candidate) {
            if (candidate.owner_id != owner_id) return false;
            const bool segment =
                candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
                candidate.semantic_key.starts_with("segment:");
            const bool circular =
                candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
                (candidate.semantic_key.starts_with("circle:") ||
                 candidate.semantic_key.starts_with("arc:"));
            if ((!equal && !segment) || (equal && !segment && !circular)) {
                return false;
            }
            const auto separator = candidate.semantic_key.find(':');
            if (separator != std::string::npos &&
                candidate.semantic_key.substr(separator + 1) ==
                    pending_geometry_id) {
                return false;
            }
            if (pending_geometry_id.empty()) return true;
            return reference_is_circular ? circular : segment;
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
    } else if (pending_pair_kind_ == zima::sketcher::ConstraintKind::EqualLength &&
               candidate.kind == zima::viewer::CandidateKind::SketchCurve) {
        for (const std::string_view prefix : {
                std::string_view{"circle:"}, std::string_view{"arc:"}}) {
            if (!candidate.semantic_key.starts_with(prefix)) continue;
            geometry_id = candidate.semantic_key.substr(prefix.size());
            is_circular = true;
            break;
        }
    }
    if (geometry_id.empty()) return;
    if (pending_pair_geometry_id_.empty()) {
        pending_pair_geometry_id_ = geometry_id;
        pending_pair_reference_is_circular_ = is_circular;
        set_sketch_pair_contract();
        state_->setText(pending_pair_kind_ == zima::sketcher::ConstraintKind::Parallel
            ? tr("Rovnoběžnost: vyberte řízenou úsečku.")
            : pending_pair_kind_ == zima::sketcher::ConstraintKind::Perpendicular
                ? tr("Kolmost: vyberte řízenou úsečku.")
                : is_circular
                    ? tr("Stejné: vyberte řízenou kružnici nebo oblouk.")
                    : tr("Stejné: vyberte řízenou úsečku."));
        return;
    }
    if (geometry_id == pending_pair_geometry_id_) {
        state_->setText(tr("Vyberte jinou řízenou geometrii."));
        return;
    }
    if (is_circular != pending_pair_reference_is_circular_) {
        state_->setText(tr(
            "Stejné vyžaduje dvě úsečky nebo dvě kružnice či kruhové oblouky."));
        return;
    }
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    try {
        auto next = part->session.document();
        const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == next.sketches.end()) return;
        if (pending_pair_reference_is_circular_) {
            static_cast<void>(sketch->add_equal_radius_constraint(
                pending_pair_geometry_id_, geometry_id));
        } else {
            static_cast<void>(sketch->add_segment_pair_constraint(
                pending_pair_geometry_id_, geometry_id, pending_pair_kind_));
        }
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        const bool equal_radius = pending_pair_reference_is_circular_;
        pending_pair_geometry_id_.clear();
        pending_pair_reference_is_circular_ = false;
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(pending_pair_kind_ == zima::sketcher::ConstraintKind::Parallel
            ? tr("Rovnoběžnost vytvořena. Vyberte další referenční úsečku.")
            : pending_pair_kind_ == zima::sketcher::ConstraintKind::Perpendicular
                ? tr("Kolmost vytvořena. Vyberte další referenční úsečku.")
                : equal_radius
                    ? tr("Stejný poloměr vytvořen. Vyberte další referenční geometrii.")
                    : tr("Stejná délka vytvořena. Vyberte další referenční geometrii."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
}

void AssemblyWorkspaceWindow::toggle_selected_sketch_point_fixed() {
    if (properties_dialog_ != nullptr || sketch_coincident_active_ ||
        sketch_midpoint_active_ || sketch_symmetric_active_ ||
        sketch_concentric_active_ || sketch_tangent_active_ ||
        sketch_segment_pair_active_ || sketch_mirror_active_ ||
        selected_sketch_point_id_.empty() || active_sketch_id_.empty()) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
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
        sketch_rectangle_active_ || sketch_polygon_active_ ||
        sketch_mirror_active_ || sketch_circle_active_ || sketch_arc_active_ ||
        sketch_ellipse_active_ || sketch_elliptical_arc_active_ ||
        sketch_bspline_active_ ||
        sketch_coincident_active_ || sketch_midpoint_active_ ||
        sketch_symmetric_active_ || sketch_concentric_active_ ||
        sketch_tangent_active_ ||
        sketch_segment_pair_active_ || candidate.kind !=
            zima::viewer::CandidateKind::SketchPoint ||
        candidate.owner_id != active_sketch_id_ ||
        !candidate.semantic_key.starts_with("point:")) return false;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return false;
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

bool AssemblyWorkspaceWindow::begin_assembly_mate_drag(
    const zima::viewer::ViewerCandidate& candidate) {
    if (candidate.kind != zima::viewer::CandidateKind::Dimension ||
        !candidate.semantic_key.starts_with("mate:") ||
        candidate.owner_id != workspace_.active_document_id() ||
        properties_dialog_ != nullptr) return false;
    auto* assembly = workspace_.open_assembly(candidate.owner_id);
    const std::string mate_id = candidate.semantic_key.substr(5);
    const auto* mate = assembly == nullptr
        ? nullptr : assembly->session.document().find_mate(mate_id);
    if (mate == nullptr || mate->kind != zima::assembly::MateKind::PlaneCoincident ||
        mate->suppressed || mate->status != zima::assembly::MateStatus::Valid) return false;
    const auto prerequisite = assembly->session.document().resolve_plane(mate->prerequisite);
    if (prerequisite.status != zima::assembly::MateStatus::Valid) return false;
    assembly_drag_document_ = assembly->session.document();
    assembly_drag_document_id_ = candidate.owner_id;
    assembly_drag_mate_id_ = mate_id;
    assembly_drag_axis_point_ = prerequisite.plane.point;
    assembly_drag_axis_direction_ = prerequisite.plane.normal;
    assembly_drag_changed_ = false;
    state_->setText(tr("Tažením měníte odsazení vazby v povolených mezích."));
    return true;
}

void AssemblyWorkspaceWindow::update_assembly_mate_drag(
    const zima::kernel::Vec3& ray_origin,
    const zima::kernel::Vec3& ray_direction) {
    if (!assembly_drag_document_) return;
    double value = zima::assembly::AssemblyDocument::project_linear_drag_value(
        assembly_drag_axis_point_, assembly_drag_axis_direction_,
        ray_origin, ray_direction);
    const auto* mate = assembly_drag_document_->find_mate(assembly_drag_mate_id_);
    if (mate == nullptr) return;
    if (mate->lower_limit) value = std::max(value, *mate->lower_limit);
    if (mate->upper_limit) value = std::min(value, *mate->upper_limit);
    if (std::abs(value - mate->offset) <= 1.0e-9) return;
    if (!assembly_drag_document_->set_mate_value(assembly_drag_mate_id_, value)) return;
    assembly_drag_changed_ = true;
    viewer_->set_mesh(assembly_drag_document_->build_scene(), false);
    state_->setText(tr("Odsazení vazby: %1 mm").arg(value, 0, 'f', 3));
}

void AssemblyWorkspaceWindow::end_assembly_mate_drag() {
    if (!assembly_drag_document_) return;
    const std::string document_id = assembly_drag_document_id_;
    auto result = std::move(*assembly_drag_document_);
    const bool changed = assembly_drag_changed_;
    assembly_drag_document_.reset();
    assembly_drag_document_id_.clear();
    assembly_drag_mate_id_.clear();
    assembly_drag_changed_ = false;
    if (changed) {
        if (auto* assembly = workspace_.open_assembly(document_id)) {
            assembly->session.commit(std::move(result));
        }
    }
    refresh_tabs();
    refresh_scene();
}

bool AssemblyWorkspaceWindow::delete_selected_sketch_geometry() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty() ||
        sketch_point_active_ || sketch_segment_active_ ||
        sketch_rectangle_active_ || sketch_polygon_active_ || sketch_mirror_active_ ||
        sketch_circle_active_ ||
        sketch_arc_active_ || sketch_ellipse_active_ ||
        sketch_elliptical_arc_active_ || sketch_bspline_active_ ||
        sketch_coincident_active_ || sketch_midpoint_active_ ||
        sketch_symmetric_active_ || sketch_concentric_active_ ||
        sketch_tangent_active_ ||
        sketch_segment_pair_active_) return false;
    const std::string geometry_id = !selected_sketch_segment_id_.empty()
        ? selected_sketch_segment_id_
        : !selected_sketch_circle_id_.empty() ? selected_sketch_circle_id_
        : !selected_sketch_arc_id_.empty() ? selected_sketch_arc_id_
        : !selected_sketch_ellipse_id_.empty() ? selected_sketch_ellipse_id_
        : !selected_sketch_elliptical_arc_id_.empty()
            ? selected_sketch_elliptical_arc_id_
        : !selected_sketch_bspline_id_.empty() ? selected_sketch_bspline_id_
        : !selected_sketch_text_id_.empty() ? selected_sketch_text_id_
        : selected_sketch_external_reference_id_;
    if (geometry_id.empty() && selected_sketch_point_id_.empty()) return false;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return false;
    try {
        auto next = part->session.document();
        const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == next.sketches.end()) return false;
        if (!geometry_id.empty()) sketch->remove_geometry(geometry_id);
        else sketch->remove_point(selected_sketch_point_id_);
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        selected_sketch_segment_id_.clear();
        selected_sketch_circle_id_.clear();
        selected_sketch_arc_id_.clear();
        selected_sketch_ellipse_id_.clear();
        selected_sketch_elliptical_arc_id_.clear();
        selected_sketch_bspline_id_.clear();
        selected_sketch_text_id_.clear();
        selected_sketch_external_reference_id_.clear();
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
        sketch_rectangle_active_ || sketch_polygon_active_ ||
        sketch_mirror_active_ || sketch_circle_active_ || sketch_arc_active_ ||
        sketch_ellipse_active_ || sketch_elliptical_arc_active_ ||
        sketch_bspline_active_ ||
        sketch_coincident_active_ || sketch_midpoint_active_ ||
        sketch_symmetric_active_ || sketch_concentric_active_ ||
        sketch_tangent_active_ ||
        sketch_segment_pair_active_) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
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

bool AssemblyWorkspaceWindow::accept_sketch_polygon_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_polygon_active_) return false;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || active_sketch_id_.empty()) {
        return false;
    }
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_polygon_center_) {
        pending_polygon_center_ = *position;
        state_->setText(tr("Pravidelný %1úhelník: určete vrchol na pomocné kružnici.")
            .arg(sketch_polygon_sides_));
        return true;
    }
    const double radius = std::hypot(
        (*position)[0] - (*pending_polygon_center_)[0],
        (*position)[1] - (*pending_polygon_center_)[1]);
    if (radius <= 1.0e-9) {
        state_->setText(tr("Mnohoúhelník musí mít nenulový poloměr."));
        return true;
    }
    try {
        auto next = part->session.document();
        const auto target = std::find_if(next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (target == next.sketches.end()) return true;
        static_cast<void>(target->add_regular_polygon(
            (*pending_polygon_center_)[0], (*pending_polygon_center_)[1],
            (*position)[0], (*position)[1], sketch_polygon_sides_));
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        pending_polygon_center_.reset();
        viewer_->set_transient_edges({});
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr(
            "Pravidelný %1úhelník vytvořen. Kliknutím určete střed dalšího.")
            .arg(sketch_polygon_sides_));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
    return true;
}

void AssemblyWorkspaceWindow::preview_sketch_polygon_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_polygon_active_ || !pending_polygon_center_) return;
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
    const double dx = (*position)[0] - (*pending_polygon_center_)[0];
    const double dy = (*position)[1] - (*pending_polygon_center_)[1];
    const double radius = std::hypot(dx, dy);
    if (radius <= 1.0e-9) {
        viewer_->set_transient_edges({});
        return;
    }
    const double start_angle = std::atan2(dy, dx);
    constexpr double full_turn = 2.0 * 3.14159265358979323846;
    std::vector<zima::kernel::Vec3> vertices;
    vertices.reserve(sketch_polygon_sides_);
    for (unsigned index = 0; index < sketch_polygon_sides_; ++index) {
        const double angle = start_angle + full_turn *
            static_cast<double>(index) /
            static_cast<double>(sketch_polygon_sides_);
        vertices.push_back(sketch->world_point(
            (*pending_polygon_center_)[0] + radius * std::cos(angle),
            (*pending_polygon_center_)[1] + radius * std::sin(angle)));
    }
    std::vector<zima::kernel::ViewerEdge> preview;
    preview.reserve(sketch_polygon_sides_ + 1);
    for (unsigned index = 0; index < sketch_polygon_sides_; ++index) {
        preview.push_back({{
            vertices[index], vertices[(index + 1) % sketch_polygon_sides_]}, {}});
    }
    zima::kernel::ViewerEdge support_circle;
    constexpr std::size_t samples = 96;
    support_circle.points.reserve(samples + 1);
    for (std::size_t sample = 0; sample <= samples; ++sample) {
        const double angle = full_turn * static_cast<double>(sample) /
            static_cast<double>(samples);
        support_circle.points.push_back(sketch->world_point(
            (*pending_polygon_center_)[0] + radius * std::cos(angle),
            (*pending_polygon_center_)[1] + radius * std::sin(angle)));
    }
    preview.push_back(std::move(support_circle));
    viewer_->set_transient_edges(std::move(preview));
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

bool AssemblyWorkspaceWindow::accept_sketch_elliptical_arc_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_elliptical_arc_active_) return false;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || active_sketch_id_.empty()) return false;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_elliptical_arc_center_) {
        pending_elliptical_arc_center_ = *position;
        state_->setText(tr(
            "Eliptický oblouk: určete konec hlavní poloosy."));
        return true;
    }
    if (!pending_elliptical_arc_major_) {
        if (std::hypot(
                (*position)[0] - (*pending_elliptical_arc_center_)[0],
                (*position)[1] - (*pending_elliptical_arc_center_)[1]) <=
            1.0e-9) {
            state_->setText(tr(
                "Hlavní poloosa eliptického oblouku musí mít nenulovou délku."));
            return true;
        }
        pending_elliptical_arc_major_ = *position;
        state_->setText(tr(
            "Eliptický oblouk: určete délku a stranu vedlejší poloosy."));
        return true;
    }
    if (!pending_elliptical_arc_minor_) {
        const auto minor = projected_ellipse_minor(
            *pending_elliptical_arc_center_, *pending_elliptical_arc_major_,
            *position);
        if (!minor) {
            state_->setText(tr(
                "Vedlejší poloosa eliptického oblouku musí mít nenulovou délku."));
            return true;
        }
        pending_elliptical_arc_minor_ = *minor;
        const double major_x =
            (*pending_elliptical_arc_major_)[0] -
            (*pending_elliptical_arc_center_)[0];
        const double major_y =
            (*pending_elliptical_arc_major_)[1] -
            (*pending_elliptical_arc_center_)[1];
        const double minor_x =
            (*pending_elliptical_arc_minor_)[0] -
            (*pending_elliptical_arc_center_)[0];
        const double minor_y =
            (*pending_elliptical_arc_minor_)[1] -
            (*pending_elliptical_arc_center_)[1];
        pending_elliptical_arc_reversed_ =
            major_x * minor_y - major_y * minor_x < 0.0;
        state_->setText(tr(
            "Eliptický oblouk: určete počáteční bod na elipse."));
        return true;
    }
    const auto projected = projected_ellipse_position(
        *pending_elliptical_arc_center_, *pending_elliptical_arc_major_,
        *pending_elliptical_arc_minor_, *position);
    if (!projected) {
        state_->setText(tr(
            "Bod eliptického oblouku nesmí ležet ve středu."));
        return true;
    }
    if (!pending_elliptical_arc_start_) {
        pending_elliptical_arc_start_ = projected->position;
        state_->setText(pending_elliptical_arc_reversed_
            ? tr("Eliptický oblouk: určete koncový bod ve směru hodinových ručiček.")
            : tr("Eliptický oblouk: určete koncový bod proti směru hodinových ručiček."));
        return true;
    }
    if (std::hypot(
            projected->position[0] - (*pending_elliptical_arc_start_)[0],
            projected->position[1] - (*pending_elliptical_arc_start_)[1]) <=
        1.0e-9) {
        state_->setText(tr(
            "Počáteční a koncový bod eliptického oblouku musí být odlišné."));
        return true;
    }
    try {
        auto next = part->session.document();
        const auto target = std::find_if(
            next.sketches.begin(), next.sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (target == next.sketches.end()) return true;
        static_cast<void>(target->add_elliptical_arc(
            (*pending_elliptical_arc_center_)[0],
            (*pending_elliptical_arc_center_)[1],
            (*pending_elliptical_arc_major_)[0],
            (*pending_elliptical_arc_major_)[1],
            (*pending_elliptical_arc_minor_)[0],
            (*pending_elliptical_arc_minor_)[1],
            (*pending_elliptical_arc_start_)[0],
            (*pending_elliptical_arc_start_)[1],
            projected->position[0], projected->position[1],
            pending_elliptical_arc_reversed_));
        part->session.commit(
            std::move(next), part->session.calculated_boundaries());
        pending_elliptical_arc_center_.reset();
        pending_elliptical_arc_major_.reset();
        pending_elliptical_arc_minor_.reset();
        pending_elliptical_arc_start_.reset();
        pending_elliptical_arc_reversed_ = false;
        viewer_->set_transient_edges({});
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr(
            "Eliptický oblouk vytvořen. Kliknutím určete střed dalšího."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
    return true;
}

void AssemblyWorkspaceWindow::preview_sketch_elliptical_arc_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_elliptical_arc_active_ ||
        !pending_elliptical_arc_center_) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto sketch = std::find_if(
        part->session.document().sketches.begin(),
        part->session.document().sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == part->session.document().sketches.end()) return;
    const auto cursor = sketch->intersect_ray(origin, direction);
    if (!cursor) return;
    if (!pending_elliptical_arc_major_) {
        viewer_->set_transient_edges({{{
            sketch->world_point(
                (*pending_elliptical_arc_center_)[0],
                (*pending_elliptical_arc_center_)[1]),
            sketch->world_point((*cursor)[0], (*cursor)[1])}, {}}});
        return;
    }
    SketchPosition minor;
    if (pending_elliptical_arc_minor_) {
        minor = *pending_elliptical_arc_minor_;
    } else {
        const auto projected_minor = projected_ellipse_minor(
            *pending_elliptical_arc_center_, *pending_elliptical_arc_major_,
            *cursor);
        if (!projected_minor) {
            viewer_->set_transient_edges({});
            return;
        }
        minor = *projected_minor;
    }
    std::vector<zima::kernel::ViewerEdge> preview;
    zima::kernel::ViewerEdge axes;
    axes.construction = true;
    axes.points = {
        sketch->world_point(
            (*pending_elliptical_arc_center_)[0],
            (*pending_elliptical_arc_center_)[1]),
        sketch->world_point(
            (*pending_elliptical_arc_major_)[0],
            (*pending_elliptical_arc_major_)[1]),
        sketch->world_point(
            (*pending_elliptical_arc_center_)[0],
            (*pending_elliptical_arc_center_)[1]),
        sketch->world_point(minor[0], minor[1])};
    preview.push_back(std::move(axes));
    if (!pending_elliptical_arc_minor_) {
        preview.push_back(ellipse_preview_edge(
            *sketch, *pending_elliptical_arc_center_,
            *pending_elliptical_arc_major_, minor));
        viewer_->set_transient_edges(std::move(preview));
        return;
    }
    const auto projected = projected_ellipse_position(
        *pending_elliptical_arc_center_, *pending_elliptical_arc_major_,
        minor, *cursor);
    if (!projected) {
        viewer_->set_transient_edges(std::move(preview));
        return;
    }
    if (!pending_elliptical_arc_start_) {
        preview.push_back(ellipse_preview_edge(
            *sketch, *pending_elliptical_arc_center_,
            *pending_elliptical_arc_major_, minor));
        zima::kernel::ViewerEdge radial;
        radial.construction = true;
        radial.points = {
            sketch->world_point(
                (*pending_elliptical_arc_center_)[0],
                (*pending_elliptical_arc_center_)[1]),
            sketch->world_point(
                projected->position[0], projected->position[1])};
        preview.push_back(std::move(radial));
        viewer_->set_transient_edges(std::move(preview));
        return;
    }
    const auto start = projected_ellipse_position(
        *pending_elliptical_arc_center_, *pending_elliptical_arc_major_,
        minor, *pending_elliptical_arc_start_);
    if (!start) {
        viewer_->set_transient_edges(std::move(preview));
        return;
    }
    constexpr double full_turn = 2.0 * 3.14159265358979323846;
    double end_parameter = projected->parameter;
    while (end_parameter <= start->parameter) end_parameter += full_turn;
    const double sweep = end_parameter - start->parameter;
    if (sweep >= full_turn - 1.0e-12) {
        viewer_->set_transient_edges(std::move(preview));
        return;
    }
    zima::kernel::ViewerEdge arc;
    const auto samples = std::max<std::size_t>(8,
        static_cast<std::size_t>(std::ceil(192.0 * sweep / full_turn)));
    arc.points.reserve(samples + 1);
    for (std::size_t sample = 0; sample <= samples; ++sample) {
        const double parameter = start->parameter + sweep *
            static_cast<double>(sample) / static_cast<double>(samples);
        arc.points.push_back(sketch->world_point(
            (*pending_elliptical_arc_center_)[0] +
                ((*pending_elliptical_arc_major_)[0] -
                 (*pending_elliptical_arc_center_)[0]) * std::cos(parameter) +
                (minor[0] - (*pending_elliptical_arc_center_)[0]) *
                    std::sin(parameter),
            (*pending_elliptical_arc_center_)[1] +
                ((*pending_elliptical_arc_major_)[1] -
                 (*pending_elliptical_arc_center_)[1]) * std::cos(parameter) +
                (minor[1] - (*pending_elliptical_arc_center_)[1]) *
                    std::sin(parameter)));
    }
    preview.push_back(std::move(arc));
    viewer_->set_transient_edges(std::move(preview));
}

void AssemblyWorkspaceWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && edge_treatment_selection_) {
        edge_treatment_selection_.reset();
        pending_edge_treatment_edges_.clear();
        preserve_view_on_refresh_ = true;
        refresh_scene();
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
        (sketch_point_active_ || sketch_segment_active_ ||
         sketch_rectangle_active_ || sketch_polygon_active_ || sketch_trim_active_ ||
         sketch_circle_active_ ||
         sketch_mirror_active_ || sketch_arc_active_ || sketch_ellipse_active_ ||
         sketch_elliptical_arc_active_ ||
         sketch_bspline_active_ ||
         sketch_coincident_active_ || sketch_midpoint_active_ ||
         sketch_symmetric_active_ || sketch_concentric_active_ ||
         sketch_tangent_active_ ||
         sketch_segment_pair_active_)) {
        const bool discarded_trim = sketch_trim_active_ && sketch_trim_changed_;
        cancel_sketch_segment();
        preserve_view_on_refresh_ = true;
        refresh_scene();
        if (discarded_trim) {
            state_->setText(tr(
                "Ořezání bylo zrušeno; původní skica zůstala beze změny."));
        }
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

std::pair<zima::kernel::Vec3, zima::kernel::Vec3>
AssemblyWorkspaceWindow::active_part_local_ray(
    const zima::kernel::Vec3& origin,
    const zima::kernel::Vec3& direction) const {
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    const auto* assembly =
        workspace_.open_assembly(workspace_.displayed_document_id());
    if (part == nullptr || assembly == nullptr) return {origin, direction};
    const auto occurrence = resolve_active_occurrence(
        part->session.document().document_id);
    if (!occurrence || occurrence->empty()) return {origin, direction};
    const auto path = zima::assembly::InstancePath::decode(*occurrence);
    return {
        workspace_.occurrence_point_from_scene(
            assembly->session.document().document_id, path, origin),
        workspace_.occurrence_direction_from_scene(
            assembly->session.document().document_id, path, direction),
    };
}

void AssemblyWorkspaceWindow::regenerate_active_part() {
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || properties_dialog_ != nullptr) return;
    try {
        auto next = part->session.document();
        auto calculated = calculate_part(next);
        const bool references_changed =
            refresh_sketch_external_references(next, calculated) |
            workspace_.refresh_context_external_references(next);
        if (references_changed) calculated = calculate_part(next);
        if (references_changed) {
            part->session.commit(std::move(next), std::move(calculated));
        } else {
            part->session.update_calculated_boundaries(std::move(calculated));
        }
        refresh_tabs();
        refresh_scene();
        state_->setText(references_changed
            ? tr("Part byl regenerován a externí reference skic byly obnoveny.")
            : tr("Part byl regenerován."));
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
    int displayed_index = -1;
    QString displayed_label = tr("Bez dokumentu");
    for (const auto& state : workspace_.documents()) {
        std::visit([&](const auto& document) {
            using State = std::decay_t<decltype(document)>;
            if constexpr(std::is_same_v<State,zima::workspace::DrawingState>) {
                const QString label = document.path.empty()
                    ? QString::fromStdString(document.document.name)
                    : QString::fromStdString(document.path.filename().string());
                const int index=tabs_->addTab(resource_icon("drawing"),
                    label);
                tabs_->setTabData(index,QString::fromStdString(document.document.document_id));
                if(document.document.document_id==workspace_.displayed_document_id()) {
                    displayed_index=index;
                    displayed_label=label;
                }
            } else {
                const auto& model = document.session.document();
                const QString label = document.path.empty()
                    ? QString::fromStdString(model.name)
                    : QString::fromStdString(document.path.filename().string());
                const int index = tabs_->addTab(
                    resource_icon(std::is_same_v<State, zima::workspace::PartState>
                        ? "part" : "assembly"),
                    label + (document.session.is_dirty() ? QStringLiteral(" *") : QString{}));
                tabs_->setTabData(index, QString::fromStdString(model.document_id));
                if (model.document_id == workspace_.displayed_document_id()) {
                    displayed_index = index;
                    displayed_label = label;
                }
            }
        }, state);
    }
    tabs_->setCurrentIndex(displayed_index);
    tabs_->blockSignals(false);
    setWindowTitle(tr("ZIMA-CAD — %1").arg(displayed_label));
    update_document_area_visibility();
}

void AssemblyWorkspaceWindow::refresh_scene() {
    update_document_area_visibility();
    tree_->clear();
    viewer_->set_transient_point_transform({});
    if (workspace_.size() == 0) {
        workspace_stack_->setCurrentWidget(model_workspace_);
        tree_->setHeaderLabels({tr("DÍL")});
        viewer_->set_mesh({});
        close_document_action_->setEnabled(false);
        save_action_->setEnabled(false);
        save_as_action_->setEnabled(false);
        regenerate_document_action_->setEnabled(false);
        undo_action_->setEnabled(false);
        redo_action_->setEnabled(false);
        sketch_constraints_action_->setEnabled(false);
        sketch_dimensions_action_->setEnabled(false);
        sketch_text_action_->setEnabled(false);
        sketch_external_reference_action_->setEnabled(false);
        rebuild_application_toolbar();
        return;
    }
    if (auto* drawing =
            workspace_.open_drawing(workspace_.displayed_document_id())) {
        workspace_stack_->setCurrentWidget(drawing_workspace_);
        drawing_workspace_->edit_workspace_document(drawing->document.document_id);
        tree_->setHeaderLabels({tr("VÝKRES-%1").arg(
            QString::fromStdString(drawing->document.name))});
        auto* root = new QTreeWidgetItem(
            tree_, {QString::fromStdString(drawing->document.name)});
        for (const auto& sheet : drawing->document.sheets) {
            auto* sheet_item = new QTreeWidgetItem(
                root, {QString::fromStdString(sheet.name)});
            for (const auto& drawing_view : sheet.views) {
                new QTreeWidgetItem(
                    sheet_item, {QString::fromStdString(drawing_view.name)});
            }
            sheet_item->setExpanded(true);
        }
        root->setExpanded(true);
        active_application_ = ApplicationMode::Drawing;
        insert_action_->setEnabled(false);
        regenerate_action_->setEnabled(false);
        for (auto* action : {box_action_, cylinder_action_, sphere_action_, cone_action_,
                             pyramid_action_, wedge_action_, construction_point_action_,
                             construction_axis_action_, construction_plane_action_,
                             sketch_action_, extrusion_action_, revolution_action_,
                             fillet_action_, chamfer_action_, regenerate_part_action_,
                             sketch_normal_view_action_,
                             sketch_external_reference_action_, sketch_point_action_,
                             sketch_construction_action_, sketch_segment_action_,
                             sketch_polyline_action_,
                             sketch_rectangle_action_, sketch_polygon_action_,
                             sketch_trim_action_,
                             sketch_mirror_action_,
                             sketch_circle_action_,
                             sketch_arc_action_, sketch_ellipse_action_,
                             sketch_elliptical_arc_action_,
                             sketch_bspline_action_, sketch_text_action_,
                             sketch_constraints_action_,
                             sketch_dimensions_action_, finish_sketch_action_,
                             plane_mate_action_, axis_mate_action_, point_mate_action_,
                             angle_mate_action_, plane_angle_mate_action_}) {
            action->setEnabled(false);
        }
        save_action_->setEnabled(true);
        save_as_action_->setEnabled(true);
        close_document_action_->setEnabled(true);
        regenerate_document_action_->setEnabled(true);
        undo_action_->setEnabled(false);
        redo_action_->setEnabled(false);
        state_->setText(tr("Zobrazený výkres: %1").arg(
            QString::fromStdString(drawing->document.name)));
        update_application_actions();
        rebuild_application_toolbar();
        return;
    }
    workspace_stack_->setCurrentWidget(model_workspace_);
    const auto* assembly = workspace_.open_assembly(workspace_.displayed_document_id());
    if (assembly == nullptr) {
        const auto* part = workspace_.open_part(workspace_.displayed_document_id());
        if (part == nullptr) {
            viewer_->set_mesh({});
            state_->setText(tr("Není vybrán zobrazovaný dokument."));
            rebuild_application_toolbar();
            return;
        }
        const auto& document = part->session.document();
        tree_->setHeaderLabels({tr("DÍL-%1").arg(
            QString::fromStdString(document.name))});
        if (!active_sketch_id_.empty() && std::none_of(
                document.sketches.begin(), document.sketches.end(),
                [&](const auto& sketch) { return sketch.id == active_sketch_id_; })) {
            active_sketch_id_.clear();
            cancel_sketch_segment();
        }
        if (!selected_sketch_id_.empty() && std::none_of(
                document.sketches.begin(), document.sketches.end(),
                [&](const auto& sketch) { return sketch.id == selected_sketch_id_; })) {
            selected_sketch_id_.clear();
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
            item->setSelected(sketch.id == selected_sketch_id_);
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
        for (const auto& object : document.constructions) {
            auto* item = new QTreeWidgetItem(root, {QString::fromStdString(object.name)});
            item->setData(0, Qt::UserRole, QString::fromStdString(object.id));
            item->setData(0, Qt::UserRole + 3, "part-construction");
        }
        root->setExpanded(true);
        viewer_->set_selection_contract(!selection_action_->isChecked()
            ? std::vector<zima::viewer::CandidateKind>{}
            : sketch_external_reference_active_
                ? std::vector{zima::viewer::CandidateKind::Edge,
                              zima::viewer::CandidateKind::Vertex,
                              zima::viewer::CandidateKind::Axis,
                              zima::viewer::CandidateKind::Face}
            : sketch_trim_active_
                ? std::vector{zima::viewer::CandidateKind::SketchTrimPiece}
            : sketch_mirror_active_
                ? sketch_mirror_selecting_sources_
                    ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchPoint,
                                  zima::viewer::CandidateKind::SketchCurve}
                    : std::vector{zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchAxis}
            : sketch_coincident_active_
                ? std::vector{zima::viewer::CandidateKind::SketchPoint}
            : sketch_midpoint_active_
                ? pending_midpoint_point_id_.empty()
                    ? std::vector{zima::viewer::CandidateKind::SketchPoint}
                    : std::vector{zima::viewer::CandidateKind::SketchSegment}
            : sketch_symmetric_active_
                ? pending_symmetric_point_ids_.size() < 2
                    ? std::vector{zima::viewer::CandidateKind::SketchPoint}
                    : std::vector{zima::viewer::CandidateKind::SketchSegment}
            : sketch_concentric_active_
                ? std::vector{zima::viewer::CandidateKind::SketchCurve}
            : sketch_tangent_active_
                ? pending_tangent_geometry_id_.empty()
                    ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchCurve}
                    : pending_tangent_reference_is_segment_
                        ? std::vector{zima::viewer::CandidateKind::SketchCurve}
                        : pending_tangent_reference_supports_curve_pair_
                            ? std::vector{
                                  zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchCurve}
                        : std::vector{zima::viewer::CandidateKind::SketchSegment}
            : sketch_segment_pair_active_
                ? pending_pair_kind_ == zima::sketcher::ConstraintKind::EqualLength &&
                        pending_pair_geometry_id_.empty()
                    ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchCurve}
                    : pending_pair_reference_is_circular_
                        ? std::vector{zima::viewer::CandidateKind::SketchCurve}
                        : std::vector{zima::viewer::CandidateKind::SketchSegment}
            : active_sketch_id_.empty()
                ? [this] {
                    switch (selection_filter_combo_->currentIndex()) {
                        case 1:
                        case 4:
                            return std::vector{zima::viewer::CandidateKind::Face};
                        case 2:
                            return std::vector{zima::viewer::CandidateKind::Vertex};
                        case 3:
                            return std::vector{zima::viewer::CandidateKind::Axis};
                        default:
                            return std::vector{zima::viewer::CandidateKind::Container};
                    }
                }()
                : std::vector{zima::viewer::CandidateKind::SketchSegment,
                              zima::viewer::CandidateKind::SketchPoint,
                              zima::viewer::CandidateKind::Dimension,
                              zima::viewer::CandidateKind::SketchCurve,
                              zima::viewer::CandidateKind::SketchText,
                              zima::viewer::CandidateKind::SketchExternalReference});
        if (sketch_external_reference_active_) {
            const auto source_owners = sketch_external_reference_source_owners(
                document, active_sketch_id_);
            viewer_->set_candidate_filter(
                [source_owners](const auto& candidate) {
                    return candidate.geometry ==
                            zima::viewer::CandidateGeometry::OriginalReference &&
                        candidate.instance_path.empty() &&
                        source_owners.contains(candidate.owner_id) &&
                        (candidate.kind == zima::viewer::CandidateKind::Edge ||
                         candidate.kind == zima::viewer::CandidateKind::Vertex ||
                         candidate.kind == zima::viewer::CandidateKind::Axis ||
                         candidate.kind == zima::viewer::CandidateKind::Face);
                });
        } else if (sketch_symmetric_active_ &&
                   pending_symmetric_point_ids_.size() == 2) {
            set_sketch_symmetric_axis_contract();
        } else if (sketch_concentric_active_) {
            set_sketch_concentric_contract();
        } else if (sketch_tangent_active_) {
            set_sketch_tangent_contract();
        } else if (sketch_segment_pair_active_) {
            set_sketch_pair_contract();
        }
        if (part_rollback_ &&
            part_rollback_->part_document_id == document.document_id) {
            viewer_->set_mesh(part_rollback_->input_body
                ? part_rollback_->input_body->mesh : zima::kernel::ViewerMesh{});
        } else {
            const auto& calculated = part->session.calculated_boundaries();
            zima::kernel::ViewerMesh display = calculated.empty()
                ? zima::kernel::ViewerMesh{} : calculated.back().mesh;
            for (const auto& sketch : document.sketches) {
                if (!active_sketch_id_.empty() && sketch.id != active_sketch_id_) {
                    continue;
                }
                const auto* displayed_sketch = sketch_trim_active_ &&
                        sketch_trim_preview_ && sketch.id == active_sketch_id_
                    ? &*sketch_trim_preview_ : &sketch;
                auto sketch_mesh = displayed_sketch->viewer_mesh();
                if (!editing_sketch_text_id_.empty() &&
                    sketch.id == active_sketch_id_) {
                    std::erase_if(sketch_mesh.edges, [&](const auto& edge) {
                        const auto text_id = sketch_text_id_from_key(
                            edge.reference.semantic_key);
                        return text_id && *text_id == editing_sketch_text_id_;
                    });
                }
                if (sketch_trim_active_ && sketch_trim_preview_ &&
                    sketch.id == active_sketch_id_) {
                    std::set<std::string> piece_geometry_ids;
                    for (const auto& piece : sketch_trim_topology_) {
                        piece_geometry_ids.insert(piece.geometry_id);
                    }
                    std::erase_if(sketch_mesh.edges, [&](const auto& edge) {
                        const auto separator = edge.reference.semantic_key.find(':');
                        return separator != std::string::npos &&
                            piece_geometry_ids.contains(
                                edge.reference.semantic_key.substr(separator + 1));
                    });
                    for (std::size_t index = 0;
                         index < sketch_trim_topology_.size(); ++index) {
                        zima::kernel::ViewerEdge edge;
                        edge.reference = {
                            displayed_sketch->id,
                            "trim_piece:" + std::to_string(index), {}};
                        edge.overlay = true;
                        for (const auto& point : sketch_trim_topology_[index].points) {
                            edge.points.push_back(
                                displayed_sketch->world_point(point[0], point[1]));
                        }
                        sketch_mesh.edges.push_back(std::move(edge));
                    }
                }
                if (sketch.id != active_sketch_id_) sketch_mesh.axes.clear();
                append_mesh(display, std::move(sketch_mesh));
            }
            append_mesh(display, document.construction_viewer_mesh());
            if (sketch_external_reference_active_) {
                const auto source_owners = sketch_external_reference_source_owners(
                    document, active_sketch_id_);
                for (const auto& axis : display.original_references.axes) {
                    if (axis.reference.instance_path.empty() &&
                        source_owners.contains(axis.reference.owner_id)) {
                        display.axes.push_back(axis);
                    }
                }
            }
            viewer_->set_mesh(std::move(display), !preserve_view_on_refresh_);
            preserve_view_on_refresh_ = false;
        }
        state_->setText(document.history.empty()
            ? tr("Nový Part: začněte příkazem Kvádr, jiným tělesem nebo Skica.")
            : tr("Zobrazený Part: %1").arg(QString::fromStdString(document.name)));
        insert_action_->setEnabled(false);
        regenerate_action_->setEnabled(false);
        plane_mate_action_->setEnabled(false);
        axis_mate_action_->setEnabled(false);
        point_mate_action_->setEnabled(false);
        angle_mate_action_->setEnabled(false);
        plane_angle_mate_action_->setEnabled(false);
        save_action_->setEnabled(true);
        save_as_action_->setEnabled(true);
        close_document_action_->setEnabled(true);
        regenerate_document_action_->setEnabled(true);
        box_action_->setEnabled(true);
        cylinder_action_->setEnabled(true);
        sphere_action_->setEnabled(true);
        cone_action_->setEnabled(true);
        pyramid_action_->setEnabled(true);
        wedge_action_->setEnabled(true);
        fillet_action_->setEnabled(!document.history.empty());
        chamfer_action_->setEnabled(!document.history.empty());
        construction_point_action_->setEnabled(true);
        construction_axis_action_->setEnabled(true);
        construction_plane_action_->setEnabled(true);
        const bool has_selected_sketch =
            !active_sketch_id_.empty() || !selected_sketch_id_.empty();
        extrusion_action_->setEnabled(has_selected_sketch);
        revolution_action_->setEnabled(has_selected_sketch);
        sketch_action_->setEnabled(true);
        sketch_normal_view_action_->setEnabled(!active_sketch_id_.empty());
        sketch_external_reference_action_->setEnabled(!active_sketch_id_.empty());
        sketch_point_action_->setEnabled(!active_sketch_id_.empty());
        sketch_construction_action_->setEnabled(!active_sketch_id_.empty());
        sketch_segment_action_->setEnabled(!active_sketch_id_.empty());
        sketch_polyline_action_->setEnabled(!active_sketch_id_.empty());
        sketch_rectangle_action_->setEnabled(!active_sketch_id_.empty());
        sketch_polygon_action_->setEnabled(!active_sketch_id_.empty());
        sketch_trim_action_->setEnabled(
            !active_sketch_id_.empty() && !sketch_trim_active_);
        sketch_mirror_action_->setEnabled(
            !active_sketch_id_.empty() && !sketch_mirror_active_ &&
            !sketch_trim_active_);
        sketch_circle_action_->setEnabled(!active_sketch_id_.empty());
        sketch_arc_action_->setEnabled(!active_sketch_id_.empty());
        sketch_ellipse_action_->setEnabled(!active_sketch_id_.empty());
        sketch_elliptical_arc_action_->setEnabled(!active_sketch_id_.empty());
        sketch_bspline_action_->setEnabled(!active_sketch_id_.empty());
        sketch_text_action_->setEnabled(!active_sketch_id_.empty());
        sketch_constraints_action_->setEnabled(!active_sketch_id_.empty());
        sketch_dimensions_action_->setEnabled(!active_sketch_id_.empty());
        sketch_horizontal_action_->setEnabled(!selected_sketch_segment_id_.empty());
        sketch_vertical_action_->setEnabled(!selected_sketch_segment_id_.empty());
        sketch_coincident_action_->setEnabled(!active_sketch_id_.empty());
        sketch_midpoint_action_->setEnabled(!active_sketch_id_.empty());
        sketch_symmetric_action_->setEnabled(!active_sketch_id_.empty());
        sketch_concentric_action_->setEnabled(!active_sketch_id_.empty());
        sketch_tangent_action_->setEnabled(!active_sketch_id_.empty());
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
        finish_sketch_action_->setEnabled(!active_sketch_id_.empty());
        regenerate_part_action_->setEnabled(true);
        undo_action_->setEnabled(part->session.can_undo());
        redo_action_->setEnabled(part->session.can_redo());
        if (sketch_trim_active_) {
            for (auto* action : {
                    box_action_, cylinder_action_, sphere_action_, cone_action_,
                    pyramid_action_, wedge_action_, construction_point_action_,
                    construction_axis_action_, construction_plane_action_,
                    extrusion_action_, revolution_action_, fillet_action_,
                    chamfer_action_, sketch_action_, sketch_point_action_,
                    sketch_construction_action_, sketch_segment_action_,
                    sketch_polyline_action_, sketch_rectangle_action_,
                    sketch_polygon_action_, sketch_trim_action_, sketch_mirror_action_,
                    sketch_circle_action_, sketch_arc_action_, sketch_ellipse_action_,
                    sketch_elliptical_arc_action_,
                    sketch_bspline_action_, sketch_text_action_,
                    sketch_external_reference_action_,
                    sketch_constraints_action_,
                    sketch_dimensions_action_, sketch_horizontal_action_,
                    sketch_vertical_action_, sketch_coincident_action_,
                    sketch_midpoint_action_,
                    sketch_symmetric_action_,
                    sketch_concentric_action_,
                    sketch_tangent_action_,
                    sketch_parallel_action_, sketch_perpendicular_action_,
                    sketch_equal_length_action_, sketch_dimension_action_,
                    sketch_dimension_x_action_, sketch_dimension_y_action_,
                    sketch_angle_dimension_action_, sketch_radius_dimension_action_,
                    sketch_diameter_dimension_action_,
                    sketch_ellipse_major_dimension_action_,
                    sketch_ellipse_minor_dimension_action_,
                    sketch_ellipse_rotation_dimension_action_,
                    sketch_fix_point_action_, finish_sketch_action_,
                    regenerate_part_action_, regenerate_document_action_,
                    save_action_, save_as_action_, close_document_action_,
                    undo_action_, redo_action_}) {
                action->setEnabled(false);
            }
        }
        update_application_actions();
        rebuild_application_toolbar();
        return;
    }
    const auto& document = assembly->session.document();
    const auto* active_part =
        workspace_.open_part(workspace_.active_document_id());
    const auto active_part_occurrence = active_part == nullptr
        ? std::optional<std::string>{}
        : resolve_active_occurrence(active_part->session.document().document_id);
    if (active_part != nullptr && !active_sketch_id_.empty() &&
        std::none_of(active_part->session.document().sketches.begin(),
            active_part->session.document().sketches.end(),
            [&](const auto& sketch) { return sketch.id == active_sketch_id_; })) {
        active_sketch_id_.clear();
        cancel_sketch_segment();
    }
    if (active_part_occurrence && !active_part_occurrence->empty()) {
        const auto top_assembly_id = document.document_id;
        const auto occurrence_path = zima::assembly::InstancePath::decode(
            *active_part_occurrence);
        viewer_->set_transient_point_transform(
            [this, top_assembly_id, occurrence_path](const auto& point) {
                return workspace_.occurrence_point_to_scene(
                    top_assembly_id, occurrence_path, point);
            });
    }
    tree_->setHeaderLabels({tr("SESTAVA-%1").arg(
        QString::fromStdString(document.name))});
    auto* root = new QTreeWidgetItem(tree_, {QString::fromStdString(document.name)});
    add_assembly_tree_children(root, document.document_id, {});
    if (document.document_id == workspace_.active_document_id()) {
        add_mate_tree_children(root, document.document_id);
    }
    root->setExpanded(true);
    viewer_->set_selection_contract(!selection_action_->isChecked()
        ? std::vector<zima::viewer::CandidateKind>{}
        : active_part != nullptr && sketch_trim_active_
            ? std::vector{zima::viewer::CandidateKind::SketchTrimPiece}
        : active_part != nullptr && sketch_external_reference_active_
            ? std::vector{zima::viewer::CandidateKind::Edge,
                          zima::viewer::CandidateKind::Vertex,
                          zima::viewer::CandidateKind::Axis,
                          zima::viewer::CandidateKind::Face}
        : active_part != nullptr && !active_sketch_id_.empty()
            ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                          zima::viewer::CandidateKind::SketchPoint,
                          zima::viewer::CandidateKind::Dimension,
                          zima::viewer::CandidateKind::SketchCurve,
                          zima::viewer::CandidateKind::SketchText,
                          zima::viewer::CandidateKind::SketchExternalReference}
        : [this] {
            switch (selection_filter_combo_->currentIndex()) {
                case 1:
                case 4:
                    return std::vector{zima::viewer::CandidateKind::Face};
                case 2:
                    return std::vector{zima::viewer::CandidateKind::Vertex};
                case 3:
                    return std::vector{zima::viewer::CandidateKind::Axis};
                default:
                    return std::vector{zima::viewer::CandidateKind::Dimension,
                                       zima::viewer::CandidateKind::Occurrence};
            }
        }());
    if (active_part != nullptr && sketch_external_reference_active_ &&
        active_part_occurrence && !active_part_occurrence->empty()) {
        const auto allowed_local_owners = sketch_external_reference_source_owners(
            active_part->session.document(), active_sketch_id_);
        const auto active_document_id = active_part->session.document().document_id;
        const auto top_assembly_id = document.document_id;
        const auto dependent_path = *active_part_occurrence;
        viewer_->set_candidate_filter(
            [this, allowed_local_owners, active_document_id,
             top_assembly_id, dependent_path](const auto& candidate) {
                if (candidate.geometry !=
                        zima::viewer::CandidateGeometry::OriginalReference ||
                    candidate.instance_path.empty()) return false;
                if (candidate.instance_path == dependent_path) {
                    return allowed_local_owners.contains(candidate.owner_id);
                }
                try {
                    const auto address = workspace_.resolve_occurrence(
                        top_assembly_id, zima::assembly::InstancePath::decode(
                            candidate.instance_path));
                    return address && address->source_kind ==
                            zima::assembly::ComponentSourceKind::Part &&
                        address->source_document_id != active_document_id;
                } catch (const std::invalid_argument&) {
                    return false;
                }
            });
    }
    if (part_rollback_ && !part_rollback_->instance_path.empty()) {
        const auto* active_part =
            workspace_.open_part(part_rollback_->part_document_id);
        if (active_part != nullptr) {
            zima::kernel::BodyResult boundary = part_rollback_->input_body
                .value_or(zima::kernel::BodyResult{});
            viewer_->set_mesh(workspace_.build_scene_with_part_override(
                document.document_id,
                zima::assembly::InstancePath::decode(part_rollback_->instance_path),
                std::move(boundary)));
        } else {
            viewer_->set_mesh(document.build_scene());
        }
    } else {
        if (active_part_occurrence && !active_part_occurrence->empty()) {
            zima::kernel::BodyResult live_source;
            if (!active_part->session.calculated_boundaries().empty()) {
                live_source = active_part->session.calculated_boundaries().back();
            }
            for (const auto& sketch : active_part->session.document().sketches) {
                if (!active_sketch_id_.empty() && sketch.id != active_sketch_id_) {
                    continue;
                }
                const auto* displayed_sketch = sketch_trim_active_ &&
                        sketch_trim_preview_ && sketch.id == active_sketch_id_
                    ? &*sketch_trim_preview_ : &sketch;
                auto sketch_mesh = displayed_sketch->viewer_mesh();
                if (sketch_trim_active_ && sketch_trim_preview_ &&
                    sketch.id == active_sketch_id_) {
                    std::set<std::string> piece_geometry_ids;
                    for (const auto& piece : sketch_trim_topology_) {
                        piece_geometry_ids.insert(piece.geometry_id);
                    }
                    std::erase_if(sketch_mesh.edges, [&](const auto& edge) {
                        const auto separator = edge.reference.semantic_key.find(':');
                        return separator != std::string::npos &&
                            piece_geometry_ids.contains(
                                edge.reference.semantic_key.substr(separator + 1));
                    });
                    for (std::size_t index = 0;
                         index < sketch_trim_topology_.size(); ++index) {
                        zima::kernel::ViewerEdge edge;
                        edge.reference = {displayed_sketch->id,
                            "trim_piece:" + std::to_string(index), {}};
                        edge.overlay = true;
                        for (const auto& point : sketch_trim_topology_[index].points) {
                            edge.points.push_back(
                                displayed_sketch->world_point(point[0], point[1]));
                        }
                        sketch_mesh.edges.push_back(std::move(edge));
                    }
                }
                append_mesh(live_source.mesh, std::move(sketch_mesh));
            }
            append_mesh(live_source.mesh,
                active_part->session.document().construction_viewer_mesh());
            viewer_->set_mesh(workspace_.build_scene_with_part_override(
                document.document_id,
                zima::assembly::InstancePath::decode(*active_part_occurrence),
                std::move(live_source)));
        } else {
            viewer_->set_mesh(document.build_scene());
        }
    }
    state_->setText(document.components.empty()
        ? has_insertable_component()
            ? tr("Nová sestava: zvolte Vložit otevřený dokument.")
            : tr("Nová sestava: nejprve vytvořte a vypočtěte Part, potom jej zde vložte.")
        : tr("Zobrazená sestava: %1\nAktivní dokument: %2")
            .arg(QString::fromStdString(document.name),
                 QString::fromStdString(workspace_.active_document_id())));
    rebuild_insert_menu();
    insert_action_->setEnabled(has_insertable_component());
    regenerate_action_->setEnabled(true);
    plane_mate_action_->setEnabled(
        workspace_.open_assembly(workspace_.active_document_id()) != nullptr);
    axis_mate_action_->setEnabled(
        workspace_.open_assembly(workspace_.active_document_id()) != nullptr);
    point_mate_action_->setEnabled(
        workspace_.open_assembly(workspace_.active_document_id()) != nullptr);
    angle_mate_action_->setEnabled(
        workspace_.open_assembly(workspace_.active_document_id()) != nullptr);
    plane_angle_mate_action_->setEnabled(
        workspace_.open_assembly(workspace_.active_document_id()) != nullptr);
    save_action_->setEnabled(true);
    save_as_action_->setEnabled(true);
    close_document_action_->setEnabled(true);
    regenerate_document_action_->setEnabled(true);
    box_action_->setEnabled(active_part != nullptr);
    cylinder_action_->setEnabled(active_part != nullptr);
    sphere_action_->setEnabled(active_part != nullptr);
    cone_action_->setEnabled(active_part != nullptr);
    pyramid_action_->setEnabled(active_part != nullptr);
    wedge_action_->setEnabled(active_part != nullptr);
    fillet_action_->setEnabled(active_part != nullptr &&
        !active_part->session.document().history.empty());
    chamfer_action_->setEnabled(active_part != nullptr &&
        !active_part->session.document().history.empty());
    construction_point_action_->setEnabled(active_part != nullptr);
    construction_axis_action_->setEnabled(active_part != nullptr);
    construction_plane_action_->setEnabled(active_part != nullptr);
    const bool active_part_has_selected_sketch = active_part != nullptr &&
        (!active_sketch_id_.empty() || !selected_sketch_id_.empty());
    extrusion_action_->setEnabled(active_part_has_selected_sketch);
    revolution_action_->setEnabled(active_part_has_selected_sketch);
    sketch_action_->setEnabled(active_part != nullptr);
    const bool has_active_part_sketch =
        active_part != nullptr && !active_sketch_id_.empty();
    sketch_normal_view_action_->setEnabled(has_active_part_sketch);
    sketch_external_reference_action_->setEnabled(has_active_part_sketch);
    sketch_point_action_->setEnabled(has_active_part_sketch);
    sketch_construction_action_->setEnabled(has_active_part_sketch);
    sketch_segment_action_->setEnabled(has_active_part_sketch);
    sketch_polyline_action_->setEnabled(has_active_part_sketch);
    sketch_rectangle_action_->setEnabled(has_active_part_sketch);
    sketch_polygon_action_->setEnabled(has_active_part_sketch);
    sketch_trim_action_->setEnabled(has_active_part_sketch && !sketch_trim_active_);
    sketch_mirror_action_->setEnabled(
        has_active_part_sketch && !sketch_mirror_active_ && !sketch_trim_active_);
    sketch_circle_action_->setEnabled(has_active_part_sketch);
    sketch_arc_action_->setEnabled(has_active_part_sketch);
    sketch_ellipse_action_->setEnabled(has_active_part_sketch);
    sketch_elliptical_arc_action_->setEnabled(has_active_part_sketch);
    sketch_bspline_action_->setEnabled(has_active_part_sketch);
    sketch_text_action_->setEnabled(has_active_part_sketch);
    sketch_constraints_action_->setEnabled(has_active_part_sketch);
    sketch_dimensions_action_->setEnabled(has_active_part_sketch);
    const bool has_segment = has_active_part_sketch &&
        !selected_sketch_segment_id_.empty();
    sketch_horizontal_action_->setEnabled(has_segment);
    sketch_vertical_action_->setEnabled(has_segment);
    sketch_coincident_action_->setEnabled(has_active_part_sketch);
    sketch_midpoint_action_->setEnabled(has_active_part_sketch);
    sketch_symmetric_action_->setEnabled(has_active_part_sketch);
    sketch_concentric_action_->setEnabled(has_active_part_sketch);
    sketch_tangent_action_->setEnabled(has_active_part_sketch);
    sketch_parallel_action_->setEnabled(has_active_part_sketch);
    sketch_perpendicular_action_->setEnabled(has_active_part_sketch);
    sketch_equal_length_action_->setEnabled(has_active_part_sketch);
    sketch_dimension_action_->setEnabled(has_segment);
    sketch_dimension_x_action_->setEnabled(has_segment);
    sketch_dimension_y_action_->setEnabled(has_segment);
    sketch_angle_dimension_action_->setEnabled(has_segment);
    sketch_radius_dimension_action_->setEnabled(has_active_part_sketch &&
        (!selected_sketch_circle_id_.empty() || !selected_sketch_arc_id_.empty()));
    sketch_diameter_dimension_action_->setEnabled(has_active_part_sketch &&
        !selected_sketch_circle_id_.empty());
    sketch_ellipse_major_dimension_action_->setEnabled(has_active_part_sketch &&
        !selected_sketch_ellipse_id_.empty());
    sketch_ellipse_minor_dimension_action_->setEnabled(has_active_part_sketch &&
        !selected_sketch_ellipse_id_.empty());
    sketch_ellipse_rotation_dimension_action_->setEnabled(has_active_part_sketch &&
        !selected_sketch_ellipse_id_.empty());
    sketch_fix_point_action_->setEnabled(has_active_part_sketch &&
        !selected_sketch_point_id_.empty());
    finish_sketch_action_->setEnabled(has_active_part_sketch);
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
    update_application_actions();
    rebuild_application_toolbar();
}

void AssemblyWorkspaceWindow::add_part_tree_children(
    QTreeWidgetItem* parent,
    const zima::document::PartDocument& document) {
    for (std::size_t index = 0; index < document.history.size(); ++index) {
        const auto& container = document.history[index];
        auto* item = new QTreeWidgetItem(
            parent, {QString::fromStdString(container.name)});
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
        auto* item = new QTreeWidgetItem(
            parent, {QString::fromStdString(sketch.name)});
        item->setData(0, Qt::UserRole, QString::fromStdString(sketch.id));
        item->setData(0, Qt::UserRole + 3, "part-sketch");
        item->setSelected(sketch.id == selected_sketch_id_);
        for (const auto& dimension : sketch.dimensions) {
            const auto label = dimension.kind == zima::sketcher::DimensionKind::Radius
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
            auto* child = new QTreeWidgetItem(item, {label});
            child->setData(
                0, Qt::UserRole, QString::fromStdString(dimension.id));
            child->setData(0, Qt::UserRole + 3, "part-sketch-dimension");
            child->setData(
                0, Qt::UserRole + 4, QString::fromStdString(sketch.id));
        }
        item->setExpanded(true);
    }
    for (const auto& object : document.constructions) {
        auto* item = new QTreeWidgetItem(
            parent, {QString::fromStdString(object.name)});
        item->setData(0, Qt::UserRole, QString::fromStdString(object.id));
        item->setData(0, Qt::UserRole + 3, "part-construction");
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
        if (parent_path.occurrence_ids.empty()) {
            if (const auto* owner = workspace_.open_assembly(
                    owner_assembly_document_id)) {
                const auto& live = owner->session.document();
                if (live.find_occurrence(component.occurrence_id) != nullptr &&
                    !component.grounded && !suppressed) {
                    label += tr(" [%1 DOF]").arg(
                        live.remaining_degrees_of_freedom(component.occurrence_id));
                }
            }
        }
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
        const bool active_occurrence =
            path.encoded() == active_occurrence_path_ &&
            component.source_document_id == workspace_.active_document_id();
        if ((part_rollback_ && path.encoded() == part_rollback_->instance_path) ||
            active_occurrence) {
            item->setForeground(0, QBrush(QColor(70, 190, 95)));
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
        } else if (suppressed || !component.visible) {
            item->setForeground(0, QBrush(QColor(125, 125, 125)));
        }
        if (component.source_kind == zima::assembly::ComponentSourceKind::Assembly) {
            const auto* active_source = active_occurrence
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
        } else if (active_occurrence) {
            const auto* active_part = workspace_.open_part(component.source_document_id);
            if (active_part != nullptr) {
                add_part_tree_children(item, active_part->session.document());
                item->setExpanded(true);
            }
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
    const auto parent_path =
        zima::assembly::InstancePath::decode(instance_path).parent();
    auto* select_parent = parent_path
        ? menu.addAction(tr("Vybrat rodiče")) : nullptr;
    auto* properties = menu.addAction(tr("Vlastnosti"));
    auto* visibility = menu.addAction(
        occurrence->visible ? tr("Skrýt") : tr("Zobrazit"));
    auto* suppression = menu.addAction(
        occurrence->suppressed ? tr("Obnovit") : tr("Potlačit"));
    auto* grounding = menu.addAction(
        occurrence->grounded ? tr("Uvolnit") : tr("Uzemnit"));
    const QAction* selected = menu.exec(global_position);
    if (selected == select_parent && parent_path) {
        const std::string encoded = parent_path->encoded();
        viewer_->confirm_occurrence(encoded);
        select_occurrence(encoded);
        return;
    }
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
            if (!next.replace_mate_and_calculate(std::move(committed))) {
                throw std::runtime_error(
                    "Změna vazby je mimo povolené meze nebo je v konfliktu s jinou vazbou");
            }
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
