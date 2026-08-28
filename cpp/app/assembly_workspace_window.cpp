#include "assembly_workspace_window.hpp"
#include "component_properties_dialog.hpp"
#include "primitive_properties_dialog.hpp"
#include "construction_properties_dialog.hpp"
#include "orientation_dialog.hpp"
#include "sketch_properties_dialog.hpp"
#include "sketch_bspline_properties_dialog.hpp"
#include "sketch_text_properties_dialog.hpp"
#include "sketch_dimension_properties_dialog.hpp"
#include "drawing_window.hpp"
#include "document_tools_dialogs.hpp"
#include "construction_reference_candidate_policy.hpp"
#include "file_dialog.hpp"
#include "global_settings_dialog.hpp"
#include "resource_icon.hpp"

#include <zima/viewer/mesh_view.hpp>
#include <zima/interchange/interchange.hpp>
#include <zima/interchange/dxf.hpp>
#include <zima/interchange/step.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QAbstractItemView>
#include <QBrush>
#include <QComboBox>
#include <QColor>
#include <QCursor>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
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
#include <QMouseEvent>
#include <QKeySequence>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QTextStream>
#include <QTimer>
#include <QPixmap>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

namespace zima::app {
namespace {

// Builds the precise per-entity key set for every currently-toggled
// highlighted reference row. Each ConstructionReference already carries a
// (owner_id, semantic_key, instance_path) triple that uniquely identifies
// one specific entity -- e.g. one exact Origin plane/axis/point
// ("origin:plane:xz" vs "origin:plane:xy"), or one exact face of a body
// ("plocha 6" vs "plocha 3") -- even though many sibling entities share the
// same owner_id (the Origin's own three planes and three axes, or every
// face of the same body). Matching by owner_id alone (as
// highlighted_reference_owner_ids() does, kept only for whole-container
// references that have no such siblings) would wrongly highlight every one
// of those siblings at once instead of just the single referenced entity.
std::set<zima::viewer::EdgeKey> highlighted_reference_edge_keys(
    const auto& dialog) {
    std::set<zima::viewer::EdgeKey> keys;
    for (const auto& reference : dialog.highlighted_reference_entries()) {
        keys.insert(zima::viewer::EdgeKey{
            reference.owner_id, reference.semantic_key, reference.instance_path});
        // A standalone Plane persists its semantic entity as "plane", while
        // its only visible/pickable geometry is the rectangular "border".
        // They are one object, so reference highlighting must address that
        // border without broad owner-only matching.
        if (reference.semantic_key == "plane") {
            keys.insert(zima::viewer::EdgeKey{
                reference.owner_id, "border", reference.instance_path});
        }
    }
    return keys;
}

// Every primitive solid shares the universal container placement UI
// (position/orientation reference tables) wired in PrimitivePropertiesDialog.
bool supports_placement_reference_picking(zima::document::FeatureKind kind) {
    using zima::document::FeatureKind;
    return kind == FeatureKind::Box || kind == FeatureKind::Cylinder ||
        kind == FeatureKind::Sphere || kind == FeatureKind::Cone ||
        kind == FeatureKind::Pyramid || kind == FeatureKind::Wedge ||
        kind == FeatureKind::Extrusion || kind == FeatureKind::Revolution ||
        kind == FeatureKind::ImportedStep;
}

bool sketch_visible_outside_sketcher(
    const zima::document::PartDocument& document,
    const zima::sketcher::Sketch& sketch) {
    const auto* owner = document.find_container(sketch.owner_container_id);
    return owner != nullptr &&
        owner->feature_kind == zima::document::FeatureKind::Sketch;
}

// A picked reference supports an editable offset when it is a planar
// surface: either a solid/sketch Face, or a construction/origin datum Plane
// (Container candidate whose semantic_key resolves to "plane"), matching
// Python's `_reference_supports_offset()` (also true for EntityKind.PLANE).
bool candidate_supports_offset(const zima::viewer::ViewerCandidate& candidate) {
    return candidate.kind == zima::viewer::CandidateKind::Plane ||
        candidate.kind == zima::viewer::CandidateKind::Face;
}

// A picked reference is a directional (planar/linear) candidate when it is a
// Face, Edge or Axis -- including the persisted origin plane/axis overlays,
// whose semantic keys are "origin:plane:*"/"origin:axis:*" -- matching
// Python's `is_orientation_candidate` test in `_add_reference()`.  Such a
// reference simultaneously fixes both position AND, unlike a bare vertex,
// part of the object's orientation.
bool candidate_drives_rotation(const zima::viewer::ViewerCandidate& candidate) {
    return candidate.kind == zima::viewer::CandidateKind::Plane ||
        candidate.kind == zima::viewer::CandidateKind::Face ||
        candidate.kind == zima::viewer::CandidateKind::Edge ||
        candidate.kind == zima::viewer::CandidateKind::Axis ||
        candidate.semantic_key.starts_with("origin:plane:") ||
        candidate.semantic_key.starts_with("origin:axis:");
}

// Whether the classic history-order shortcut ("1st point = origin, 2nd =
// axis direction" for an Axis; "1st point = origin, 2nd/3rd = plane-defining
// points" for a Plane) already has every point it needs -- used to stop
// offering further reference rows once the container's direction/normal is
// fully determined this way, since the generic rotation-DOF count cannot
// see it (a bare point/vertex is never marked orientation-driving).
bool construction_shortcut_satisfied(zima::document::ConstructionKind kind,
    const std::vector<zima::document::ConstructionReference>& references,
    const zima::kernel::ViewerReferenceGeometry& geometry) {
    const std::size_t required =
        kind == zima::document::ConstructionKind::Axis ? 2
        : kind == zima::document::ConstructionKind::Plane ? 3 : 0;
    if (required == 0 || references.size() < required) return false;
    return std::all_of(references.begin(), references.end(),
        [&](const auto& reference) {
            return !reference.orientation_drives_rotation &&
                zima::document::construction_reference_is_point(
                    reference, geometry);
        });
}

// Assigns the next unused orientation role ("normal" first, then "up") to a
// newly accepted Point position reference that drives rotation, matching
// Python's `_default_orientation_role()`/`_ensure_automatic_orientation_roles()`.
// A Point container has no dedicated orientation-reference table: the same
// position reference simultaneously participates in placement (equations
// solved by `resolve_construction`) and, once marked, in the rotation-DOF
// count via `orientation_constraint_remaining_dof(..., marked_only=true)`.
void assign_automatic_orientation_role(
    zima::document::ConstructionReference& reference,
    const std::vector<zima::document::ConstructionReference>& existing) {
    std::set<std::string> used_roles;
    for (const auto& other : existing) {
        if (other.orientation_drives_rotation) used_roles.insert(other.orientation_role);
    }
    reference.orientation_role = !used_roles.contains("front") ? "front"
        : !used_roles.contains("top") ? "top" : "none";
    reference.orientation_drives_rotation = reference.orientation_role != "none";
}

class HistoryTreeWidget final : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    std::function<void(std::size_t)> history_cursor_moved;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        auto* item = itemAt(event->position().toPoint());
        if (event->button() == Qt::LeftButton && item == nullptr) {
            clearSelection();
            setCurrentItem(nullptr);
            event->accept();
            return;
        }
        if (event->button() == Qt::RightButton &&
            property("commandSelectionActive").toBool()) {
            event->accept();
            return;
        }
        if (event->button() == Qt::RightButton && item != nullptr &&
            item->isSelected()) {
            // Preserve Ctrl multi-selection until the custom context menu is
            // evaluated, matching the Python HistoryTreeWidget contract.
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && item != nullptr &&
            item->data(0, Qt::UserRole + 3).toString() == "part-insert-here") {
            dragging_cursor_ = true;
            drag_started_ = false;
            drag_origin_ = event->position().toPoint();
            setCurrentItem(item);
            event->accept();
            return;
        }
        QTreeWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!dragging_cursor_) return QTreeWidget::mouseMoveEvent(event);
        if (!drag_started_ &&
            (event->position().toPoint() - drag_origin_).manhattanLength() >=
                QApplication::startDragDistance()) {
            drag_started_ = true;
            viewport()->setCursor(Qt::ClosedHandCursor);
        }
        if (drag_started_) {
            insertion_y_ = event->position().toPoint().y();
            viewport()->update();
        }
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (!dragging_cursor_) return QTreeWidget::mouseReleaseEvent(event);
        dragging_cursor_ = false;
        viewport()->unsetCursor();
        if (drag_started_ && history_cursor_moved) {
            std::size_t cursor = 0;
            auto* root = topLevelItemCount() == 1 ? topLevelItem(0) : nullptr;
            if (root != nullptr) {
                for (int index = 0; index < root->childCount(); ++index) {
                    auto* child = root->child(index);
                    const auto role = child->data(0, Qt::UserRole + 3).toString();
                    if (role != "part-container" && role != "part-sketch" &&
                        role != "part-construction") continue;
                    if (visualItemRect(child).center().y() <
                        event->position().toPoint().y()) ++cursor;
                }
            }
            history_cursor_moved(cursor);
        }
        drag_started_ = false;
        insertion_y_.reset();
        viewport()->update();
        event->accept();
    }

    void paintEvent(QPaintEvent* event) override {
        QTreeWidget::paintEvent(event);
        if (!insertion_y_) return;
        QPainter painter(viewport());
        painter.setPen(QPen(QColor("#4DD811"), 2));
        painter.drawLine(4, *insertion_y_, viewport()->width() - 4, *insertion_y_);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            event->accept();
            return;
        }
        QTreeWidget::mouseDoubleClickEvent(event);
    }

private:
    bool dragging_cursor_{};
    bool drag_started_{};
    QPoint drag_origin_;
    std::optional<int> insertion_y_;
};

void apply_start_part_template(
    zima::document::PartDocument& document,
    const ApplicationSettings& settings) {
    QString path = settings.part_template;
    if (!QDir::isAbsolutePath(path)) {
        path = QDir(settings.resolved_paths.value("Templates")).absoluteFilePath(path);
    }
    if (!QFileInfo::exists(path)) return;
    QSettings source(path, QSettings::IniFormat);
    const auto copy_group = [&source](const char* group,
                                      std::map<std::string, std::string>& target) {
        source.beginGroup(QString::fromLatin1(group));
        target.clear();
        for (const auto& key : source.childKeys())
            target[key.toStdString()] = source.value(key).toString().toStdString();
        source.endGroup();
    };
    copy_group("DocumentUnits", document.document_units);
    copy_group("DocumentPrecision", document.document_precision);
    copy_group("MaterialProperties", document.physical_parameters);
    copy_group("MaterialUnits", document.physical_parameter_units);
    copy_group("UserParameterValues", document.user_parameters);
    source.beginGroup("UserParameters");
    const QStringList parameter_order = source.value("Order").toStringList();
    source.endGroup();
    document.user_parameter_order.clear();
    for (const auto& key : parameter_order)
        document.user_parameter_order.push_back(key.trimmed().toStdString());
    source.beginGroup("UserParameterLabels");
    document.user_parameter_labels.clear();
    for (const auto& key : source.childGroups()) {
        source.beginGroup(key);
        for (const auto& language : source.childKeys()) {
            document.user_parameter_labels[key.toStdString()][language.toStdString()] =
                source.value(language).toString().toStdString();
        }
        source.endGroup();
    }
    source.endGroup();
    document.user_parameter_values.clear();
    for (const auto& [key, value] : document.user_parameters)
        document.user_parameter_values[key][""] = value;
    source.beginGroup("Material");
    const QString material_name = source.value("Name").toString();
    source.endGroup();
    if (!material_name.isEmpty())
        document.physical_parameters["MATERIAL_NAME"] = material_name.toStdString();
    QString relations = QStringLiteral("[]");
    QFile raw_source(path);
    if (raw_source.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&raw_source);
        bool in_relations = false;
        while (!stream.atEnd()) {
            const QString line = stream.readLine();
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
                in_relations = trimmed.compare(
                    QStringLiteral("[Relations]"), Qt::CaseInsensitive) == 0;
                continue;
            }
            if (in_relations && trimmed.startsWith(
                    QStringLiteral("Data"), Qt::CaseInsensitive)) {
                const int equals = line.indexOf('=');
                if (equals >= 0) relations = line.mid(equals + 1).trimmed();
                break;
            }
        }
    }
    const auto parsed = nlohmann::json::parse(relations.toStdString());
    if (!parsed.is_array()) throw std::invalid_argument(
        "Start Part template Relations/Data must be a JSON array");
    document.relations.clear();
    for (const auto& relation : parsed) {
        document.relations.push_back({relation.at("target").get<std::string>(),
            relation.at("expression").get<std::string>()});
    }
}

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
    target.constraint_markers.insert(target.constraint_markers.end(),
        source.constraint_markers.begin(), source.constraint_markers.end());
    append_reference_geometry(
        target.original_references, std::move(source.original_references));
}

void keep_only_inactive_sketch_profile(
    zima::kernel::ViewerMesh& mesh) {
    // Ordinary Part/Assembly view presents a Sketch as its clean result
    // profile. Editing aids belong exclusively to active Sketcher.
    std::erase_if(mesh.edges, [](const auto& edge) {
        const auto& key = edge.reference.semantic_key;
        const bool native_profile = key.starts_with("segment:") ||
            key.starts_with("circle:") || key.starts_with("arc:") ||
            key.starts_with("ellipse:") ||
            key.starts_with("elliptical_arc:") ||
            key.starts_with("bspline:") || key.starts_with("text:");
        return edge.construction || !native_profile;
    });
    std::erase_if(mesh.points, [](const auto& point) {
        return point.construction ||
            (!point.reference.semantic_key.starts_with("point:") &&
             point.reference.semantic_key !=
                "external_point:sketch_origin");
    });
    // Keep the work-plane origin available for whole-Sketch highlighting,
    // but do not publish it as an ordinary external reference or draw it
    // while the inactive Sketch is idle.  It is a presentation companion of
    // the Sketch container, not an independently selectable object here.
    for (auto& point : mesh.points) {
        if (point.reference.semantic_key ==
            "external_point:sketch_origin") {
            point.reference.semantic_key = "sketch:origin-marker";
            point.always_visible = false;
        }
    }
    mesh.axes.clear();
    mesh.dimensions.clear();
    mesh.constraint_markers.clear();
    mesh.original_references = {};
}

QTreeWidgetItem* add_origin_tree_item(QTreeWidgetItem* parent,
    const std::string& document_id, bool assembly,
    const zima::assembly::InstancePath& instance_path = {}) {
    auto* origin = new QTreeWidgetItem(parent, {
        assembly ? QObject::tr("Počátek sestavy") : QObject::tr("Počátek dílu")});
    origin->setIcon(0, resource_icon("origin"));
    origin->setData(0, Qt::UserRole,
        QString::fromStdString(document_id + ":origin"));
    origin->setData(0, Qt::UserRole + 1,
        QString::fromStdString(instance_path.encoded()));
    origin->setData(0, Qt::UserRole + 3, "document-origin");
    const std::array children{
        std::pair{QObject::tr("Point"), "point"},
        std::pair{QObject::tr("X Axis"), "axis:x"},
        std::pair{QObject::tr("Y Axis"), "axis:y"},
        std::pair{QObject::tr("Z Axis"), "axis:z"},
        std::pair{QObject::tr("XY Plane"), "plane:xy"},
        std::pair{QObject::tr("YZ Plane"), "plane:yz"},
        std::pair{QObject::tr("XZ Plane"), "plane:xz"}};
    for (const auto& [label, key] : children) {
        auto* child = new QTreeWidgetItem(origin, {label});
        child->setIcon(0, resource_icon(
            key == std::string_view("point") ? "point" :
            std::string_view(key).starts_with("axis:") ? "axis" : "plane"));
        child->setData(0, Qt::UserRole,
            QString::fromStdString(document_id + ":origin"));
        child->setData(0, Qt::UserRole + 1,
            QString::fromStdString(instance_path.encoded()));
        child->setData(0, Qt::UserRole + 3, "origin-reference");
        child->setData(0, Qt::UserRole + 5,
            QString::fromStdString(std::string("origin:") + key));
    }
    return origin;
}

QTreeWidgetItem* add_construction_origin_tree_item(QTreeWidgetItem* parent,
    const zima::document::ContainerOrigin& container_origin,
    const std::string& container_name,
    const zima::assembly::InstancePath& instance_path = {},
    bool point_kind_container = false) {
    auto* origin = new QTreeWidgetItem(parent, {QObject::tr("Počátek kontejneru")});
    origin->setIcon(0, resource_icon("origin"));
    origin->setData(0, Qt::UserRole, QString::fromStdString(container_origin.id));
    origin->setData(0, Qt::UserRole + 1,
        QString::fromStdString(instance_path.encoded()));
    origin->setData(0, Qt::UserRole + 3, "construction-origin");
    for (const auto& origin_child : container_origin.children) {
        const auto label = origin_child.kind == zima::document::OriginChildKind::Point
            ? QObject::tr("Point (%1)").arg(QString::fromStdString(container_name))
            : QString::fromStdString(origin_child.name);
        auto* child = new QTreeWidgetItem(origin, {label});
        child->setIcon(0, resource_icon(
            origin_child.kind == zima::document::OriginChildKind::Point ? "point" :
            origin_child.kind == zima::document::OriginChildKind::Axis ? "axis" : "plane"));
        child->setData(0, Qt::UserRole, QString::fromStdString(origin_child.id));
        child->setData(0, Qt::UserRole + 1,
            QString::fromStdString(instance_path.encoded()));
        child->setData(0, Qt::UserRole + 3, "origin-reference");
        // A Point-kind container's ":point" origin child IS the container's
        // own committed point (construction_viewer_mesh()'s Point branch
        // publishes it into the reference geometry as owner=container_origin
        // .id, semantic_key="point" -- no "origin:" prefix, unlike the
        // document's own root origin point or an Axis/Plane container's
        // local frame markers). Keeping the "origin:" prefix here for a
        // Point container made this node's semantic_key never match that
        // geometry entry, so picking it as a 2nd/3rd reference (the classic
        // "2 points define an axis"/"3 points define a plane" shortcut)
        // silently contributed zero constraint and never satisfied the
        // shortcut-completion check.
        const bool matches_own_point =
            point_kind_container &&
            origin_child.kind == zima::document::OriginChildKind::Point;
        child->setData(0, Qt::UserRole + 5, matches_own_point
            ? QStringLiteral("point")
            : QString::fromStdString(std::string("origin:") + origin_child.key));
        child->setData(0, Qt::UserRole + 6,
            QString::fromStdString(container_origin.id));
    }
    return origin;
}

QString feature_icon_name(zima::document::FeatureKind kind) {
    using zima::document::FeatureKind;
    switch (kind) {
        case FeatureKind::Sketch: return QStringLiteral("sketch");
        case FeatureKind::Box: return QStringLiteral("box");
        case FeatureKind::Cylinder: return QStringLiteral("cylinder");
        case FeatureKind::Sphere: return QStringLiteral("sphere");
        case FeatureKind::Cone: return QStringLiteral("cone");
        case FeatureKind::Pyramid: return QStringLiteral("pyramid");
        case FeatureKind::Wedge: return QStringLiteral("wedge");
        case FeatureKind::Extrusion: return QStringLiteral("protrusion");
        case FeatureKind::Revolution: return QStringLiteral("revolve");
        case FeatureKind::ImportedStep: return QStringLiteral("import-step");
        case FeatureKind::Fillet: return QStringLiteral("fillet");
        case FeatureKind::Chamfer: return QStringLiteral("chamfer");
    }
    return {};
}

void add_history_container_tree_children(QTreeWidgetItem* parent,
    const zima::document::HistoryContainer& container,
    const zima::assembly::InstancePath& instance_path = {},
    const zima::sketcher::Sketch* owned_sketch = nullptr) {
    add_construction_origin_tree_item(
        parent, container.container_origin, container.name, instance_path);
    if (owned_sketch != nullptr) {
        auto* plane = new QTreeWidgetItem(parent, {QObject::tr("Rovina")});
        plane->setIcon(0, resource_icon("plane"));
        plane->setData(0, Qt::UserRole,
            QString::fromStdString(owned_sketch->id));
        plane->setData(0, Qt::UserRole + 3, "part-sketch-plane");
        auto* sketch = new QTreeWidgetItem(parent,
            {QString::fromStdString(owned_sketch->name)});
        sketch->setIcon(0, resource_icon("sketch"));
        sketch->setData(0, Qt::UserRole,
            QString::fromStdString(owned_sketch->id));
        sketch->setData(0, Qt::UserRole + 3, "part-sketch");
    }
    if (container.feature_kind == zima::document::FeatureKind::Sketch) return;
    const QString operation = container.combine_mode ==
            zima::document::CombineMode::Subtract
        ? QStringLiteral("− ") : QStringLiteral("+ ");
    auto* feature = new QTreeWidgetItem(parent,
        {operation + QString::fromStdString(container.name)});
    feature->setIcon(0, resource_icon(feature_icon_name(container.feature_kind)));
    feature->setData(0, Qt::UserRole,
        QString::fromStdString(container.feature_id));
    feature->setData(0, Qt::UserRole + 1,
        QString::fromStdString(instance_path.encoded()));
    feature->setData(0, Qt::UserRole + 3, "part-container-entity");
    feature->setData(0, Qt::UserRole + 6,
        QString::fromStdString(container.id));
}

void add_construction_tree_children(QTreeWidgetItem* parent,
    const zima::document::ConstructionObject& object,
    const zima::assembly::InstancePath& instance_path = {}) {
    add_construction_origin_tree_item(
        parent, object.container_origin, object.name, instance_path,
        object.kind == zima::document::ConstructionKind::Point);
    if (object.kind == zima::document::ConstructionKind::Point) {
        return;
    }
    const bool axis = object.kind == zima::document::ConstructionKind::Axis;
    auto* entity = new QTreeWidgetItem(
        parent, {QString::fromStdString(object.name)});
    entity->setIcon(0, resource_icon(axis ? "axis" : "plane"));
    entity->setData(0, Qt::UserRole, QString::fromStdString(object.entity_id));
    entity->setData(0, Qt::UserRole + 1,
        QString::fromStdString(instance_path.encoded()));
    entity->setData(0, Qt::UserRole + 3, "construction-entity");
    entity->setData(0, Qt::UserRole + 5, axis ? "axis" : "plane");
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

std::string revolution_axis_segment_id(
    const zima::sketcher::Sketch& sketch,
    const std::string& configured_id = {}) {
    const auto valid_axis = [&](const auto& segment) {
        return segment.construction && segment.centerline;
    };
    if (!configured_id.empty()) {
        const auto configured = std::find_if(
            sketch.segments.begin(), sketch.segments.end(), [&](const auto& segment) {
                return segment.id == configured_id && valid_axis(segment);
            });
        if (configured != sketch.segments.end()) return configured->id;
    }
    std::string result;
    for (const auto& segment : sketch.segments) {
        if (!valid_axis(segment)) continue;
        if (!result.empty()) {
            throw std::runtime_error(
                "Skica rotace smí obsahovat právě jednu zelenou konstrukční osu.");
        }
        result = segment.id;
    }
    if (result.empty()) {
        throw std::runtime_error(
            "Ve skice rotace nakreslete zelenou konstrukční osu.");
    }
    return result;
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

QString sketch_constraint_label(zima::sketcher::ConstraintKind kind) {
    using Kind = zima::sketcher::ConstraintKind;
    switch (kind) {
    case Kind::Horizontal: return QObject::tr("Vodorovná vazba");
    case Kind::Vertical: return QObject::tr("Svislá vazba");
    case Kind::Coincident: return QObject::tr("Shodnost bodů");
    case Kind::Parallel: return QObject::tr("Rovnoběžnost");
    case Kind::Perpendicular: return QObject::tr("Kolmost");
    case Kind::EqualLength: return QObject::tr("Stejná délka");
    case Kind::EqualRadius: return QObject::tr("Stejný poloměr");
    case Kind::PointOnCircle: return QObject::tr("Bod na křivce");
    case Kind::PointOnLine: return QObject::tr("Bod na přímce");
    case Kind::Symmetric: return QObject::tr("Symetrie");
    case Kind::Midpoint: return QObject::tr("Bod ve středu");
    case Kind::Concentric: return QObject::tr("Soustřednost");
    case Kind::Tangent: return QObject::tr("Tečnost");
    }
    return QObject::tr("Vazba");
}

QString sketch_dimension_label(const zima::sketcher::SketchDimension& dimension) {
    using Kind = zima::sketcher::DimensionKind;
    const auto value = [value = dimension.value](const QString& pattern) {
        return pattern.arg(value, 0, 'f', 3);
    };
    switch (dimension.kind) {
    case Kind::Distance: return value(QObject::tr("Vzdálenost %1 mm"));
    case Kind::DistanceX: return value(QObject::tr("Vodorovná kóta X %1 mm"));
    case Kind::DistanceY: return value(QObject::tr("Svislá kóta Y %1 mm"));
    case Kind::DistancePointLine:
        return value(QObject::tr("Vzdálenost bod–přímka %1 mm"));
    case Kind::DistanceSymmetric:
        return value(QObject::tr("Symetrická kóta Ø%1 mm"));
    case Kind::DistanceLine:
        return value(QObject::tr("Vzdálenost rovnoběžek %1 mm"));
    case Kind::Radius: return value(QObject::tr("Poloměr R%1 mm"));
    case Kind::Diameter: return value(QObject::tr("Průměr Ø%1 mm"));
    case Kind::Angle: return value(QObject::tr("Úhlová kóta %1°"));
    case Kind::AngleThreePoint:
        return value(QObject::tr("Tříbodový úhel %1°"));
    case Kind::AngleBetween:
        return value(QObject::tr("Úhel mezi přímkami %1°"));
    case Kind::EllipseMajorRadius:
        return value(QObject::tr("Hlavní poloosa a=%1 mm"));
    case Kind::EllipseMinorRadius:
        return value(QObject::tr("Vedlejší poloosa b=%1 mm"));
    case Kind::EllipseRotation:
        return value(QObject::tr("Natočení elipsy %1°"));
    }
    return QObject::tr("Kóta");
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

zima::kernel::ViewerReferenceGeometry construction_reference_source_geometry(
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries) {
    zima::kernel::ViewerReferenceGeometry source;
    if (!calculated_boundaries.empty()) {
        source = calculated_boundaries.back().mesh.original_references;
    }
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

bool refresh_assembly_sketch_external_references(
    zima::assembly::AssemblyDocument& document) {
    const auto source = document.build_scene().original_references;
    bool changed = false;
    for (auto& sketch : document.sketches) {
        std::set<std::string> source_documents;
        for (const auto& reference : sketch.external_references) {
            if (!reference.source_document_id.empty()) {
                source_documents.insert(reference.source_document_id);
            }
        }
        for (const auto& source_document_id : source_documents) {
            changed = sketch.refresh_external_references(
                source_document_id, source) || changed;
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
        set_centered_on_show();
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
        add_type(
            layout, QObject::tr("Formát výkresu"), "drawing_format",
            "drawing-format", true);
        add_type(
            layout, QObject::tr("Razítko výkresu"), "title_block",
            "title-block", true);
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
        const QString stem = QFileInfo(name_->text().trimmed())
                                 .completeBaseName().trimmed();
        if (stem.isEmpty()) {
            error_->setText(QObject::tr("Zadejte název souboru."));
            error_->show();
            return false;
        }
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

class RenameDocumentDialog final : public zima::ui::PropertiesSubWindow {
public:
    RenameDocumentDialog(const QString& initial_name,
                         std::function<QString(QString)> accepted,
                         const ApplicationSettings& settings, QMainWindow* parent)
        : PropertiesSubWindow(settings.text("dialog.rename.title", tr("Přejmenovat")),
                              parent),
          accepted_(std::move(accepted)) {
        setObjectName("renameDocumentDialog");
        set_centered_on_show();
        setMinimumWidth(360);
        auto* form = new QFormLayout;
        name_ = new QLineEdit(initial_name, this);
        name_->setObjectName("renameDocumentName");
        form->addRow(settings.text("dialog.rename.label", tr("Nový název:")), name_);
        content_layout()->addLayout(form);
        error_ = new QLabel(this);
        error_->setObjectName("renameDocumentError");
        error_->setWordWrap(true);
        error_->setStyleSheet(QStringLiteral("color:#F08A85;"));
        error_->hide();
        content_layout()->addWidget(error_);
        setAttribute(Qt::WA_DeleteOnClose);
    }

private:
    QLineEdit* name_{};
    QLabel* error_{};
    std::function<QString(QString)> accepted_;

    bool submit() override {
        const QString error = accepted_(name_->text().trimmed());
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

AssemblyWorkspaceWindow::AssemblyWorkspaceWindow(const QString& working_directory) {
    if (!working_directory.trimmed().isEmpty()) {
        working_directory_ =
            QFileInfo(working_directory).absoluteFilePath().toStdString();
    }
    application_settings_ = ApplicationSettings::load(
        QString::fromStdString(working_directory_.string()));
    setWindowTitle(tr("ZIMA-CAD"));
    setWindowIcon(application_icon());
    resize(1200, 800);
    create_actions();
    create_layout();
    refresh_tabs();
    refresh_scene();
}

AssemblyWorkspaceWindow::~AssemblyWorkspaceWindow() {
    // Qt only destroys QObject children (including any open Properties
    // dialog) from inside the QWidget base-class destructor, which runs
    // after every data member of this derived class has already been
    // destroyed. Several dialogs connect to `destroyed` with a lambda that
    // reads or assigns members such as viewer_, tree_, or
    // construction_reference_geometry_ (see e.g.
    // show_construction_properties). Left to Qt's automatic child deletion,
    // that lambda would run against already-freed members and corrupt the
    // heap (observed as a double free on exit). Deleting the dialog here,
    // while the destructor body is still executing and every member is
    // still valid, guarantees its `destroyed` handler runs safely.
    delete properties_dialog_;
    delete rename_document_dialog_;
}

void AssemblyWorkspaceWindow::create_actions() {
    const auto t = [this](const char* key, const char* fallback) {
        return application_settings_.text(
            QString::fromLatin1(key), QString::fromUtf8(fallback));
    };
    const auto make_action = [this](const QString& text, const char* icon = nullptr) {
        auto* action = new QAction(text, this);
        if (icon != nullptr) action->setIcon(resource_icon(QString::fromLatin1(icon)));
        return action;
    };

    auto* file = menuBar()->addMenu(t("menu.file", "Soubor"));
    new_document_action_ = make_action(t("menu.file.new", "Nový"), "new");
    new_document_action_->setObjectName("newDocumentAction");
    new_document_action_->setShortcut(QKeySequence::New);
    connect(new_document_action_, &QAction::triggered, this,
        [this] { new_document(); });
    file->addAction(new_document_action_);
    open_document_action_ = make_action(t("menu.file.open", "Otevřít..."), "open");
    open_document_action_->setObjectName("openDocumentAction");
    open_document_action_->setShortcut(QKeySequence::Open);
    connect(open_document_action_, &QAction::triggered, this,
        [this] { open_document(); });
    file->addAction(open_document_action_);
    auto* import_action = make_action(t("menu.file.import", "Importovat…"), "open");
    import_action->setObjectName("importDocumentAction");
    connect(import_action, &QAction::triggered, this, [this] { import_file(); });
    file->addAction(import_action);
    export_action_ = make_action(t("menu.file.export", "Exportovat…"), "save");
    export_action_->setObjectName("exportDocumentAction");
    connect(export_action_, &QAction::triggered, this, [this] { export_file(); });
    file->addAction(export_action_);
    close_document_action_ = make_action(t("menu.file.close", "Zavřít"));
    close_document_action_->setObjectName("closeDocumentAction");
    close_document_action_->setShortcut(QKeySequence(QStringLiteral("F2")));
    connect(close_document_action_, &QAction::triggered, this,
        [this] { close_document(); });
    file->addAction(close_document_action_);
    save_action_ = make_action(t("menu.file.save", "Uložit"), "save");
    save_action_->setObjectName("saveDocumentAction");
    save_action_->setShortcuts({QKeySequence::Save,
                                QKeySequence(QStringLiteral("F1"))});
    save_action_->setShortcutContext(Qt::ApplicationShortcut);
    connect(save_action_, &QAction::triggered, this,
        [this] { save_active_document(); });
    file->addAction(save_action_);
    save_as_action_ = make_action(t("menu.file.save_as", "Uložit jako..."));
    save_as_action_->setObjectName("saveDocumentAsAction");
    save_as_action_->setShortcut(QKeySequence::SaveAs);
    save_as_action_->setEnabled(false);
    connect(save_as_action_, &QAction::triggered, this,
        [this] { save_active_document_as(); });
    file->addAction(save_as_action_);
    rename_document_action_ = make_action(
        t("menu.file.rename", "Přejmenovat…"));
    rename_document_action_->setObjectName("renameDocumentAction");
    rename_document_action_->setEnabled(false);
    connect(rename_document_action_, &QAction::triggered, this,
        [this] { rename_document_file(); });
    file->addAction(rename_document_action_);

    delete_file_menu_ = file->addMenu(
        t("menu.file.delete", "Odstranit"));
    delete_file_menu_->setObjectName("deleteFileMenu");
    delete_current_file_action_ = delete_file_menu_->addAction(
        resource_icon("delete"),
        t("menu.file.delete.current_file", "Aktuální soubor"));
    delete_current_file_action_->setObjectName("deleteCurrentFileAction");
    connect(delete_current_file_action_, &QAction::triggered, this,
        [this] { delete_current_document_file(); });
    delete_all_versions_action_ = delete_file_menu_->addAction(
        t("menu.file.delete.current_file_and_versions",
          "Aktuální soubor a všechny verze"));
    delete_all_versions_action_->setObjectName("deleteAllVersionsAction");
    connect(delete_all_versions_action_, &QAction::triggered, this,
        [this] { delete_all_file_versions(); });
    delete_file_menu_->addSeparator();
    delete_old_versions_action_ = delete_file_menu_->addAction(
        t("menu.file.delete.old_versions", "Staré verze"));
    delete_old_versions_action_->setObjectName("deleteOldVersionsAction");
    connect(delete_old_versions_action_, &QAction::triggered, this,
        [this] { delete_old_file_versions(); });
    delete_old_versions_keep_latest_action_ = delete_file_menu_->addAction(
        t("menu.file.delete.old_versions_keep_latest",
          "Staré verze kromě nejnovější"));
    delete_old_versions_keep_latest_action_->setObjectName(
        "deleteOldVersionsKeepLatestAction");
    connect(delete_old_versions_keep_latest_action_, &QAction::triggered, this,
        [this] { delete_old_file_versions_keep_latest(); });
    delete_file_menu_->addSeparator();
    delete_working_directory_menu_ = delete_file_menu_->addMenu(
        t("menu.file.delete.working_directory", "Pracovní adresář"));
    delete_working_directory_menu_->setObjectName("deleteWorkingDirectoryMenu");
    delete_working_directory_old_versions_action_ =
        delete_working_directory_menu_->addAction(
            t("menu.file.delete.working_directory_old_versions",
              "Odstranit staré verze"));
    delete_working_directory_old_versions_action_->setObjectName(
        "deleteWorkingDirectoryOldVersionsAction");
    connect(delete_working_directory_old_versions_action_, &QAction::triggered,
        this, [this] { delete_working_directory_old_versions(); });
    delete_working_directory_keep_latest_action_ =
        delete_working_directory_menu_->addAction(
            t("menu.file.delete.working_directory_keep_latest",
              "Ponechat nejnovější verzi"));
    delete_working_directory_keep_latest_action_->setObjectName(
        "deleteWorkingDirectoryKeepLatestAction");
    connect(delete_working_directory_keep_latest_action_, &QAction::triggered,
        this, [this] { delete_working_directory_old_versions_keep_latest(); });
    for (auto* action : {delete_current_file_action_, delete_all_versions_action_,
                         delete_old_versions_action_,
                         delete_old_versions_keep_latest_action_,
                         delete_working_directory_old_versions_action_,
                         delete_working_directory_keep_latest_action_}) {
        action->setEnabled(false);
    }
    file->addSeparator();
    working_directory_action_ = make_action(
        t("menu.file.working_directory", "Nastavit pracovní adresář..."));
    working_directory_action_->setObjectName("workingDirectoryAction");
    connect(working_directory_action_, &QAction::triggered, this,
        [this] { set_working_directory(); });
    file->addAction(working_directory_action_);

    auto* edit = menuBar()->addMenu(t("menu.edit", "Upravit"));
    regenerate_document_action_ = make_action(tr("Regenerovat"));
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
    auto* normal_view_action = make_action(tr("Pohled kolmo"), "view-normal");
    normal_view_action->setObjectName("normalViewAction");
    normal_view_action->setToolTip(
        tr("Vyberte plochu ve 3D pohledu – natočí pohled kolmo k ní"));
    connect(normal_view_action, &QAction::triggered, this,
        [this] { show_orientation_dialog(); });
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

    auto* view = menuBar()->addMenu(t("menu.view", "Zobrazení"));
    view->addAction(fit_view_action_);
    standard_views_menu_ = view->addMenu(
        t("toolbar.standard_views", "Základní pohledy"));
    standard_views_menu_->setObjectName("standardViewsMenu");
    const auto add_standard_view = [this](
        const QString& text, zima::viewer::StandardView standard_view) {
        auto* action = standard_views_menu_->addAction(text);
        connect(action, &QAction::triggered, this, [this, standard_view] {
            if (viewer_ != nullptr) viewer_->set_standard_view(standard_view);
        });
    };
    add_standard_view(t("toolbar.view.default", "Výchozí – izometrický"), zima::viewer::StandardView::Isometric);
    add_standard_view(t("toolbar.view.front", "Front – XZ"), zima::viewer::StandardView::Front);
    add_standard_view(t("toolbar.view.back", "Back – XZ opačně"), zima::viewer::StandardView::Back);
    add_standard_view(t("toolbar.view.left", "Left – YZ"), zima::viewer::StandardView::Left);
    add_standard_view(t("toolbar.view.right", "Right – YZ opačně"), zima::viewer::StandardView::Right);
    add_standard_view(t("toolbar.view.top", "Top – XY"), zima::viewer::StandardView::Top);
    add_standard_view(t("toolbar.view.bottom", "Bottom – XY opačně"), zima::viewer::StandardView::Bottom);
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
    view->addSeparator();
    colors_menu_ = view->addMenu(t("menu.view.colors", "Barvy"));
    colors_menu_->setObjectName("colorsMenu");
    const std::array<std::pair<const char*, const char*>, 8> color_items{{
        {"menu.view.colors.white", "Bílá"},
        {"menu.view.colors.graphite", "Grafitová"},
        {"menu.view.colors.silver", "Stříbrná"},
        {"menu.view.colors.blue", "Modrá"},
        {"menu.view.colors.green", "Zelená"},
        {"menu.view.colors.violet", "Fialová"},
        {"menu.view.colors.burgundy", "Vínová"},
        {"menu.view.colors.sand", "Písková"},
    }};
    for (const auto& [key, fallback] : color_items) {
        auto* action = colors_menu_->addAction(t(key, fallback));
        action->setEnabled(false);
    }
    colors_menu_->addSeparator();
    auto* custom_body_color = colors_menu_->addAction(
        t("menu.view.colors.body", "Vlastní barva tělesa…"));
    custom_body_color->setObjectName("customBodyColorAction");
    custom_body_color->setEnabled(false);
    auto* reset_body_color = colors_menu_->addAction(
        t("menu.view.colors.body_reset", "Obnovit barvu tělesa"));
    reset_body_color->setObjectName("resetBodyColorAction");
    reset_body_color->setEnabled(false);

    auto* applications = menuBar()->addMenu(t("menu.applications", "Aplikace"));
    application_group_ = new QActionGroup(this);
    application_group_->setExclusive(true);
    const std::array<QString, 6> application_names{
        t("application.modeling", "Modelování"),
        t("application.assembly", "Sestava"),
        t("application.sheet_metal", "Plech"),
        t("application.surface", "Plochy"),
        t("application.piping", "Potrubí"),
        t("application.drawing", "Výkres")};
    for (std::size_t index = 0; index < application_actions_.size(); ++index) {
        auto* action = applications->addAction(application_names[index]);
        action->setObjectName(
            QStringLiteral("applicationModeAction%1").arg(index));
        action->setCheckable(true);
        action->setData(static_cast<int>(index));
        application_group_->addAction(action);
        connect(action, &QAction::triggered, this, [this, index] {
            set_active_application(static_cast<ApplicationMode>(index));
        });
        application_actions_[index] = action;
    }
    application_actions_[0]->setChecked(true);

    auto* tools = menuBar()->addMenu(t("menu.tools", "Nástroje"));
    material_action_ = tools->addAction(t("menu.tools.material", "Materiál..."));
    material_action_->setObjectName("materialAction");
    connect(material_action_, &QAction::triggered, this, &AssemblyWorkspaceWindow::edit_material);
    parameters_action_ = tools->addAction(
        t("menu.tools.parameters", "Parametry..."));
    parameters_action_->setObjectName("documentParametersAction");
    connect(parameters_action_, &QAction::triggered,
        this, &AssemblyWorkspaceWindow::edit_document_parameters);
    relations_action_ = tools->addAction(t("menu.tools.relations", "Relace..."));
    relations_action_->setObjectName("relationsAction");
    connect(relations_action_, &QAction::triggered, this, &AssemblyWorkspaceWindow::edit_relations);
    family_table_action_ = tools->addAction(t("menu.tools.family_table", "Family Table..."));
    family_table_action_->setObjectName("familyTableAction");
    connect(family_table_action_, &QAction::triggered, this, &AssemblyWorkspaceWindow::edit_family_table);
    tools->addSeparator();
    file_settings_action_ = tools->addAction(
        t("menu.tools.file_settings", "Nastavení souboru..."));
    file_settings_action_->setObjectName("fileSettingsAction");
    connect(file_settings_action_, &QAction::triggered, this, &AssemblyWorkspaceWindow::edit_file_settings);
    settings_action_ = make_action(
        t("menu.tools.global_settings", "Globální nastavení..."), "settings");
    settings_action_->setObjectName("globalSettingsAction");
    connect(settings_action_, &QAction::triggered,
        this, &AssemblyWorkspaceWindow::show_global_settings);
    tools->addAction(settings_action_);
    auto* window_menu = menuBar()->addMenu(t("menu.window", "Okno"));
    window_menu->setObjectName("windowMenu");
    connect(window_menu, &QMenu::aboutToShow, this, [this, window_menu] {
        window_menu->clear();
        auto* new_window = window_menu->addAction(application_settings_.text(
            "menu.window.new_window", tr("Nové okno")));
        new_window->setObjectName("newWindowAction");
        connect(new_window, &QAction::triggered,
            this, &AssemblyWorkspaceWindow::open_new_window);
        window_menu->addSeparator();
        if (tabs_ == nullptr || tabs_->count() == 0) {
            auto* empty = window_menu->addAction(application_settings_.text(
                "status.no_open_documents", tr("Není otevřen žádný dokument")));
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
    auto* help = menuBar()->addMenu(t("menu.help", "Nápověda"));
    auto* about_action = help->addAction(
        t("menu.help.about", "O aplikaci ZIMA-CAD"));
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
    construction_point_action_->setObjectName("constructionPointAction");
    construction_axis_action_->setObjectName("constructionAxisAction");
    construction_plane_action_->setObjectName("constructionPlaneAction");
    extrusion_action_ = make_action(tr("Vytažení"), "protrusion");
    extrusion_action_->setObjectName("extrusionAction");
    revolution_action_ = make_action(tr("Rotace"), "revolve");
    revolution_action_->setObjectName("revolutionAction");
    fillet_action_ = make_action(tr("Zaoblení"), "fillet");
    chamfer_action_ = make_action(tr("Sražení"), "chamfer");
    sketch_action_ = make_action(tr("Skica"), "sketch");
    sketch_action_->setObjectName("sketchAction");
    sketch_normal_view_action_ = make_action(tr("Pohled kolmo"), "view-normal");
    sketch_normal_view_action_->setObjectName("sketchNormalViewAction");
    sketch_normal_view_action_->setEnabled(false);
    sketch_flip_view_action_ = make_action(
        tr("Převrátit"), "sketch-view-flip");
    sketch_flip_view_action_->setObjectName("sketchFlipViewAction");
    sketch_flip_view_action_->setEnabled(false);
    sketch_rotate_view_action_ = make_action(
        tr("Otočit"), "sketch-view-rotate");
    sketch_rotate_view_action_->setObjectName("sketchRotateViewAction");
    sketch_rotate_view_action_->setEnabled(false);
    sketch_external_reference_action_ = make_action(
        tr("Externí reference"), "sketch-reference");
    sketch_external_reference_action_->setObjectName(
        "sketchExternalReferenceAction");
    sketch_external_reference_action_->setCheckable(true);
    sketch_external_reference_action_->setEnabled(false);
    sketch_external_profile_action_ = make_action(
        tr("Reference → obrys"), "sketch-segment");
    sketch_external_profile_action_->setObjectName(
        "sketchExternalProfileAction");
    sketch_external_profile_action_->setCheckable(true);
    sketch_external_profile_action_->setEnabled(false);
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
    sketch_corner_fillet_action_ = make_action(tr("Zaoblit roh"), "sketch-fillet");
    sketch_corner_fillet_action_->setObjectName("sketchCornerFilletAction");
    sketch_corner_fillet_action_->setEnabled(false);
    sketch_mirror_action_ = make_action(tr("Zrcadlit"), "sketch-mirror");
    sketch_mirror_action_->setObjectName("sketchMirrorAction");
    sketch_mirror_action_->setEnabled(false);
    sketch_circle_action_ = make_action(tr("Kružnice"), "sketch-circle");
    sketch_circle_action_->setObjectName("sketchCircleAction");
    sketch_arc_action_ = make_action(tr("Oblouk"), "sketch-arc");
    sketch_arc_action_->setObjectName("sketchArcAction");
    sketch_ellipse_action_ = make_action(tr("Elipsa"), "sketch-ellipse");
    sketch_ellipse_action_->setObjectName("sketchEllipseAction");
    sketch_elliptical_arc_action_ = make_action(
        tr("Eliptický oblouk"), "sketch-elliptical-arc");
    sketch_elliptical_arc_action_->setObjectName("sketchEllipticalArcAction");
    sketch_bspline_action_ = make_action(tr("B-spline"), "sketch-spline");
    sketch_bspline_action_->setObjectName("sketchBSplineAction");
    sketch_text_action_ = make_action(tr("Text"), "sketch-text");
    sketch_text_action_->setObjectName("sketchTextAction");
    sketch_text_action_->setEnabled(false);
    sketch_horizontal_action_ = make_action(tr("Vodorovná úsečka"));
    sketch_horizontal_action_->setObjectName("sketchHorizontalAction");
    sketch_vertical_action_ = make_action(tr("Svislá úsečka"));
    sketch_vertical_action_->setObjectName("sketchVerticalAction");
    sketch_coincident_action_ = make_action(tr("Shodnost bodů"));
    sketch_coincident_action_->setObjectName("sketchCoincidentAction");
    sketch_midpoint_action_ = make_action(tr("Bod ve středu"));
    sketch_midpoint_action_->setObjectName("sketchMidpointAction");
    sketch_symmetric_action_ = make_action(tr("Symetrická"));
    sketch_symmetric_action_->setObjectName("sketchSymmetricAction");
    sketch_concentric_action_ = make_action(tr("Soustředná"));
    sketch_concentric_action_->setObjectName("sketchConcentricAction");
    sketch_tangent_action_ = make_action(tr("Tečná"));
    sketch_tangent_action_->setObjectName("sketchTangentAction");
    sketch_parallel_action_ = make_action(tr("Rovnoběžnost úseček"));
    sketch_parallel_action_->setObjectName("sketchParallelAction");
    sketch_perpendicular_action_ = make_action(tr("Kolmost úseček"));
    sketch_perpendicular_action_->setObjectName("sketchPerpendicularAction");
    sketch_equal_length_action_ = make_action(tr("Stejné"));
    sketch_equal_length_action_->setObjectName("sketchEqualAction");
    sketch_fix_point_action_ = make_action(tr("Fixovat/uvolnit bod"));
    sketch_fix_point_action_->setObjectName("sketchFixPointAction");
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
    sketch_dimension_action_ = make_action(tr("Kóta"), "sketch-dimensions");
    sketch_dimension_action_->setObjectName("sketchDimensionAction");
    sketch_dimension_x_action_ = make_action(tr("Vodorovná kóta úsečky…"));
    sketch_dimension_x_action_->setObjectName("sketchDimensionXAction");
    sketch_dimension_y_action_ = make_action(tr("Svislá kóta úsečky…"));
    sketch_dimension_y_action_->setObjectName("sketchDimensionYAction");
    sketch_point_line_dimension_action_ =
        make_action(tr("Vzdálenost bodu od přímky…"));
    sketch_point_line_dimension_action_->setObjectName(
        "sketchPointLineDimensionAction");
    sketch_symmetric_dimension_action_ =
        make_action(tr("Symetrická kóta od osy…"));
    sketch_symmetric_dimension_action_->setObjectName(
        "sketchSymmetricDimensionAction");
    sketch_three_point_angle_dimension_action_ =
        make_action(tr("Tříbodová úhlová kóta…"));
    sketch_three_point_angle_dimension_action_->setObjectName(
        "sketchThreePointAngleDimensionAction");
    sketch_angle_dimension_action_ = make_action(tr("Úhel mezi přímkami…"));
    sketch_angle_dimension_action_->setObjectName("sketchAngleDimensionAction");
    sketch_parallel_distance_dimension_action_ =
        make_action(tr("Vzdálenost rovnoběžek…"));
    sketch_parallel_distance_dimension_action_->setObjectName(
        "sketchParallelDistanceDimensionAction");
    sketch_radius_dimension_action_ = make_action(tr("Kóta poloměru…"));
    sketch_radius_dimension_action_->setObjectName("sketchRadiusDimensionAction");
    sketch_diameter_dimension_action_ = make_action(tr("Kóta průměru…"));
    sketch_diameter_dimension_action_->setObjectName("sketchDiameterDimensionAction");
    sketch_ellipse_major_dimension_action_ = make_action(tr("Kóta hlavní poloosy elipsy…"));
    sketch_ellipse_major_dimension_action_->setObjectName(
        "sketchEllipseMajorDimensionAction");
    sketch_ellipse_minor_dimension_action_ = make_action(tr("Kóta vedlejší poloosy elipsy…"));
    sketch_ellipse_minor_dimension_action_->setObjectName(
        "sketchEllipseMinorDimensionAction");
    sketch_ellipse_rotation_dimension_action_ = make_action(tr("Kóta natočení elipsy…"));
    sketch_ellipse_rotation_dimension_action_->setObjectName(
        "sketchEllipseRotationDimensionAction");
    sketch_dimensions_menu_ = new QMenu(tr("Kóty"), this);
    sketch_dimensions_menu_->setObjectName("sketchDimensionsMenu");
    for (auto* action : {sketch_dimension_action_, sketch_dimension_x_action_,
                         sketch_dimension_y_action_,
                         sketch_point_line_dimension_action_,
                         sketch_symmetric_dimension_action_,
                         sketch_three_point_angle_dimension_action_,
                         sketch_angle_dimension_action_,
                         sketch_parallel_distance_dimension_action_,
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
    regenerate_part_action_ = make_action(tr("Regenerovat"));
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
    connect(sketch_flip_view_action_, &QAction::triggered, this,
        [this] { flip_active_sketch_view(); });
    connect(sketch_rotate_view_action_, &QAction::triggered, this,
        [this] { rotate_active_sketch_view(); });
    connect(sketch_external_reference_action_, &QAction::toggled, this,
        [this](bool enabled) {
            if (enabled) sketch_external_profile_active_ = false;
            set_sketch_external_reference_mode(enabled);
        });
    connect(sketch_external_profile_action_, &QAction::toggled, this,
        [this](bool enabled) {
            sketch_external_profile_active_ = enabled;
            set_sketch_external_reference_mode(enabled);
        });
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
    connect(sketch_corner_fillet_action_, &QAction::triggered, this,
        [this] { start_sketch_corner_fillet(); });
    connect(sketch_trim_action_, &QAction::changed,
        sketch_corner_fillet_action_, [this] {
            sketch_corner_fillet_action_->setEnabled(
                sketch_trim_action_->isEnabled());
        });
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
        if (!selected_sketch_segment_id_.empty()) {
            constrain_selected_segment(zima::sketcher::ConstraintKind::Horizontal);
        } else start_sketch_coincident(zima::sketcher::ConstraintKind::Horizontal);
    });
    connect(sketch_vertical_action_, &QAction::triggered, this, [this] {
        if (!selected_sketch_segment_id_.empty()) {
            constrain_selected_segment(zima::sketcher::ConstraintKind::Vertical);
        } else start_sketch_coincident(zima::sketcher::ConstraintKind::Vertical);
    });
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
        [this] {
            if (!selected_sketch_ellipse_id_.empty()) {
                show_sketch_dimension_properties(active_sketch_id_, {},
                    zima::sketcher::DimensionKind::EllipseMajorRadius);
            } else if (!selected_sketch_segment_id_.empty() ||
                       !selected_sketch_circle_id_.empty() ||
                       !selected_sketch_arc_id_.empty()) {
                show_sketch_dimension_properties(active_sketch_id_);
            } else {
                start_sketch_point_dimension(zima::sketcher::DimensionKind::Distance);
            }
        });
    connect(sketch_dimension_x_action_, &QAction::triggered, this, [this] {
        if (selected_sketch_segment_id_.empty()) {
            start_sketch_point_dimension(zima::sketcher::DimensionKind::DistanceX);
        } else show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::DistanceX); });
    connect(sketch_dimension_y_action_, &QAction::triggered, this, [this] {
        if (selected_sketch_segment_id_.empty()) {
            start_sketch_point_dimension(zima::sketcher::DimensionKind::DistanceY);
        } else show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::DistanceY); });
    connect(sketch_point_line_dimension_action_, &QAction::triggered, this,
        [this] {
            start_sketch_point_dimension(
                zima::sketcher::DimensionKind::DistancePointLine);
        });
    connect(sketch_symmetric_dimension_action_, &QAction::triggered, this,
        [this] {
            start_sketch_point_dimension(
                zima::sketcher::DimensionKind::DistanceSymmetric);
        });
    connect(sketch_three_point_angle_dimension_action_, &QAction::triggered, this,
        [this] {
            start_sketch_point_dimension(
                zima::sketcher::DimensionKind::AngleThreePoint);
        });
    connect(sketch_angle_dimension_action_, &QAction::triggered, this, [this] {
        start_sketch_line_pair_dimension(
            zima::sketcher::DimensionKind::AngleBetween); });
    connect(sketch_parallel_distance_dimension_action_, &QAction::triggered,
        this, [this] {
            start_sketch_line_pair_dimension(
                zima::sketcher::DimensionKind::DistanceLine);
        });
    connect(sketch_angle_dimension_action_, &QAction::changed,
        sketch_parallel_distance_dimension_action_,
        [this] {
            sketch_parallel_distance_dimension_action_->setEnabled(
                sketch_angle_dimension_action_->isEnabled());
        });
    sketch_parallel_distance_dimension_action_->setEnabled(
        sketch_angle_dimension_action_->isEnabled());
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
    insert_action_ = make_action(tr("Vložit komponentu"));
    insert_action_->setIcon(resource_icon("assembly"));
    insert_action_->setObjectName("insertComponentAction");
    connect(insert_action_, &QAction::triggered, this,
        [this] { insert_component_from_file(); });
    connect(insert_menu_, &QMenu::aboutToShow, this,
        [this] { rebuild_insert_menu(); });
    regenerate_action_ = make_action(tr("Regenerovat"));
    regenerate_action_->setObjectName("regenerateAssemblyAction");
    connect(regenerate_action_, &QAction::triggered, this, [this] { regenerate_assembly(); });

    main_toolbar_ = new QToolBar(tr("Dokument"), this);
    main_toolbar_->setObjectName("mainToolbar");
    main_toolbar_->setMovable(false);
    main_toolbar_->setIconSize(QSize(24, 24));
    main_toolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    main_toolbar_->addAction(new_document_action_);
    main_toolbar_->addAction(open_document_action_);
    main_toolbar_->addAction(save_action_);
    main_toolbar_->addSeparator();
    main_toolbar_->addAction(settings_action_);
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
    auto* history_tree = new HistoryTreeWidget(document_splitter_);
    tree_ = history_tree;
    history_tree->history_cursor_moved = [this](std::size_t cursor) {
        auto* part = workspace_.open_part(workspace_.active_document_id());
        if (part == nullptr) return;
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
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Dimension,
                                     zima::viewer::CandidateKind::Occurrence});
    viewer_->set_confirmation_callback([this](const auto& candidate) {
        if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
            candidate.semantic_key.starts_with("parameter:") &&
            properties_dialog_ != nullptr) {
            focus_parameter_dimension_field(candidate.semantic_key);
            return;
        }
        if (construction_reference_dialog_ != nullptr &&
            pending_construction_reference_index_) {
            accept_construction_reference(candidate);
            return;
        }
        if (primitive_reference_dialog_ != nullptr &&
            pending_primitive_reference_index_) {
            accept_primitive_reference(candidate);
            return;
        }
        if (component_placement_dialog_ != nullptr &&
            pending_component_placement_index_) {
            accept_component_placement_reference(candidate);
            return;
        }
        if (extrusion_target_dialog_ != nullptr) {
            accept_extrusion_target(candidate);
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
        if (sketch_segment_pair_active_) {
            accept_sketch_segment_pair(candidate);
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
        clear_selected_sketch_geometry();
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
                tree_->clearSelection();
                QTreeWidgetItemIterator iterator(tree_);
                while (*iterator != nullptr) {
                    auto* item = *iterator;
                    const auto role = item->data(0, Qt::UserRole + 3).toString();
                    if ((role == QStringLiteral("sketch-geometry") ||
                         role == QStringLiteral("sketch-external-reference")) &&
                        item->data(0, Qt::UserRole).toString() ==
                            QString::fromStdString(selected_id)) {
                        item->setSelected(true);
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
                 sketch_dimension_action_, sketch_dimension_x_action_,
                 sketch_dimension_y_action_, sketch_point_line_dimension_action_,
                 sketch_symmetric_dimension_action_,
                 sketch_three_point_angle_dimension_action_,
                 sketch_angle_dimension_action_}) {
            action->setEnabled(sketch_tools_available);
        }
        sketch_trim_action_->setEnabled(
            !active_sketch_id_.empty() && !sketch_trim_active_);
        sketch_mirror_action_->setEnabled(
            !active_sketch_id_.empty() && !sketch_mirror_active_ &&
            !sketch_trim_active_);
    });
    viewer_->set_empty_confirmation_callback([this] {
        clear_selected_sketch_geometry();
        tree_->clearSelection();
        tree_->setCurrentItem(nullptr);
        if (properties_dialog_ == nullptr &&
            !construction_dimension_object_id_.empty()) {
            construction_dimension_object_id_.clear();
            preserve_view_on_refresh_ = true;
            refresh_scene();
        }
    });
    viewer_->set_context_menu_callback(
        [this](const auto& candidate, const QPoint& global_position) {
            if (edge_treatment_selection_ ||
                extrusion_target_dialog_ != nullptr ||
                sketch_external_reference_active_ || sketch_trim_active_ ||
                sketch_mirror_active_ || sketch_coincident_active_ ||
                sketch_midpoint_active_ || sketch_symmetric_active_ ||
                sketch_concentric_active_ || sketch_tangent_active_ ||
                sketch_segment_pair_active_ || sketch_point_dimension_active_) return;
            if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
                candidate.semantic_key.starts_with("dimension:")) {
                const auto dimension_id = candidate.semantic_key.substr(10);
                QMenu menu(this);
                auto* edit = menu.addAction(tr("Editovat hodnotu"));
                auto* properties = menu.addAction(tr("Vlastnosti…"));
                auto* remove = menu.addAction(tr("Odstranit"));
                const auto* chosen = menu.exec(global_position);
                if (chosen == edit || chosen == properties) {
                    show_sketch_dimension_properties(
                        candidate.owner_id, dimension_id);
                    if (chosen == edit && properties_dialog_ != nullptr) {
                        if (auto* value = properties_dialog_->findChild<
                                QDoubleSpinBox*>("sketchDimensionValue")) {
                            value->setFocus();
                            value->selectAll();
                        }
                    }
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
                auto* role = menu.addAction(*construction
                    ? tr("Převést na obrys profilu")
                    : tr("Převést na pomocnou geometrii"));
                auto* remove = menu.addAction(tr("Odstranit"));
                const auto* chosen = menu.exec(global_position);
                if (chosen == role) {
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
                auto* edit = menu.addAction(tr("Editovat hodnotu"));
                auto* properties = menu.addAction(tr("Vlastnosti"));
                const auto* selected = menu.exec(global_position);
                if (selected != edit && selected != properties) return;
                if (auto* item = find_container_item(candidate.owner_id)) {
                    construction_dimension_object_id_ = candidate.owner_id;
                    show_tree_item_properties(item);
                    if (selected == edit && properties_dialog_ != nullptr) {
                        focus_parameter_dimension_field(candidate.semantic_key);
                    }
                }
                return;
            }
            if (candidate.kind == zima::viewer::CandidateKind::Container) {
                auto* item = find_container_item(candidate.owner_id);
                if (item == nullptr) return;
                QMenu menu(this);
                auto* edit = menu.addAction(tr("Editovat"));
                auto* properties = menu.addAction(tr("Vlastnosti"));
                const auto* selected = menu.exec(global_position);
                if (selected == edit) {
                    construction_dimension_object_id_ = candidate.owner_id;
                    preserve_view_on_refresh_ = true;
                    refresh_scene();
                } else if (selected == properties) {
                    construction_dimension_object_id_ = candidate.owner_id;
                    show_tree_item_properties(item);
                }
                return;
            }
            if (candidate.kind != zima::viewer::CandidateKind::Occurrence) return;
            show_component_context_menu(candidate.instance_path, global_position);
    });
    viewer_->set_world_click_callback([this](const auto& origin, const auto& direction) {
        const auto [local_origin, local_direction] =
            active_part_local_ray(origin, direction);
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
        if (!sketch_inference_cycle_refresh_) {
            sketch_segment_inference_cycle_ = 0;
            sketch_skip_candidate_snap_ = false;
        }
        sketch_inference_cycle_refresh_ = false;
        auto [local_origin, local_direction] =
            active_part_local_ray(origin, direction);
        if (sketch_point_dimension_active_ &&
            !pending_point_dimension_second_id_.empty()) {
            if (const auto* sketch = active_sketch()) {
                pending_point_dimension_cursor_ =
                    sketch->intersect_ray(local_origin, local_direction);
            }
        }
        if (!sketch_skip_candidate_snap_) {
            if (const auto candidate = viewer_->hovered_candidate()) {
            if (const auto snapped = sketch_candidate_snap_ray(
                    *candidate, local_origin, local_direction)) {
                local_origin = snapped->origin;
                local_direction = snapped->direction;
            }
            }
        }
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
        // A short MMB click is never a geometry input for the ordinary
        // Segment tool.  Without consuming it here MeshView falls back to
        // world_click_callback(), which used to create the pending endpoint
        // at the mouse position.  The Segment stays active until a rapid MMB
        // double-click; both of its endpoints are confirmed exclusively by
        // LMB, matching the C++ Sketcher interaction contract.
        if (sketch_segment_active_ && !sketch_polyline_active_) return true;
        return confirm_current_sketch_step();
    });
    viewer_->set_double_middle_click_callback(
        [this] { return finish_current_sketch_tool(); });
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
        if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
            candidate.semantic_key.starts_with("dimension:")) {
            show_sketch_dimension_properties(
                candidate.owner_id, candidate.semantic_key.substr(10));
        } else if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
                   candidate.semantic_key.starts_with("placement-reference:") &&
                   workspace_.open_assembly(candidate.owner_id) != nullptr) {
            const auto occurrence_id = candidate.semantic_key.substr(
                20, candidate.semantic_key.rfind(':') - 20);
            auto prefix = active_occurrence_path_.empty()
                ? zima::assembly::InstancePath{}
                : zima::assembly::InstancePath::decode(active_occurrence_path_);
            show_component_properties(prefix.child(occurrence_id).encoded());
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
        } else if (candidate.kind == zima::viewer::CandidateKind::Container) {
            construction_dimension_object_id_ = candidate.owner_id;
            preserve_view_on_refresh_ = true;
            refresh_scene();
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
                   begin_sketch_dimension_drag(candidate) ||
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
            const auto selected_items = tree_->selectedItems();
            if (selected_items.empty()) {
                if (!refreshing_scene_) {
                    construction_dimension_object_id_.clear();
                }
                clear_selected_sketch_geometry();
                viewer_->clear_selection();
                return;
            }
            auto* item = tree_->currentItem();
            if (item == nullptr || !selected_items.contains(item)) {
                item = selected_items.front();
            }
            if (item->parent() == nullptr) return;
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
            // Tree and View share one confirmed-selection state. Switching
            // to any ordinary Tree item must release the previous Sketch
            // geometry latch before the new item is synchronized below.
            clear_selected_sketch_geometry();
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
                    !selected_sketch_circle_id_.empty());
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
                        : semantic.starts_with("origin:axis:")
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
            if (item->data(0, Qt::UserRole + 3).toString() == "part-container") {
                viewer_->confirm_container(
                    item->data(0, Qt::UserRole).toString().toStdString());
            } else if (item->data(0, Qt::UserRole + 3).toString() ==
                           "part-result-body") {
                viewer_->confirm_result_body();
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
                viewer_->confirm_occurrence(
                    item->data(0, Qt::UserRole + 1).toString().toStdString());
            }
        };
    connect(tree_, &QTreeWidget::itemSelectionChanged, this,
        synchronize_tree_selection);
    connect(tree_, &QTreeWidget::itemClicked, this,
        [this, synchronize_tree_selection](QTreeWidgetItem* item, int) {
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
                edge_treatment_selection_ ||
                sketch_external_reference_active_ || sketch_trim_active_ ||
                sketch_mirror_active_ || sketch_coincident_active_ ||
                sketch_midpoint_active_ || sketch_symmetric_active_ ||
                sketch_concentric_active_ || sketch_tangent_active_ ||
                sketch_segment_pair_active_ || sketch_point_dimension_active_) return;
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
            if (item == nullptr || item->parent() == nullptr) return;
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
            } else if (item->data(0, Qt::UserRole + 3).toString() == "assembly-cut") {
                const auto id = item->data(0, Qt::UserRole).toString().toStdString();
                auto* assembly =
                    workspace_.open_assembly(workspace_.active_document_id());
                const auto* cut = assembly == nullptr
                    ? nullptr : assembly->session.document().find_cut(id);
                if (cut == nullptr) return;
                QMenu menu(this);
                auto* properties = menu.addAction(tr("Vlastnosti…"));
                auto* suppress = menu.addAction(cut->definition.suppressed
                    ? tr("Obnovit") : tr("Potlačit"));
                auto* move_up = menu.addAction(tr("Posunout výše"));
                auto* move_down = menu.addAction(tr("Posunout níže"));
                auto* remove = menu.addAction(tr("Odstranit"));
                const auto* selected = menu.exec(
                    tree_->viewport()->mapToGlobal(position));
                if (selected == properties) {
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
                    delete_part_object(id, QStringLiteral("assembly-cut"));
                }
            } else if (item->data(0, Qt::UserRole + 3).toString() == "part-container") {
                const std::string id = item->data(0, Qt::UserRole).toString().toStdString();
                const auto* part = workspace_.open_part(workspace_.active_document_id());
                const auto* container = part == nullptr
                    ? nullptr : part->session.document().find_container(id);
                if (container == nullptr) return;
                QMenu menu(this);
                auto* properties = menu.addAction(tr("Vlastnosti…"));
                auto* suppress = menu.addAction(container->suppressed
                    ? tr("Obnovit") : tr("Potlačit"));
                auto* move_up = menu.addAction(tr("Posunout výše"));
                auto* move_down = menu.addAction(tr("Posunout níže"));
                auto* remove = menu.addAction(tr("Odstranit"));
                const auto* selected = menu.exec(
                    tree_->viewport()->mapToGlobal(position));
                if (selected == properties) {
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
                } else if (selected == suppress) {
                    toggle_part_container_suppressed(id);
                } else if (selected == move_up) {
                    move_part_container(id, -1);
                } else if (selected == move_down) {
                    move_part_container(id, 1);
                } else if (selected == remove) {
                    delete_part_object(id, QStringLiteral("container"));
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
                if (selected == properties) {
                    show_construction_properties(object->kind, id);
                } else if (selected == suppress) {
                    toggle_part_container_suppressed(id);
                } else if (selected == move_up) {
                    move_part_container(id, -1);
                } else if (selected == move_down) {
                    move_part_container(id, 1);
                } else if (selected == remove) {
                    delete_part_object(id, item->data(0, Qt::UserRole + 3).toString());
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
                if (selected == properties) show_sketch_properties(id);
                else if (selected == suppress) toggle_part_container_suppressed(id);
                else if (selected == move_up) move_part_container(id, -1);
                else if (selected == move_down) move_part_container(id, 1);
                else if (selected == remove) {
                    delete_part_object(id,
                        item->data(0, Qt::UserRole + 3).toString() ==
                                QStringLiteral("assembly-sketch")
                            ? QStringLiteral("assembly-sketch")
                            : QStringLiteral("sketch"));
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
            apply_start_part_template(document, application_settings_);
            document.name = name;
            for (auto it = application_settings_.units.cbegin();
                 it != application_settings_.units.cend(); ++it)
                document.document_units[it.key().toStdString()] = it.value().toStdString();
            id = document.document_id;
            workspace_.add_part(std::move(document), {}, path);
            active_application_ = ApplicationMode::Modeling;
        } else if (document_type == QStringLiteral("assembly")) {
            auto document = zima::assembly::AssemblyDocument::create_default();
            document.name = name;
            for (auto it = application_settings_.units.cbegin();
                 it != application_settings_.units.cend(); ++it)
                document.document_units[it.key().toStdString()] = it.value().toStdString();
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
    export_action_->setEnabled(has_document);
    parameters_action_->setEnabled(has_document);
    const bool has_editable_model = has_document &&
        (workspace_.open_part(workspace_.active_document_id()) != nullptr ||
         workspace_.open_assembly(workspace_.active_document_id()) != nullptr);
    material_action_->setEnabled(has_editable_model);
    relations_action_->setEnabled(has_editable_model);
    family_table_action_->setEnabled(relations_action_->isEnabled());
    file_settings_action_->setEnabled(has_editable_model);
    standard_views_menu_->menuAction()->setEnabled(has_document);
    colors_menu_->menuAction()->setEnabled(has_document);
    for (auto* action : {wire_action_, hidden_edges_action_, no_hidden_edges_action_,
                         shaded_edges_action_, shaded_action_, show_origins_action_,
                         show_points_action_, show_axes_action_, show_planes_action_,
                         show_sketches_action_}) {
        action->setEnabled(has_document);
    }
    update_application_actions();
    refresh_delete_file_actions();
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
                    if (action->objectName() == "drawingSelectionAction") {
                        action->setIcon(resource_icon("select"));
                    } else if (action->objectName() == "insertDrawingViewAction") {
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
        add_command(sketch_flip_view_action_);
        add_command(sketch_rotate_view_action_);
        add_command(selection_action_);
        add_command(sketch_external_reference_action_);
        add_command(sketch_external_profile_action_);
        tools_toolbar_->addSeparator();
        add_command(sketch_trim_action_);
        add_command(sketch_corner_fillet_action_);
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
        add_command(sketch_dimension_action_);
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
        for (auto* action : {construction_point_action_, construction_axis_action_,
                             construction_plane_action_}) {
            add_command(action);
        }
        add_green_separator();
        add_command(sketch_action_);
        tools_toolbar_->addSeparator();
        add_command(extrusion_action_);
        add_command(revolution_action_);
        add_green_separator();
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

void AssemblyWorkspaceWindow::edit_document_parameters() {
    if (properties_dialog_ != nullptr) {
        properties_dialog_->raise();
        return;
    }
    const std::string active_id = workspace_.active_document_id();
    UserParameterData data;
    if (const auto* part = workspace_.open_part(active_id)) {
        const auto& document = part->session.document();
        data = {document.user_parameters, document.user_parameter_order,
            document.user_parameter_labels, document.user_parameter_values};
    } else if (const auto* assembly = workspace_.open_assembly(active_id)) {
        const auto& document = assembly->session.document();
        data = {document.user_parameters, document.user_parameter_order,
            document.user_parameter_labels, document.user_parameter_values};
    } else {
        return;
    }
    if (data.order.empty()) {
        for (const auto& [key, value] : data.flat) {
            data.order.push_back(key); data.values[key][""] = value;
        }
    }
    auto* dialog = new UserParametersDialog(std::move(data),
        application_settings_.language, [this, active_id](UserParameterData values) {
            if (auto* part = workspace_.open_part(active_id)) {
                auto next = part->session.document();
                next.user_parameters = std::move(values.flat);
                next.user_parameter_order = std::move(values.order);
                next.user_parameter_labels = std::move(values.labels);
                next.user_parameter_values = std::move(values.values);
                part->session.commit(
                    std::move(next), part->session.calculated_boundaries());
            } else if (auto* assembly = workspace_.open_assembly(active_id)) {
                auto next = assembly->session.document();
                next.user_parameters = std::move(values.flat);
                next.user_parameter_order = std::move(values.order);
                next.user_parameter_labels = std::move(values.labels);
                next.user_parameter_values = std::move(values.values);
                assembly->session.commit(std::move(next));
            }
            refresh_tabs();
        }, application_settings_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (properties_dialog_ == dialog) properties_dialog_ = nullptr;
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::edit_material() {
    if (properties_dialog_ != nullptr) { properties_dialog_->raise(); return; }
    const auto id = workspace_.active_document_id();
    DocumentToolData data;
    if (const auto* part = workspace_.open_part(id)) {
        const auto& document = part->session.document();
        data = {document.document_units, document.document_precision,
            document.physical_parameters, document.physical_parameter_units,
            document.material_parameter_descriptions, document.family_table};
    } else if (const auto* assembly = workspace_.open_assembly(id)) {
        const auto& document = assembly->session.document();
        data = {document.document_units, document.document_precision,
            document.physical_parameters, document.physical_parameter_units,
            document.material_parameter_descriptions, document.family_table};
    } else return;
    auto accepted = [this, id](DocumentToolData values) {
        if (auto* part = workspace_.open_part(id)) {
            auto next = part->session.document();
            next.physical_parameters = std::move(values.physical_parameters);
            next.physical_parameter_units = std::move(values.physical_parameter_units);
            next.material_parameter_descriptions = std::move(values.descriptions);
            part->session.commit(std::move(next), part->session.calculated_boundaries());
        } else if (auto* assembly = workspace_.open_assembly(id)) {
            auto next = assembly->session.document();
            next.physical_parameters = std::move(values.physical_parameters);
            next.physical_parameter_units = std::move(values.physical_parameter_units);
            next.material_parameter_descriptions = std::move(values.descriptions);
            assembly->session.commit(std::move(next));
        }
        refresh_tabs();
    };
    auto* dialog = new MaterialDialog(std::move(data), std::move(accepted), application_settings_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] { if (properties_dialog_ == dialog) properties_dialog_ = nullptr; });
    dialog->show();
}

void AssemblyWorkspaceWindow::edit_relations() {
    if (properties_dialog_ != nullptr) { properties_dialog_->raise(); return; }
    const auto id = workspace_.active_document_id();
    std::map<std::string, std::string> parameters;
    std::vector<zima::document::ModelRelation> relations;
    std::map<std::string, double> model_values;
    int decimal_places = 3;
    if (const auto* part = workspace_.open_part(id)) {
        const auto& document = part->session.document();
        parameters = document.user_parameters; relations = document.relations;
        try { decimal_places = std::stoi(document.document_precision.at("decimal_places")); } catch (...) {}
        double density{};
        try {
            density = std::stod(document.physical_parameters.at("MASS_DENSITY"));
            const auto unit = document.physical_parameter_units.at("MASS_DENSITY");
            if (unit == "kg/m^3") density *= 1.0e-9;
            else if (unit == "g/cm^3") density *= 1.0e-6;
            else if (unit == "lb/in^3") density *= 0.45359237 / std::pow(25.4, 3);
        } catch (...) { density = 0.0; }
        const auto& boundaries = part->session.calculated_boundaries();
        const double volume = boundaries.empty() ? 0.0 : std::abs(boundaries.back().volume);
        const double area = boundaries.empty() ? 0.0 : std::abs(boundaries.back().surface_area);
        model_values = {{"model.volume", volume}, {"model.area", area},
            {"model.mass", volume * density}, {"material.density", density}};
    } else if (const auto* assembly = workspace_.open_assembly(id)) {
        const auto& document = assembly->session.document();
        parameters = document.user_parameters; relations = document.relations;
        try { decimal_places = std::stoi(document.document_precision.at("decimal_places")); } catch (...) {}
        double volume{}; double area{};
        for (const auto& component : document.components) {
            volume += std::abs(component.calculated_source.volume);
            area += std::abs(component.calculated_source.surface_area);
        }
        model_values = {{"model.volume", volume}, {"model.area", area},
            {"model.mass", 0.0}, {"material.density", 0.0}};
    }
    else return;
    auto* dialog = new RelationsDialog(std::move(parameters), std::move(relations),
        [this, id](auto values, auto next_relations) {
            const auto sync_relation_targets = [](auto& document, const auto& parameters,
                                                   const auto& relations) {
                for (const auto& relation : relations) {
                    const auto value = parameters.find(relation.target);
                    if (value == parameters.end()) continue;
                    document.user_parameter_values[relation.target][""] = value->second;
                    if (std::find(document.user_parameter_order.begin(),
                            document.user_parameter_order.end(), relation.target) ==
                        document.user_parameter_order.end()) {
                        document.user_parameter_order.push_back(relation.target);
                    }
                }
            };
            if (auto* part = workspace_.open_part(id)) { auto next = part->session.document(); sync_relation_targets(next, values, next_relations); next.user_parameters = std::move(values); next.relations = std::move(next_relations); part->session.commit(std::move(next), part->session.calculated_boundaries()); }
            else if (auto* assembly = workspace_.open_assembly(id)) { auto next = assembly->session.document(); sync_relation_targets(next, values, next_relations); next.user_parameters = std::move(values); next.relations = std::move(next_relations); assembly->session.commit(std::move(next)); }
            refresh_tabs();
        }, application_settings_, this, std::move(model_values), decimal_places);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] { if (properties_dialog_ == dialog) properties_dialog_ = nullptr; });
    dialog->show();
}

void AssemblyWorkspaceWindow::edit_family_table() {
    if (properties_dialog_ != nullptr) { properties_dialog_->raise(); return; }
    const auto id = workspace_.active_document_id(); DocumentToolData data; QString name;
    if (const auto* part = workspace_.open_part(id)) { const auto& d = part->session.document(); name = QString::fromStdString(d.name); data.family_table = d.family_table; }
    else if (const auto* assembly = workspace_.open_assembly(id)) { const auto& d = assembly->session.document(); name = QString::fromStdString(d.name); data.family_table = d.family_table; }
    else return;
    auto* dialog = new FamilyTableDialog(name, std::move(data), [this, id](DocumentToolData values) {
        if (auto* part = workspace_.open_part(id)) { auto next = part->session.document(); next.family_table = std::move(values.family_table); part->session.commit(std::move(next), part->session.calculated_boundaries()); }
        else if (auto* assembly = workspace_.open_assembly(id)) { auto next = assembly->session.document(); next.family_table = std::move(values.family_table); assembly->session.commit(std::move(next)); }
        refresh_tabs();
    }, application_settings_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] { if (properties_dialog_ == dialog) properties_dialog_ = nullptr; });
    dialog->show();
}

void AssemblyWorkspaceWindow::edit_file_settings() {
    if (properties_dialog_ != nullptr) { properties_dialog_->raise(); return; }
    const auto id = workspace_.active_document_id(); DocumentToolData data;
    if (const auto* part = workspace_.open_part(id)) { const auto& d = part->session.document(); data.units = d.document_units; data.precision = d.document_precision; }
    else if (const auto* assembly = workspace_.open_assembly(id)) { const auto& d = assembly->session.document(); data.units = d.document_units; data.precision = d.document_precision; }
    else return;
    auto* dialog = new FileSettingsDialog(std::move(data), [this, id](DocumentToolData values) {
        if (auto* part = workspace_.open_part(id)) { auto next = part->session.document(); next.document_units = std::move(values.units); next.document_precision = std::move(values.precision); part->session.commit(std::move(next), part->session.calculated_boundaries()); }
        else if (auto* assembly = workspace_.open_assembly(id)) { auto next = assembly->session.document(); next.document_units = std::move(values.units); next.document_precision = std::move(values.precision); assembly->session.commit(std::move(next)); }
        refresh_tabs();
    }, application_settings_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] { if (properties_dialog_ == dialog) properties_dialog_ = nullptr; });
    dialog->show();
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
    const QString path = open_file(this,
        application_settings_.text("file.open_document", tr("Otevřít dokument")),
        QString::fromStdString(working_directory_.string()),
        application_settings_.text("file.filter.document",
            tr("Dokumenty ZIMA-CAD (*.prtz *.asmz *.drwz *.frmz *.tblz)")),
        application_settings_.translations);
    if (path.isEmpty()) return;
    static_cast<void>(open_document_path(path));
}

bool AssemblyWorkspaceWindow::open_document_path(const QString& path) {
    const std::filesystem::path opened_path = path.toStdString();
    try {
        std::string id;
        if (const auto already_open = workspace_.document_id_for_path(opened_path)) {
            id = *already_open;
        } else if (path.endsWith(".prtz", Qt::CaseInsensitive)) {
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
    auto* choose_file = insert_menu_->addAction(tr("Vybrat soubor…"));
    choose_file->setObjectName("insertComponentFromFileAction");
    connect(choose_file, &QAction::triggered, this,
        [this] { insert_component_from_file(); });
    insert_menu_->addSeparator();
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
}

void AssemblyWorkspaceWindow::insert_component_from_file() {
    const QString path = open_file(this, tr("Vložit komponentu"),
        QString::fromStdString(working_directory_.string()),
        tr("Komponenty ZIMA-CAD (*.prtz *.asmz)"),
        application_settings_.translations);
    if (path.isEmpty()) return;
    const std::filesystem::path source_path = path.toStdString();
    try {
        std::string source_id;
        if (const auto open_id = workspace_.document_id_for_path(source_path)) {
            source_id = *open_id;
        } else if (path.endsWith(".prtz", Qt::CaseInsensitive)) {
            std::vector<zima::kernel::BodyResult> calculated;
            auto source = zima::document::PartDocument::load(
                source_path, &calculated);
            source_id = source.document_id;
            workspace_.add_part(std::move(source), std::move(calculated), source_path);
        } else if (path.endsWith(".asmz", Qt::CaseInsensitive)) {
            auto source = zima::assembly::AssemblyDocument::load(source_path);
            source_id = source.document_id;
            workspace_.add_assembly(std::move(source), source_path);
        } else {
            throw std::runtime_error("Komponenta musí být Part nebo sestava.");
        }
        if (!source_path.parent_path().empty()) {
            working_directory_ = source_path.parent_path();
        }
        insert_component(source_id);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Vložení selhalo"), error.what());
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
        show_component_properties(
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
                    const auto& previous = part->session.calculated_boundaries();
                    auto calculated = calculate_part(next, &previous);
                    const auto previous_constructions = next.constructions;
                    next.resolve_constructions(calculated.empty()
                        ? zima::kernel::ViewerReferenceGeometry{}
                        : calculated.back().mesh.original_references);
                    const bool references_changed =
                        refresh_sketch_external_references(next, calculated) |
                        workspace_.refresh_context_external_references(next);
                    if (references_changed) {
                        calculated = calculate_part(next, &calculated);
                    }
                    if (references_changed ||
                        next.constructions != previous_constructions) {
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
        if (auto* regenerated = workspace_.open_assembly(id)) {
            auto next = regenerated->session.document();
            const bool references_changed =
                refresh_assembly_sketch_external_references(next);
            calculate_assembly_cuts(next);
            if (references_changed || !next.cuts.empty()) {
                regenerated->session.commit(std::move(next));
            }
        }
        refresh_tabs();
        preserve_view_on_refresh_ = true;
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Regenerace selhala"), error.what());
    }
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
    auto initial = zima::document::PartDocument::create_box_container();
    initial.feature_kind = kind;
    initial.name = kind == zima::document::FeatureKind::Fillet
        ? tr("Zaoblení").toStdString() : tr("Sražení").toStdString();
    initial.edge_treatment.edges.clear();
    const std::string part_id = part->session.document().document_id;
    auto* dialog = new PrimitivePropertiesDialog(
        initial, false, false,
        [this, part_id](zima::document::HistoryContainer committed,
                        std::vector<std::string>) {
            if (committed.edge_treatment.edges.empty()) {
                throw std::runtime_error("Vyberte alespoň jednu hranu Tělesa");
            }
            auto* target = workspace_.open_part(part_id);
            if (target == nullptr) throw std::runtime_error("Part is no longer open");
            auto next = target->session.document();
            next.insert_history_entry(
                zima::document::PartHistoryKind::Feature, committed.id);
            next.history.push_back(std::move(committed));
            auto calculated = calculate_part(next);
            static_cast<void>(refresh_sketch_external_references(next, calculated));
            target->session.commit(std::move(next), std::move(calculated));
        }, this);
    properties_dialog_ = dialog;
    edge_treatment_dialog_ = dialog;
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
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        edge_treatment_dialog_ = nullptr;
        edge_treatment_selection_.reset();
        pending_edge_treatment_edges_.clear();
        viewer_->set_candidate_filter({});
        viewer_->set_constraint_reference_highlights({}, {});
        refresh_tabs();
        preserve_view_on_refresh_ = true;
        refresh_scene();
    });
    dialog->show();
    state_->setText(tr(
        "Vyberte jednu nebo více hran Tělesa. OK operaci vypočítá."));
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
    const auto existing = std::find(pending_edge_treatment_edges_.begin(),
        pending_edge_treatment_edges_.end(), edge);
    if (existing == pending_edge_treatment_edges_.end()) {
        pending_edge_treatment_edges_.push_back(edge);
    } else {
        pending_edge_treatment_edges_.erase(existing);
    }
    if (edge_treatment_dialog_ != nullptr) {
        edge_treatment_dialog_->set_edge_references(
            pending_edge_treatment_edges_);
    }
    std::set<zima::viewer::EdgeKey> highlighted_edges;
    for (const auto& selected : pending_edge_treatment_edges_) {
        highlighted_edges.insert({selected.owner_id,
            selected.semantic_key, candidate.instance_path});
    }
    viewer_->set_constraint_reference_highlights({}, highlighted_edges);
    state_->setText(tr("Vybrané hrany Tělesa: %1. Potvrďte OK.")
        .arg(pending_edge_treatment_edges_.size()));
}

bool AssemblyWorkspaceWindow::finish_edge_treatment_selection() {
    return false;
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
    auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    if (part == nullptr && assembly == nullptr) return;
    if (part != nullptr) {
        const auto active_occurrence = resolve_active_occurrence(
            part->session.document().document_id);
        if (!active_occurrence || candidate.instance_path != *active_occurrence) {
            state_->setText(tr(
                "Vyberte plochu přesného aktivního výskytu Partu."));
            return;
        }
    } else {
        const auto path = zima::assembly::InstancePath::decode(candidate.instance_path);
        if (path.occurrence_ids.empty() ||
            assembly->session.document().find_occurrence(path.occurrence_ids.front()) ==
                nullptr) {
            state_->setText(tr("Vyberte plochu komponenty aktivní sestavy."));
            return;
        }
    }
    zima::kernel::Vec3 origin;
    zima::kernel::Vec3 normal;
    bool resolved = false;
    std::vector<zima::kernel::Vec3> surface_triangles;
    const auto* construction = part == nullptr ? nullptr
        : part->session.document().find_construction(candidate.owner_id);
    if (construction != nullptr &&
        construction->kind == zima::document::ConstructionKind::Plane) {
        origin = construction->origin;
        normal = construction->direction;
        resolved = true;
    } else {
        const auto scene = assembly == nullptr
            ? zima::kernel::ViewerMesh{}
            : assembly->session.document().build_scene();
        const auto* references = assembly != nullptr
            ? &scene.original_references
            : part->session.calculated_boundaries().empty()
                ? nullptr
                : &part->session.calculated_boundaries().back()
                    .mesh.original_references;
        if (references == nullptr) {
            state_->setText(tr("Vybraná plocha nemá uloženou geometrii."));
            return;
        }
        for (std::size_t triangle = 0;
             triangle < references->triangle_references.size(); ++triangle) {
            const auto& reference = references->triangle_references[triangle];
            if (reference.owner_id != candidate.owner_id ||
                reference.semantic_key != candidate.semantic_key ||
                reference.instance_path != candidate.instance_path) continue;
            const auto first = references->vertices[
                references->triangles[triangle * 3]];
            const auto second = references->vertices[
                references->triangles[triangle * 3 + 1]];
            const auto third = references->vertices[
                references->triangles[triangle * 3 + 2]];
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
                 other < references->triangle_references.size(); ++other) {
                if (references->triangle_references[other] != reference) continue;
                for (int corner = 0; corner < 3; ++corner) {
                    const auto& point = references->vertices[
                        references->triangles[other * 3 + corner]];
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
            {candidate.owner_id, candidate.semantic_key, candidate.instance_path},
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
        {candidate.owner_id, candidate.semantic_key, candidate.instance_path},
        origin, normal);
    extrusion_target_dialog_ = nullptr;
    state_->setText(tr("Cílová plocha vytažení byla nastavena."));
}


void AssemblyWorkspaceWindow::begin_normal_view_selection() {
    if (viewer_ == nullptr || properties_dialog_ != nullptr) return;
    normal_view_selection_active_ = true;
    viewer_->clear_selection();
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Face});
    state_->setText(tr("Vyberte plochu ve 3D pohledu – pohled se natočí kolmo k ní."));
}

void AssemblyWorkspaceWindow::accept_normal_view_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    normal_view_selection_active_ = false;
    const auto normal = viewer_->candidate_face_normal(candidate);
    viewer_->clear_selection();
    refresh_scene();
    if (!normal) {
        state_->setText(tr("Z vybrané plochy nelze určit normálu."));
        return;
    }
    // Python's _set_view_normal looks along the negative face normal so the
    // face itself faces the camera.
    viewer_->set_view_direction(
        zima::kernel::Vec3{-normal->x, -normal->y, -normal->z});
    viewer_->fit_all();
    state_->setText(tr("Pohled je kolmý k vybrané ploše."));
}

namespace {

// Port of Python's camera_angles_for_view_direction() (viewer.py:264):
// maps a world viewing direction onto (yaw_degrees, pitch_degrees).
std::pair<double, double> camera_angles_for_view_direction(
    const zima::kernel::Vec3& direction) {
    const double length = std::sqrt(direction.x * direction.x +
        direction.y * direction.y + direction.z * direction.z);
    if (length <= 1e-12) return {0.0, 0.0};
    const double x = direction.x / length;
    const double y = direction.y / length;
    const double z = direction.z / length;
    const double horizontal = std::hypot(x, y);
    const double yaw = horizontal > 1e-12
        ? std::atan2(x, y) * 180.0 / std::numbers::pi : 0.0;
    const double pitch = std::atan2(-horizontal, -z) * 180.0 / std::numbers::pi;
    return {yaw, pitch};
}

// Port of Python's _camera_roll_for_direction() (app.py:38114): aligns a
// world direction to a requested screen-space angle (used to roll the
// camera so the secondary orientation reference points TOP/BOTTOM/LEFT/RIGHT).
double camera_roll_for_direction(
    const zima::kernel::Vec3& view_direction,
    const zima::kernel::Vec3& world_direction,
    double target_angle_degrees) {
    const auto [yaw_degrees, pitch_degrees] =
        camera_angles_for_view_direction(view_direction);
    const double yaw = yaw_degrees * std::numbers::pi / 180.0;
    const double pitch = pitch_degrees * std::numbers::pi / 180.0;
    const double length = std::sqrt(world_direction.x * world_direction.x +
        world_direction.y * world_direction.y +
        world_direction.z * world_direction.z);
    if (length <= 1e-12) return 0.0;
    const double dx = world_direction.x / length;
    const double dy = world_direction.y / length;
    const double dz = world_direction.z / length;
    const double yaw_x = std::cos(yaw) * dx - std::sin(yaw) * dy;
    const double yaw_y = std::sin(yaw) * dx + std::cos(yaw) * dy;
    const double screen_y = std::cos(pitch) * yaw_y - std::sin(pitch) * dz;
    if (std::hypot(yaw_x, screen_y) <= 1e-9) return 0.0;
    return target_angle_degrees -
        std::atan2(screen_y, yaw_x) * 180.0 / std::numbers::pi;
}

}  // namespace

void AssemblyWorkspaceWindow::show_orientation_dialog() {
    if (viewer_ == nullptr || properties_dialog_ != nullptr ||
        orientation_dialog_ != nullptr) return;
    const auto document_id = workspace_.active_document_id();
    std::string named_views_json = "[]";
    if (const auto* part = workspace_.open_part(document_id)) {
        named_views_json = part->session.document().named_views;
    } else if (const auto* assembly = workspace_.open_assembly(document_id)) {
        named_views_json = assembly->session.document().named_views;
    } else {
        return;
    }
    std::vector<zima::app::OrientationSavedView> custom_views;
    try {
        const auto parsed = nlohmann::json::parse(named_views_json);
        if (parsed.is_array()) {
            for (const auto& entry : parsed) {
                if (!entry.is_object() || !entry.contains("name")) continue;
                zima::app::OrientationSavedView view;
                view.name = QString::fromStdString(
                    entry.value("name", std::string()));
                const auto zoom = static_cast<float>(entry.value("zoom", 1.0));
                const std::array<float, 8> state{
                    1.0F, 0.0F, 0.0F, 0.0F, zoom,
                    static_cast<float>(entry.value("pan_x", 0.0)),
                    static_cast<float>(entry.value("pan_y", 0.0)),
                    static_cast<float>(entry.value("reference_scale", zoom))};
                view.camera_state = state;
                custom_views.push_back(std::move(view));
            }
        }
    } catch (const nlohmann::json::exception&) {
        custom_views.clear();
    }
    auto* dialog = new zima::app::OrientationDialog(std::move(custom_views), this);
    orientation_dialog_ = dialog;
    orientation_dialog_original_camera_ = viewer_->camera_state();
    orientation_reference_candidates_.clear();
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Face});
    dialog->set_reference_request_callback([this](std::size_t index) {
        pending_orientation_reference_index_ = index;
        state_->setText(index == 0
            ? tr("Vyberte plochu nebo rovinu pro první směr pohledu.")
            : tr("Vyberte plochu nebo rovinu pro orientační referenci."));
    });
    // Reject a candidate reference whose direction is (anti-)parallel to a
    // reference already accepted in the other row -- e.g. picking TOP twice,
    // or two opposite faces -- mirroring Python's
    // _orientation_references_are_independent().
    dialog->set_independence_check_callback(
        [this](const std::string& existing, const std::string& candidate) {
            const auto existing_it = orientation_reference_candidates_.find(existing);
            const auto candidate_it = orientation_reference_candidates_.find(candidate);
            if (existing_it == orientation_reference_candidates_.end() ||
                candidate_it == orientation_reference_candidates_.end()) {
                return true;
            }
            const auto existing_normal = viewer_->candidate_face_normal(existing_it->second);
            const auto candidate_normal = viewer_->candidate_face_normal(candidate_it->second);
            if (!existing_normal || !candidate_normal) return true;
            const double dot = existing_normal->x * candidate_normal->x +
                existing_normal->y * candidate_normal->y +
                existing_normal->z * candidate_normal->z;
            return std::abs(dot) < 1.0 - 1.0e-8;
        });
    dialog->set_reference_rejected_callback([this] {
        state_->setText(tr(
            "Tuto referenci nelze použít, je rovnoběžná s již zadanou "
            "referencí."));
    });
    const auto apply_rows = [this](
            const std::vector<zima::app::OrientationReferenceRow>& rows) {
        std::vector<std::pair<zima::app::OrientationReferenceRow,
            zima::viewer::ViewerCandidate>> resolved;
        for (const auto& row : rows) {
            const auto found = orientation_reference_candidates_.find(row.reference);
            if (found == orientation_reference_candidates_.end()) continue;
            resolved.emplace_back(row, found->second);
        }
        if (resolved.empty()) return;
        const auto primary_it = std::find_if(resolved.begin(), resolved.end(),
            [](const auto& entry) {
                return entry.first.role == "front" || entry.first.role == "back";
            });
        const auto& primary = primary_it != resolved.end() ? *primary_it : resolved.front();
        auto primary_normal = viewer_->candidate_face_normal(primary.second);
        if (!primary_normal) return;
        zima::kernel::Vec3 normal = *primary_normal;
        const bool reverse_role =
            primary.first.role == "back" || primary.first.role == "bottom" ||
            primary.first.role == "right";
        if (primary.first.flip != reverse_role) {
            normal = {-normal.x, -normal.y, -normal.z};
        }
        double roll_degrees = 0.0;
        const auto secondary_it = std::find_if(resolved.begin(), resolved.end(),
            [&](const auto& entry) {
                return &entry != &primary &&
                    (entry.first.role == "top" || entry.first.role == "bottom" ||
                     entry.first.role == "left" || entry.first.role == "right");
            });
        if (secondary_it != resolved.end()) {
            auto secondary_normal = viewer_->candidate_face_normal(secondary_it->second);
            if (secondary_normal) {
                zima::kernel::Vec3 secondary = *secondary_normal;
                if (secondary_it->first.flip) {
                    secondary = {-secondary.x, -secondary.y, -secondary.z};
                }
                static const std::map<std::string, double> target_angles{
                    {"right", 0.0}, {"top", 90.0}, {"left", 180.0}, {"bottom", -90.0}};
                const auto target = target_angles.find(secondary_it->first.role);
                if (target != target_angles.end()) {
                    roll_degrees = camera_roll_for_direction(
                        {-normal.x, -normal.y, -normal.z}, secondary,
                        target->second);
                }
            }
        }
        viewer_->set_view_direction(
            {-normal.x, -normal.y, -normal.z}, static_cast<float>(roll_degrees));
    };
    dialog->set_rows_changed_callback(apply_rows);
    dialog->set_view_requested_callback(
        [this](const zima::app::OrientationSavedView& view) {
            if (viewer_ == nullptr) return;
            if (!view.standard.empty()) {
                static const std::map<std::string, zima::viewer::StandardView>
                    standard_views{
                        {"default", zima::viewer::StandardView::Isometric},
                        {"front", zima::viewer::StandardView::Front},
                        {"back", zima::viewer::StandardView::Back},
                        {"top", zima::viewer::StandardView::Top},
                        {"bottom", zima::viewer::StandardView::Bottom},
                        {"left", zima::viewer::StandardView::Left},
                        {"right", zima::viewer::StandardView::Right}};
                const auto found = standard_views.find(view.standard);
                if (found != standard_views.end())
                    viewer_->set_standard_view(found->second);
                return;
            }
            viewer_->animate_camera_state(view.camera_state);
        });
    const auto persist_named_views =
        [this, document_id](const nlohmann::json& merged) {
        if (auto* part = workspace_.open_part(document_id)) {
            auto next = part->session.document();
            next.named_views = merged.dump();
            part->session.commit(std::move(next), part->session.calculated_boundaries());
        } else if (auto* assembly = workspace_.open_assembly(document_id)) {
            auto next = assembly->session.document();
            next.named_views = merged.dump();
            assembly->session.commit(std::move(next));
        }
    };
    const auto load_named_views = [this, document_id]() -> nlohmann::json {
        std::string source = "[]";
        if (const auto* part = workspace_.open_part(document_id)) {
            source = part->session.document().named_views;
        } else if (const auto* assembly = workspace_.open_assembly(document_id)) {
            source = assembly->session.document().named_views;
        }
        try {
            auto parsed = nlohmann::json::parse(source);
            if (parsed.is_array()) return parsed;
        } catch (const nlohmann::json::exception&) {
        }
        return nlohmann::json::array();
    };
    dialog->set_save_view_callback(
        [this, dialog, load_named_views, persist_named_views](const QString& name) {
        if (viewer_ == nullptr) return;
        zima::app::OrientationSavedView view;
        view.name = name;
        view.camera_state = viewer_->camera_state();
        dialog->append_saved_view(view);
        auto existing = load_named_views();
        nlohmann::json merged = nlohmann::json::array();
        for (const auto& entry : existing) {
            if (entry.is_object() &&
                entry.value("name", std::string()) != name.toStdString())
                merged.push_back(entry);
        }
        merged.push_back({{"name", name.toStdString()},
            {"pan_x", view.camera_state[5]}, {"pan_y", view.camera_state[6]},
            {"zoom", view.camera_state[4]},
            {"reference_scale", view.camera_state[7]}});
        persist_named_views(merged);
    });
    dialog->set_delete_view_callback(
        [this, load_named_views, persist_named_views](const QString& name) {
        auto existing = load_named_views();
        nlohmann::json merged = nlohmann::json::array();
        for (const auto& entry : existing) {
            if (entry.is_object() &&
                entry.value("name", std::string()) != name.toStdString())
                merged.push_back(entry);
        }
        persist_named_views(merged);
    });
    connect(dialog, &QObject::destroyed, this, [this] {
        orientation_dialog_ = nullptr;
        orientation_reference_candidates_.clear();
        pending_orientation_reference_index_ = 0;
        if (viewer_ != nullptr) {
            viewer_->set_candidate_filter({});
            viewer_->clear_selection();
        }
    });
    connect(dialog, &QDialog::finished, this, [this](int result) {
        if (result != QDialog::Accepted && viewer_ != nullptr) {
            viewer_->set_camera_state(orientation_dialog_original_camera_);
        }
    });
    dialog->show();
    state_->setText(tr("Vyberte plochu nebo rovinu pro první směr pohledu."));
}

void AssemblyWorkspaceWindow::accept_orientation_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    if (orientation_dialog_ == nullptr) return;
    const std::string descriptor =
        candidate.owner_id + ":face:" + std::to_string(
            orientation_reference_candidates_.size());
    orientation_reference_candidates_[descriptor] = candidate;
    const auto label = candidate.semantic_key.starts_with("origin:plane:")
        ? tr("Rovina %1").arg(QString::fromStdString(
            candidate.semantic_key.substr(std::string("origin:plane:").size())).toUpper())
        : tr("Plocha");
    orientation_dialog_->accept_reference(descriptor, label);
    viewer_->clear_selection();
}

void AssemblyWorkspaceWindow::save_active_assembly() {
    auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    if (assembly == nullptr) return;
    QString path = QString::fromStdString(assembly->path.string());
    if (path.isEmpty()) path = save_file(
        this, application_settings_.text("file.save_assembly", tr("Uložit sestavu ZIMA-CAD")),
        QString::fromStdString((working_directory_ / "assembly.asmz").string()),
        application_settings_.text("file.filter.assembly",
            tr("Sestava ZIMA-CAD (*.asmz)")), "asmz",
        application_settings_.translations);
    if (path.isEmpty()) return;
    if (!path.endsWith(".asmz", Qt::CaseInsensitive)) {
        auto normalized = std::filesystem::path(path.toStdString());
        normalized.replace_extension(".asmz");
        path = QString::fromStdString(normalized.string());
    }
    try {
        assembly->session.document().save(path.toStdString());
        assembly->path = path.toStdString();
        working_directory_ = assembly->path.parent_path();
        assembly->session.mark_saved();
        refresh_tabs();
    } catch (const std::exception& error) {
        QMessageBox::critical(this,
            application_settings_.text("message.save_failed", tr("Uložení se nezdařilo")),
            error.what());
    }
}

void AssemblyWorkspaceWindow::save_active_document() {
    if(auto* drawing=workspace_.open_drawing(workspace_.active_document_id())) {
        QString path=QString::fromStdString(drawing->path.string());
        if(path.isEmpty()) path=save_file(
            this, application_settings_.text("file.save_drawing", tr("Uložit výkres")),
            QString::fromStdString((working_directory_ / "drawing.drwz").string()),
            application_settings_.text("file.filter.drawing",
                tr("Výkres ZIMA-CAD (*.drwz)")), "drwz",
            application_settings_.translations);
        if(path.isEmpty()) return;
        if(!path.endsWith(".drwz", Qt::CaseInsensitive)) {
            auto normalized = std::filesystem::path(path.toStdString());
            normalized.replace_extension(".drwz");
            path = QString::fromStdString(normalized.string());
        }
        try { drawing->document.save(path.toStdString()); drawing->path=path.toStdString();
            working_directory_ = drawing->path.parent_path();
            drawing_workspace_->edit_workspace_document(drawing->document.document_id);
            refresh_tabs(); }
        catch(const std::exception& error) {
            QMessageBox::critical(this,
                application_settings_.text("message.save_failed", tr("Uložení se nezdařilo")),
                error.what()); }
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
        this, application_settings_.text("file.save_part", tr("Uložit díl ZIMA-CAD")),
        QString::fromStdString((working_directory_ / "part.prtz").string()),
        application_settings_.text("file.filter.part",
            tr("Díl ZIMA-CAD (*.prtz)")), "prtz",
        application_settings_.translations);
    if (path.isEmpty()) return;
    if (!path.endsWith(".prtz", Qt::CaseInsensitive)) {
        auto normalized = std::filesystem::path(path.toStdString());
        normalized.replace_extension(".prtz");
        path = QString::fromStdString(normalized.string());
    }
    try {
        part->session.document().save(path.toStdString(),
                                      part->session.calculated_boundaries());
        part->path = path.toStdString();
        working_directory_ = part->path.parent_path();
        part->session.mark_saved();
        refresh_tabs();
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::critical(this,
            application_settings_.text("message.save_failed", tr("Uložení se nezdařilo")),
            error.what());
    }
}

void AssemblyWorkspaceWindow::save_active_document_as() {
    const std::string document_id = workspace_.active_document_id();
    if (document_id.empty()) return;
    QString caption;
    QString fallback_name;
    QString filter;
    QString suffix;
    if (const auto* drawing = workspace_.open_drawing(document_id)) {
        static_cast<void>(drawing);
        caption = application_settings_.text("file.save_drawing", tr("Uložit výkres"));
        fallback_name = QStringLiteral("drawing.drwz");
        filter = application_settings_.text("file.filter.drawing",
            tr("Výkres ZIMA-CAD (*.drwz)"));
        suffix = "drwz";
    } else if (const auto* assembly = workspace_.open_assembly(document_id)) {
        static_cast<void>(assembly);
        caption = application_settings_.text("file.save_assembly",
            tr("Uložit sestavu ZIMA-CAD"));
        fallback_name = QStringLiteral("assembly.asmz");
        filter = application_settings_.text("file.filter.assembly",
            tr("Sestava ZIMA-CAD (*.asmz)"));
        suffix = "asmz";
    } else if (const auto* part = workspace_.open_part(document_id)) {
        static_cast<void>(part);
        caption = application_settings_.text("file.save_part",
            tr("Uložit díl ZIMA-CAD"));
        fallback_name = QStringLiteral("part.prtz");
        filter = application_settings_.text("file.filter.part",
            tr("Díl ZIMA-CAD (*.prtz)"));
        suffix = "prtz";
    } else {
        return;
    }
    const QString initial = QString::fromStdString((working_directory_ /
        fallback_name.toStdString()).string());
    QString selected = save_file(this, caption, initial, filter, suffix,
                                 application_settings_.translations);
    if (selected.isEmpty()) return;
    const QString dotted_suffix = QStringLiteral(".") + suffix;
    std::filesystem::path target = selected.toStdString();
    if (QString::fromStdString(target.extension().string()).compare(
            dotted_suffix, Qt::CaseInsensitive) != 0) {
        target.replace_extension(dotted_suffix.toStdString());
        selected = QString::fromStdString(target.string());
    }
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
        QMessageBox::critical(this,
            application_settings_.text("message.save_failed", tr("Uložení se nezdařilo")),
            error.what());
    }
}

void AssemblyWorkspaceWindow::set_working_directory() {
    const QString selected = choose_directory(
        this, application_settings_.text("file.set_working_directory",
            tr("Nastavit pracovní adresář")),
        QString::fromStdString(working_directory_.string()),
        application_settings_.translations);
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

std::optional<std::filesystem::path>
AssemblyWorkspaceWindow::active_document_file_path() const {
    const std::string id = workspace_.active_document_id();
    if (id.empty()) return std::nullopt;
    if (const auto* part = workspace_.open_part(id);
        part != nullptr && !part->path.empty()) return part->path;
    if (const auto* assembly = workspace_.open_assembly(id);
        assembly != nullptr && !assembly->path.empty()) return assembly->path;
    if (const auto* drawing = workspace_.open_drawing(id);
        drawing != nullptr && !drawing->path.empty()) return drawing->path;
    return std::nullopt;
}

std::vector<std::filesystem::path> AssemblyWorkspaceWindow::document_archive_paths(
    const std::filesystem::path& file_path) {
    std::vector<std::pair<int, std::filesystem::path>> archives;
    const auto target = std::filesystem::absolute(file_path).lexically_normal();
    const auto parent = target.parent_path();
    if (std::filesystem::is_directory(parent)) {
        const std::string prefix = target.filename().string() + ".";
        for (const auto& entry : std::filesystem::directory_iterator(parent)) {
            if (!entry.is_regular_file()) continue;
            const std::string name = entry.path().filename().string();
            if (name.rfind(prefix, 0) != 0) continue;
            const std::string suffix = name.substr(prefix.size());
            if (suffix.empty() || !std::all_of(suffix.begin(), suffix.end(),
                    [](unsigned char ch) { return std::isdigit(ch) != 0; })) continue;
            archives.emplace_back(std::stoi(suffix), entry.path());
        }
    }
    std::sort(archives.begin(), archives.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    std::vector<std::filesystem::path> result;
    result.reserve(archives.size());
    for (auto& [version, path] : archives) result.push_back(std::move(path));
    return result;
}

std::map<std::filesystem::path, std::vector<std::filesystem::path>>
AssemblyWorkspaceWindow::working_directory_archive_groups(
    const std::filesystem::path& directory) {
    std::map<std::filesystem::path, std::vector<std::pair<int, std::filesystem::path>>>
        groups;
    if (!std::filesystem::is_directory(directory)) return {};
    static const std::array<std::string, 5> document_extensions = {
        ".prtz", ".asmz", ".drwz", ".frmz", ".tblz"};
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        const auto& path = entry.path();
        const std::string numeric_suffix = path.extension().string().empty() ? "" :
            path.extension().string().substr(1);
        if (numeric_suffix.empty() || !std::all_of(numeric_suffix.begin(),
                numeric_suffix.end(),
                [](unsigned char ch) { return std::isdigit(ch) != 0; })) continue;
        const auto document_path = path.stem().empty() ? path :
            path.parent_path() / path.stem();
        const auto document_extension = document_path.extension().string();
        std::string lowered = document_extension;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
            [](unsigned char ch) { return std::tolower(ch); });
        if (std::find(document_extensions.begin(), document_extensions.end(), lowered) ==
                document_extensions.end()) continue;
        groups[document_path].emplace_back(std::stoi(numeric_suffix), path);
    }
    std::map<std::filesystem::path, std::vector<std::filesystem::path>> result;
    for (auto& [document_path, archives] : groups) {
        std::sort(archives.begin(), archives.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
        std::vector<std::filesystem::path> paths;
        paths.reserve(archives.size());
        for (auto& [version, path] : archives) paths.push_back(std::move(path));
        result.emplace(document_path, std::move(paths));
    }
    return result;
}

void AssemblyWorkspaceWindow::refresh_delete_file_actions() {
    const auto target = active_document_file_path();
    const bool has_saved_document = target.has_value() && std::filesystem::is_regular_file(*target);
    const auto archives = has_saved_document
        ? document_archive_paths(*target) : std::vector<std::filesystem::path>{};
    rename_document_action_->setEnabled(has_saved_document);
    delete_old_versions_action_->setEnabled(!archives.empty());
    delete_old_versions_keep_latest_action_->setEnabled(archives.size() > 1);
    delete_current_file_action_->setEnabled(has_saved_document);
    delete_all_versions_action_->setEnabled(has_saved_document);
    const bool has_working_directory = std::filesystem::is_directory(working_directory_);
    delete_working_directory_old_versions_action_->setEnabled(has_working_directory);
    delete_working_directory_keep_latest_action_->setEnabled(has_working_directory);
}

namespace {
QString format_file_size(std::uintmax_t size) {
    double value = static_cast<double>(size);
    for (const char* unit : {"B", "kB", "MB"}) {
        if (value < 1000.0) {
            return QStringLiteral("%1 %2").arg(
                QString::number(value, 'f', std::string(unit) == "B" ? 0 : 1), unit);
        }
        value /= 1000.0;
    }
    return QStringLiteral("%1 GB").arg(QString::number(value, 'f', 1));
}

std::uintmax_t paths_total_size(const std::vector<std::filesystem::path>& paths) {
    std::uintmax_t total = 0;
    for (const auto& path : paths) {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (!error) total += size;
    }
    return total;
}
}  // namespace

void AssemblyWorkspaceWindow::rename_document_file() {
    const auto target = active_document_file_path();
    if (!target.has_value()) return;
    if (rename_document_dialog_ != nullptr) {
        rename_document_dialog_->raise();
        rename_document_dialog_->activateWindow();
        return;
    }
    const std::filesystem::path old_path = std::filesystem::absolute(*target).lexically_normal();
    auto* dialog = new RenameDocumentDialog(
        QString::fromStdString(old_path.filename().string()),
        [this, old_path](QString new_name) -> QString {
            std::filesystem::path candidate(new_name.toStdString());
            candidate = candidate.filename();
            if (candidate.empty())
                return tr("Zadejte platný název souboru.");
            std::string requested_extension = candidate.extension().string();
            std::string current_extension = old_path.extension().string();
            std::transform(requested_extension.begin(), requested_extension.end(),
                requested_extension.begin(),
                [](unsigned char ch) { return std::tolower(ch); });
            std::string current_extension_lower = current_extension;
            std::transform(current_extension_lower.begin(), current_extension_lower.end(),
                current_extension_lower.begin(),
                [](unsigned char ch) { return std::tolower(ch); });
            if (requested_extension != current_extension_lower) {
                candidate = candidate.stem();
                candidate += current_extension;
            }
            const std::filesystem::path new_path = old_path.parent_path() / candidate;
            if (new_path == old_path) return QString();
            if (std::filesystem::exists(new_path)) {
                return tr("Soubor %1 již existuje.")
                    .arg(QString::fromStdString(new_path.filename().string()));
            }
            const bool is_source_document =
                old_path.extension() == ".prtz" || old_path.extension() == ".asmz";
            const std::filesystem::path old_drawing_path = is_source_document
                ? std::filesystem::path(old_path).replace_extension(".drwz")
                : std::filesystem::path{};
            const std::filesystem::path new_drawing_path = is_source_document
                ? std::filesystem::path(new_path).replace_extension(".drwz")
                : std::filesystem::path{};
            const bool rename_companion_drawing = is_source_document &&
                std::filesystem::is_regular_file(old_drawing_path);
            if (rename_companion_drawing && std::filesystem::exists(new_drawing_path)) {
                return tr("Soubor %1 již existuje.")
                    .arg(QString::fromStdString(new_drawing_path.filename().string()));
            }

            // Rewrite in-memory Assembly component references and Drawing
            // source references that point at the file being renamed, both
            // for currently open documents and for documents saved on disk
            // in the same directory or the working directory.
            std::unordered_set<std::string> updated_ids;
            const auto rewrite_open_assembly_paths = [&](zima::workspace::AssemblyState& state) {
                bool changed = false;
                auto document = state.session.document();
                for (auto& component : document.components) {
                    if (std::filesystem::absolute(component.source_path).lexically_normal() ==
                            old_path) {
                        component.source_path = new_path;
                        changed = true;
                    }
                }
                if (changed) state.session.replace(std::move(document));
                return changed;
            };
            for (auto& state : workspace_.documents()) {
                if (auto* assembly = std::get_if<zima::workspace::AssemblyState>(&state)) {
                    if (rewrite_open_assembly_paths(*assembly)) {
                        updated_ids.insert(assembly->session.document().document_id);
                    }
                } else if (auto* drawing = std::get_if<zima::workspace::DrawingState>(&state)) {
                    for (auto& sheet : drawing->document.sheets) {
                        for (auto& view : sheet.views) {
                            if (!view.source_path.empty() &&
                                std::filesystem::absolute(view.source_path).lexically_normal() ==
                                    old_path) {
                                view.source_path = new_path;
                                updated_ids.insert(drawing->document.document_id);
                            }
                        }
                    }
                }
            }

            // Also rewrite Assembly/Drawing documents saved on disk but not
            // currently open, matching Python's _rename_document_file_to
            // (which loads every candidate document in the file's directory
            // and the working directory, rewrites any reference to the
            // renamed file, and re-saves it). Only Assembly (.asmz) and
            // Drawing (.drwz) documents can hold such references; Part
            // (.prtz) documents cannot reference other documents.
            std::unordered_set<std::string> open_document_paths;
            for (auto& state : workspace_.documents()) {
                if (auto* part = std::get_if<zima::workspace::PartState>(&state)) {
                    open_document_paths.insert(
                        std::filesystem::absolute(part->path).lexically_normal().string());
                } else if (auto* assembly = std::get_if<zima::workspace::AssemblyState>(&state)) {
                    open_document_paths.insert(
                        std::filesystem::absolute(assembly->path).lexically_normal().string());
                } else if (auto* drawing = std::get_if<zima::workspace::DrawingState>(&state)) {
                    open_document_paths.insert(
                        std::filesystem::absolute(drawing->path).lexically_normal().string());
                }
            }
            std::unordered_set<std::string> scanned_paths;
            const auto scan_directory_for_references = [&](const std::filesystem::path& directory) {
                if (!std::filesystem::is_directory(directory)) return;
                for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                    if (!entry.is_regular_file()) continue;
                    const auto candidate =
                        std::filesystem::absolute(entry.path()).lexically_normal();
                    const std::string extension_lower = [&] {
                        std::string ext = candidate.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(),
                            [](unsigned char ch) { return std::tolower(ch); });
                        return ext;
                    }();
                    if (extension_lower != ".asmz" && extension_lower != ".drwz") continue;
                    const std::string key = candidate.string();
                    if (open_document_paths.count(key) != 0) continue;
                    if (!scanned_paths.insert(key).second) continue;
                    if (extension_lower == ".asmz") {
                        zima::assembly::AssemblyDocument document;
                        try {
                            document = zima::assembly::AssemblyDocument::load(candidate);
                        } catch (const std::exception&) {
                            continue;
                        }
                        bool changed = false;
                        for (auto& component : document.components) {
                            if (std::filesystem::absolute(component.source_path)
                                    .lexically_normal() == old_path) {
                                component.source_path = new_path;
                                changed = true;
                            }
                        }
                        if (changed) {
                            try {
                                document.save(candidate);
                            } catch (const std::exception&) {
                            }
                        }
                    } else {
                        zima::drawing::DrawingDocument document;
                        try {
                            document = zima::drawing::DrawingDocument::load(candidate);
                        } catch (const std::exception&) {
                            continue;
                        }
                        bool changed = false;
                        for (auto& sheet : document.sheets) {
                            for (auto& view : sheet.views) {
                                if (!view.source_path.empty() &&
                                    std::filesystem::absolute(view.source_path)
                                            .lexically_normal() == old_path) {
                                    view.source_path = new_path;
                                    changed = true;
                                }
                            }
                        }
                        if (changed) {
                            try {
                                document.save(candidate);
                            } catch (const std::exception&) {
                            }
                        }
                    }
                }
            };
            scan_directory_for_references(old_path.parent_path());
            if (!working_directory_.empty() &&
                std::filesystem::is_directory(working_directory_)) {
                scan_directory_for_references(working_directory_);
                for (const auto& entry :
                        std::filesystem::recursive_directory_iterator(working_directory_)) {
                    if (entry.is_directory()) {
                        scan_directory_for_references(entry.path());
                    }
                }
            }

            try {
                std::filesystem::rename(old_path, new_path);
                if (rename_companion_drawing) {
                    std::filesystem::rename(old_drawing_path, new_drawing_path);
                }
            } catch (const std::exception& error) {
                return QString::fromStdString(error.what());
            }

            for (auto& state : workspace_.documents()) {
                if (auto* part = std::get_if<zima::workspace::PartState>(&state)) {
                    if (std::filesystem::absolute(part->path).lexically_normal() == old_path)
                        part->path = new_path;
                } else if (auto* assembly = std::get_if<zima::workspace::AssemblyState>(&state)) {
                    if (std::filesystem::absolute(assembly->path).lexically_normal() == old_path)
                        assembly->path = new_path;
                    if (rename_companion_drawing) continue;
                } else if (auto* drawing = std::get_if<zima::workspace::DrawingState>(&state)) {
                    if (std::filesystem::absolute(drawing->path).lexically_normal() == old_path)
                        drawing->path = new_path;
                    else if (rename_companion_drawing &&
                             std::filesystem::absolute(drawing->path).lexically_normal() ==
                                 std::filesystem::absolute(old_drawing_path).lexically_normal())
                        drawing->path = new_drawing_path;
                }
            }
            refresh_tabs();
            refresh_scene();
            state_->setText(tr("Soubor přejmenován na %1")
                .arg(QString::fromStdString(new_path.filename().string())));
            return QString();
        }, application_settings_, this);
    rename_document_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (rename_document_dialog_ == dialog) rename_document_dialog_ = nullptr;
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::delete_current_document_file() {
    const auto target = active_document_file_path();
    if (!target.has_value() || !std::filesystem::is_regular_file(*target)) return;
    const auto answer = QMessageBox::warning(
        this, tr("Odstranit aktuální soubor"),
        tr("Opravdu chcete odstranit soubor %1?")
            .arg(QString::fromStdString(target->filename().string())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    std::error_code error;
    std::filesystem::remove(*target, error);
    if (error) {
        QMessageBox::critical(this, tr("Odstranění selhalo"),
            QString::fromStdString(error.message()));
        return;
    }
    const QString deleted_name = QString::fromStdString(target->filename().string());
    close_document(-1);
    state_->setText(tr("Soubor %1 odstraněn.").arg(deleted_name));
}

void AssemblyWorkspaceWindow::delete_old_file_versions_keep_latest() {
    const auto target = active_document_file_path();
    if (!target.has_value()) return;
    const auto archives = document_archive_paths(*target);
    if (archives.size() < 2) {
        QMessageBox::information(this, tr("Staré verze kromě nejnovější"),
            tr("Žádné starší verze souboru %1 nebyly nalezeny.")
                .arg(QString::fromStdString(target->filename().string())));
        return;
    }
    const std::vector<std::filesystem::path> to_delete(
        archives.begin(), archives.end() - 1);
    const auto answer = QMessageBox::question(
        this, tr("Staré verze kromě nejnovější"),
        tr("Odstranit %1 starších verzí souboru %2?")
            .arg(to_delete.size())
            .arg(QString::fromStdString(target->filename().string())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    for (const auto& path : to_delete) {
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error) {
            QMessageBox::critical(this, tr("Odstranění selhalo"),
                QString::fromStdString(error.message()));
            return;
        }
    }
    refresh_delete_file_actions();
    state_->setText(tr("Odstraněno %1 starších verzí.").arg(to_delete.size()));
}

void AssemblyWorkspaceWindow::delete_old_file_versions() {
    const auto target = active_document_file_path();
    if (!target.has_value()) return;
    const auto archives = document_archive_paths(*target);
    if (archives.empty()) {
        QMessageBox::information(this, tr("Staré verze"),
            tr("Žádné starší verze souboru %1 nebyly nalezeny.")
                .arg(QString::fromStdString(target->filename().string())));
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Staré verze"),
        tr("Odstranit %1 starších verzí souboru %2?")
            .arg(archives.size())
            .arg(QString::fromStdString(target->filename().string())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    for (const auto& path : archives) {
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error) {
            QMessageBox::critical(this, tr("Odstranění selhalo"),
                QString::fromStdString(error.message()));
            return;
        }
    }
    refresh_delete_file_actions();
    state_->setText(tr("Odstraněno %1 starších verzí.").arg(archives.size()));
}

void AssemblyWorkspaceWindow::delete_all_file_versions() {
    const auto target = active_document_file_path();
    if (!target.has_value()) return;
    auto archives = document_archive_paths(*target);
    std::vector<std::filesystem::path> existing_paths;
    for (auto& path : archives) {
        if (std::filesystem::is_regular_file(path)) existing_paths.push_back(std::move(path));
    }
    if (std::filesystem::is_regular_file(*target)) existing_paths.push_back(*target);
    const auto answer = QMessageBox::warning(
        this, tr("Aktuální soubor a všechny verze"),
        tr("Odstranit soubor %1 a všech %2 souvisejících souborů?")
            .arg(QString::fromStdString(target->filename().string()))
            .arg(existing_paths.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    for (const auto& path : existing_paths) {
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error) {
            QMessageBox::critical(this, tr("Odstranění selhalo"),
                QString::fromStdString(error.message()));
            return;
        }
    }
    const QString deleted_name = QString::fromStdString(target->filename().string());
    close_document(-1);
    state_->setText(tr("Soubor %1 a všechny verze odstraněny.").arg(deleted_name));
}

void AssemblyWorkspaceWindow::delete_working_directory_old_versions() {
    const auto directory = std::filesystem::absolute(working_directory_).lexically_normal();
    const auto groups = working_directory_archive_groups(directory);
    std::vector<std::filesystem::path> paths;
    for (const auto& [document_path, archives] : groups) {
        for (const auto& path : archives) paths.push_back(path);
    }
    if (paths.empty()) {
        QMessageBox::information(this, tr("Pracovní adresář"),
            tr("V pracovním adresáři %1 nebyly nalezeny žádné starší verze.")
                .arg(QString::fromStdString(directory.string())));
        return;
    }
    const QString size_text = format_file_size(paths_total_size(paths));
    const auto answer = QMessageBox::warning(
        this, tr("Pracovní adresář"),
        tr("Odstranit %1 souborů starších verzí (%2) z pracovního adresáře %3?")
            .arg(paths.size()).arg(size_text)
            .arg(QString::fromStdString(directory.string())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    for (const auto& path : paths) {
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error) {
            QMessageBox::critical(this, tr("Odstranění selhalo"),
                QString::fromStdString(error.message()));
            return;
        }
    }
    refresh_delete_file_actions();
    state_->setText(tr("Odstraněno %1 souborů starších verzí.").arg(paths.size()));
}

void AssemblyWorkspaceWindow::delete_working_directory_old_versions_keep_latest() {
    const auto directory = std::filesystem::absolute(working_directory_).lexically_normal();
    const auto groups = working_directory_archive_groups(directory);
    std::vector<std::filesystem::path> paths;
    for (const auto& [document_path, archives] : groups) {
        if (archives.size() < 2) continue;
        paths.insert(paths.end(), archives.begin(), archives.end() - 1);
    }
    if (paths.empty()) {
        QMessageBox::information(this, tr("Pracovní adresář"),
            tr("V pracovním adresáři %1 nebyly nalezeny žádné starší verze.")
                .arg(QString::fromStdString(directory.string())));
        return;
    }
    const QString size_text = format_file_size(paths_total_size(paths));
    const auto answer = QMessageBox::warning(
        this, tr("Pracovní adresář"),
        tr("Odstranit %1 souborů starších verzí (%2) z pracovního adresáře %3? "
           "Nejnovější verze každého dokumentu zůstane zachována.")
            .arg(paths.size()).arg(size_text)
            .arg(QString::fromStdString(directory.string())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    for (const auto& path : paths) {
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error) {
            QMessageBox::critical(this, tr("Odstranění selhalo"),
                QString::fromStdString(error.message()));
            return;
        }
    }
    refresh_delete_file_actions();
    state_->setText(tr("Odstraněno %1 souborů starších verzí.").arg(paths.size()));
}

void AssemblyWorkspaceWindow::open_new_window() {
    auto* window = new AssemblyWorkspaceWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->showNormal();
}

void AssemblyWorkspaceWindow::show_global_settings() {
    if (global_settings_dialog_ != nullptr) {
        global_settings_dialog_->raise();
        global_settings_dialog_->activateWindow();
        return;
    }
    auto* dialog = new GlobalSettingsDialog(application_settings_, this);
    global_settings_dialog_ = dialog;
    connect(dialog, &QDialog::accepted, this, [this] {
        application_settings_ = ApplicationSettings::load();
    });
    connect(dialog, &QObject::destroyed, this, [this] {
        global_settings_dialog_ = nullptr;
    });
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
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
    const QString path = open_file(this,
        application_settings_.text("menu.file.import", tr("Importovat")),
        QString::fromStdString(working_directory_.string()),
        application_settings_.text("file.filter.import",
            tr("Podporované importní formáty (*.step *.stp);;STEP (*.step *.stp)")),
        application_settings_.translations);
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
                next.insert_history_entry(
                    zima::document::PartHistoryKind::Feature, container.id);
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
        container.feature_parent_id = container.id;
        container.container_origin =
            zima::document::create_container_origin(container.id);
        document.insert_history_entry(
            zima::document::PartHistoryKind::Feature, container.id);
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
    if (format == zima::interchange::Format::Png ||
        format == zima::interchange::Format::Jpeg) {
        const auto image = viewer_->grabFramebuffer();
        if (image.isNull() || !image.save(path)) {
            QMessageBox::warning(this, tr("Export obrázku selhal"),
                tr("Aktuální 3D pohled se nepodařilo uložit."));
            return;
        }
        state_->setText(tr("Aktuální 3D pohled exportován: %1").arg(path));
        return;
    }
    if (format == zima::interchange::Format::Step ||
        format == zima::interchange::Format::Stl) {
        try {
            std::vector<zima::kernel::PlacedBody> bodies;
            if (const auto* part = workspace_.open_part(
                    workspace_.active_document_id())) {
                if (part->session.calculated_boundaries().empty()) {
                    throw std::runtime_error(
                        "Part nemá explicitně vypočtené těleso");
                }
                bodies.push_back({part->session.calculated_boundaries().back(), {}, {}});
            } else if (const auto* assembly = workspace_.open_assembly(
                           workspace_.active_document_id())) {
                const auto suppressed =
                    assembly->session.document().effectively_suppressed_occurrences();
                for (const auto& component : assembly->session.document().components) {
                    if (!component.visible || suppressed.contains(component.occurrence_id)) {
                        continue;
                    }
                    if (component.source_kind !=
                            zima::assembly::ComponentSourceKind::Part) {
                        throw std::runtime_error(
                            "STEP/STL export vnořené podsestavy zatím vyžaduje její "
                            "rozbalení na Part výskyty");
                    }
                    bodies.push_back({component.calculated_source,
                        {component.placement.x, component.placement.y,
                         component.placement.z},
                        {component.placement.rotation_x,
                         component.placement.rotation_y,
                         component.placement.rotation_z}});
                }
            }
            if (format == zima::interchange::Format::Step) {
                kernel_.export_step(bodies, path.toStdString());
            } else {
                kernel_.export_stl(bodies, path.toStdString());
            }
            state_->setText(tr("Model exportován: %1").arg(path));
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Export modelu selhal"), error.what());
        }
        return;
    }
}

std::vector<zima::kernel::BodyResult> AssemblyWorkspaceWindow::calculate_part(
    const zima::document::PartDocument& document,
    const std::vector<zima::kernel::BodyResult>* previous) const {
    const auto operations = document.kernel_operations();
    return previous == nullptr
        ? kernel_.evaluate_history(operations)
        : kernel_.evaluate_history_incremental(operations, *previous);
}

void AssemblyWorkspaceWindow::calculate_assembly_cuts(
    zima::assembly::AssemblyDocument& document) const {
    for (auto& cut : document.cuts) {
        cut.input_component_bodies.clear();
        for (const auto& component : document.components) {
            cut.input_component_bodies.emplace(
                component.occurrence_id, component.calculated_source);
        }
        if (cut.definition.suppressed || cut.target_occurrence_ids.empty()) continue;
        zima::document::PartDocument cutter_document;
        cutter_document.user_parameters = document.user_parameters;
        cutter_document.relations = document.relations;
        cutter_document.sketches = document.sketches;
        cutter_document.constructions = document.constructions;
        cutter_document.history = {cut.definition};
        auto cutter_operations = cutter_document.kernel_operations(true);
        if (cutter_operations.size() != 1) {
            throw std::runtime_error(
                "Assembly cut did not produce one cutter operation");
        }
        cutter_operations.front().operation =
            zima::kernel::BooleanOperation::Add;
        const auto cutter_boundaries = kernel_.evaluate_history(cutter_operations);
        if (cutter_boundaries.empty()) {
            throw std::runtime_error("Assembly cut body is empty");
        }
        for (const auto& target_id : cut.target_occurrence_ids) {
            auto* target = document.find_occurrence(target_id);
            if (target == nullptr) {
                throw std::runtime_error(
                    "Assembly cut target must be an immediate component occurrence");
            }
            if (target->suppressed) continue;
            target->calculated_source = kernel_.subtract_bodies(
                target->calculated_source, cutter_boundaries.back(),
                {target->placement.x, target->placement.y, target->placement.z},
                {target->placement.rotation_x, target->placement.rotation_y,
                 target->placement.rotation_z});
        }
    }
}

void AssemblyWorkspaceWindow::show_primitive_properties(
    zima::document::FeatureKind feature_kind,
    const std::string& container_id) {
    if (properties_dialog_ != nullptr) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    if (part == nullptr && assembly == nullptr) return;
    // Sketch is a HistoryContainer kind, but it is not a primitive body.
    // Keep this guard at the common entry point as well as in Tree dispatch,
    // so no caller can fall through PrimitivePropertiesDialog's default
    // shape branch (which is Box) and accidentally show Kvádr properties.
    if (feature_kind == zima::document::FeatureKind::Sketch) {
        if (part == nullptr) return;
        const auto sketch = std::find_if(
            part->session.document().sketches.begin(),
            part->session.document().sketches.end(), [&](const auto& value) {
                return value.owner_container_id == container_id;
            });
        if (sketch != part->session.document().sketches.end()) {
            show_sketch_properties(sketch->id);
        }
        return;
    }
    const bool assembly_cut = assembly != nullptr;
    if (assembly_cut && feature_kind != zima::document::FeatureKind::Extrusion &&
        feature_kind != zima::document::FeatureKind::Revolution) return;
    std::string source_sketch_id;
    std::optional<zima::sketcher::Sketch> pending_owned_sketch;
    if (container_id.empty() &&
        (feature_kind == zima::document::FeatureKind::Extrusion ||
         feature_kind == zima::document::FeatureKind::Revolution)) {
        if (assembly != nullptr) {
            // Assembly cuts currently own their profile in the Assembly's
            // persisted Sketch collection; use the explicitly selected
            // Assembly Sketch. Part features use the new nested owned-Sketch
            // workflow below.
            source_sketch_id = !selected_sketch_id_.empty()
                ? selected_sketch_id_ : active_sketch_id_;
        } else {
            pending_owned_sketch = zima::sketcher::Sketch::create_default();
            pending_owned_sketch->name = tr("Skica").toStdString();
            source_sketch_id = pending_owned_sketch->id;
        }
    }
    const auto* edited_cut = assembly_cut && !container_id.empty()
        ? assembly->session.document().find_cut(container_id) : nullptr;
    const auto* edited = assembly_cut
        ? (edited_cut == nullptr ? nullptr : &edited_cut->definition)
        : container_id.empty() ? nullptr
                               : part->session.document().find_container(container_id);
    if (!container_id.empty() &&
        (edited == nullptr || edited->feature_kind != feature_kind)) return;
    const bool edit_mode = edited != nullptr;
    const bool pending_profile_edit = edit_mode && pending_profile_feature_ &&
        pending_profile_feature_->id == container_id;
    const bool profile_feature =
        feature_kind == zima::document::FeatureKind::Extrusion ||
        feature_kind == zima::document::FeatureKind::Revolution;
    std::optional<std::size_t> assembly_cut_index;
    if (edit_mode && assembly_cut) {
        const auto& cuts = assembly->session.document().cuts;
        const auto found = std::find_if(cuts.begin(), cuts.end(), [&](const auto& cut) {
            return cut.definition.id == container_id;
        });
        if (found == cuts.end() || found->input_component_bodies.empty()) {
            QMessageBox::warning(this, tr("Chybí vypočtený vstup"),
                tr("Řez nelze editovat bez uložené geometrie jeho vstupu. "
                   "Nejprve explicitně regenerujte sestavu."));
            return;
        }
        assembly_cut_index = static_cast<std::size_t>(
            std::distance(cuts.begin(), found));
    }
    std::optional<std::string> rollback_occurrence;
    const auto rollback_boundary = edit_mode && !assembly_cut && !pending_profile_edit
        ? part->session.rollback_boundary(container_id)
        : std::optional<zima::document::HistoryRollbackBoundary>{};
    if (edit_mode && !assembly_cut && !pending_profile_edit) {
        if (!rollback_boundary) {
            QMessageBox::warning(this, tr("Chybí vypočtený vstup"),
                tr("Prvek nelze editovat bez uložené geometrie jeho vstupu. "
                   "Nejprve explicitně regenerujte Part."));
            return;
        }
        rollback_occurrence = resolve_active_occurrence(
            part->session.document().document_id);
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
    auto initial = edit_mode ? *edited
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
    if (pending_owned_sketch) pending_owned_sketch->owner_container_id = initial.id;
    if (!edit_mode && (feature_kind == zima::document::FeatureKind::Extrusion ||
                       feature_kind == zima::document::FeatureKind::Revolution)) {
        const auto source = zima::document::ProfileSource::Internal;
        if (feature_kind == zima::document::FeatureKind::Extrusion) {
            initial.extrusion.profile_source = source;
        } else {
            initial.revolution.profile_source = source;
        }
    }
    if (part != nullptr &&
        (feature_kind == zima::document::FeatureKind::Extrusion ||
         feature_kind == zima::document::FeatureKind::Revolution)) {
        const auto profile_source = feature_kind ==
                zima::document::FeatureKind::Extrusion
            ? initial.extrusion.profile_source : initial.revolution.profile_source;
        const auto& sketch_id = feature_kind ==
                zima::document::FeatureKind::Extrusion
            ? initial.extrusion.sketch_id : initial.revolution.sketch_id;
        const auto source = std::find_if(part->session.document().sketches.begin(),
            part->session.document().sketches.end(), [&](const auto& sketch) {
                return sketch.id == sketch_id;
            });
        if (profile_source == zima::document::ProfileSource::Internal &&
            source != part->session.document().sketches.end()) {
            if (feature_kind == zima::document::FeatureKind::Extrusion) {
                initial.extrusion.profile_plane_offset = source->plane_offset;
            } else {
                initial.revolution.profile_plane_offset = source->plane_offset;
            }
        }
    }
    const bool allow_subtract = assembly_cut || (!part->session.document().history.empty() &&
        !(edit_mode && part->session.document().history.front().id == initial.id));
    const std::string owner_id = assembly_cut
        ? assembly->session.document().document_id
        : part->session.document().document_id;
    std::vector<PrimitivePropertiesDialog::AssemblyTarget> assembly_targets;
    std::vector<std::string> selected_targets;
    if (assembly_cut) {
        for (const auto& component : assembly->session.document().components) {
            if (component.suppressed || component.source_kind !=
                    zima::assembly::ComponentSourceKind::Part) continue;
            assembly_targets.emplace_back(component.occurrence_id, component.name);
            if (!edit_mode) selected_targets.push_back(component.occurrence_id);
        }
        if (edited_cut != nullptr) selected_targets = edited_cut->target_occurrence_ids;
    }
    auto* dialog = new PrimitivePropertiesDialog(
        initial, edit_mode, allow_subtract,
        [this, owner_id, edit_mode, assembly_cut, pending_owned_sketch, container_id,
         pending_profile_edit](
            zima::document::HistoryContainer committed,
            std::vector<std::string> target_occurrences) mutable {
            if (assembly_cut) {
                workspace_.regenerate_assembly_from_open_dependencies(owner_id);
                auto* target = workspace_.open_assembly(owner_id);
                if (target == nullptr) throw std::runtime_error(
                    "Assembly is no longer open");
                auto next = target->session.document();
                if (pending_owned_sketch && !edit_mode) {
                    auto owned = *pending_owned_sketch;
                    owned.owner_container_id = committed.id;
                    owned.plane_offset = committed.feature_kind ==
                            zima::document::FeatureKind::Extrusion
                        ? committed.extrusion.profile_plane_offset
                        : committed.revolution.profile_plane_offset;
                    next.sketches.push_back(std::move(owned));
                }
                if (committed.feature_kind ==
                        zima::document::FeatureKind::Revolution) {
                    const auto axis_sketch = std::find_if(
                        next.sketches.begin(), next.sketches.end(),
                        [&](const auto& sketch) {
                            return sketch.id == committed.revolution.sketch_id;
                        });
                    if (axis_sketch == next.sketches.end()) {
                        throw std::runtime_error("Skica rotace nebyla nalezena.");
                    }
                    committed.revolution.axis_segment_id =
                        revolution_axis_segment_id(*axis_sketch,
                            committed.revolution.axis_segment_id);
                }
                zima::assembly::AssemblyCut cut{
                    std::move(committed), std::move(target_occurrences)};
                if (edit_mode) {
                    auto* existing = next.find_cut(cut.definition.id);
                    if (existing == nullptr) throw std::runtime_error(
                        "Assembly cut no longer exists");
                    if (*existing == cut) return;
                    *existing = std::move(cut);
                } else {
                    next.cuts.push_back(std::move(cut));
                }
                calculate_assembly_cuts(next);
                target->session.commit(std::move(next));
                return;
            }
            auto* target_part = workspace_.open_part(owner_id);
            if (target_part == nullptr) throw std::runtime_error("Part is no longer open");
            auto next = target_part->session.document();
            if (pending_owned_sketch && !edit_mode) {
                next.sketches.push_back(*pending_owned_sketch);
            }
            if (committed.feature_kind ==
                    zima::document::FeatureKind::Revolution) {
                const auto axis_sketch = std::find_if(
                    next.sketches.begin(), next.sketches.end(),
                    [&](const auto& sketch) {
                        return sketch.id == committed.revolution.sketch_id;
                    });
                if (axis_sketch == next.sketches.end()) {
                    throw std::runtime_error("Skica rotace nebyla nalezena.");
                }
                committed.revolution.axis_segment_id =
                    revolution_axis_segment_id(*axis_sketch,
                        committed.revolution.axis_segment_id);
            }
            if (edit_mode) {
                auto* target = next.find_container(committed.id);
                if (target == nullptr) throw std::runtime_error("Container no longer exists");
                // OK on a profile feature is an explicit body-calculation
                // request even when its visible numeric fields are unchanged:
                // the owned Sketch geometry lives outside HistoryContainer
                // equality and may have changed from blank to a closed
                // rectangle. Skipping here left the valid cyan preview with
                // no calculated solid.
                if (*target == committed && !pending_profile_edit &&
                    committed.feature_kind !=
                        zima::document::FeatureKind::Extrusion &&
                    committed.feature_kind !=
                        zima::document::FeatureKind::Revolution) return;
                *target = std::move(committed);
            } else {
                next.insert_history_entry(
                    zima::document::PartHistoryKind::Feature, committed.id);
                next.history.push_back(std::move(committed));
            }
            const auto* committed_container = next.find_container(
                edit_mode ? container_id : next.history.back().id);
            if (committed_container != nullptr &&
                (committed_container->feature_kind ==
                        zima::document::FeatureKind::Extrusion ||
                 committed_container->feature_kind ==
                        zima::document::FeatureKind::Revolution)) {
                const bool extrusion = committed_container->feature_kind ==
                    zima::document::FeatureKind::Extrusion;
                const auto profile_source = extrusion
                    ? committed_container->extrusion.profile_source
                    : committed_container->revolution.profile_source;
                const auto& sketch_id = extrusion
                    ? committed_container->extrusion.sketch_id
                    : committed_container->revolution.sketch_id;
                if (profile_source == zima::document::ProfileSource::Internal) {
                    const auto owned = std::find_if(next.sketches.begin(),
                        next.sketches.end(), [&](const auto& sketch) {
                            return sketch.id == sketch_id;
                        });
                    if (owned == next.sketches.end()) {
                        throw std::runtime_error(
                            "Internal profile Sketch no longer exists");
                    }
                    owned->owner_container_id = committed_container->id;
                    // The first planar placement reference defines local
                    // FRONT (+Y). The owned profile must therefore use the
                    // local XZ plane, whose normal is parallel to that first
                    // reference, exactly like standalone Sketch Properties.
                    const auto first_reference = std::find_if(
                        committed_container->placement.references.begin(),
                        committed_container->placement.references.end(),
                        [](const auto& reference) {
                            return !reference.owner_id.empty();
                        });
                    if (first_reference !=
                            committed_container->placement.references.end() &&
                        first_reference->supports_offset) {
                        owned->plane = zima::sketcher::SketchPlane::XZ;
                    }
                    owned->plane_offset = extrusion
                        ? committed_container->extrusion.profile_plane_offset
                        : committed_container->revolution.profile_plane_offset;
                }
            }
            // Universal container placement: resolve any HistoryContainer
            // placement references against the geometry that existed before
            // this edit, exactly like ConstructionObject editing does, before
            // the kernel evaluates the (possibly placement-dependent) history.
            const auto calculated_before = target_part->session.calculated_boundaries();
            auto reference_geometry =
                construction_reference_source_geometry(calculated_before);
            append_reference_geometry(reference_geometry,
                next.origin_viewer_mesh().original_references);
            append_reference_geometry(reference_geometry,
                next.construction_viewer_mesh().original_references);
            next.resolve_constructions(reference_geometry);
            auto calculated = calculate_part(next);
            static_cast<void>(refresh_sketch_external_references(next, calculated));
            const bool completes_pending_profile = pending_profile_feature_ &&
                pending_profile_feature_->id == committed_container->id;
            target_part->session.commit(std::move(next), std::move(calculated));
            if (completes_pending_profile) {
                pending_profile_feature_.reset();
            }
        }, this, std::move(assembly_targets), std::move(selected_targets),
        assembly_cut);
    // Extrusion/Revolution OK always means validate + calculate. Their owned
    // Sketch is stored separately from the parameter object, so numeric
    // equality cannot prove that the operation is a no-op.
    dialog->set_commit_required(pending_profile_edit || profile_feature);
    std::function<void(const zima::document::HistoryContainer&)> placement_preview;
    const auto prepare_owned_profile_preview = [this](
            zima::document::PartDocument& preview_document,
            const zima::document::HistoryContainer& preview) {
        const bool extrusion = preview.feature_kind ==
            zima::document::FeatureKind::Extrusion;
        const auto profile_source = extrusion ? preview.extrusion.profile_source
                                              : preview.revolution.profile_source;
        if (profile_source != zima::document::ProfileSource::Internal) return;
        const auto& sketch_id = extrusion ? preview.extrusion.sketch_id
                                          : preview.revolution.sketch_id;
        const auto sketch = std::find_if(preview_document.sketches.begin(),
            preview_document.sketches.end(), [&](const auto& value) {
                return value.id == sketch_id;
            });
        if (sketch == preview_document.sketches.end()) return;
        sketch->owner_container_id = preview.id;
        const auto first_reference = std::find_if(
            preview.placement.references.begin(),
            preview.placement.references.end(), [](const auto& reference) {
                return !reference.owner_id.empty();
            });
        if (first_reference != preview.placement.references.end() &&
            first_reference->supports_offset) {
            sketch->plane = zima::sketcher::SketchPlane::XZ;
        }
        const double next_offset = extrusion
            ? preview.extrusion.profile_plane_offset
            : preview.revolution.profile_plane_offset;
        const double offset_delta = next_offset - sketch->plane_offset;
        sketch->resolved_origin.x += sketch->resolved_normal.x * offset_delta;
        sketch->resolved_origin.y += sketch->resolved_normal.y * offset_delta;
        sketch->resolved_origin.z += sketch->resolved_normal.z * offset_delta;
        sketch->plane_offset = next_offset;
        const auto owner = std::find_if(preview_document.history.begin(),
            preview_document.history.end(), [&](const auto& value) {
                return value.id == preview.id;
            });
        if (owner == preview_document.history.end()) {
            preview_document.history.push_back(preview);
        } else {
            *owner = preview;
        }
        preview_document.resolve_constructions(primitive_reference_geometry_);
    };
    const auto update_owned_profile_context_preview = [this](
            zima::document::PartDocument& preview_document,
            const zima::document::HistoryContainer& preview) {
        const bool extrusion = preview.feature_kind ==
            zima::document::FeatureKind::Extrusion;
        const auto& sketch_id = extrusion ? preview.extrusion.sketch_id
                                          : preview.revolution.sketch_id;
        const auto sketch = std::find_if(preview_document.sketches.begin(),
            preview_document.sketches.end(), [&](const auto& value) {
                return value.id == sketch_id;
            });
        if (sketch == preview_document.sketches.end()) return;
        auto plane = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Plane);
        plane.id = preview.id;
        plane.entity_id = preview.feature_id;
        plane.entity_parent_id = preview.id;
        plane.container_origin = preview.container_origin;
        plane.name = tr("Rovina").toStdString();
        plane.origin = {preview.placement.x, preview.placement.y,
                        preview.placement.z};
        plane.rotation = {preview.placement.rotation_x,
                          preview.placement.rotation_y,
                          preview.placement.rotation_z};
        plane.absolute_rotation = {preview.placement.absolute_rotation_x,
                                   preview.placement.absolute_rotation_y,
                                   preview.placement.absolute_rotation_z};
        plane.orientation_back = preview.placement.orientation_back;
        plane.orientation_quarter_turns =
            preview.placement.orientation_quarter_turns;
        plane.base_plane = sketch->plane == zima::sketcher::SketchPlane::XY
            ? zima::document::LocalDatumPlane::XY
            : sketch->plane == zima::sketcher::SketchPlane::XZ
                ? zima::document::LocalDatumPlane::XZ
                : zima::document::LocalDatumPlane::YZ;
        plane.offset = sketch->plane_offset;
        plane.reference_valid = true;
        preview_document.constructions.push_back(plane);
        preview_document.resolve_constructions(primitive_reference_geometry_);
        primitive_origin_preview_mesh_ =
            preview_document.construction_viewer_mesh(plane.id);
        viewer_->set_feature_preview_owners({plane.entity_id});
    };
    if (!assembly_cut && supports_placement_reference_picking(feature_kind)) {
        // Universal container placement PoC: position/orientation reference
        // picking, reusing the exact same DOF math ConstructionObject uses.
        // Assembly cuts have no owning Part document/origin of their own yet,
        // so placement reference picking is scoped to Part-hosted containers.
        const auto calculated = part->session.calculated_boundaries();
        auto reference_geometry = construction_reference_source_geometry(calculated);
        const auto& document = part->session.document();
        append_reference_geometry(reference_geometry,
            document.origin_viewer_mesh().original_references);
        append_reference_geometry(reference_geometry,
            document.construction_viewer_mesh().original_references);
        primitive_reference_geometry_ = reference_geometry;
        dialog->set_reference_request_callback(
            [this](std::size_t index) { start_primitive_reference_selection(index); });
        dialog->set_reference_highlights_changed_callback([this, dialog] {
            viewer_->set_constraint_reference_highlights(
                {}, highlighted_reference_edge_keys(*dialog));
        });
        primitive_reference_dialog_ = dialog;
        const bool fit_new_basic_preview = !edit_mode &&
            feature_kind != zima::document::FeatureKind::Extrusion &&
            feature_kind != zima::document::FeatureKind::Revolution &&
            feature_kind != zima::document::FeatureKind::Fillet &&
            feature_kind != zima::document::FeatureKind::Chamfer;
        placement_preview = [this, fit_new_basic_preview](
                const zima::document::HistoryContainer& preview) {
            if (primitive_reference_dialog_ == nullptr) return;
            auto resolved_preview = preview;
            auto& placement = resolved_preview.placement;
            zima::kernel::Vec3 base_rotation;
            bool orientation_from_reference = false;
            static_cast<void>(zima::document::resolve_placement(
                placement, primitive_reference_geometry_, &base_rotation,
                &orientation_from_reference));
            const auto constraint_state = zima::document::point_constraint_state(
                placement.references, primitive_reference_geometry_);
            primitive_translation_dof_ = constraint_state.remaining_dof;
            primitive_reference_dialog_->set_translation_constraint_state(
                constraint_state,
                {placement.x, placement.y, placement.z});
            primitive_reference_dialog_->set_remaining_rotation_dof(
                zima::document::orientation_constraint_remaining_dof(
                    placement.references, primitive_reference_geometry_, false));
            primitive_reference_dialog_->set_orientation_base_rotation(
                base_rotation, orientation_from_reference);
            zima::document::PartDocument preview_geometry;
            viewer_->set_transient_edges(
                preview_geometry.primitive_preview_edges(resolved_preview));
            // Basic solids are only an analytical cyan wire until OK. Their
            // local Container Origin is nevertheless real editing context:
            // standard-colour axes/planes plus a cyan origin point, all
            // resolved from the same live placement as the wire preview.
            zima::document::ConstructionObject origin_preview;
            origin_preview.id = resolved_preview.id;
            origin_preview.entity_id = resolved_preview.feature_id;
            origin_preview.container_origin = resolved_preview.container_origin;
            origin_preview.kind = zima::document::ConstructionKind::Axis;
            origin_preview.origin = {placement.x, placement.y, placement.z};
            origin_preview.rotation = {placement.rotation_x,
                placement.rotation_y, placement.rotation_z};
            origin_preview.reference_valid = false;
            preview_geometry.constructions.push_back(std::move(origin_preview));
            primitive_origin_preview_mesh_ =
                preview_geometry.construction_viewer_mesh(resolved_preview.id);
            parameter_dimension_preview_ = resolved_preview;
            construction_dimension_object_id_ = resolved_preview.id;
            viewer_->set_feature_preview_owners({
                resolved_preview.feature_id,
                resolved_preview.container_origin.id});
            preserve_view_on_refresh_ = true;
            refresh_scene();
            if (fit_new_basic_preview) viewer_->fit_all();
        };
        // Box/Cylinder/.../Wedge have no other preview needs: install the
        // placement-only preview directly. Extrusion/Revolution below merge
        // this with their own transient-edge preview into one callback.
        if (feature_kind != zima::document::FeatureKind::Extrusion &&
            feature_kind != zima::document::FeatureKind::Revolution) {
            dialog->set_preview_callback(placement_preview);
        }
    }
    if (feature_kind == zima::document::FeatureKind::Extrusion) {
        dialog->set_extrusion_target_request([this, dialog, assembly_cut] {
            extrusion_target_dialog_ = dialog;
            viewer_->set_selection_contract({zima::viewer::CandidateKind::Face});
            const auto* part =
                workspace_.open_part(workspace_.active_document_id());
            const auto expected_path = part == nullptr
                ? std::optional<std::string>{}
                : resolve_active_occurrence(part->session.document().document_id);
            viewer_->set_candidate_filter(
                [path = expected_path.value_or(std::string{}), assembly_cut](
                    const auto& candidate) {
                    bool owned_path = candidate.instance_path == path;
                    if (assembly_cut && !candidate.instance_path.empty()) {
                        const auto decoded = zima::assembly::InstancePath::decode(
                            candidate.instance_path);
                        owned_path = !decoded.occurrence_ids.empty();
                    }
                    return candidate.kind == zima::viewer::CandidateKind::Face &&
                        candidate.geometry ==
                            zima::viewer::CandidateGeometry::OriginalReference &&
                        owned_path;
                });
            state_->setText(tr("Vyberte cílovou rovinnou plochu ve view."));
        });
        dialog->set_preview_callback([this, owner_id, assembly_cut,
                                      pending_owned_sketch, placement_preview,
                                      prepare_owned_profile_preview,
                                      update_owned_profile_context_preview](
                                          const auto& preview) {
            if (placement_preview) placement_preview(preview);
            try {
                if (assembly_cut) {
                    const auto* owner = workspace_.open_assembly(owner_id);
                    if (owner == nullptr) return;
                    zima::document::PartDocument preview_document;
                    preview_document.sketches = owner->session.document().sketches;
                    if (pending_owned_sketch) {
                        preview_document.sketches.push_back(*pending_owned_sketch);
                    }
                    prepare_owned_profile_preview(preview_document, preview);
                    update_owned_profile_context_preview(preview_document, preview);
                    viewer_->set_transient_edges(
                        preview_document.extrusion_preview_edges(preview));
                } else {
                    const auto* owner = workspace_.open_part(owner_id);
                    if (owner == nullptr) return;
                    auto preview_document = owner->session.document();
                    if (pending_owned_sketch) {
                        preview_document.sketches.push_back(*pending_owned_sketch);
                    }
                    prepare_owned_profile_preview(preview_document, preview);
                    update_owned_profile_context_preview(preview_document, preview);
                    viewer_->set_transient_edges(
                        preview_document.extrusion_preview_edges(preview));
                }
                // placement_preview refreshed the scene before the owned
                // profile Plane had been rebuilt. Publish that new cyan
                // Plane/point now, preserving the user's camera; the
                // transient extrusion wire survives refresh_scene().
                preserve_view_on_refresh_ = true;
                refresh_scene();
                state_->setText(tr("Azurový drát zobrazuje náhled vytažení."));
            } catch (const std::exception& error) {
                viewer_->set_transient_edges({});
                state_->setText(QString::fromUtf8(error.what()));
            }
        });
    } else if (feature_kind == zima::document::FeatureKind::Revolution) {
        dialog->set_preview_callback([this, owner_id, assembly_cut,
                                      pending_owned_sketch, placement_preview,
                                      prepare_owned_profile_preview,
                                      update_owned_profile_context_preview](
                                          const auto& preview) {
            if (placement_preview) placement_preview(preview);
            try {
                if (assembly_cut) {
                    const auto* owner = workspace_.open_assembly(owner_id);
                    if (owner == nullptr) return;
                    zima::document::PartDocument preview_document;
                    preview_document.sketches = owner->session.document().sketches;
                    if (pending_owned_sketch) {
                        preview_document.sketches.push_back(*pending_owned_sketch);
                    }
                    prepare_owned_profile_preview(preview_document, preview);
                    update_owned_profile_context_preview(preview_document, preview);
                    viewer_->set_transient_edges(
                        preview_document.revolution_preview_edges(preview));
                } else {
                    const auto* owner = workspace_.open_part(owner_id);
                    if (owner == nullptr) return;
                    auto preview_document = owner->session.document();
                    if (pending_owned_sketch) {
                        preview_document.sketches.push_back(*pending_owned_sketch);
                    }
                    prepare_owned_profile_preview(preview_document, preview);
                    update_owned_profile_context_preview(preview_document, preview);
                    viewer_->set_transient_edges(
                        preview_document.revolution_preview_edges(preview));
                }
                // Same ordering contract as Extrusion: the live offset Plane
                // is calculated after the generic placement preview, so the
                // final scene refresh must happen here.
                preserve_view_on_refresh_ = true;
                refresh_scene();
                state_->setText(tr("Azurový drát zobrazuje náhled rotace."));
            } catch (const std::exception& error) {
                viewer_->set_transient_edges({});
                state_->setText(QString::fromUtf8(error.what()));
            }
        });
    }
    if (feature_kind == zima::document::FeatureKind::Extrusion ||
        feature_kind == zima::document::FeatureKind::Revolution) {
        dialog->set_edit_sketch_callback(
            [this, owner_id, edit_mode, pending_owned_sketch](
                zima::document::HistoryContainer pending_feature) {
            const bool extrusion = pending_feature.feature_kind ==
                zima::document::FeatureKind::Extrusion;
            const std::string sketch_id = extrusion
                ? pending_feature.extrusion.sketch_id
                : pending_feature.revolution.sketch_id;
            if (!edit_mode) {
                auto* target_part = workspace_.open_part(owner_id);
                if (target_part == nullptr || !pending_owned_sketch) return;
                auto next = target_part->session.document();
                auto draft_container =
                    zima::document::PartDocument::create_sketch_container();
                draft_container.id = pending_feature.id;
                draft_container.feature_id = pending_feature.feature_id;
                draft_container.container_origin = pending_feature.container_origin;
                draft_container.name = pending_feature.name;
                draft_container.placement = pending_feature.placement;
                auto draft_sketch = *pending_owned_sketch;
                draft_sketch.owner_container_id = draft_container.id;
                const auto first_reference = std::find_if(
                    pending_feature.placement.references.begin(),
                    pending_feature.placement.references.end(),
                    [](const auto& reference) {
                        return !reference.owner_id.empty();
                    });
                if (first_reference !=
                        pending_feature.placement.references.end() &&
                    first_reference->supports_offset) {
                    draft_sketch.plane = zima::sketcher::SketchPlane::XZ;
                }
                draft_sketch.plane_offset = extrusion
                    ? pending_feature.extrusion.profile_plane_offset
                    : pending_feature.revolution.profile_plane_offset;
                next.sketches.push_back(std::move(draft_sketch));
                next.insert_history_entry(
                    zima::document::PartHistoryKind::Feature,
                    draft_container.id);
                next.history.push_back(std::move(draft_container));
                // Resolve the owned Sketch frame before activating Sketcher.
                // Merely changing Sketch::plane to XZ is insufficient: the
                // camera consumes resolved_normal/resolved_origin, which
                // otherwise still contain create_default()'s stale XY frame
                // and align to the wrong plane after the first reference.
                const auto calculated = target_part->session.calculated_boundaries();
                auto reference_geometry =
                    construction_reference_source_geometry(calculated);
                append_reference_geometry(reference_geometry,
                    next.origin_viewer_mesh().original_references);
                append_reference_geometry(reference_geometry,
                    next.construction_viewer_mesh().original_references);
                next.resolve_constructions(reference_geometry);
                target_part->session.commit(std::move(next),
                    calculated);
                pending_profile_feature_ = std::move(pending_feature);
            }
            active_sketch_id_ = sketch_id;
            selected_sketch_id_ = active_sketch_id_;
            refresh_scene();
            // Entering the owned profile from Extrusion/Revolution follows
            // the same camera contract as the standalone SKETCH button. Run
            // it after the feature dialog has finished closing; its teardown
            // refreshes the scene once more and used to leave the camera in
            // the previous 3D view instead of looking along the sketch-plane
            // normal.
            QTimer::singleShot(0, this, [this, sketch_id] {
                if (active_sketch_id_ == sketch_id) align_active_sketch_view();
            });
        });
    }
    properties_dialog_ = dialog;
    const std::string dialog_container_id = initial.id;
    if (edit_mode && !assembly_cut &&
        (feature_kind == zima::document::FeatureKind::Fillet ||
         feature_kind == zima::document::FeatureKind::Chamfer)) {
        edge_treatment_selection_ = feature_kind;
        edge_treatment_dialog_ = dialog;
        pending_edge_treatment_edges_ = initial.edge_treatment.edges;
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
    }
    if (pending_profile_edit) {
        connect(dialog, &QDialog::accepted, this, [this, dialog_container_id] {
            if (pending_profile_feature_ &&
                pending_profile_feature_->id == dialog_container_id) {
                pending_profile_feature_.reset();
            }
        });
    }
    connect(dialog, &QDialog::rejected, this, [this, dialog_container_id] {
        if (!pending_profile_feature_ ||
            pending_profile_feature_->id != dialog_container_id) return;
        if (auto* target_part = workspace_.open_part(
                workspace_.active_document_id())) {
            auto next = target_part->session.document();
            std::erase_if(next.sketches, [&](const auto& sketch) {
                return sketch.owner_container_id == dialog_container_id;
            });
            std::erase_if(next.history, [&](const auto& container) {
                return container.id == dialog_container_id;
            });
            std::erase_if(next.history_order, [&](const auto& entry) {
                return entry.id == dialog_container_id;
            });
            target_part->session.commit(std::move(next),
                target_part->session.calculated_boundaries());
        }
        pending_profile_feature_.reset();
    });
    if (assembly_cut_index) {
        assembly_cut_rollback_ = AssemblyCutRollbackContext{
            assembly->session.document().document_id, container_id,
            *assembly_cut_index, edited_cut->input_component_bodies};
        refresh_scene();
    }
    if (rollback_boundary) {
        part_rollback_ = PartRollbackContext{
            part->session.document().document_id, *rollback_occurrence,
            rollback_boundary->history_index, rollback_boundary->input_body};
        refresh_scene();
    }
    connect(dialog, &QObject::destroyed, this, [this, dialog_container_id] {
        properties_dialog_ = nullptr;
        edge_treatment_dialog_ = nullptr;
        edge_treatment_selection_.reset();
        pending_edge_treatment_edges_.clear();
        extrusion_target_dialog_ = nullptr;
        primitive_reference_dialog_ = nullptr;
        pending_primitive_reference_index_.reset();
        primitive_reference_geometry_ = {};
        primitive_origin_preview_mesh_.reset();
        parameter_dimension_preview_.reset();
        primitive_translation_dof_ = 3;
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_transient_edges({});
        viewer_->set_feature_preview_owners({});
        viewer_->set_candidate_filter({});
        viewer_->set_constraint_reference_highlights({}, {});
        part_rollback_.reset();
        assembly_cut_rollback_.reset();
        refresh_tabs();
        // See the identical guard in show_construction_properties()'s
        // destroyed handler: closing this dialog must not re-fit/zoom the
        // camera to the just-committed (or reverted) feature geometry.
        preserve_view_on_refresh_ = true;
        refresh_scene();
    });
    dialog->show();
    // Every placement-capable history container starts with its first
    // reference row armed, just like Point/Axis/Plane. This makes clicking
    // the whole document Origin in the Tree an immediate bulk-fill action;
    // the user must not first click an otherwise empty table cell.
    if (primitive_reference_dialog_ == dialog) {
        start_primitive_reference_selection(0);
    }
}

void AssemblyWorkspaceWindow::show_construction_properties(
    zima::document::ConstructionKind kind, const std::string& object_id) {
    if (properties_dialog_ != nullptr) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    if (part == nullptr && assembly == nullptr) return;
    const auto* edited = object_id.empty() ? nullptr : part != nullptr
        ? part->session.document().find_construction(object_id)
        : assembly->session.document().find_construction(object_id);
    if (!object_id.empty() && (edited == nullptr || edited->kind != kind)) return;
    const bool edit_mode = edited != nullptr;
    const auto initial = edit_mode ? *edited
        : zima::document::PartDocument::create_construction(kind);
    const std::string document_id = workspace_.active_document_id();
    auto* dialog = new ConstructionPropertiesDialog(
        initial, edit_mode,
        [this, document_id, edit_mode](zima::document::ConstructionObject committed) {
            const auto committed_id = committed.id;
            if (auto* target_part = workspace_.open_part(document_id)) {
                auto next = target_part->session.document();
                if (edit_mode) {
                    auto* target = next.find_construction(committed.id);
                    if (target == nullptr) {
                        throw std::runtime_error("Construction object no longer exists");
                    }
                    *target = std::move(committed);
                } else {
                    next.insert_history_entry(
                        zima::document::PartHistoryKind::Construction,
                        committed.id);
                    next.constructions.push_back(std::move(committed));
                }
                auto calculated = target_part->session.calculated_boundaries();
                next.resolve_constructions(
                    construction_reference_source_geometry(calculated));
                if (const auto* resolved = next.find_construction(committed_id);
                    resolved == nullptr || !resolved->reference_valid) {
                    throw std::runtime_error(
                        "Construction definition has a missing or cyclic reference");
                }
                static_cast<void>(refresh_sketch_external_references(next, calculated));
                target_part->session.commit(std::move(next), std::move(calculated));
                return;
            }
            auto* target_assembly = workspace_.open_assembly(document_id);
            if (target_assembly == nullptr) {
                throw std::runtime_error("Assembly is no longer open");
            }
            auto next = target_assembly->session.document();
            if (edit_mode) {
                auto* target = next.find_construction(committed.id);
                if (target == nullptr) {
                    throw std::runtime_error("Construction object no longer exists");
                }
                *target = std::move(committed);
            } else {
                next.constructions.push_back(std::move(committed));
            }
            next.resolve_constructions();
            if (const auto* resolved = next.find_construction(committed_id);
                resolved == nullptr || !resolved->reference_valid) {
                throw std::runtime_error(
                    "Construction definition has a missing or cyclic reference");
            }
            target_assembly->session.commit(std::move(next));
        }, this);
    dialog->set_reference_request_callback(
        [this](std::size_t index) { start_construction_reference_selection(index); });
    dialog->set_reference_highlights_changed_callback([this, dialog] {
        viewer_->set_constraint_reference_highlights(
            {}, highlighted_reference_edge_keys(*dialog));
    });
    construction_reference_dialog_ = dialog;
    dialog->set_preview_callback(
        [this, document_id, edit_mode](zima::document::ConstructionObject preview) {
            const std::set<std::string> preview_owners{
                preview.entity_id, preview.container_origin.id};
            zima::kernel::ViewerMesh mesh;
            zima::kernel::ViewerReferenceGeometry reference_geometry;
            zima::kernel::Vec3 resolved_origin = preview.origin;
            zima::kernel::Vec3 resolved_rotation = preview.rotation_base;
            bool resolved_orientation_inherited =
                preview.orientation_inherited_from_reference;
            if (const auto* source = workspace_.open_part(document_id)) {
                auto next = source->session.document();
                if (edit_mode) {
                    if (auto* target = next.find_construction(preview.id)) {
                        *target = preview;
                    }
                } else {
                    next.constructions.push_back(preview);
                }
                const auto& calculated = source->session.calculated_boundaries();
                reference_geometry =
                    construction_reference_source_geometry(calculated);
                next.resolve_constructions(reference_geometry);
                if (const auto* resolved = next.find_construction(preview.id)) {
                    resolved_origin = resolved->origin;
                    resolved_rotation = resolved->rotation_base;
                    resolved_orientation_inherited =
                        resolved->orientation_inherited_from_reference;
                    construction_parameter_preview_ = *resolved;
                }
                {
                    // Origin's own on-screen size (document and container
                    // alike) is now a fixed constant independent of the
                    // scene/model size -- see kDocumentOriginAxisLength's
                    // comment in part_document.cpp. `construction_viewer_mesh`
                    // still accepts a scene_size parameter for source
                    // compatibility, but it is unused; pass 0.0.
                    mesh = next.construction_viewer_mesh(preview.id);
                }
                append_reference_geometry(reference_geometry,
                    next.origin_viewer_mesh().original_references);
                append_reference_geometry(reference_geometry,
                    next.construction_viewer_mesh().original_references);
            } else if (const auto* source = workspace_.open_assembly(document_id)) {
                auto next = source->session.document();
                if (edit_mode) {
                    if (auto* target = next.find_construction(preview.id)) {
                        *target = preview;
                    }
                } else {
                    next.constructions.push_back(preview);
                }
                next.resolve_constructions();
                if (const auto* resolved = next.find_construction(preview.id)) {
                    resolved_origin = resolved->origin;
                    resolved_rotation = resolved->rotation_base;
                    resolved_orientation_inherited =
                        resolved->orientation_inherited_from_reference;
                    construction_parameter_preview_ = *resolved;
                }
                mesh = next.construction_viewer_mesh(preview.id);
                reference_geometry = next.build_scene().original_references;
            }
            construction_reference_geometry_ = reference_geometry;
            if (construction_reference_dialog_ != nullptr) {
                const auto constraint_state =
                    zima::document::point_constraint_state(
                        preview.references, reference_geometry);
                construction_translation_dof_ = constraint_state.remaining_dof;
                construction_reference_dialog_->set_translation_constraint_state(
                    constraint_state, resolved_origin);
                construction_rotation_dof_ =
                    zima::document::orientation_constraint_remaining_dof(
                        preview.references, reference_geometry, true);
                if (construction_shortcut_satisfied(
                        preview.kind, preview.references, reference_geometry)) {
                    construction_rotation_dof_ = 0;
                }
                construction_reference_dialog_->set_remaining_rotation_dof(
                    construction_rotation_dof_);
                const bool has_orientation_references = std::any_of(
                    preview.references.begin(), preview.references.end(),
                    [](const auto& reference) {
                        return reference.orientation_drives_rotation;
                    }) || resolved_orientation_inherited;
                construction_reference_dialog_->set_orientation_base_rotation(
                    resolved_rotation, has_orientation_references);
                construction_reference_dialog_->set_orientation_inherited_from_reference(
                    resolved_orientation_inherited);
            }
            construction_preview_mesh_ = std::move(mesh);
            construction_dimension_object_id_ = preview.id;
            viewer_->set_feature_preview_owners(preview_owners);
            viewer_->set_transient_edges({});
            preserve_view_on_refresh_ = true;
            refresh_scene();
        });
    viewer_->set_editing_origin_visible(true);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        construction_reference_dialog_ = nullptr;
        construction_preview_mesh_.reset();
        construction_parameter_preview_.reset();
        construction_reference_geometry_ = {};
        pending_construction_reference_index_.reset();
        tree_->setProperty("commandSelectionActive", false);
        construction_translation_dof_ = 3;
        construction_rotation_dof_ = 3;
        viewer_->set_transient_edges({});
        viewer_->set_feature_preview_owners({});
        viewer_->set_editing_origin_visible(false);
        // A construction command installs its own persisted-reference filter.
        // Returning to ordinary Part selection must also remove that filter;
        // restoring only CandidateKind::Container leaves every basic history
        // container unpickable in the View.
        viewer_->set_candidate_filter({});
        viewer_->clear_selection();
        viewer_->set_constraint_reference_highlights({}, {});
        refresh_tabs();
        // Closing the dialog (OK or Cancel) must not re-fit/zoom the camera
        // to the just-committed (or reverted) construction geometry -- an
        // Axis/Plane's real display-size extent is typically much larger
        // than the small Origin preview shown while the dialog was open,
        // and an un-preserved refresh_scene() here would re-fit the camera
        // to it, making the Origin (and everything else) appear to shrink
        // the instant the dialog closes. See the identical guard right
        // before every other refresh_scene() call in this dialog's
        // callbacks above.
        preserve_view_on_refresh_ = true;
        refresh_scene();
    });
    dialog->show();
    // Construction commands own their viewer selection contract.  Match the
    // Python workflow: opening Point/Axis/Plane immediately arms the first
    // reference row; the user must not have to discover and press a separate
    // "pick" button before orange hover and RMB cycling become available.
    start_construction_reference_selection(0);
}

void AssemblyWorkspaceWindow::start_construction_reference_selection(
    std::size_t index) {
    if (construction_reference_dialog_ == nullptr) return;
    const bool orientation_reference = index >= 3;
    const auto baseline_references =
        construction_reference_dialog_->references_without(index);
    // A position row (index < 3) is only truly "full" once both translation
    // AND rotation are resolved: a lone point already zeroes translation,
    // but the 2nd/3rd point still carries the direction/normal information
    // needed by the "2 points define an axis"/"3 points define a plane"
    // shortcut, so it must stay enterable until rotation is resolved too.
    // An orientation row (index >= 3) must stay armable even when rotation
    // is ALREADY fully resolved by an automatically-derived FRONT role on
    // position row 0/1 (Plane's "1st reference decides orientation"
    // contract) -- otherwise clicking an empty, still-clickable FRONT/TOP
    // row in "Orientace kontejneru" would silently do nothing, matching
    // Python's `_container_orientation_references` table, which is always
    // independently pickable regardless of what row 0 already resolved.
    const int baseline_dof = orientation_reference
        ? 1
        : zima::document::point_constraint_remaining_dof(
            baseline_references, construction_reference_geometry_) +
          zima::document::orientation_constraint_remaining_dof(
              baseline_references, construction_reference_geometry_, true);
    // A completed placement has no next input. Existing rows can still be
    // armed because replacing one reference is evaluated with that row removed.
    const bool shortcut_satisfied = !orientation_reference &&
        construction_shortcut_satisfied(
            construction_reference_dialog_->construction_kind(),
            baseline_references, construction_reference_geometry_);
    if (baseline_dof == 0 || shortcut_satisfied) {
        pending_construction_reference_index_.reset();
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_candidate_filter({});
        viewer_->clear_selection();
        state_->setText(tr("Konstrukce je již plně určená."));
        return;
    }
    pending_construction_reference_index_ = index;
    tree_->setProperty("commandSelectionActive", true);
    // Every container consumes the same complete persisted candidate universe.
    // The active container contract interprets a candidate after hover/RMB
    // cycling instead of prematurely hiding valid placement references here.
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Vertex,
        zima::viewer::CandidateKind::Axis,
        zima::viewer::CandidateKind::Plane,
        zima::viewer::CandidateKind::Face,
        zima::viewer::CandidateKind::Dimension});
    const auto prefix = active_occurrence_path_.empty()
        ? zima::assembly::InstancePath{}
        : zima::assembly::InstancePath::decode(active_occurrence_path_);
    const bool active_part =
        workspace_.open_part(workspace_.active_document_id()) != nullptr;
    std::set<std::string> unavailable_construction_owners;
    const auto collect_unavailable = [&](const auto& document) {
        bool at_or_after_edited = false;
        for (const auto& object : document.constructions) {
            if (object.id == construction_reference_dialog_->construction_id())
                at_or_after_edited = true;
            if (!at_or_after_edited) continue;
            unavailable_construction_owners.insert(object.id);
            unavailable_construction_owners.insert(object.entity_id);
            unavailable_construction_owners.insert(object.container_origin.id);
        }
    };
    if (const auto* part = workspace_.open_part(workspace_.active_document_id()))
        collect_unavailable(part->session.document());
    else if (const auto* assembly =
            workspace_.open_assembly(workspace_.active_document_id()))
        collect_unavailable(assembly->session.document());
    viewer_->set_candidate_filter([this, prefix, active_part, index,
            orientation_reference, baseline_references, baseline_dof,
            unavailable_construction_owners](const auto& candidate) {
        if (construction_reference_dialog_ == nullptr) return false;
        if (!construction_reference_candidate_passes_static_filters(candidate,
                orientation_reference,
                construction_reference_dialog_->owns_reference_owner(
                    candidate.owner_id),
                unavailable_construction_owners.contains(candidate.owner_id))) {
            return false;
        }
        auto local_path = candidate.instance_path;
        try {
            auto path = zima::assembly::InstancePath::decode(
                candidate.instance_path);
            const bool allowed_path = active_part ? path == prefix : path == prefix ||
                (path.occurrence_ids.size() == prefix.occurrence_ids.size() + 1 &&
                 std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(),
                     path.occurrence_ids.begin()));
            if (!allowed_path) return false;
            if (!prefix.occurrence_ids.empty()) {
                path.occurrence_ids.erase(path.occurrence_ids.begin(),
                    path.occurrence_ids.begin() + static_cast<std::ptrdiff_t>(
                        prefix.occurrence_ids.size()));
                local_path = path.encoded();
            }
        } catch (const std::invalid_argument&) {
            return false;
        }
        auto candidate_reference = zima::document::ConstructionReference{
            std::move(local_path), candidate.owner_id, candidate.semantic_key, 0.0,
            candidate_supports_offset(candidate)};
        if (orientation_reference) {
            candidate_reference.orientation_drives_rotation = true;
            candidate_reference.orientation_role = index == 3 ? "front" : "top";
        }
        auto proposed = baseline_references;
        proposed.push_back(std::move(candidate_reference));
        const int proposed_dof = orientation_reference
            ? zima::document::orientation_constraint_remaining_dof(
                proposed, construction_reference_geometry_, true)
            : zima::document::point_constraint_remaining_dof(
                proposed, construction_reference_geometry_);
        return proposed_dof < baseline_dof;
    });
    state_->setText(tr("Vyberte stabilní geometrii pro definici konstrukčního objektu."));
}

void AssemblyWorkspaceWindow::accept_construction_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    if (construction_reference_dialog_ == nullptr ||
        !pending_construction_reference_index_) return;
    const std::size_t selected_index = *pending_construction_reference_index_;
    const bool orientation_reference = selected_index >= 3;
    auto local_path = candidate.instance_path;
    if (!active_occurrence_path_.empty()) {
        auto path = zima::assembly::InstancePath::decode(candidate.instance_path);
        const auto prefix =
            zima::assembly::InstancePath::decode(active_occurrence_path_);
        if (path.occurrence_ids.size() < prefix.occurrence_ids.size() ||
            !std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(),
                path.occurrence_ids.begin())) return;
        path.occurrence_ids.erase(path.occurrence_ids.begin(),
            path.occurrence_ids.begin() +
                static_cast<std::ptrdiff_t>(prefix.occurrence_ids.size()));
        local_path = path.encoded();
    }
    const auto references_current_or_later_construction = [&](const auto& document) {
        bool at_or_after_edited = false;
        for (const auto& object : document.constructions) {
            if (object.id == construction_reference_dialog_->construction_id())
                at_or_after_edited = true;
            if (at_or_after_edited &&
                (candidate.owner_id == object.id ||
                 candidate.owner_id == object.entity_id ||
                 candidate.owner_id == object.container_origin.id)) return true;
        }
        return false;
    };
    const auto active_document_id = workspace_.active_document_id();
    if (const auto* part = workspace_.open_part(active_document_id)) {
        if (references_current_or_later_construction(part->session.document())) return;
    } else if (const auto* assembly = workspace_.open_assembly(active_document_id)) {
        if (references_current_or_later_construction(
                assembly->session.document())) return;
    }
    if (!construction_reference_candidate_passes_static_filters(candidate,
            orientation_reference,
            construction_reference_dialog_->owns_reference_owner(candidate.owner_id),
            false)) {
        return;
    }
    // Every container kind (Point, Axis, Plane) now shares one placement
    // model, matching Placement/resolve_placement() used by primitives and
    // Extrusion/Revolution: placement references are solved generically
    // (any combination/count of point/axis/edge/plane position rows), and
    // any reference marked orientation_drives_rotation (front/top role)
    // additionally composes the object's orientation. There is no more
    // per-kind "named" definition (TwoPointAxis/AxisReference/
    // ThreePointPlane/PlaneReference) that requires an exact reference
    // count/type -- resolve_construction() detects the classic "2 points
    // define an axis"/"3 points define a plane" shortcuts on its own when
    // no orientation-driving reference is present, and otherwise falls back
    // to the generic position-only solve for any other combination.
    const auto kind = construction_reference_dialog_->construction_kind();
    auto baseline_references =
        construction_reference_dialog_->references_without(selected_index);
    const auto definition = zima::document::ConstructionDefinition::PointReference;
    const int baseline_dof = orientation_reference
        ? zima::document::orientation_constraint_remaining_dof(
            baseline_references, construction_reference_geometry_, true)
        : zima::document::point_constraint_remaining_dof(
            baseline_references, construction_reference_geometry_);
    auto proposed_reference = zima::document::ConstructionReference{
        local_path, candidate.owner_id, candidate.semantic_key, 0.0,
        candidate_supports_offset(candidate)};
    if (orientation_reference) {
        proposed_reference.orientation_drives_rotation = true;
        proposed_reference.orientation_role = selected_index == 3 ? "front" : "top";
    }
    baseline_references.push_back(proposed_reference);
    const int proposed_dof = orientation_reference
        ? zima::document::orientation_constraint_remaining_dof(
            baseline_references, construction_reference_geometry_, true)
        : zima::document::point_constraint_remaining_dof(
            baseline_references, construction_reference_geometry_);
    // A 2nd (Axis) or 2nd/3rd (Plane) plain point reference carries real
    // direction/normal information via the classic history-order shortcut
    // (1st point = origin, 2nd = direction, 3rd = plane-completing point) --
    // point_constraint_remaining_dof() cannot see that, since a single point
    // already zeroes translation and a bare vertex never drives rotation.
    // Such a reference must therefore be accepted even when it does not
    // shrink the generic DOF count, as long as the container still has room
    // for it (2 points for an Axis, 3 for a Plane).
    // baseline_references already includes the just-pushed proposed_reference
    // at this point, so the total-count checks below compare against the
    // post-push size (i.e. <= the shortcut's point quota, not < it).
    const bool is_shortcut_point = !orientation_reference &&
        candidate.kind == zima::viewer::CandidateKind::Vertex &&
        ((kind == zima::document::ConstructionKind::Axis &&
             baseline_references.size() <= 2) ||
         (kind == zima::document::ConstructionKind::Plane &&
             baseline_references.size() <= 3));
    // An orientation row (index >= 3) is always accepted as an explicit
    // user override, even when FRONT/TOP is already fully resolved by an
    // automatically-derived role on a position row -- exactly like the
    // `baseline_dof` override in start_construction_reference_selection()
    // above that arms picking for such a row in the first place. Rejecting
    // the pick here via the generic "no independent constraint" DOF check
    // would silently ignore every manual FRONT/TOP selection the user makes
    // once row 0 already supplied a default, matching Python's always-
    // overridable `_container_orientation_references` contract.
    if (!orientation_reference && !is_shortcut_point && proposed_dof >= baseline_dof) {
        state_->setText(tr("Tato reference nepřidává žádnou nezávislou vazbu."));
        viewer_->clear_selection();
        return;
    }
    QString reference_label;
    const auto label_from_constructions = [&](const auto& document) {
        const auto found = std::find_if(document.constructions.begin(),
            document.constructions.end(), [&](const auto& object) {
                return candidate.owner_id == object.id ||
                    candidate.owner_id == object.entity_id ||
                    candidate.owner_id == object.container_origin.id;
            });
        if (found != document.constructions.end()) {
            reference_label = QString::fromStdString(found->name);
        }
    };
    if (const auto* part = workspace_.open_part(active_document_id)) {
        label_from_constructions(part->session.document());
        if (candidate.owner_id == part->session.document().document_id + ":origin") {
            reference_label = tr("Počátek dílu");
        }
    } else if (const auto* assembly = workspace_.open_assembly(active_document_id)) {
        label_from_constructions(assembly->session.document());
        if (candidate.owner_id == assembly->session.document().document_id + ":origin") {
            reference_label = tr("Počátek sestavy");
        }
    }
    const auto semantic_label = candidate.semantic_key.starts_with("origin:axis:")
        ? tr("Osa %1").arg(QString::fromStdString(
            candidate.semantic_key.substr(12)).toUpper())
        : candidate.semantic_key.starts_with("origin:plane:")
            ? tr("Rovina %1").arg(QString::fromStdString(
                candidate.semantic_key.substr(13)).toUpper())
        : candidate.kind == zima::viewer::CandidateKind::Vertex ? tr("Bod")
        : candidate.kind == zima::viewer::CandidateKind::Axis ? tr("Osa")
        : candidate.kind == zima::viewer::CandidateKind::Plane ? tr("Rovina")
        : tr("Plocha");
    reference_label = reference_label.isEmpty()
        ? semantic_label : reference_label + QStringLiteral(" — ") + semantic_label;
    auto committed_reference = zima::document::ConstructionReference{
        std::move(local_path), candidate.owner_id, candidate.semantic_key, 0.0,
        candidate_supports_offset(candidate)};
    // Every construction dialog now owns the same independent orientation
    // table. Planar position references are mirrored into its first two
    // slots by ContainerPlacementSection; position rows themselves never
    // acquire a second rotational meaning.
    if (!construction_reference_dialog_->set_reference(
        selected_index, committed_reference, reference_label, definition)) {
        state_->setText(tr("Stejná reference už je pro tento objekt zadaná."));
        viewer_->clear_selection();
        return;
    }
    pending_construction_reference_index_.reset();
    viewer_->clear_selection();
    // set_reference() above already triggered the dialog's preview callback
    // synchronously (via notify_changed()), which itself already called
    // refresh_scene() with preserve_view_on_refresh_ temporarily true --
    // but that call resets the flag back to false once done. Without
    // re-arming it here, this second, redundant refresh_scene() call runs
    // with fit_view enabled and re-fits/zooms the camera to the newly
    // resolved construction geometry (e.g. an Axis/Plane's real display-size
    // extent, which is typically much larger than the small Origin preview
    // shown before the reference was picked). That camera re-fit is what
    // makes the Origin appear to shrink the instant the first reference is
    // entered, even though the Origin's own world-space size never changes.
    preserve_view_on_refresh_ = true;
    refresh_scene();
    // Keep offering the next row for as long as ANY DOF (position or
    // rotation) remains open, matching the "1st point = origin, 2nd = axis
    // direction, 3rd = plane-completing point" history-order contract: a
    // single point already fully fixes translation, but a 2nd/3rd point
    // still carries orientation information and must remain enterable.
    if ((construction_translation_dof_ + construction_rotation_dof_) > 0 &&
        selected_index + 1 < 3) {
        start_construction_reference_selection(selected_index + 1);
    } else {
        // The accepted reference completed the placement (or exhausted the
        // three placement rows). Do not leave the previous row's candidate
        // predicate alive: it would keep painting orange offers although no
        // reference input is armed. Clicking an existing row installs a fresh
        // replacement contract with that row removed from the rank baseline.
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_selection_contract({});
        viewer_->set_candidate_filter([](const auto&) { return false; });
        viewer_->clear_selection();
    }
}

void AssemblyWorkspaceWindow::start_primitive_reference_selection(
    std::size_t index) {
    if (primitive_reference_dialog_ == nullptr) return;
    const bool orientation_reference = index >= 3;
    const auto baseline_references =
        primitive_reference_dialog_->references_without(index);
    const int baseline_dof = orientation_reference
        ? zima::document::orientation_constraint_remaining_dof(
            baseline_references, primitive_reference_geometry_, false)
        : zima::document::point_constraint_remaining_dof(
            baseline_references, primitive_reference_geometry_);
    if (baseline_dof == 0) {
        pending_primitive_reference_index_.reset();
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_candidate_filter({});
        viewer_->clear_selection();
        state_->setText(tr("Umístění kontejneru je již plně určené."));
        return;
    }
    pending_primitive_reference_index_ = index;
    tree_->setProperty("commandSelectionActive", true);
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Vertex,
        zima::viewer::CandidateKind::Axis,
        zima::viewer::CandidateKind::Plane,
        zima::viewer::CandidateKind::Face});
    const auto prefix = active_occurrence_path_.empty()
        ? zima::assembly::InstancePath{}
        : zima::assembly::InstancePath::decode(active_occurrence_path_);
    const bool active_part =
        workspace_.open_part(workspace_.active_document_id()) != nullptr;
    viewer_->set_candidate_filter([this, prefix, active_part, index,
            orientation_reference, baseline_references, baseline_dof](
                const auto& candidate) {
        if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
            candidate.owner_id == construction_dimension_object_id_ &&
            candidate.semantic_key.starts_with("parameter:")) return true;
        if (!placement_reference_candidate_has_stable_geometry(candidate)) return false;
        if (primitive_reference_dialog_ == nullptr ||
            primitive_reference_dialog_->owns_reference_owner(candidate.owner_id))
            return false;
        auto local_path = candidate.instance_path;
        try {
            auto path = zima::assembly::InstancePath::decode(
                candidate.instance_path);
            const bool allowed_path = active_part ? path == prefix : path == prefix ||
                (path.occurrence_ids.size() == prefix.occurrence_ids.size() + 1 &&
                 std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(),
                     path.occurrence_ids.begin()));
            if (!allowed_path) return false;
            if (!prefix.occurrence_ids.empty()) {
                path.occurrence_ids.erase(path.occurrence_ids.begin(),
                    path.occurrence_ids.begin() + static_cast<std::ptrdiff_t>(
                        prefix.occurrence_ids.size()));
                local_path = path.encoded();
            }
        } catch (const std::invalid_argument&) {
            return false;
        }
        auto candidate_reference = zima::document::ConstructionReference{
            std::move(local_path), candidate.owner_id, candidate.semantic_key, 0.0,
            candidate_supports_offset(candidate)};
        if (orientation_reference) {
            candidate_reference.orientation_drives_rotation = true;
            candidate_reference.orientation_role = index == 3 ? "front" : "top";
        }
        auto proposed = baseline_references;
        proposed.push_back(std::move(candidate_reference));
        const int proposed_dof = orientation_reference
            ? zima::document::orientation_constraint_remaining_dof(
                proposed, primitive_reference_geometry_, false)
            : zima::document::point_constraint_remaining_dof(
                proposed, primitive_reference_geometry_);
        return proposed_dof < baseline_dof;
    });
    state_->setText(tr("Vyberte stabilní geometrii pro umístění kontejneru."));
}

void AssemblyWorkspaceWindow::start_component_placement_reference_selection(
    std::size_t index, bool component_side) {
    if (component_placement_dialog_ == nullptr) return;
    pending_component_placement_index_ = index;
    pending_component_placement_component_side_ = component_side;
    tree_->setProperty("commandSelectionActive", true);
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Vertex,
        zima::viewer::CandidateKind::Axis,
        zima::viewer::CandidateKind::Face});
    const auto prefix = active_occurrence_path_.empty()
        ? zima::assembly::InstancePath{}
        : zima::assembly::InstancePath::decode(active_occurrence_path_);
    const std::string occurrence_id = component_placement_occurrence_id_;
    viewer_->set_candidate_filter(
        [prefix, component_side, occurrence_id](const auto& candidate) {
        if (candidate.geometry !=
                zima::viewer::CandidateGeometry::OriginalReference ||
            candidate.instance_path.empty() || candidate.owner_id.empty() ||
            candidate.semantic_key.empty()) return false;
        try {
            const auto path = zima::assembly::InstancePath::decode(
                candidate.instance_path);
            if (path.occurrence_ids.size() != prefix.occurrence_ids.size() + 1 ||
                !std::equal(prefix.occurrence_ids.begin(),
                    prefix.occurrence_ids.end(), path.occurrence_ids.begin())) {
                return false;
            }
            // Component-side picks are restricted to geometry that belongs
            // to the very occurrence being edited (this dialog's own
            // component); target-side picks accept geometry on any OTHER
            // direct sibling occurrence, matching Python's "this díl" vs.
            // "cíl" (any other already-placed component/assembly datum)
            // column semantics.
            const bool is_own_occurrence = path.occurrence_ids.back() == occurrence_id;
            return component_side ? is_own_occurrence : !is_own_occurrence;
        } catch (const std::invalid_argument&) {
            return false;
        }
    });
    state_->setText(component_side
        ? tr("Vyberte referenci na tomto dílu.")
        : tr("Vyberte cílovou referenci."));
}

void AssemblyWorkspaceWindow::accept_component_placement_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    if (component_placement_dialog_ == nullptr ||
        !pending_component_placement_index_ || candidate.owner_id.empty() ||
        candidate.semantic_key.empty() ||
        candidate.geometry != zima::viewer::CandidateGeometry::OriginalReference)
        return;
    const auto kind = candidate.kind == zima::viewer::CandidateKind::Face
        ? zima::assembly::MateReferenceKind::Face
        : candidate.kind == zima::viewer::CandidateKind::Axis
            ? zima::assembly::MateReferenceKind::Axis
            : candidate.kind == zima::viewer::CandidateKind::Vertex
                ? zima::assembly::MateReferenceKind::Point
                : zima::assembly::MateReferenceKind::Face;
    if (candidate.kind != zima::viewer::CandidateKind::Face &&
        candidate.kind != zima::viewer::CandidateKind::Axis &&
        candidate.kind != zima::viewer::CandidateKind::Vertex) return;
    auto local_path = candidate.instance_path;
    if (!active_occurrence_path_.empty()) {
        auto path = zima::assembly::InstancePath::decode(candidate.instance_path);
        const auto prefix =
            zima::assembly::InstancePath::decode(active_occurrence_path_);
        if (path.occurrence_ids.size() <= prefix.occurrence_ids.size() ||
            !std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(),
                path.occurrence_ids.begin())) return;
        path.occurrence_ids.erase(path.occurrence_ids.begin(),
            path.occurrence_ids.begin() +
                static_cast<std::ptrdiff_t>(prefix.occurrence_ids.size()));
        local_path = path.encoded();
    }
    const std::size_t selected_index = *pending_component_placement_index_;
    const bool component_side = pending_component_placement_component_side_;
    zima::assembly::MateReference reference{
        kind, zima::assembly::InstancePath::decode(local_path),
        candidate.owner_id, candidate.semantic_key};
    const auto semantic_label = candidate.kind == zima::viewer::CandidateKind::Vertex
        ? tr("Bod") : candidate.kind == zima::viewer::CandidateKind::Axis
            ? tr("Osa") : tr("Plocha");
    component_placement_dialog_->set_placement_reference(
        selected_index, component_side, std::move(reference), semantic_label);
    pending_component_placement_index_.reset();
    viewer_->clear_selection();
    // Auto-advance the picking flow, matching Python's _advance_pick /
    // _activate_pick chain: after the component-side cell of a row is
    // filled, arm the row's target-side cell next; once both sides of a row
    // are filled, arm the next row's component-side cell (up to 3 rows).
    // The user is never made to hunt for the next cell to click.
    const auto& rows = component_placement_dialog_->placement_references();
    const bool row_target_filled = selected_index < rows.size() &&
        !rows[selected_index].target_reference.owner_id.empty();
    const bool row_component_filled = selected_index < rows.size() &&
        !rows[selected_index].component_reference.owner_id.empty();
    if (component_side && !row_target_filled) {
        start_component_placement_reference_selection(selected_index, false);
    } else if (!component_side && !row_component_filled) {
        start_component_placement_reference_selection(selected_index, true);
    } else if (selected_index + 1 < 3) {
        start_component_placement_reference_selection(selected_index + 1, true);
    } else {
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_candidate_filter({});
        state_->setText(tr("Reference zadána."));
    }
}

void AssemblyWorkspaceWindow::accept_primitive_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    if (primitive_reference_dialog_ == nullptr ||
        !pending_primitive_reference_index_ || candidate.owner_id.empty() ||
        candidate.semantic_key.empty() ||
        primitive_reference_dialog_->owns_reference_owner(candidate.owner_id) ||
        !placement_reference_candidate_has_stable_geometry(candidate)) return;
    auto local_path = candidate.instance_path;
    if (!active_occurrence_path_.empty()) {
        auto path = zima::assembly::InstancePath::decode(candidate.instance_path);
        const auto prefix =
            zima::assembly::InstancePath::decode(active_occurrence_path_);
        if (path.occurrence_ids.size() < prefix.occurrence_ids.size() ||
            !std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(),
                path.occurrence_ids.begin())) return;
        path.occurrence_ids.erase(path.occurrence_ids.begin(),
            path.occurrence_ids.begin() +
                static_cast<std::ptrdiff_t>(prefix.occurrence_ids.size()));
        local_path = path.encoded();
    }
    const std::size_t selected_index = *pending_primitive_reference_index_;
    const bool orientation_reference = selected_index >= 3;
    auto baseline_references =
        primitive_reference_dialog_->references_without(selected_index);
    const int baseline_dof = orientation_reference
        ? zima::document::orientation_constraint_remaining_dof(
            baseline_references, primitive_reference_geometry_, false)
        : zima::document::point_constraint_remaining_dof(
            baseline_references, primitive_reference_geometry_);
    auto proposed_reference = zima::document::ConstructionReference{
        local_path, candidate.owner_id, candidate.semantic_key, 0.0,
        candidate_supports_offset(candidate)};
    if (orientation_reference) {
        proposed_reference.orientation_drives_rotation = true;
        proposed_reference.orientation_role = selected_index == 3 ? "front" : "top";
    }
    baseline_references.push_back(proposed_reference);
    const int proposed_dof = orientation_reference
        ? zima::document::orientation_constraint_remaining_dof(
            baseline_references, primitive_reference_geometry_, false)
        : zima::document::point_constraint_remaining_dof(
            baseline_references, primitive_reference_geometry_);
    if (proposed_dof >= baseline_dof) {
        state_->setText(tr("Tato reference nepřidává žádnou nezávislou vazbu."));
        viewer_->clear_selection();
        return;
    }
    QString reference_label;
    const auto label_from_constructions = [&](const auto& document) {
        const auto found = std::find_if(document.constructions.begin(),
            document.constructions.end(), [&](const auto& object) {
                return candidate.owner_id == object.id ||
                    candidate.owner_id == object.entity_id ||
                    candidate.owner_id == object.container_origin.id;
            });
        if (found != document.constructions.end()) {
            reference_label = QString::fromStdString(found->name);
        }
    };
    const auto active_document_id = workspace_.active_document_id();
    if (const auto* part = workspace_.open_part(active_document_id)) {
        label_from_constructions(part->session.document());
        if (candidate.owner_id == part->session.document().document_id + ":origin") {
            reference_label = tr("Počátek dílu");
        }
    }
    const auto semantic_label = candidate.semantic_key.starts_with("origin:axis:")
        ? tr("Osa %1").arg(QString::fromStdString(
            candidate.semantic_key.substr(12)).toUpper())
        : candidate.semantic_key.starts_with("origin:plane:")
            ? tr("Rovina %1").arg(QString::fromStdString(
                candidate.semantic_key.substr(13)).toUpper())
        : candidate.kind == zima::viewer::CandidateKind::Vertex ? tr("Bod")
        : candidate.kind == zima::viewer::CandidateKind::Axis ? tr("Osa")
        : candidate.kind == zima::viewer::CandidateKind::Plane ? tr("Rovina")
        : tr("Plocha");
    reference_label = reference_label.isEmpty()
        ? semantic_label : reference_label + QStringLiteral(" — ") + semantic_label;
    auto committed_reference = zima::document::ConstructionReference{
        std::move(local_path), candidate.owner_id, candidate.semantic_key, 0.0,
        candidate_supports_offset(candidate)};
    // A primitive container's placement, like a Point, has no dedicated
    // orientation-reference table: a position-admitted face/edge/axis
    // reference must still drive rotation, matching Python's
    // `_ensure_automatic_orientation_roles()`.
    if (selected_index < 3 && candidate_drives_rotation(candidate)) {
        assign_automatic_orientation_role(committed_reference, baseline_references);
    }
    if (!primitive_reference_dialog_->set_reference(
        selected_index, committed_reference, reference_label)) {
        state_->setText(tr("Stejná reference už je pro toto umístění zadaná."));
        viewer_->clear_selection();
        return;
    }
    pending_primitive_reference_index_.reset();
    viewer_->clear_selection();
    refresh_scene();
    if (primitive_translation_dof_ > 0 && selected_index + 1 < 3) {
        start_primitive_reference_selection(selected_index + 1);
    } else {
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_selection_contract({});
        viewer_->set_candidate_filter([](const auto&) { return false; });
        viewer_->clear_selection();
    }
}

bool AssemblyWorkspaceWindow::accept_primitive_tree_reference(
    const QTreeWidgetItem* item) {
    if (item == nullptr || primitive_reference_dialog_ == nullptr ||
        !pending_primitive_reference_index_) return false;
    const auto item_kind = item->data(0, Qt::UserRole + 3).toString();
    if (item_kind == QStringLiteral("document-origin") ||
        item_kind == QStringLiteral("construction-origin")) {
        const auto origin_id = item->data(0, Qt::UserRole).toString().toStdString();
        if (primitive_reference_dialog_->owns_reference_owner(origin_id)) return false;
        if (*pending_primitive_reference_index_ >= 3) return false;
        struct CapturedPlaneReference {
            std::string owner_id;
            std::string instance_path;
            std::string semantic_key;
        };
        std::vector<CapturedPlaneReference> planes;
        for (int index = 0; index < item->childCount(); ++index) {
            const auto* child = item->child(index);
            const auto semantic = child->data(0, Qt::UserRole + 5)
                .toString().toStdString();
            if (!semantic.starts_with("origin:plane:") &&
                !semantic.starts_with("plane:")) continue;
            planes.push_back({
                child->data(0, Qt::UserRole + 6).isValid()
                    ? child->data(0, Qt::UserRole + 6).toString().toStdString()
                    : child->data(0, Qt::UserRole).toString().toStdString(),
                child->data(0, Qt::UserRole + 1).toString().toStdString(),
                semantic});
        }
        const auto origin_plane_rank = [](const auto& value) {
            return value.semantic_key.ends_with("plane:xz") ? 0
                : value.semantic_key.ends_with("plane:xy") ? 1 : 2;
        };
        std::ranges::sort(planes, {}, origin_plane_rank);
        bool accepted_any = false;
        for (const auto& plane : planes) {
            const auto row = primitive_reference_dialog_->first_empty_position_index();
            if (row >= 3) break;
            start_primitive_reference_selection(row);
            if (!pending_primitive_reference_index_) break;
            const auto before = primitive_reference_dialog_->first_empty_position_index();
            zima::viewer::ViewerCandidate candidate;
            candidate.geometry = zima::viewer::CandidateGeometry::OriginalReference;
            candidate.kind = zima::viewer::CandidateKind::Plane;
            candidate.owner_id = plane.owner_id;
            candidate.instance_path = plane.instance_path;
            candidate.semantic_key = plane.semantic_key;
            accept_primitive_reference(candidate);
            accepted_any = accepted_any ||
                primitive_reference_dialog_->first_empty_position_index() != before;
        }
        return accepted_any;
    }
    if (item_kind != QStringLiteral("origin-reference")) return false;
    zima::viewer::ViewerCandidate candidate;
    candidate.geometry = zima::viewer::CandidateGeometry::OriginalReference;
    candidate.instance_path =
        item->data(0, Qt::UserRole + 1).toString().toStdString();
    candidate.owner_id = item->data(0, Qt::UserRole + 6).isValid()
        ? item->data(0, Qt::UserRole + 6).toString().toStdString()
        : item->data(0, Qt::UserRole).toString().toStdString();
    candidate.semantic_key =
        item->data(0, Qt::UserRole + 5).toString().toStdString();
    candidate.kind = candidate.semantic_key == "origin:point" ||
            candidate.semantic_key == "point"
        ? zima::viewer::CandidateKind::Vertex
        : candidate.semantic_key.starts_with("origin:axis:")
            ? zima::viewer::CandidateKind::Axis
            : zima::viewer::CandidateKind::Plane;
    const auto before = primitive_reference_dialog_->first_empty_position_index();
    accept_primitive_reference(candidate);
    return primitive_reference_dialog_->first_empty_position_index() != before;
}

bool AssemblyWorkspaceWindow::accept_construction_tree_reference(
    const QTreeWidgetItem* item) {
    if (item == nullptr || construction_reference_dialog_ == nullptr ||
        !pending_construction_reference_index_) return false;
    const auto item_kind = item->data(0, Qt::UserRole + 3).toString();
    if ((item_kind == QStringLiteral("document-origin") ||
         item_kind == QStringLiteral("construction-origin"))) {
        const auto origin_id =
            item->data(0, Qt::UserRole).toString().toStdString();
        if (origin_id == construction_reference_dialog_->construction_id() +
                ":origin") return false;
        const std::size_t selected_index = *pending_construction_reference_index_;
        // Matches Python's PointConstraintDialog.add_reference() Origin-kind
        // branch (shared, unoverridden, by Point/Axis/every placement
        // dialog): clicking the whole "Počátek dílu"/"Počátek sestavy" tree
        // node -- as opposed to one of its Point/X Axis/.../XZ Plane
        // children -- picking a POSITION row expands the Origin into its
        // datum planes so all three land as position references in one
        // click, fully constraining the container immediately. Only
        // FRONT/TOP no longer accept `origin:plane:*` at all: a document's
        // own datum planes looked selectable there but were misleading, and
        // a container-origin plane is only the container's already-derived
        // preview frame, not an independent orientation anchor.
        const auto find_plane_child = [&](std::string_view plane_key)
            -> const QTreeWidgetItem* {
            for (int i = 0; i < item->childCount(); ++i) {
                const auto* child = item->child(i);
                const auto key = child->data(0, Qt::UserRole + 5)
                    .toString().toStdString();
                if (key == std::string("origin:") + std::string(plane_key) ||
                    key == plane_key) return child;
            }
            return nullptr;
        };
        // Extract each plane child's identifying data into plain QStrings
        // *before* accepting any one of them: accept_origin_reference_value()
        // synchronously calls accept_construction_reference(), which
        // triggers refresh_scene() -> tree_->clear(), destroying every
        // QTreeWidgetItem including `item` and its still-unprocessed
        // siblings. Recursing back into accept_construction_tree_reference()
        // with a QTreeWidgetItem* found before that clear (as this loop used
        // to do) is therefore a use-after-free on the 2nd/3rd iteration --
        // this is the root cause of the crash when clicking the whole
        // "Počátek" origin node while entering a Point/Axis/Plane container
        // reference.
        struct CapturedPlaneReference {
            QString owner_id;
            QString instance_path;
            QString semantic_key;
        };
        const auto capture_plane_child = [&](const QTreeWidgetItem* child)
            -> CapturedPlaneReference {
            return CapturedPlaneReference{
                child->data(0, Qt::UserRole + 6).isValid()
                    ? child->data(0, Qt::UserRole + 6).toString()
                    : child->data(0, Qt::UserRole).toString(),
                child->data(0, Qt::UserRole + 1).toString(),
                child->data(0, Qt::UserRole + 5).toString()};
        };
        if (selected_index >= 3) return false;
        std::vector<CapturedPlaneReference> captured_planes;
        // XZ first becomes FRONT (+Y after its required inversion), XY
        // second becomes TOP (+Z); YZ completes the positional triad.
        for (const auto plane_key : {"plane:xz", "plane:xy", "plane:yz"}) {
            const auto* plane_child = find_plane_child(plane_key);
            if (plane_child != nullptr) {
                captured_planes.push_back(capture_plane_child(plane_child));
            }
        }
        // Always target the real first empty position row(s) (0/1/2),
        // never assume whatever row happened to be armed when "Počátek"
        // was clicked. A 2nd bulk-fill attempt (e.g. after the user deleted
        // one reference and re-triggered the bulk-fill) can leave the armed
        // row at 1 or 2 while row 0 is still empty (or vice versa) -- the
        // old code always fed xy/yz/xz starting from whatever row was
        // currently armed, so the first one or two planes collided with an
        // already-populated row (duplicate-reference rejection) or were
        // rejected as not adding an independent DOF, and silently failed to
        // show up in the 3D view.
        bool accepted_any = false;
        for (const auto& reference : captured_planes) {
            const auto target_index =
                construction_reference_dialog_->first_empty_position_index();
            if (target_index >= 3) break;
            start_construction_reference_selection(target_index);
            if (!pending_construction_reference_index_) break;
            if (accept_origin_reference_value(reference.owner_id,
                    reference.instance_path, reference.semantic_key)) {
                accepted_any = true;
            }
        }
        return accepted_any;
    }
    zima::viewer::ViewerCandidate candidate;
    candidate.geometry = zima::viewer::CandidateGeometry::OriginalReference;
    candidate.instance_path =
        item->data(0, Qt::UserRole + 1).toString().toStdString();
    try {
        const auto path = zima::assembly::InstancePath::decode(
            candidate.instance_path);
        const auto prefix = active_occurrence_path_.empty()
            ? zima::assembly::InstancePath{}
            : zima::assembly::InstancePath::decode(active_occurrence_path_);
        const bool active_part =
            workspace_.open_part(workspace_.active_document_id()) != nullptr;
        const bool in_scope = active_part ? path == prefix
            : path == prefix ||
                (path.occurrence_ids.size() == prefix.occurrence_ids.size() + 1 &&
                 std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(),
                     path.occurrence_ids.begin()));
        if (!in_scope) return false;
    } catch (const std::invalid_argument&) {
        return false;
    }
    if (item_kind == QStringLiteral("origin-reference")) {
        candidate.owner_id = item->data(0, Qt::UserRole + 6).isValid()
            ? item->data(0, Qt::UserRole + 6).toString().toStdString()
            : item->data(0, Qt::UserRole).toString().toStdString();
        candidate.semantic_key =
            item->data(0, Qt::UserRole + 5).toString().toStdString();
        candidate.kind =
            (candidate.semantic_key == "origin:point" ||
                candidate.semantic_key == "point")
            ? zima::viewer::CandidateKind::Vertex
            : candidate.semantic_key.starts_with("origin:axis:")
                ? zima::viewer::CandidateKind::Axis
                : zima::viewer::CandidateKind::Plane;
    } else if (item_kind == QStringLiteral("part-construction") ||
               item_kind == QStringLiteral("assembly-construction")) {
        const auto id = item->data(0, Qt::UserRole).toString().toStdString();
        const auto* part = workspace_.open_part(workspace_.active_document_id());
        const auto* assembly =
            workspace_.open_assembly(workspace_.active_document_id());
        const auto* object = part != nullptr
            ? part->session.document().find_construction(id)
            : assembly != nullptr
                ? assembly->session.document().find_construction(id) : nullptr;
        if (object == nullptr) return false;
        if (object->id == construction_reference_dialog_->construction_id()) {
            return false;
        }
        candidate.owner_id = object->kind ==
                zima::document::ConstructionKind::Point
            ? object->container_origin.id : object->entity_id;
        candidate.kind = object->kind == zima::document::ConstructionKind::Point
            ? zima::viewer::CandidateKind::Vertex
            : object->kind == zima::document::ConstructionKind::Axis
                ? zima::viewer::CandidateKind::Axis
                : zima::viewer::CandidateKind::Plane;
        candidate.semantic_key = object->kind ==
                zima::document::ConstructionKind::Point ? "point"
            : object->kind == zima::document::ConstructionKind::Axis ? "axis"
            : "plane";
    } else {
        return false;
    }
    const bool accepted = candidate.kind == zima::viewer::CandidateKind::Vertex ||
        candidate.kind == zima::viewer::CandidateKind::Axis ||
        candidate.kind == zima::viewer::CandidateKind::Plane;
    if (!accepted) return false;
    accept_construction_reference(candidate);
    return true;
}

bool AssemblyWorkspaceWindow::accept_origin_reference_value(
    const QString& owner_id, const QString& instance_path,
    const QString& semantic_key) {
    if (construction_reference_dialog_ == nullptr ||
        !pending_construction_reference_index_ || owner_id.isEmpty() ||
        semantic_key.isEmpty()) return false;
    zima::viewer::ViewerCandidate candidate;
    candidate.geometry = zima::viewer::CandidateGeometry::OriginalReference;
    candidate.instance_path = instance_path.toStdString();
    try {
        const auto path = zima::assembly::InstancePath::decode(
            candidate.instance_path);
        const auto prefix = active_occurrence_path_.empty()
            ? zima::assembly::InstancePath{}
            : zima::assembly::InstancePath::decode(active_occurrence_path_);
        const bool active_part =
            workspace_.open_part(workspace_.active_document_id()) != nullptr;
        const bool in_scope = active_part ? path == prefix
            : path == prefix ||
                (path.occurrence_ids.size() == prefix.occurrence_ids.size() + 1 &&
                 std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(),
                     path.occurrence_ids.begin()));
        if (!in_scope) return false;
    } catch (const std::invalid_argument&) {
        return false;
    }
    candidate.owner_id = owner_id.toStdString();
    candidate.semantic_key = semantic_key.toStdString();
    candidate.kind =
        (candidate.semantic_key == "origin:point" ||
            candidate.semantic_key == "point")
        ? zima::viewer::CandidateKind::Vertex
        : candidate.semantic_key.starts_with("origin:axis:")
            ? zima::viewer::CandidateKind::Axis
            : zima::viewer::CandidateKind::Plane;
    const bool accepted = candidate.kind == zima::viewer::CandidateKind::Vertex ||
        candidate.kind == zima::viewer::CandidateKind::Axis ||
        candidate.kind == zima::viewer::CandidateKind::Plane;
    if (!accepted) return false;
    accept_construction_reference(candidate);
    return true;
}

void AssemblyWorkspaceWindow::show_sketch_properties(const std::string& sketch_id) {
    if (properties_dialog_ != nullptr) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    if (part == nullptr && assembly == nullptr) return;
    const auto& sketches = part != nullptr
        ? part->session.document().sketches : assembly->session.document().sketches;
    const auto found = std::find_if(sketches.begin(), sketches.end(),
        [&](const auto& sketch) { return sketch.id == sketch_id; });
    const bool edit_mode = found != sketches.end();
    if (!sketch_id.empty() && !edit_mode) return;
    auto initial = edit_mode ? *found : zima::sketcher::Sketch::create_default();
    std::optional<zima::document::HistoryContainer> new_sketch_container;
    if (!edit_mode) {
        new_sketch_container = zima::document::PartDocument::create_sketch_container();
        initial.owner_container_id = new_sketch_container->id;
    }
    zima::document::Placement initial_placement;
    if (part != nullptr) {
        const auto& history = part->session.document().history;
        const auto owner_container = std::find_if(history.begin(), history.end(),
            [&](const auto& container) {
                return container.id == initial.owner_container_id;
            });
        if (owner_container != history.end()) {
            initial_placement = owner_container->placement;
        }
    }
    if (!edit_mode && new_sketch_container) {
        initial_placement = new_sketch_container->placement;
    }
    const auto& constructions = part != nullptr
        ? part->session.document().constructions : assembly->session.document().constructions;
    std::vector<SketchPropertiesDialog::PlaneOption> plane_options;
    for (const auto& object : constructions) {
        if (object.kind != zima::document::ConstructionKind::Plane) continue;
        plane_options.push_back(SketchPropertiesDialog::PlaneOption{
            object.entity_id, QString::fromStdString(object.name)});
    }
    const std::string owner_id = workspace_.active_document_id();
    auto* dialog = new SketchPropertiesDialog(
        std::move(initial), initial_placement, edit_mode, std::move(plane_options),
        [this, owner_id, edit_mode, new_sketch_container](
            zima::sketcher::Sketch committed,
            zima::document::Placement committed_placement,
            bool enter_sketch) {
            if (enter_sketch) active_sketch_id_ = committed.id;
            selected_sketch_id_ = committed.id;
            const auto update = [&](auto& next) {
                if (edit_mode) {
                    const auto target = std::find_if(next.sketches.begin(), next.sketches.end(),
                        [&](const auto& sketch) { return sketch.id == committed.id; });
                    if (target == next.sketches.end()) {
                        throw std::runtime_error("Sketch no longer exists");
                    }
                    *target = committed;
                    if constexpr (requires { next.history; }) {
                        const auto owner = std::find_if(
                            next.history.begin(), next.history.end(),
                            [&](const auto& container) {
                                return container.id == committed.owner_container_id;
                            });
                        if (owner == next.history.end()) {
                            throw std::runtime_error(
                                "Sketch owning container no longer exists");
                        }
                        owner->placement = committed_placement;
                    }
                } else {
                    if (committed.owner_container_id.empty() || !new_sketch_container ||
                        committed.owner_container_id != new_sketch_container->id) {
                        throw std::runtime_error("Sketch has no owning container");
                    }
                    auto container = *new_sketch_container;
                    container.placement = committed_placement;
                    next.sketches.push_back(committed);
                    if constexpr (requires { next.insert_history_entry(
                            zima::document::PartHistoryKind::Sketch,
                            committed.id); }) {
                        next.insert_history_entry(
                            zima::document::PartHistoryKind::Feature,
                            new_sketch_container->id);
                        next.history.push_back(std::move(container));
                    }
                }
            };
            if (auto* target_part = workspace_.open_part(owner_id)) {
                auto next = target_part->session.document();
                update(next);
                auto calculated = target_part->session.calculated_boundaries();
                // Resolves the just-committed Sketch's frame from its
                // (possibly just changed) plane_reference_owner_id, the
                // same way any other reference to a Plane container is
                // resolved -- see PartDocument::resolve_constructions()'s
                // dedicated Sketch loop.
                next.resolve_constructions(
                    construction_reference_source_geometry(calculated));
                static_cast<void>(refresh_sketch_external_references(next, calculated));
                target_part->session.commit(std::move(next), std::move(calculated));
                return;
            }
            auto* target_assembly = workspace_.open_assembly(owner_id);
            if (target_assembly == nullptr) {
                throw std::runtime_error("Sketch owner is no longer open");
            }
            auto next = target_assembly->session.document();
            update(next);
            next.resolve_constructions();
            target_assembly->session.commit(std::move(next));
        }, this);
    if (part != nullptr) {
        auto reference_geometry = construction_reference_source_geometry(
            part->session.calculated_boundaries());
        const auto& document = part->session.document();
        append_reference_geometry(reference_geometry,
            document.origin_viewer_mesh().original_references);
        append_reference_geometry(reference_geometry,
            document.construction_viewer_mesh().original_references);
        primitive_reference_geometry_ = reference_geometry;
        primitive_reference_dialog_ = dialog;
        dialog->set_reference_geometry(reference_geometry);
        dialog->set_reference_request_callback(
            [this](std::size_t index) {
                start_primitive_reference_selection(index);
            });
        dialog->set_reference_highlights_changed_callback([this, dialog] {
            viewer_->set_constraint_reference_highlights(
                {}, highlighted_reference_edge_keys(*dialog));
        });
        dialog->set_preview_callback([this](
                const zima::sketcher::Sketch& sketch,
                const zima::document::Placement& pending_placement) {
            auto placement = pending_placement;
            zima::kernel::Vec3 base_rotation;
            bool orientation_from_reference = false;
            static_cast<void>(zima::document::resolve_placement(
                placement, primitive_reference_geometry_, &base_rotation,
                &orientation_from_reference));
            const auto state = zima::document::point_constraint_state(
                placement.references, primitive_reference_geometry_);
            primitive_translation_dof_ = state.remaining_dof;
            if (primitive_reference_dialog_ != nullptr) {
                primitive_reference_dialog_->set_translation_constraint_state(
                    state, {placement.x, placement.y, placement.z});
                primitive_reference_dialog_->set_remaining_rotation_dof(
                    zima::document::orientation_constraint_remaining_dof(
                        placement.references, primitive_reference_geometry_, false));
                primitive_reference_dialog_->set_orientation_base_rotation(
                    base_rotation, orientation_from_reference);
            }
            auto geometric_placement = pending_placement;
            geometric_placement.orientation_back = false;
            geometric_placement.orientation_quarter_turns = 0;
            static_cast<void>(zima::document::resolve_placement(
                geometric_placement, primitive_reference_geometry_));

            auto plane = zima::document::PartDocument::create_construction(
                zima::document::ConstructionKind::Plane);
            if (!sketch.owner_container_id.empty()) {
                plane.id = sketch.owner_container_id;
                plane.entity_id = plane.id + ":entity";
                plane.entity_parent_id = plane.id;
                plane.container_origin =
                    zima::document::create_container_origin(plane.id);
            }
            plane.name = sketch.name;
            plane.origin = {geometric_placement.x, geometric_placement.y,
                            geometric_placement.z};
            plane.rotation = {geometric_placement.rotation_x,
                geometric_placement.rotation_y, geometric_placement.rotation_z};
            plane.absolute_rotation = {geometric_placement.absolute_rotation_x,
                geometric_placement.absolute_rotation_y,
                geometric_placement.absolute_rotation_z};
            plane.orientation_back = false;
            plane.orientation_quarter_turns = 0;
            plane.base_plane = sketch.plane == zima::sketcher::SketchPlane::XY
                ? zima::document::LocalDatumPlane::XY
                : sketch.plane == zima::sketcher::SketchPlane::XZ
                    ? zima::document::LocalDatumPlane::XZ
                    : zima::document::LocalDatumPlane::YZ;
            plane.offset = sketch.plane_offset;
            plane.reference_valid = true;
            zima::document::PartDocument preview_document;
            preview_document.constructions.push_back(plane);
            auto preview_container =
                zima::document::PartDocument::create_sketch_container();
            preview_container.id = sketch.owner_container_id;
            preview_container.placement = geometric_placement;
            preview_document.history.push_back(std::move(preview_container));
            preview_document.sketches.push_back(sketch);
            preview_document.resolve_constructions(primitive_reference_geometry_);
            primitive_origin_preview_mesh_ =
                preview_document.construction_viewer_mesh(plane.id);
            // A standalone Sketch container remains visible while its
            // Properties window is open. Only suppress Sketcher's infinite
            // working X/Y cross here; removing the complete viewer_mesh()
            // also removed the actual white sketch curves. Extrusion and
            // Revolution own separate internal sketches and continue to
            // control their normal-view visibility through their history
            // container contract.
            auto sketch_preview = preview_document.sketches.front().viewer_mesh();
            sketch_preview.axes.clear();
            append_mesh(*primitive_origin_preview_mesh_, std::move(sketch_preview));
            sketch_properties_preview_id_ = sketch.id;
            viewer_->set_feature_preview_owners(
                {plane.entity_id, sketch.id});
            preserve_view_on_refresh_ = true;
            refresh_scene();
        });
    }
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        primitive_reference_dialog_ = nullptr;
        pending_primitive_reference_index_.reset();
        primitive_reference_geometry_ = {};
        primitive_translation_dof_ = 3;
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_candidate_filter({});
        viewer_->set_selection_contract({});
        viewer_->set_constraint_reference_highlights({}, {});
        viewer_->set_feature_preview_owners({});
        primitive_origin_preview_mesh_.reset();
        sketch_properties_preview_id_.clear();
        refresh_tabs();
        refresh_scene();
        if (!active_sketch_id_.empty()) align_active_sketch_view();
    });
    dialog->show();
    if (primitive_reference_dialog_ == dialog &&
        dialog->first_empty_position_index() < 3) {
        start_primitive_reference_selection(
            dialog->first_empty_position_index());
    }
}

void AssemblyWorkspaceWindow::show_sketch_bspline_properties(
    const std::string& sketch_id, const std::string& bspline_id) {
    if (properties_dialog_ != nullptr || sketch_bspline_active_) return;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
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
    auto* dialog = new SketchBSplinePropertiesDialog(
        spline->degree, spline->closed, std::move(points),
        [this, sketch_id, bspline_id](
            unsigned degree, bool closed,
            const std::vector<std::array<double, 2>>& values) {
            if (active_sketch_id_ != sketch_id ||
                !mutate_active_sketch([&](auto& target_sketch) {
                    const auto target_spline = std::find_if(
                        target_sketch.bsplines.begin(), target_sketch.bsplines.end(),
                        [&](const auto& value) { return value.id == bspline_id; });
                    if (target_spline == target_sketch.bsplines.end() ||
                        values.size() != target_spline->control_point_ids.size()) {
                        throw std::runtime_error("B-spline no longer exists");
                    }
                    target_spline->degree = degree;
                    target_spline->closed = closed;
                    for (std::size_t index = 0; index < values.size(); ++index) {
                        auto* point = target_sketch.find_point(
                            target_spline->control_point_ids[index]);
                        if (point == nullptr) throw std::runtime_error(
                            "Missing B-spline control point");
                        point->x = values[index][0];
                        point->y = values[index][1];
                    }
                })) throw std::runtime_error("Sketch no longer exists");
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

    auto* dialog = new SketchTextPropertiesDialog(
        std::move(initial), anchor,
        [this, sketch_id](
            const std::optional<zima::sketcher::SketchText>& preview) {
            if (!preview) {
                viewer_->set_transient_edges({});
                return;
            }
            const auto* target_sketch = active_sketch();
            if (target_sketch == nullptr || target_sketch->id != sketch_id) return;
            viewer_->set_transient_edges(
                sketch_text_preview_edges(*target_sketch, *preview));
        },
        [this, sketch_id, edit_mode](
            zima::sketcher::SketchText committed) {
            const std::string committed_id = committed.id;
            if (active_sketch_id_ != sketch_id ||
                !mutate_active_sketch([&](auto& target_sketch) {
                    if (edit_mode) target_sketch.update_text(std::move(committed));
                    else target_sketch.add_text(std::move(committed));
                })) throw std::runtime_error("Sketch no longer exists");
            selected_sketch_text_id_ = committed_id;
            state_->setText(edit_mode
                ? tr("Text skici byl upraven jako jedna revize.")
                : tr("Text skici byl vytvořen jako jedna revize."));
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
    const bool requested_profile_mode = sketch_external_profile_active_;
    if (enabled) {
        if (properties_dialog_ != nullptr || active_sketch_id_.empty()) {
            enabled = false;
        } else if (active_sketch() == nullptr) enabled = false;
    }
    if (enabled) {
        cancel_sketch_segment();
        sketch_external_profile_active_ = requested_profile_mode;
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
        sketch_external_reference_action_->setChecked(
            enabled && !sketch_external_profile_active_);
    }
    {
        const QSignalBlocker blocker(sketch_external_profile_action_);
        sketch_external_profile_action_->setChecked(
            enabled && sketch_external_profile_active_);
    }
    if (!enabled) sketch_external_profile_active_ = false;
    if (enabled || was_active) {
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(enabled
            ? sketch_external_profile_active_
                ? tr("Reference → obrys: vyberte persistovanou původní hranu.")
                : tr("Externí reference: vyberte persistovanou původní plochu, "
                     "hranu, vrchol nebo osu. Pravým tlačítkem lze přepínat kandidáty.")
            : tr("Režim externích referencí byl ukončen."));
    }
}

void AssemblyWorkspaceWindow::accept_sketch_external_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_external_reference_active_ ||
        (sketch_external_profile_active_ &&
         candidate.kind != zima::viewer::CandidateKind::Edge) ||
        candidate.geometry != zima::viewer::CandidateGeometry::OriginalReference ||
        (candidate.kind != zima::viewer::CandidateKind::Edge &&
         candidate.kind != zima::viewer::CandidateKind::Vertex &&
         candidate.kind != zima::viewer::CandidateKind::Axis &&
         candidate.kind != zima::viewer::CandidateKind::Face)) return;
    if (auto* assembly = workspace_.open_assembly(workspace_.active_document_id())) {
        try {
            auto next = assembly->session.document();
            const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
                [&](const auto& value) { return value.id == active_sketch_id_; });
            if (sketch == next.sketches.end() || candidate.instance_path.empty()) return;
            const auto address = workspace_.resolve_occurrence(next.document_id,
                zima::assembly::InstancePath::decode(candidate.instance_path));
            if (!address) throw std::invalid_argument(
                "External reference requires an exact component occurrence");
            auto reference = zima::sketcher::Sketch::create_external_reference(
                candidate.kind == zima::viewer::CandidateKind::Edge
                    ? zima::sketcher::ExternalReferenceKind::Edge
                    : candidate.kind == zima::viewer::CandidateKind::Axis
                        ? zima::sketcher::ExternalReferenceKind::Axis
                        : candidate.kind == zima::viewer::CandidateKind::Face
                            ? zima::sketcher::ExternalReferenceKind::Face
                            : zima::sketcher::ExternalReferenceKind::Point);
            reference.source_document_id = address->source_document_id;
            reference.source_owner_id = candidate.owner_id;
            reference.source_semantic_key = candidate.semantic_key;
            reference.source_instance_path = candidate.instance_path;
            populate_external_reference_cache(
                *sketch, reference, next.build_scene().original_references);
            const auto reference_id = reference.id;
            sketch->add_external_reference(std::move(reference));
            if (sketch_external_profile_active_) {
                static_cast<void>(
                    sketch->add_external_profile_geometry(reference_id));
            }
            assembly->session.commit(std::move(next));
            preserve_view_on_refresh_ = true;
            refresh_tabs();
            refresh_scene();
            state_->setText(tr(
                "Externí reference Assembly skici byla uložena bez volání OCCT."));
        } catch (const std::exception& error) {
            state_->setText(QString::fromUtf8(error.what()));
        }
        return;
    }
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
        const auto reference_id = reference.id;
        sketch->add_external_reference(std::move(reference));
        if (sketch_external_profile_active_) {
            static_cast<void>(
                sketch->add_external_profile_geometry(reference_id));
        }
        if (assembly != nullptr && dependent_path && source_path) {
            workspace_.add_external_sketch_dependency(
                assembly->session.document().document_id,
                *dependent_path, *source_path);
        }
        part->session.commit(
            std::move(next), part->session.calculated_boundaries());
        workspace_.synchronize_external_sketch_dependencies();
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

void AssemblyWorkspaceWindow::align_active_sketch_view(bool fit_view) {
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    const auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    if (viewer_ == nullptr || (part == nullptr && assembly == nullptr) ||
        active_sketch_id_.empty()) return;
    const auto& sketches = part != nullptr
        ? part->session.document().sketches : assembly->session.document().sketches;
    const auto sketch = std::find_if(
        sketches.begin(), sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == sketches.end()) return;
    // `plane` is only the local XY/XZ/YZ choice. Once the container has
    // placement/orientation references, the real sketch plane can point in
    // any world direction; using the enum here caused Sketcher to claim a
    // normal view while retaining the oblique view shown in 03.png.
    zima::kernel::Vec3 direction = sketch->resolved_normal;
    const double direction_length = std::sqrt(direction.x * direction.x +
        direction.y * direction.y + direction.z * direction.z);
    if (direction_length <= 1.0e-12) {
        direction = sketch->plane == zima::sketcher::SketchPlane::XY
            ? zima::kernel::Vec3{0.0, 0.0, 1.0}
            : sketch->plane == zima::sketcher::SketchPlane::XZ
                ? zima::kernel::Vec3{0.0, -1.0, 0.0}
                : zima::kernel::Vec3{1.0, 0.0, 0.0};
    }
    if (sketch_view_state_id_ != active_sketch_id_) {
        sketch_view_state_id_ = active_sketch_id_;
        sketch_view_back_ = false;
        sketch_view_quarter_turns_ = 0;
        if (part != nullptr) {
            const auto* owner = part->session.document().find_container(
                sketch->owner_container_id);
            if (owner != nullptr) {
                sketch_view_back_ = owner->placement.orientation_back;
                sketch_view_quarter_turns_ =
                    ((owner->placement.orientation_quarter_turns % 4) + 4) % 4;
            }
        }
    }
    if (sketch_view_back_) direction = {-direction.x, -direction.y, -direction.z};
    if (part != nullptr &&
        workspace_.open_assembly(workspace_.displayed_document_id()) != nullptr) {
        const auto occurrence = resolve_active_occurrence(
            part->session.document().document_id);
        if (!occurrence || occurrence->empty()) return;
        direction = workspace_.occurrence_direction_to_scene(
            workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(*occurrence), direction);
    }
    viewer_->set_view_direction(direction,
        static_cast<float>(sketch_view_quarter_turns_ * 90));
    if (fit_view) viewer_->fit_all();
    state_->setText(tr("Pohled je kolmý k rovině aktivní skici."));
}

void AssemblyWorkspaceWindow::flip_active_sketch_view() {
    if (active_sketch_id_.empty()) return;
    // Initialize the camera-only state from the active sketch before changing it.
    if (sketch_view_state_id_ != active_sketch_id_) align_active_sketch_view(false);
    sketch_view_back_ = !sketch_view_back_;
    align_active_sketch_view(false);
    state_->setText(tr("Pohled na aktivní skicu byl převrácen."));
}

void AssemblyWorkspaceWindow::rotate_active_sketch_view() {
    if (active_sketch_id_.empty()) return;
    // Rotation is deliberately a view operation; it never changes the sketch plane.
    if (sketch_view_state_id_ != active_sketch_id_) align_active_sketch_view(false);
    sketch_view_quarter_turns_ = (sketch_view_quarter_turns_ + 1) % 4;
    align_active_sketch_view(false);
    state_->setText(tr("Pohled na aktivní skicu byl otočen o 90°."));
}

const zima::sketcher::Sketch* AssemblyWorkspaceWindow::active_sketch() const {
    if (active_sketch_id_.empty()) return nullptr;
    const std::vector<zima::sketcher::Sketch>* sketches{};
    if (const auto* part = workspace_.open_part(workspace_.active_document_id())) {
        sketches = &part->session.document().sketches;
    } else if (const auto* assembly =
                   workspace_.open_assembly(workspace_.active_document_id())) {
        sketches = &assembly->session.document().sketches;
    }
    if (sketches == nullptr) return nullptr;
    const auto found = std::find_if(sketches->begin(), sketches->end(),
        [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
    return found == sketches->end() ? nullptr : &*found;
}

bool AssemblyWorkspaceWindow::mutate_active_sketch(
    const std::function<void(zima::sketcher::Sketch&)>& mutation) {
    if (active_sketch_id_.empty()) return false;
    const auto mutate = [&](auto& document) {
        const auto found = std::find_if(document.sketches.begin(),
            document.sketches.end(),
            [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
        if (found == document.sketches.end()) return false;
        mutation(*found);
        found->validate();
        return true;
    };
    if (auto* part = workspace_.open_part(workspace_.active_document_id())) {
        auto next = part->session.document();
        if (!mutate(next)) return false;
        part->session.commit(std::move(next), part->session.calculated_boundaries());
        return true;
    }
    if (auto* assembly = workspace_.open_assembly(workspace_.active_document_id())) {
        auto next = assembly->session.document();
        if (!mutate(next)) return false;
        assembly->session.commit(std::move(next));
        return true;
    }
    return false;
}

bool AssemblyWorkspaceWindow::accept_sketch_text_ray(
    const zima::kernel::Vec3& origin,
    const zima::kernel::Vec3& direction) {
    if (!sketch_text_active_ || sketch_text_dialog_ == nullptr ||
        !editing_sketch_text_id_.empty()) return false;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    sketch_text_dialog_->set_anchor((*position)[0], (*position)[1]);
    state_->setText(tr("Poloha textu určena. Upravte parametry a potvrďte OK."));
    return true;
}

void AssemblyWorkspaceWindow::finish_active_sketch() {
    if (active_sketch_id_.empty() || properties_dialog_ != nullptr) return;
    const std::string finished_sketch_id = active_sketch_id_;
    std::string return_container_id;
    std::optional<zima::document::FeatureKind> return_feature_kind;
    if (auto* part = workspace_.open_part(workspace_.active_document_id())) {
        if (pending_profile_feature_) {
            auto next = part->session.document();
            const auto draft_sketch = std::find_if(next.sketches.begin(),
                next.sketches.end(), [&](const auto& value) {
                    return value.id == finished_sketch_id &&
                        value.owner_container_id == pending_profile_feature_->id;
                });
            if (draft_sketch != next.sketches.end()) {
                auto* draft = next.find_container(pending_profile_feature_->id);
                if (draft != nullptr) {
                    auto feature = *pending_profile_feature_;
                    feature.placement = draft->placement;
                    const bool extrusion = feature.feature_kind ==
                        zima::document::FeatureKind::Extrusion;
                    draft_sketch->plane_offset = extrusion
                        ? feature.extrusion.profile_plane_offset
                        : feature.revolution.profile_plane_offset;
                    *draft = std::move(feature);
                    part->session.commit(std::move(next),
                        part->session.calculated_boundaries());
                }
            }
        }
        const auto sketch = std::find_if(part->session.document().sketches.begin(),
            part->session.document().sketches.end(), [&](const auto& value) {
                return value.id == finished_sketch_id;
            });
        if (sketch != part->session.document().sketches.end()) {
            return_container_id = sketch->owner_container_id;
            if (const auto* owner = part->session.document().find_container(
                    return_container_id)) {
                return_feature_kind = owner->feature_kind;
            }
        }
    }
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
    state_->setText(tr("Skica dokončena. Návrat do vlastností kontejneru."));
    if (return_feature_kind && !return_container_id.empty()) {
        QTimer::singleShot(0, this,
            [this, finished_sketch_id, return_container_id,
             feature_kind = *return_feature_kind] {
                if (feature_kind == zima::document::FeatureKind::Sketch) {
                    show_sketch_properties(finished_sketch_id);
                } else if (feature_kind ==
                               zima::document::FeatureKind::Extrusion ||
                           feature_kind ==
                               zima::document::FeatureKind::Revolution) {
                    show_primitive_properties(feature_kind, return_container_id);
                }
            });
    }
}

void AssemblyWorkspaceWindow::start_sketch_point() {
    if (properties_dialog_ != nullptr) return;
    if (active_sketch() == nullptr) return;
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
    if (active_sketch() == nullptr) return;
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
    sketch_polyline_arc_mode_ = false;
    pending_polyline_tangent_geometry_id_.clear();
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
    sketch_coincident_active_ = false;
    sketch_midpoint_active_ = false;
    sketch_symmetric_active_ = false;
    sketch_concentric_active_ = false;
    sketch_tangent_active_ = false;
    sketch_segment_pair_active_ = false;
    sketch_point_dimension_active_ = false;
    sketch_line_pair_dimension_active_ = false;
    pending_point_dimension_first_id_.clear();
    pending_point_dimension_second_id_.clear();
    pending_point_dimension_vertex_id_.clear();
    pending_point_dimension_cursor_.reset();
    pending_line_dimension_reference_id_.clear();
    pending_segment_start_.reset();
    pending_sketch_snap_geometry_id_.clear();
    pending_sketch_snap_kind_.reset();
    pending_segment_start_snap_geometry_id_.clear();
    pending_segment_start_snap_kind_.reset();
    sketch_segment_inference_cycle_ = 0;
    sketch_skip_candidate_snap_ = false;
    sketch_polyline_arc_mode_ = false;
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
    // A previously confirmed Sketch entity suppresses hover updates in the
    // shared viewer.  Every tool transition must release that latch so the
    // common candidate list can immediately offer points, the local origin
    // and both infinite Sketch axes for visual snapping.
    if (viewer_ != nullptr) viewer_->clear_selection();
    viewer_->set_candidate_filter({});
    viewer_->set_transient_edges({});
}

bool AssemblyWorkspaceWindow::confirm_current_sketch_step() {
    return finish_edge_treatment_selection() || finish_sketch_bspline() ||
        finish_sketch_polyline() || finish_sketch_mirror() ||
        finish_sketch_trim();
}

bool AssemblyWorkspaceWindow::finish_current_sketch_tool() {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty()) return false;
    const bool active = sketch_point_active_ || sketch_segment_active_ ||
        sketch_rectangle_active_ || sketch_polygon_active_ || sketch_trim_active_ ||
        sketch_circle_active_ || sketch_mirror_active_ || sketch_arc_active_ ||
        sketch_ellipse_active_ || sketch_elliptical_arc_active_ ||
        sketch_bspline_active_ || sketch_coincident_active_ ||
        sketch_midpoint_active_ || sketch_symmetric_active_ ||
        sketch_concentric_active_ || sketch_tangent_active_ ||
        sketch_segment_pair_active_ || sketch_point_dimension_active_ ||
        sketch_line_pair_dimension_active_ || sketch_corner_fillet_active_;
    if (!active) return false;
    cancel_sketch_segment();
    preserve_view_on_refresh_ = true;
    refresh_scene();
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
        std::set<std::string> construction_axes;
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
            zima::viewer::CandidateKind::SketchSegment});
        const auto owner_id = active_sketch_id_;
        viewer_->set_candidate_filter(
            [owner_id, construction_axes](const auto& candidate) {
                return candidate.owner_id == owner_id &&
                    candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
                    candidate.semantic_key.starts_with("segment:") &&
                    construction_axes.contains(candidate.semantic_key.substr(8));
            });
        viewer_->set_transient_edges({});
        state_->setText(tr(
            "Orientovaný obdélník: vyberte konstrukční čáru jako osu souměrnosti."));
        return true;
    }
    const bool relation_tool = sketch_coincident_active_ ||
        sketch_midpoint_active_ || sketch_symmetric_active_ ||
        sketch_concentric_active_ || sketch_tangent_active_ ||
        sketch_segment_pair_active_ || sketch_point_dimension_active_ ||
        sketch_line_pair_dimension_active_ || sketch_corner_fillet_active_;
    if (relation_tool) {
        const bool has_pending_relation =
            !pending_coincident_point_id_.empty() ||
            !pending_midpoint_point_id_.empty() ||
            !pending_symmetric_point_ids_.empty() ||
            !pending_concentric_geometry_id_.empty() ||
            !pending_tangent_geometry_id_.empty() ||
            !pending_pair_geometry_id_.empty() ||
            !pending_point_dimension_first_id_.empty() ||
            !pending_line_dimension_reference_id_.empty() ||
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
        pending_pair_geometry_id_.clear();
        pending_pair_reference_is_circular_ = false;
        pending_point_dimension_first_id_.clear();
        pending_point_dimension_vertex_id_.clear();
        pending_line_dimension_reference_id_.clear();
        pending_corner_fillet_segment_id_.clear();
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
    sketch_segment_inference_cycle_ = 0;
    sketch_skip_candidate_snap_ = false;
    pending_rectangle_corner_.reset();
    pending_polygon_center_.reset();
    pending_circle_center_.reset();
    pending_arc_center_.reset();
    pending_arc_start_.reset();
    pending_ellipse_center_.reset();
    pending_ellipse_major_.reset();
    pending_elliptical_arc_center_.reset();
    pending_elliptical_arc_major_.reset();
    pending_elliptical_arc_minor_.reset();
    pending_elliptical_arc_start_.reset();
    pending_bspline_points_.clear();
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
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_polygon(unsigned sides) {
    if (properties_dialog_ != nullptr ||
        (sides != 4 && sides != 6 && sides != 8)) return;
    if (active_sketch() == nullptr) return;
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
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
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
            "Ořezání je pouze v náhledu. Pokračujte, prostřední klik vše "
            "potvrdí a Escape obnoví původní skicu."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
}

bool AssemblyWorkspaceWindow::finish_sketch_trim() {
    if (!sketch_trim_active_) return false;
    if (active_sketch() == nullptr || !sketch_trim_preview_) return true;
    if (!sketch_trim_changed_) {
        cancel_sketch_trim();
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(tr("Ořezání ukončeno beze změny."));
        return true;
    }
    try {
        if (!mutate_active_sketch(
                [&](auto& sketch) { sketch = *sketch_trim_preview_; })) return true;
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
    if (active_sketch() == nullptr) return;
    cancel_sketch_segment();
    sketch_arc_active_ = true;
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
    viewer_->set_transient_edges({});
}

void AssemblyWorkspaceWindow::start_sketch_ellipse() {
    if (properties_dialog_ != nullptr) return;
    if (active_sketch() == nullptr) return;
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
    if (active_sketch() == nullptr) return;
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
    if (active_sketch() == nullptr) return;
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
    const auto* sketch = active_sketch();
    if (!sketch_bspline_active_ || sketch == nullptr) {
        return false;
    }
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
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                static_cast<void>(sketch.add_bspline(pending_bspline_points_));
            })) return true;
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
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
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
    const auto* sketch = active_sketch();
    if (!sketch_point_active_ || sketch == nullptr) {
        return false;
    }
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    const auto snap_geometry_id = std::exchange(
        pending_sketch_snap_geometry_id_, {});
    const auto snap_kind = std::exchange(
        pending_sketch_snap_kind_, std::nullopt);
    try {
        bool created = false;
        if (!mutate_active_sketch([&](auto& target) {
                const auto previous_size = target.points.size();
                const auto point_id = target.add_point(
                    (*position)[0], (*position)[1]);
                created = target.points.size() != previous_size;
                if (created && snap_kind && !snap_geometry_id.empty()) {
                    if (*snap_kind ==
                        zima::sketcher::ConstraintKind::Midpoint) {
                        static_cast<void>(target.add_midpoint_constraint(
                            point_id, snap_geometry_id));
                    } else if (*snap_kind ==
                               zima::sketcher::ConstraintKind::Coincident) {
                        static_cast<void>(target.add_coincident_constraint(
                            point_id, snap_geometry_id));
                    } else if (*snap_kind ==
                        zima::sketcher::ConstraintKind::PointOnLine) {
                        const auto separator = snap_geometry_id.find("||");
                        if (separator == std::string::npos) {
                            static_cast<void>(target.add_point_on_line_constraint(
                                point_id, snap_geometry_id));
                        } else {
                            const auto apply_support = [&](const std::string& id) {
                                const auto* point = target.find_point(point_id);
                                if (point != nullptr && target.project_point_to_curve(
                                        id, point->x, point->y)) {
                                    static_cast<void>(
                                        target.add_point_on_circle_constraint(
                                            point_id, id));
                                } else {
                                    static_cast<void>(
                                        target.add_point_on_line_constraint(
                                            point_id, id));
                                }
                            };
                            apply_support(snap_geometry_id.substr(0, separator));
                            apply_support(snap_geometry_id.substr(separator + 2));
                        }
                    } else if (*snap_kind ==
                               zima::sketcher::ConstraintKind::PointOnCircle) {
                        static_cast<void>(target.add_point_on_circle_constraint(
                            point_id, snap_geometry_id));
                    }
                }
            })) return true;
        if (!created) {
            state_->setText(tr("V této poloze již bod skici existuje."));
            return true;
        }
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(tr("Bod vytvořen. Kliknutím můžete vytvořit další."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
    return true;
}

std::optional<AssemblyWorkspaceWindow::SketchCandidateSnap>
AssemblyWorkspaceWindow::sketch_candidate_snap_ray(
    const zima::viewer::ViewerCandidate& candidate,
    const zima::kernel::Vec3& cursor_origin,
    const zima::kernel::Vec3& cursor_direction) const {
    if (candidate.owner_id != active_sketch_id_) return std::nullopt;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return std::nullopt;
    std::optional<std::array<double, 2>> position;
    std::string support_geometry_id;
    std::optional<zima::sketcher::ConstraintKind> relation;
    if (candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
        candidate.semantic_key.starts_with("point:")) {
        if (const auto* point = sketch->find_point(
                candidate.semantic_key.substr(6))) {
            position = std::array{point->x, point->y};
        }
    } else if (candidate.kind ==
                   zima::viewer::CandidateKind::SketchExternalReference &&
               candidate.semantic_key.starts_with("sketch_midpoint:")) {
        support_geometry_id = candidate.semantic_key.substr(16);
        const auto segment = std::find_if(
            sketch->segments.begin(), sketch->segments.end(),
            [&](const auto& value) { return value.id == support_geometry_id; });
        if (segment == sketch->segments.end()) return std::nullopt;
        const auto* first = sketch->find_point(segment->first_point_id);
        const auto* second = sketch->find_point(segment->second_point_id);
        if (first == nullptr || second == nullptr) return std::nullopt;
        position = std::array{
            (first->x + second->x) * 0.5,
            (first->y + second->y) * 0.5};
        relation = zima::sketcher::ConstraintKind::Midpoint;
    } else if (candidate.kind ==
                   zima::viewer::CandidateKind::SketchExternalReference &&
               candidate.semantic_key.starts_with("sketch_intersection:")) {
        support_geometry_id = candidate.semantic_key.substr(20);
        const auto separator = support_geometry_id.find("||");
        if (separator == std::string::npos) return std::nullopt;
        struct SnapLine {
            std::array<double, 2> origin;
            std::array<double, 2> direction;
            bool bounded{};
        };
        const auto line_for = [&](const std::string& line_id)
            -> std::optional<SnapLine> {
            if (line_id == "sketch_axis:x") {
                return SnapLine{{0.0, 0.0}, {1.0, 0.0}, false};
            }
            if (line_id == "sketch_axis:y") {
                return SnapLine{{0.0, 0.0}, {0.0, 1.0}, false};
            }
            const auto segment = std::find_if(
                sketch->segments.begin(), sketch->segments.end(),
                [&](const auto& value) { return value.id == line_id; });
            if (segment == sketch->segments.end()) return std::nullopt;
            const auto* first = sketch->find_point(segment->first_point_id);
            const auto* second = sketch->find_point(segment->second_point_id);
            if (first == nullptr || second == nullptr) return std::nullopt;
            return SnapLine{{first->x, first->y},
                {second->x - first->x, second->y - first->y},
                !segment->construction};
        };
        const auto first_id = support_geometry_id.substr(0, separator);
        const auto second_id = support_geometry_id.substr(separator + 2);
        const auto first = line_for(first_id);
        const auto second = line_for(second_id);
        if (!first) return std::nullopt;
        if (second) {
            const double denominator =
                first->direction[0] * second->direction[1] -
                first->direction[1] * second->direction[0];
            if (std::abs(denominator) <= 1.0e-12) return std::nullopt;
            const double offset_x = second->origin[0] - first->origin[0];
            const double offset_y = second->origin[1] - first->origin[1];
            const double parameter =
                (offset_x * second->direction[1] -
                 offset_y * second->direction[0]) / denominator;
            position = std::array{
                first->origin[0] + parameter * first->direction[0],
                first->origin[1] + parameter * first->direction[1]};
        } else {
            const auto intersections = sketch->curve_line_intersections(
                second_id, first->origin, first->direction, first->bounded);
            if (intersections.empty()) return std::nullopt;
            const auto cursor = sketch->intersect_ray(
                cursor_origin, cursor_direction);
            if (!cursor) return std::nullopt;
            position = *std::min_element(
                intersections.begin(), intersections.end(),
                [&](const auto& left, const auto& right) {
                    return std::hypot(left[0] - (*cursor)[0],
                                      left[1] - (*cursor)[1]) <
                        std::hypot(right[0] - (*cursor)[0],
                                   right[1] - (*cursor)[1]);
                });
        }
        relation = zima::sketcher::ConstraintKind::PointOnLine;
    } else if (candidate.kind ==
                   zima::viewer::CandidateKind::SketchExternalReference &&
               candidate.semantic_key.starts_with("sketch_curve_keypoint:")) {
        constexpr std::string_view prefix{"sketch_curve_keypoint:"};
        const auto payload = candidate.semantic_key.substr(prefix.size());
        const auto first_separator = payload.find(':');
        const auto last_separator = payload.rfind(':');
        if (first_separator == std::string::npos ||
            last_separator == first_separator) return std::nullopt;
        const auto curve_kind = payload.substr(0, first_separator);
        support_geometry_id = payload.substr(
            first_separator + 1, last_separator - first_separator - 1);
        int quarter{};
        try {
            quarter = std::stoi(payload.substr(last_separator + 1));
        } catch (const std::exception&) {
            return std::nullopt;
        }
        if (quarter < 0 || quarter > 3) return std::nullopt;
        const double angle = 0.5 * 3.14159265358979323846 *
            static_cast<double>(quarter);
        std::string center_id;
        double radius{};
        std::optional<std::array<double, 2>> major_vector;
        std::optional<std::array<double, 2>> minor_vector;
        if (curve_kind == "circle") {
            const auto circle = std::find_if(
                sketch->circles.begin(), sketch->circles.end(),
                [&](const auto& value) {
                    return value.id == support_geometry_id;
                });
            if (circle == sketch->circles.end()) return std::nullopt;
            center_id = circle->center_point_id;
            radius = circle->radius;
        } else if (curve_kind == "arc") {
            const auto arc = std::find_if(
                sketch->arcs.begin(), sketch->arcs.end(),
                [&](const auto& value) {
                    return value.id == support_geometry_id;
                });
            if (arc == sketch->arcs.end()) return std::nullopt;
            center_id = arc->center_point_id;
            radius = arc->radius;
        } else if (curve_kind == "ellipse" ||
                   curve_kind == "elliptical_arc") {
            std::string major_id;
            std::string minor_id;
            if (curve_kind == "ellipse") {
                const auto ellipse = std::find_if(
                    sketch->ellipses.begin(), sketch->ellipses.end(),
                    [&](const auto& value) {
                        return value.id == support_geometry_id;
                    });
                if (ellipse == sketch->ellipses.end()) return std::nullopt;
                center_id = ellipse->center_point_id;
                major_id = ellipse->major_point_id;
                minor_id = ellipse->minor_point_id;
            } else {
                const auto arc = std::find_if(
                    sketch->elliptical_arcs.begin(),
                    sketch->elliptical_arcs.end(), [&](const auto& value) {
                        return value.id == support_geometry_id;
                    });
                if (arc == sketch->elliptical_arcs.end()) return std::nullopt;
                center_id = arc->center_point_id;
                major_id = arc->major_point_id;
                minor_id = arc->minor_point_id;
            }
            const auto* center = sketch->find_point(center_id);
            const auto* major = sketch->find_point(major_id);
            const auto* minor = sketch->find_point(minor_id);
            if (center == nullptr || major == nullptr || minor == nullptr)
                return std::nullopt;
            major_vector = std::array{major->x - center->x,
                                      major->y - center->y};
            minor_vector = std::array{minor->x - center->x,
                                      minor->y - center->y};
        } else {
            return std::nullopt;
        }
        const auto* center = sketch->find_point(center_id);
        if (center == nullptr) return std::nullopt;
        position = major_vector && minor_vector
            ? std::array{
                  center->x + (*major_vector)[0] * std::cos(angle) +
                      (*minor_vector)[0] * std::sin(angle),
                  center->y + (*major_vector)[1] * std::cos(angle) +
                      (*minor_vector)[1] * std::sin(angle)}
            : std::array{
                  center->x + radius * std::cos(angle),
                  center->y + radius * std::sin(angle)};
        relation = zima::sketcher::ConstraintKind::PointOnCircle;
    } else if (candidate.kind ==
                   zima::viewer::CandidateKind::SketchExternalReference &&
               candidate.semantic_key.starts_with("external_point:")) {
        const auto reference_id = sketch_external_reference_id_from_key(
            candidate.semantic_key);
        if (!reference_id) return std::nullopt;
        support_geometry_id = *reference_id;
        relation = zima::sketcher::ConstraintKind::Coincident;
        if (*reference_id == "sketch_origin") {
            position = std::array{0.0, 0.0};
        } else {
            const auto reference = std::find_if(sketch->external_references.begin(),
                sketch->external_references.end(), [&](const auto& value) {
                    return value.id == *reference_id &&
                        value.kind == zima::sketcher::ExternalReferenceKind::Point &&
                        value.cached_points.size() == 1;
                });
            if (reference != sketch->external_references.end()) {
                position = reference->cached_points.front();
            }
        }
    } else {
        const auto cursor = sketch->intersect_ray(cursor_origin, cursor_direction);
        if (!cursor) return std::nullopt;
        std::optional<std::pair<std::array<double, 2>, std::array<double, 2>>> line;
        bool finite_line = true;
        if (candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
            candidate.semantic_key.starts_with("segment:")) {
            support_geometry_id = candidate.semantic_key.substr(8);
            const auto segment = std::find_if(
                sketch->segments.begin(), sketch->segments.end(),
                [&](const auto& value) { return value.id == support_geometry_id; });
            if (segment != sketch->segments.end()) {
                const auto* first = sketch->find_point(segment->first_point_id);
                const auto* second = sketch->find_point(segment->second_point_id);
                if (first != nullptr && second != nullptr) {
                    line = std::pair{std::array{first->x, first->y},
                        std::array{second->x - first->x, second->y - first->y}};
                }
            }
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
                   (candidate.semantic_key == "sketch_axis:x" ||
                    candidate.semantic_key == "sketch_axis:y")) {
            support_geometry_id = candidate.semantic_key;
            finite_line = false;
            line = candidate.semantic_key == "sketch_axis:x"
                ? std::pair{std::array{0.0, 0.0}, std::array{1.0, 0.0}}
                : std::pair{std::array{0.0, 0.0}, std::array{0.0, 1.0}};
        } else if (candidate.kind ==
                       zima::viewer::CandidateKind::SketchExternalReference &&
                   (candidate.semantic_key.starts_with("external_edge:") ||
                    candidate.semantic_key.starts_with("external_axis:") ||
                    candidate.semantic_key.starts_with("external_face:"))) {
            const auto reference_id = sketch_external_reference_id_from_key(
                candidate.semantic_key);
            const auto reference = reference_id
                ? std::find_if(sketch->external_references.begin(),
                    sketch->external_references.end(), [&](const auto& value) {
                        return value.id == *reference_id &&
                            (value.kind ==
                                 zima::sketcher::ExternalReferenceKind::Edge ||
                             value.kind ==
                                 zima::sketcher::ExternalReferenceKind::Axis ||
                             value.kind ==
                                 zima::sketcher::ExternalReferenceKind::Face) &&
                            (value.cached_points.size() >= 2 ||
                             !value.cached_paths.empty());
                    })
                : sketch->external_references.end();
            if (reference != sketch->external_references.end()) {
                support_geometry_id = reference->id;
                if (reference->kind ==
                    zima::sketcher::ExternalReferenceKind::Axis) {
                    const auto& first = reference->cached_points.front();
                    const auto& second = reference->cached_points.back();
                    line = std::pair{first,
                        std::array{second[0] - first[0],
                                   second[1] - first[1]}};
                    finite_line = false;
                } else {
                    double closest_distance =
                        std::numeric_limits<double>::infinity();
                    const auto consider_path = [&](const auto& path) {
                    for (std::size_t index = 1; index < path.size(); ++index) {
                        const auto& first = path[index - 1];
                        const auto& second = path[index];
                        const double dx = second[0] - first[0];
                        const double dy = second[1] - first[1];
                        const double length_squared = dx * dx + dy * dy;
                        if (length_squared <= 1.0e-18) continue;
                        const double parameter = std::clamp(
                            (((*cursor)[0] - first[0]) * dx +
                             ((*cursor)[1] - first[1]) * dy) /
                                length_squared,
                            0.0, 1.0);
                        const std::array candidate_position{
                            first[0] + parameter * dx,
                            first[1] + parameter * dy};
                        const double distance = std::hypot(
                            (*cursor)[0] - candidate_position[0],
                            (*cursor)[1] - candidate_position[1]);
                        if (distance < closest_distance) {
                            closest_distance = distance;
                            position = candidate_position;
                        }
                    }
                    };
                    if (reference->kind ==
                        zima::sketcher::ExternalReferenceKind::Face) {
                        for (const auto& path : reference->cached_paths)
                            consider_path(path);
                    } else {
                        consider_path(reference->cached_points);
                    }
                    if (position) {
                        relation =
                            zima::sketcher::ConstraintKind::PointOnLine;
                    }
                }
            }
        }
        if (line) {
            const double length_squared =
                line->second[0] * line->second[0] +
                line->second[1] * line->second[1];
            if (length_squared <= 1.0e-18) return std::nullopt;
            double parameter =
                (((*cursor)[0] - line->first[0]) * line->second[0] +
                 ((*cursor)[1] - line->first[1]) * line->second[1]) /
                length_squared;
            if (finite_line) parameter = std::clamp(parameter, 0.0, 1.0);
            position = std::array{
                line->first[0] + parameter * line->second[0],
                line->first[1] + parameter * line->second[1]};
            relation = zima::sketcher::ConstraintKind::PointOnLine;
        } else if (candidate.kind == zima::viewer::CandidateKind::SketchCurve) {
            for (const auto prefix : {std::string_view{"circle:"},
                     std::string_view{"arc:"}, std::string_view{"ellipse:"},
                     std::string_view{"elliptical_arc:"},
                     std::string_view{"bspline:"}}) {
                if (candidate.semantic_key.starts_with(prefix)) {
                    support_geometry_id =
                        candidate.semantic_key.substr(prefix.size());
                    break;
                }
            }
            if (support_geometry_id.empty()) return std::nullopt;
            position = sketch->project_point_to_curve(
                support_geometry_id, (*cursor)[0], (*cursor)[1]);
            if (!position) return std::nullopt;
            relation = zima::sketcher::ConstraintKind::PointOnCircle;
        }
    }
    if (!position) return std::nullopt;
    auto origin = sketch->world_point((*position)[0], (*position)[1]);
    zima::kernel::Vec3 direction;
    switch (sketch->plane) {
    case zima::sketcher::SketchPlane::XY:
        origin.z += 1.0;
        direction = {0.0, 0.0, -1.0};
        break;
    case zima::sketcher::SketchPlane::XZ:
        origin.y += 1.0;
        direction = {0.0, -1.0, 0.0};
        break;
    case zima::sketcher::SketchPlane::YZ:
        origin.x += 1.0;
        direction = {-1.0, 0.0, 0.0};
        break;
    }
    return SketchCandidateSnap{
        origin, direction, std::move(support_geometry_id), relation};
}

bool AssemblyWorkspaceWindow::accept_sketch_external_snap(
    const zima::viewer::ViewerCandidate& candidate,
    const zima::kernel::Vec3& origin,
    const zima::kernel::Vec3& direction) {
    if (!(sketch_point_active_ || sketch_segment_active_ ||
          sketch_rectangle_active_ || sketch_polygon_active_ ||
          sketch_circle_active_ || sketch_arc_active_ || sketch_ellipse_active_ ||
          sketch_elliptical_arc_active_ || sketch_bspline_active_)) return false;
    const auto snap = sketch_candidate_snap_ray(candidate, origin, direction);
    if (!snap) return false;
    pending_sketch_snap_geometry_id_ = snap->support_geometry_id;
    pending_sketch_snap_kind_ = snap->relation;
    if (accept_sketch_point_ray(snap->origin, snap->direction)) return true;
    if (accept_sketch_segment_ray(snap->origin, snap->direction)) return true;
    pending_sketch_snap_geometry_id_.clear();
    pending_sketch_snap_kind_.reset();
    if (accept_sketch_rectangle_ray(snap->origin, snap->direction)) return true;
    if (accept_sketch_polygon_ray(snap->origin, snap->direction)) return true;
    if (accept_sketch_circle_ray(snap->origin, snap->direction)) return true;
    if (accept_sketch_arc_ray(snap->origin, snap->direction)) return true;
    if (accept_sketch_ellipse_ray(snap->origin, snap->direction)) return true;
    if (accept_sketch_elliptical_arc_ray(snap->origin, snap->direction)) return true;
    return accept_sketch_bspline_ray(snap->origin, snap->direction);
}

bool AssemblyWorkspaceWindow::accept_sketch_segment_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    const auto* sketch = active_sketch();
    if (!sketch_segment_active_ || sketch == nullptr) {
        return false;
    }
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_segment_start_) {
        pending_segment_start_ = *position;
        pending_segment_start_snap_geometry_id_ = std::exchange(
            pending_sketch_snap_geometry_id_, {});
        pending_segment_start_snap_kind_ = std::exchange(
            pending_sketch_snap_kind_, std::nullopt);
        state_->setText(sketch_polyline_active_
            ? tr("Lomená čára: určete další bod.")
            : sketch_segment_construction_
                ? tr("Konstrukční čára: určete druhý bod.")
                : tr("Úsečka skici: určete druhý bod. Escape příkaz zruší."));
        return true;
    }
    auto inferred_end = inferred_sketch_segment_end(*position);
    auto confirmed_position = inferred_end.position;
    auto direction_inference = inferred_end.kind;
    const auto end_snap_geometry_id = std::exchange(
        pending_sketch_snap_geometry_id_, {});
    const auto end_snap_kind = std::exchange(
        pending_sketch_snap_kind_, std::nullopt);
    if (end_snap_kind) {
        confirmed_position = *position;
        direction_inference.reset();
        inferred_end.reference_point_id.clear();
        inferred_end.equal_length_reference_id.clear();
        inferred_end.symmetry_axis_id.clear();
        inferred_end.tangent_reference_id.clear();
        inferred_end.perpendicular_reference_id.clear();
        inferred_end.parallel_reference_id.clear();
        inferred_end.midpoint_line_reference_id.clear();
    }
    if (sketch_polyline_active_ && sketch_polyline_arc_mode_) {
        confirmed_position = *position;
        direction_inference.reset();
    }
    const double dx = confirmed_position[0] - (*pending_segment_start_)[0];
    const double dy = confirmed_position[1] - (*pending_segment_start_)[1];
    if (std::hypot(dx, dy) <= 1.0e-9) {
        state_->setText(tr("Úsečka musí mít nenulovou délku."));
        return true;
    }
    std::string created_geometry_id;
    if (!mutate_active_sketch([&](auto& target) {
            if (sketch_polyline_active_ && sketch_polyline_arc_mode_) {
                const auto start = std::find_if(
                    target.points.begin(), target.points.end(), [&](const auto& point) {
                        return std::hypot(
                            point.x - (*pending_segment_start_)[0],
                            point.y - (*pending_segment_start_)[1]) <= 1.0e-6;
                    });
                if (start == target.points.end() ||
                    pending_polyline_tangent_geometry_id_.empty()) {
                    throw std::runtime_error(
                        "Tangent polyline arc has no connected start geometry");
                }
                created_geometry_id = target.add_tangent_arc(
                    start->id, confirmed_position[0], confirmed_position[1],
                    pending_polyline_tangent_geometry_id_);
            } else {
                created_geometry_id = target.add_segment(
                    (*pending_segment_start_)[0], (*pending_segment_start_)[1],
                    confirmed_position[0], confirmed_position[1], 1.0e-6,
                    sketch_segment_construction_);
                if (sketch_segment_construction_) {
                    target.set_segment_centerline(created_geometry_id, true);
                }
                const auto segment = std::find_if(
                    target.segments.begin(), target.segments.end(),
                    [&](const auto& value) {
                        return value.id == created_geometry_id;
                    });
                if (segment == target.segments.end()) {
                    throw std::runtime_error(
                        "Created Sketch segment cannot be resolved");
                }
                const auto first_point_id = segment->first_point_id;
                const auto second_point_id = segment->second_point_id;
                const auto apply_snap = [&](const std::string& point_id,
                        const std::string& geometry_id,
                        const auto& kind) {
                    if (!kind || geometry_id.empty()) return;
                    try {
                        if (*kind ==
                            zima::sketcher::ConstraintKind::Midpoint) {
                            static_cast<void>(target.add_midpoint_constraint(
                                point_id, geometry_id));
                        } else if (*kind ==
                                   zima::sketcher::ConstraintKind::Coincident) {
                            static_cast<void>(target.add_coincident_constraint(
                                point_id, geometry_id));
                        } else if (*kind ==
                                   zima::sketcher::ConstraintKind::PointOnLine) {
                            const auto separator = geometry_id.find("||");
                            if (separator == std::string::npos) {
                                static_cast<void>(target.add_point_on_line_constraint(
                                    point_id, geometry_id));
                            } else {
                                const auto apply_support = [&](const std::string& id) {
                                    const auto* point = target.find_point(point_id);
                                    if (point != nullptr && target.project_point_to_curve(
                                            id, point->x, point->y)) {
                                        static_cast<void>(
                                            target.add_point_on_circle_constraint(
                                                point_id, id));
                                    } else {
                                        static_cast<void>(
                                            target.add_point_on_line_constraint(
                                                point_id, id));
                                    }
                                };
                                apply_support(geometry_id.substr(0, separator));
                                apply_support(geometry_id.substr(separator + 2));
                            }
                        } else if (*kind ==
                                   zima::sketcher::ConstraintKind::PointOnCircle) {
                            static_cast<void>(target.add_point_on_circle_constraint(
                                point_id, geometry_id));
                        }
                    } catch (const std::invalid_argument&) {
                        // A shared endpoint can already own the same support.
                    }
                };
                apply_snap(first_point_id,
                    pending_segment_start_snap_geometry_id_,
                    pending_segment_start_snap_kind_);
                apply_snap(second_point_id,
                    end_snap_geometry_id, end_snap_kind);
                if (!inferred_end.symmetry_axis_id.empty()) {
                    static_cast<void>(target.add_symmetric_constraint(
                        first_point_id, second_point_id,
                        inferred_end.symmetry_axis_id));
                }
                if (!inferred_end.tangent_reference_id.empty()) {
                    static_cast<void>(target.add_tangent_constraint(
                        inferred_end.tangent_reference_id,
                        created_geometry_id));
                }
                if (!inferred_end.perpendicular_reference_id.empty()) {
                    static_cast<void>(target.add_segment_pair_constraint(
                        inferred_end.perpendicular_reference_id,
                        created_geometry_id,
                        zima::sketcher::ConstraintKind::Perpendicular));
                }
                if (!inferred_end.parallel_reference_id.empty()) {
                    static_cast<void>(target.add_segment_pair_constraint(
                        inferred_end.parallel_reference_id,
                        created_geometry_id,
                        zima::sketcher::ConstraintKind::Parallel));
                }
                if (!inferred_end.midpoint_line_reference_id.empty()) {
                    static_cast<void>(target.add_midpoint_on_line_constraint(
                        created_geometry_id,
                        inferred_end.midpoint_line_reference_id));
                }
                if (direction_inference) {
                    try {
                        static_cast<void>(target.add_point_pair_constraint(
                            inferred_end.reference_point_id.empty()
                                ? first_point_id
                                : inferred_end.reference_point_id,
                            second_point_id,
                            *direction_inference));
                    } catch (const std::invalid_argument&) {
                        // Shared/coincident endpoints can already determine
                        // this direction. In that case the inferred H/V would
                        // be redundant and must not be persisted a second time.
                    }
                }
                if (!inferred_end.equal_length_reference_id.empty()) {
                    try {
                        static_cast<void>(target.add_segment_pair_constraint(
                            inferred_end.equal_length_reference_id,
                            created_geometry_id,
                            zima::sketcher::ConstraintKind::EqualLength));
                    } catch (const std::invalid_argument&) {
                        // A previously implied equality must not be stored a
                        // second time merely because it was also offered by
                        // the interactive inference.
                    }
                }
            }
        })) return true;
    preserve_view_on_refresh_ = true;
    if (sketch_polyline_active_) {
        pending_segment_start_ = confirmed_position;
        pending_polyline_tangent_geometry_id_ = created_geometry_id;
    }
    else pending_segment_start_.reset();
    pending_segment_start_snap_geometry_id_.clear();
    pending_segment_start_snap_kind_.reset();
    sketch_segment_inference_cycle_ = 0;
    sketch_skip_candidate_snap_ = false;
    viewer_->set_transient_edges({});
    refresh_tabs();
    refresh_scene();
    state_->setText(sketch_polyline_active_
        ? sketch_polyline_arc_mode_
            ? tr("Tečný oblouk vytvořen. Určete konec dalšího oblouku, RMB přepne úsečku.")
            : tr("Úsek lomené čáry vytvořen. Určete další bod; RMB přepne tečný oblouk.")
        : sketch_segment_construction_
            ? tr("Konstrukční čára vytvořena. Určete první bod další čáry.")
            : tr("Úsečka vytvořena. Kliknutím určete první bod další úsečky."));
    return true;
}

AssemblyWorkspaceWindow::SketchSegmentInference
AssemblyWorkspaceWindow::inferred_sketch_segment_end(
    const std::array<double, 2>& position) const {
    if (!pending_segment_start_ || viewer_ == nullptr) {
        return {position, std::nullopt, {}};
    }
    const double dx = std::abs(position[0] - (*pending_segment_start_)[0]);
    const double dy = std::abs(position[1] - (*pending_segment_start_)[1]);
    const double tolerance = viewer_->world_tolerance_for_pixels(
        9.0 * viewer_->devicePixelRatioF());
    std::vector<std::pair<double, SketchSegmentInference>> point_alignments;
    std::vector<std::pair<double, SketchSegmentInference>> directions;
    std::vector<std::pair<double, SketchSegmentInference>> symmetries;
    std::vector<SketchSegmentInference> tangencies;
    std::vector<SketchSegmentInference> perpendiculars;
    std::vector<std::pair<double, SketchSegmentInference>> parallels;
    std::vector<std::pair<double, SketchSegmentInference>> midpoint_lines;
    // Alignment to another persisted point outranks direction from the
    // segment's own first point. The perpendicular screen distance selects
    // the closest guide, while the other coordinate remains cursor-driven.
    if (const auto* sketch = active_sketch()) {
        if (pending_segment_start_snap_kind_ ==
                zima::sketcher::ConstraintKind::PointOnCircle &&
            !pending_segment_start_snap_geometry_id_.empty()) {
            if (const auto tangent = sketch->curve_tangent_at_point(
                    pending_segment_start_snap_geometry_id_,
                    (*pending_segment_start_)[0], (*pending_segment_start_)[1])) {
                    const double tangent_x = (*tangent)[0];
                    const double tangent_y = (*tangent)[1];
                    const double cursor_x =
                        position[0] - (*pending_segment_start_)[0];
                    const double cursor_y =
                        position[1] - (*pending_segment_start_)[1];
                    double along = cursor_x * tangent_x + cursor_y * tangent_y;
                    if (std::abs(along) <= 1.0e-9) {
                        along = std::copysign(
                            std::max(std::hypot(cursor_x, cursor_y), tolerance),
                            along == 0.0 ? 1.0 : along);
                    }
                    SketchSegmentInference tangent_candidate{{
                        (*pending_segment_start_)[0] + along * tangent_x,
                        (*pending_segment_start_)[1] + along * tangent_y},
                        std::nullopt, {}};
                    tangent_candidate.tangent_reference_id =
                        pending_segment_start_snap_geometry_id_;
                    tangencies.push_back(std::move(tangent_candidate));
            }
        }
        if (pending_segment_start_snap_kind_ ==
                zima::sketcher::ConstraintKind::PointOnLine &&
            !pending_segment_start_snap_geometry_id_.empty()) {
            std::optional<std::array<double, 2>> support_direction;
            const auto support_id = pending_segment_start_snap_geometry_id_;
            if (support_id == "sketch_axis:x") {
                support_direction = std::array{1.0, 0.0};
            } else if (support_id == "sketch_axis:y") {
                support_direction = std::array{0.0, 1.0};
            } else if (const auto segment = std::find_if(
                    sketch->segments.begin(), sketch->segments.end(),
                    [&](const auto& value) { return value.id == support_id; });
                segment != sketch->segments.end()) {
                const auto* first = sketch->find_point(segment->first_point_id);
                const auto* second = sketch->find_point(segment->second_point_id);
                support_direction = std::array{
                    second->x - first->x, second->y - first->y};
            } else if (const auto reference = std::find_if(
                    sketch->external_references.begin(),
                    sketch->external_references.end(),
                    [&](const auto& value) {
                        return value.id == support_id &&
                            (value.kind ==
                                 zima::sketcher::ExternalReferenceKind::Edge ||
                             value.kind ==
                                 zima::sketcher::ExternalReferenceKind::Axis) &&
                            value.cached_points.size() >= 2;
                    }); reference != sketch->external_references.end()) {
                if (reference->kind ==
                    zima::sketcher::ExternalReferenceKind::Axis) {
                    support_direction = std::array{
                        reference->cached_points.back()[0] -
                            reference->cached_points.front()[0],
                        reference->cached_points.back()[1] -
                            reference->cached_points.front()[1]};
                } else {
                    const auto& first = reference->cached_points.front();
                    const auto& last = reference->cached_points.back();
                    const double dx = last[0] - first[0];
                    const double dy = last[1] - first[1];
                    const double length = std::hypot(dx, dy);
                    bool straight = length > 1.0e-12;
                    for (const auto& point : reference->cached_points) {
                        const double deviation = std::abs(
                            (point[0] - first[0]) * dy -
                            (point[1] - first[1]) * dx) /
                            std::max(length, 1.0e-12);
                        if (deviation > 1.0e-8) {
                            straight = false;
                            break;
                        }
                    }
                    if (straight) support_direction = std::array{dx, dy};
                }
            }
            if (support_direction) {
                const double length = std::hypot(
                    (*support_direction)[0], (*support_direction)[1]);
                if (length > 1.0e-12) {
                    const double normal_x = -(*support_direction)[1] / length;
                    const double normal_y = (*support_direction)[0] / length;
                    const double cursor_x =
                        position[0] - (*pending_segment_start_)[0];
                    const double cursor_y =
                        position[1] - (*pending_segment_start_)[1];
                    double along = cursor_x * normal_x + cursor_y * normal_y;
                    if (std::abs(along) <= 1.0e-9) {
                        along = std::copysign(
                            std::max(std::hypot(cursor_x, cursor_y), tolerance),
                            along == 0.0 ? 1.0 : along);
                    }
                    SketchSegmentInference candidate{{
                        (*pending_segment_start_)[0] + along * normal_x,
                        (*pending_segment_start_)[1] + along * normal_y},
                        std::nullopt, {}};
                    candidate.perpendicular_reference_id = support_id;
                    perpendiculars.push_back(std::move(candidate));
                }
            }
        }
        const zima::sketcher::SketchPoint* horizontal_reference = nullptr;
        const zima::sketcher::SketchPoint* vertical_reference = nullptr;
        double horizontal_distance = tolerance;
        double vertical_distance = tolerance;
        for (const auto& point : sketch->points) {
            if (std::hypot(point.x - (*pending_segment_start_)[0],
                           point.y - (*pending_segment_start_)[1]) <= 1.0e-9) {
                continue;
            }
            const double h_distance = std::abs(position[1] - point.y);
            const double v_distance = std::abs(position[0] - point.x);
            if (h_distance <= horizontal_distance) {
                horizontal_distance = h_distance;
                horizontal_reference = &point;
            }
            if (v_distance <= vertical_distance) {
                vertical_distance = v_distance;
                vertical_reference = &point;
            }
        }
        if (horizontal_reference != nullptr) {
            auto aligned = position;
            aligned[1] = horizontal_reference->y;
            point_alignments.push_back({horizontal_distance,
                {aligned, zima::sketcher::ConstraintKind::Horizontal,
                 horizontal_reference->id}});
        }
        if (vertical_reference != nullptr) {
            auto aligned = position;
            aligned[0] = vertical_reference->x;
            point_alignments.push_back({vertical_distance,
                {aligned, zima::sketcher::ConstraintKind::Vertical,
                 vertical_reference->id}});
        }
        for (const auto& axis : sketch->segments) {
            if (!axis.construction) continue;
            const auto* first = sketch->find_point(axis.first_point_id);
            const auto* second = sketch->find_point(axis.second_point_id);
            if (first == nullptr || second == nullptr) continue;
            const double ax = second->x - first->x;
            const double ay = second->y - first->y;
            const double length_squared = ax * ax + ay * ay;
            if (length_squared <= 1.0e-18) continue;
            const double parameter =
                (((*pending_segment_start_)[0] - first->x) * ax +
                 ((*pending_segment_start_)[1] - first->y) * ay) /
                length_squared;
            const double foot_x = first->x + parameter * ax;
            const double foot_y = first->y + parameter * ay;
            const std::array mirrored{
                2.0 * foot_x - (*pending_segment_start_)[0],
                2.0 * foot_y - (*pending_segment_start_)[1]};
            const double distance = std::hypot(
                position[0] - mirrored[0], position[1] - mirrored[1]);
            if (distance <= tolerance) {
                SketchSegmentInference candidate{mirrored, std::nullopt, {}};
                candidate.symmetry_axis_id = axis.id;
                symmetries.push_back({distance, std::move(candidate)});
            }
        }
        const double cursor_x = position[0] - (*pending_segment_start_)[0];
        const double cursor_y = position[1] - (*pending_segment_start_)[1];
        const auto offer_midpoint_on_line = [&](const std::string& reference_id,
                const std::array<double, 2>& first,
                const std::array<double, 2>& direction, bool finite) {
            const double squared = direction[0] * direction[0] +
                direction[1] * direction[1];
            if (squared <= 1.0e-18) return;
            const std::array cursor_midpoint{
                ((*pending_segment_start_)[0] + position[0]) * 0.5,
                ((*pending_segment_start_)[1] + position[1]) * 0.5};
            double parameter =
                ((cursor_midpoint[0] - first[0]) * direction[0] +
                 (cursor_midpoint[1] - first[1]) * direction[1]) / squared;
            if (finite && (parameter < 0.0 || parameter > 1.0)) return;
            const std::array foot{
                first[0] + parameter * direction[0],
                first[1] + parameter * direction[1]};
            const double distance = std::hypot(
                cursor_midpoint[0] - foot[0], cursor_midpoint[1] - foot[1]);
            if (distance > tolerance) return;
            SketchSegmentInference candidate{{
                2.0 * foot[0] - (*pending_segment_start_)[0],
                2.0 * foot[1] - (*pending_segment_start_)[1]},
                std::nullopt, {}};
            candidate.midpoint_line_reference_id = reference_id;
            midpoint_lines.push_back({distance, std::move(candidate)});
        };
        offer_midpoint_on_line(
            "sketch_axis:x", {0.0, 0.0}, {1.0, 0.0}, false);
        offer_midpoint_on_line(
            "sketch_axis:y", {0.0, 0.0}, {0.0, 1.0}, false);
        for (const auto& reference : sketch->segments) {
            const auto* first = sketch->find_point(reference.first_point_id);
            const auto* second = sketch->find_point(reference.second_point_id);
            if (first == nullptr || second == nullptr) continue;
            const double rx = second->x - first->x;
            const double ry = second->y - first->y;
            offer_midpoint_on_line(reference.id, {first->x, first->y},
                {rx, ry}, !reference.centerline);
            const double length = std::hypot(rx, ry);
            if (length <= 1.0e-12) continue;
            const double ux = rx / length;
            const double uy = ry / length;
            const double along = cursor_x * ux + cursor_y * uy;
            const double normal_distance = std::abs(cursor_x * uy - cursor_y * ux);
            if (std::abs(along) <= 1.0e-9 || normal_distance > tolerance) continue;
            SketchSegmentInference candidate{{
                (*pending_segment_start_)[0] + along * ux,
                (*pending_segment_start_)[1] + along * uy},
                std::nullopt, {}};
            candidate.parallel_reference_id = reference.id;
            parallels.push_back({normal_distance, std::move(candidate)});
        }
    }
    if (dy <= tolerance) {
        auto aligned = position;
        aligned[1] = (*pending_segment_start_)[1];
        directions.push_back({dy,
            {aligned, zima::sketcher::ConstraintKind::Horizontal, {}}});
    }
    if (dx <= tolerance) {
        auto aligned = position;
        aligned[0] = (*pending_segment_start_)[0];
        directions.push_back({dx,
            {aligned, zima::sketcher::ConstraintKind::Vertical, {}}});
    }
    const auto by_distance = [](const auto& left, const auto& right) {
        return left.first < right.first;
    };
    std::ranges::sort(point_alignments, by_distance);
    std::ranges::sort(directions, by_distance);
    std::ranges::sort(symmetries, by_distance);
    std::ranges::sort(parallels, by_distance);
    std::ranges::sort(midpoint_lines, by_distance);
    struct EqualCandidate {
        double difference{};
        double length{};
        std::string geometry_id;
    };
    const auto equal_candidates_for = [&](const std::array<double, 2>& endpoint) {
        std::vector<EqualCandidate> result;
        const auto* sketch = active_sketch();
        if (sketch == nullptr) return result;
        const double requested = std::hypot(
            endpoint[0] - (*pending_segment_start_)[0],
            endpoint[1] - (*pending_segment_start_)[1]);
        if (requested <= 1.0e-12) return result;
        for (const auto& segment : sketch->segments) {
            const auto* first = sketch->find_point(segment.first_point_id);
            const auto* second = sketch->find_point(segment.second_point_id);
            if (first == nullptr || second == nullptr) continue;
            const double length = std::hypot(
                second->x - first->x, second->y - first->y);
            const double difference = std::abs(length - requested);
            if (length > 1.0e-12 && difference <= tolerance) {
                result.push_back({difference, length, segment.id});
            }
        }
        std::ranges::sort(result, [](const auto& left, const auto& right) {
            return left.difference < right.difference;
        });
        return result;
    };
    const auto exact_equal = [&](SketchSegmentInference candidate,
            const EqualCandidate& equal) {
        const double vx = candidate.position[0] - (*pending_segment_start_)[0];
        const double vy = candidate.position[1] - (*pending_segment_start_)[1];
        const double length = std::hypot(vx, vy);
        if (length > 1.0e-12) {
            candidate.position = {
                (*pending_segment_start_)[0] + vx * equal.length / length,
                (*pending_segment_start_)[1] + vy * equal.length / length};
        }
        candidate.equal_length_reference_id = equal.geometry_id;
        return candidate;
    };
    std::vector<SketchSegmentInference> variants;
    const auto* current_sketch = active_sketch();
    variants.reserve(point_alignments.size() + directions.size() +
        tangencies.size() + perpendiculars.size() + symmetries.size() +
        parallels.size() + midpoint_lines.size() +
        (current_sketch == nullptr ? 0 : current_sketch->segments.size()) *
            (directions.size() + 1) + 1);
    for (auto& candidate : tangencies) {
        variants.push_back(std::move(candidate));
    }
    for (auto& candidate : perpendiculars) {
        variants.push_back(std::move(candidate));
    }
    for (auto& candidate : midpoint_lines) {
        variants.push_back(std::move(candidate.second));
    }
    for (auto& candidate : point_alignments) {
        variants.push_back(std::move(candidate.second));
    }
    for (auto& candidate : symmetries) {
        variants.push_back(std::move(candidate.second));
    }
    for (auto& candidate : directions) {
        for (const auto& equal : equal_candidates_for(candidate.second.position)) {
            variants.push_back(exact_equal(candidate.second, equal));
        }
        variants.push_back(std::move(candidate.second));
    }
    for (auto& candidate : parallels) {
        for (const auto& equal : equal_candidates_for(candidate.second.position)) {
            variants.push_back(exact_equal(candidate.second, equal));
        }
        variants.push_back(std::move(candidate.second));
    }
    SketchSegmentInference free_candidate{position, std::nullopt, {}};
    for (const auto& equal : equal_candidates_for(position)) {
        variants.push_back(exact_equal(free_candidate, equal));
    }
    variants.push_back(std::move(free_candidate));
    auto selected = variants[sketch_segment_inference_cycle_ % variants.size()];
    selected.variant_count = variants.size();
    return selected;
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
                      zima::viewer::CandidateKind::SketchExternalReference}
        : std::vector{zima::viewer::CandidateKind::SketchPoint});
    state_->setText(!pending_coincident_point_id_.empty()
        ? kind == zima::sketcher::ConstraintKind::Horizontal
            ? tr("Vodorovnost bodů: vyberte řízený bod.")
            : kind == zima::sketcher::ConstraintKind::Vertical
                ? tr("Svislost bodů: vyberte řízený bod.")
                : tr("Shodnost bodů: vyberte druhý bod.")
        : kind == zima::sketcher::ConstraintKind::Horizontal
            ? tr("Vodorovnost bodů: vyberte první bod. Escape příkaz zruší.")
            : kind == zima::sketcher::ConstraintKind::Vertical
                ? tr("Svislost bodů: vyberte první bod. Escape příkaz zruší.")
                : tr("Shodnost bodů: vyberte první bod. Escape příkaz zruší."));
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
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                static_cast<void>(sketch.add_concentric_constraint(
                    pending_concentric_geometry_id_, geometry_id));
            })) return;
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
    const auto curve_pair_candidate = [](const auto& candidate) {
        return candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
            (candidate.semantic_key.starts_with("circle:") ||
             candidate.semantic_key.starts_with("arc:") ||
             candidate.semantic_key.starts_with("ellipse:") ||
             candidate.semantic_key.starts_with("elliptical_arc:"));
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
         curve_pair_candidate](const auto& candidate) {
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
                // Analytic conics support curve-to-curve tangency. A B-spline
                // currently has a persisted local tangent and therefore
                // supports the unambiguous line-to-curve relation only.
                supports_curve_pair = prefix != std::string_view{"bspline:"};
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
        state_->setText(is_segment
            ? tr("Tečná vazba: vyberte řízenou kružnici, oblouk, elipsu, "
                 "eliptický oblouk nebo B-spline.")
            : supports_curve_pair
                ? tr("Tečná vazba: vyberte řízenou úsečku, kružnici, oblouk, "
                     "elipsu nebo eliptický oblouk.")
                : tr("Tečná vazba: vyberte řízenou úsečku."));
        return;
    }
    if (geometry_id == pending_tangent_geometry_id_) {
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
    if (!sketch_coincident_active_ || candidate.owner_id != active_sketch_id_) return;
    std::string point_id;
    std::string support_geometry_id;
    bool circular_support = false;
    if (candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
        candidate.semantic_key.starts_with("point:")) {
        point_id = candidate.semantic_key.substr(6);
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
            "Shodnost: vyberte druhý bod, úsečku, křivku nebo externí přímku."));
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
                    static_cast<void>(sketch.add_coincident_constraint(
                        pending_coincident_point_id_, point_id));
                } else {
                    static_cast<void>(sketch.add_point_pair_constraint(
                        pending_coincident_point_id_, point_id,
                        pending_point_pair_constraint_kind_));
                }
            })) return;
        const auto completed_kind = pending_point_pair_constraint_kind_;
        pending_coincident_point_id_.clear();
        viewer_->set_candidate_filter({});
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(completed_kind == zima::sketcher::ConstraintKind::Horizontal
            ? tr("Vodorovnost bodů vytvořena. Vyberte referenční bod další vazby.")
            : completed_kind == zima::sketcher::ConstraintKind::Vertical
                ? tr("Svislost bodů vytvořena. Vyberte referenční bod další vazby.")
                : tr("Vazba shodnosti vytvořena. Vyberte první bod další vazby."));
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
        ? tr("Rovnoběžnost: vyberte referenční úsečku. Escape příkaz zruší.")
        : kind == zima::sketcher::ConstraintKind::Perpendicular
            ? tr("Kolmost: vyberte referenční úsečku. Escape příkaz zruší.")
            : tr("Stejné: vyberte referenční úsečku, kružnici nebo oblouk. "
                 "Escape příkaz zruší."));
}

void AssemblyWorkspaceWindow::set_sketch_pair_contract() {
    const bool equal = pending_pair_kind_ ==
        zima::sketcher::ConstraintKind::EqualLength;
    if (!equal && pending_pair_geometry_id_.empty()) {
        viewer_->set_selection_contract({
            zima::viewer::CandidateKind::SketchSegment,
            zima::viewer::CandidateKind::SketchExternalReference});
    } else if (!equal || (!pending_pair_geometry_id_.empty() &&
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
            const bool external_direction = !equal && pending_geometry_id.empty() &&
                candidate.kind ==
                    zima::viewer::CandidateKind::SketchExternalReference &&
                (candidate.semantic_key.starts_with("external_edge:") ||
                 candidate.semantic_key.starts_with("external_axis:"));
            const bool circular =
                candidate.kind == zima::viewer::CandidateKind::SketchCurve &&
                (candidate.semantic_key.starts_with("circle:") ||
                 candidate.semantic_key.starts_with("arc:"));
            if ((!equal && !segment && !external_direction) ||
                (equal && !segment && !circular)) {
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
    } else if (pending_pair_geometry_id_.empty() &&
               pending_pair_kind_ != zima::sketcher::ConstraintKind::EqualLength &&
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
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                if (pending_pair_reference_is_circular_) {
                    static_cast<void>(sketch.add_equal_radius_constraint(
                        pending_pair_geometry_id_, geometry_id));
                } else {
                    static_cast<void>(sketch.add_segment_pair_constraint(
                        pending_pair_geometry_id_, geometry_id, pending_pair_kind_));
                }
            })) return;
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
    const auto point_id = candidate.semantic_key.substr(6);
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
    const auto* point = sketch->find_point(point_id);
    if (point == nullptr || point->fixed) {
        state_->setText(tr("Fixovaný bod nelze táhnout."));
        return false;
    }
    if (const auto* part = workspace_.open_part(workspace_.active_document_id())) {
        sketch_drag_document_ = part->session.document();
    } else if (const auto* assembly =
                   workspace_.open_assembly(workspace_.active_document_id())) {
        assembly_sketch_drag_document_ = assembly->session.document();
    } else {
        return false;
    }
    sketch_drag_point_id_ = point_id;
    sketch_drag_changed_ = false;
    return true;
}

void AssemblyWorkspaceWindow::update_sketch_point_drag(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_drag_document_ && !assembly_sketch_drag_document_) return;
    auto& sketches = sketch_drag_document_
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
    if (next_sketch == sketches.end() || !next_sketch->move_point(
            sketch_drag_point_id_, (*position)[0], (*position)[1])) {
        state_->setText(tr("Bod nelze přesunout mimo vazby nebo absolutní meze."));
        return;
    }
    sketch_drag_changed_ = true;
    zima::kernel::ViewerMesh display;
    if (const auto* part = workspace_.open_part(workspace_.active_document_id())) {
        const auto& calculated = part->session.calculated_boundaries();
        if (!calculated.empty()) display = calculated.back().mesh;
    } else if (assembly_sketch_drag_document_) {
        display = assembly_sketch_drag_document_->build_scene();
    }
    for (const auto& sketch : sketches) {
        append_mesh(display, sketch.viewer_mesh());
    }
    viewer_->set_mesh(std::move(display), false);
    state_->setText(tr("Tažení bodu: poloha je pouze transientní do puštění LMB."));
}

void AssemblyWorkspaceWindow::end_sketch_point_drag() {
    if (!sketch_drag_document_ && !assembly_sketch_drag_document_) return;
    sketch_drag_point_id_.clear();
    const bool changed = sketch_drag_changed_;
    sketch_drag_changed_ = false;
    if (changed) {
        if (auto* part = workspace_.open_part(workspace_.active_document_id());
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
    refresh_scene();
    state_->setText(changed ? tr("Poloha bodu byla uložena jako jedna revize.")
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
    if (row.mate_type != zima::assembly::MateKind::PlaneCoincident &&
        row.mate_type != zima::assembly::MateKind::AxisAngle &&
        row.mate_type != zima::assembly::MateKind::PlaneAngle) return false;
    zima::kernel::Vec3 reference_point;
    zima::kernel::Vec3 reference_direction;
    if (row.mate_type == zima::assembly::MateKind::AxisAngle) {
        const auto target =
            assembly->session.document().resolve_axis(row.target_reference);
        if (target.status != zima::assembly::MateStatus::Valid) return false;
        reference_point = target.axis.point;
        reference_direction = target.axis.direction;
    } else {
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
        row.mate_type == zima::assembly::MateKind::AxisAngle ||
        row.mate_type == zima::assembly::MateKind::PlaneAngle;
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
    row.offset = value;
    placement_reference_drag_document_->calculate_placement_references();
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

bool AssemblyWorkspaceWindow::begin_sketch_dimension_drag(
    const zima::viewer::ViewerCandidate& candidate) {
    if (properties_dialog_ != nullptr || active_sketch_id_.empty() ||
        candidate.kind != zima::viewer::CandidateKind::Dimension ||
        candidate.owner_id != active_sketch_id_ ||
        !candidate.semantic_key.starts_with("dimension:")) return false;
    const auto dimension_id = candidate.semantic_key.substr(10);
    const auto* sketch = active_sketch();
    if (sketch == nullptr || std::none_of(
            sketch->dimensions.begin(), sketch->dimensions.end(),
            [&](const auto& value) { return value.id == dimension_id; })) return false;
    if (const auto* part = workspace_.open_part(workspace_.active_document_id())) {
        sketch_drag_document_ = part->session.document();
    } else if (const auto* assembly =
                   workspace_.open_assembly(workspace_.active_document_id())) {
        assembly_sketch_drag_document_ = assembly->session.document();
    } else {
        return false;
    }
    sketch_drag_dimension_id_ = dimension_id;
    sketch_drag_changed_ = false;
    return true;
}

void AssemblyWorkspaceWindow::update_sketch_dimension_drag(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (sketch_drag_dimension_id_.empty() ||
        (!sketch_drag_document_ && !assembly_sketch_drag_document_)) return;
    auto& sketches = sketch_drag_document_
        ? sketch_drag_document_->sketches
        : assembly_sketch_drag_document_->sketches;
    const auto sketch = std::find_if(sketches.begin(), sketches.end(),
        [&](const auto& value) { return value.id == active_sketch_id_; });
    if (sketch == sketches.end()) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position || !sketch->set_dimension_placement(
            sketch_drag_dimension_id_, (*position)[0], (*position)[1])) return;
    sketch_drag_changed_ = true;
    zima::kernel::ViewerMesh display;
    if (const auto* part = workspace_.open_part(workspace_.active_document_id())) {
        const auto& calculated = part->session.calculated_boundaries();
        if (!calculated.empty()) display = calculated.back().mesh;
    } else if (assembly_sketch_drag_document_) {
        display = assembly_sketch_drag_document_->build_scene();
    }
    for (const auto& value : sketches) append_mesh(display, value.viewer_mesh());
    viewer_->set_mesh(std::move(display), false);
    state_->setText(tr("Tažení kóty: umístění se uloží po puštění LMB."));
}

void AssemblyWorkspaceWindow::end_sketch_dimension_drag() {
    if (sketch_drag_dimension_id_.empty()) return;
    sketch_drag_dimension_id_.clear();
    const bool changed = sketch_drag_changed_;
    sketch_drag_changed_ = false;
    if (changed) {
        if (auto* part = workspace_.open_part(workspace_.active_document_id());
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
    refresh_scene();
}

bool AssemblyWorkspaceWindow::begin_component_drag(
    const zima::viewer::ViewerCandidate& candidate,
    const zima::kernel::Vec3& ray_origin,
    const zima::kernel::Vec3& ray_direction) {
    // Mirrors Python's `_on_insertion_origin_dragged` guard: free-component
    // drag is only offered while the ComponentPropertiesDialog for exactly
    // this occurrence is open (`dialog is None or not dialog.isVisible()`).
    if (properties_dialog_ == nullptr ||
        candidate.kind != zima::viewer::CandidateKind::Occurrence ||
        candidate.instance_path != properties_dialog_instance_path_ ||
        placement_reference_drag_document_ ||
        sketch_drag_document_ ||
        assembly_sketch_drag_document_) return false;
    zima::assembly::InstancePath path;
    try {
        path = zima::assembly::InstancePath::decode(candidate.instance_path);
    } catch (const std::invalid_argument&) {
        return false;
    }
    const auto address = workspace_.resolve_occurrence(
        workspace_.displayed_document_id(), path);
    // Free-drag currently supports occurrences owned directly by the
    // displayed top-level Assembly, where scene-space translation equals the
    // occurrence's own placement delta without an intermediate parent
    // transform.
    if (!address || address->owner_assembly_document_id !=
            workspace_.displayed_document_id()) return false;
    auto* assembly = workspace_.open_assembly(address->owner_assembly_document_id);
    if (assembly == nullptr) return false;
    const auto* occurrence =
        assembly->session.document().find_occurrence(address->occurrence_id);
    if (occurrence == nullptr || occurrence->grounded) return false;
    component_drag_document_ = assembly->session.document();
    component_drag_document_id_ = address->owner_assembly_document_id;
    component_drag_occurrence_id_ = address->occurrence_id;
    component_drag_start_local_origin_ =
        {occurrence->placement.x, occurrence->placement.y, occurrence->placement.z};
    component_drag_plane_point_ = component_drag_start_local_origin_;
    const double ray_length = std::sqrt(ray_direction.x * ray_direction.x +
        ray_direction.y * ray_direction.y + ray_direction.z * ray_direction.z);
    if (ray_length <= 1.0e-12) {
        component_drag_document_.reset();
        return false;
    }
    component_drag_plane_normal_ = {ray_direction.x / ray_length,
        ray_direction.y / ray_length, ray_direction.z / ray_length};
    const zima::kernel::Vec3 press_to_plane{
        component_drag_plane_point_.x - ray_origin.x,
        component_drag_plane_point_.y - ray_origin.y,
        component_drag_plane_point_.z - ray_origin.z};
    const double press_t = press_to_plane.x * component_drag_plane_normal_.x +
        press_to_plane.y * component_drag_plane_normal_.y +
        press_to_plane.z * component_drag_plane_normal_.z;
    component_drag_start_hit_ = {
        ray_origin.x + press_t * component_drag_plane_normal_.x,
        ray_origin.y + press_t * component_drag_plane_normal_.y,
        ray_origin.z + press_t * component_drag_plane_normal_.z};
    component_drag_changed_ = false;
    state_->setText(tr("Tažením přesouváte komponentu; polohu potvrdí dialog Vlastnosti."));
    return true;
}

void AssemblyWorkspaceWindow::update_component_drag(
    const zima::kernel::Vec3& ray_origin,
    const zima::kernel::Vec3& ray_direction) {
    if (!component_drag_document_) return;
    const double direction_length = std::sqrt(
        ray_direction.x * ray_direction.x + ray_direction.y * ray_direction.y +
        ray_direction.z * ray_direction.z);
    if (direction_length <= 1.0e-12) return;
    const zima::kernel::Vec3 direction{ray_direction.x / direction_length,
        ray_direction.y / direction_length, ray_direction.z / direction_length};
    // Intersects the drag ray with the camera-facing plane through the
    // component's starting position, matching Python's screen-delta ->
    // world-delta translation for an orthographic camera.
    const zima::kernel::Vec3 to_plane{
        component_drag_plane_point_.x - ray_origin.x,
        component_drag_plane_point_.y - ray_origin.y,
        component_drag_plane_point_.z - ray_origin.z};
    const double denominator = direction.x * component_drag_plane_normal_.x +
        direction.y * component_drag_plane_normal_.y +
        direction.z * component_drag_plane_normal_.z;
    if (std::abs(denominator) <= 1.0e-12) return;
    const double t = (to_plane.x * component_drag_plane_normal_.x +
        to_plane.y * component_drag_plane_normal_.y +
        to_plane.z * component_drag_plane_normal_.z) / denominator;
    const zima::kernel::Vec3 hit{ray_origin.x + t * direction.x,
        ray_origin.y + t * direction.y, ray_origin.z + t * direction.z};
    const zima::kernel::Vec3 new_origin{
        component_drag_start_local_origin_.x + (hit.x - component_drag_start_hit_.x),
        component_drag_start_local_origin_.y + (hit.y - component_drag_start_hit_.y),
        component_drag_start_local_origin_.z + (hit.z - component_drag_start_hit_.z)};
    auto* occurrence = component_drag_document_->find_occurrence(component_drag_occurrence_id_);
    if (occurrence == nullptr) return;
    occurrence->placement.x = new_origin.x;
    occurrence->placement.y = new_origin.y;
    occurrence->placement.z = new_origin.z;
    component_drag_changed_ = true;
    viewer_->set_mesh(component_drag_document_->build_scene(), false);
    if (auto* dialog = dynamic_cast<ComponentPropertiesDialog*>(properties_dialog_);
        dialog != nullptr && dialog->occurrence_id() == component_drag_occurrence_id_) {
        dialog->set_live_translation(new_origin.x, new_origin.y, new_origin.z);
    }
    state_->setText(tr("Poloha komponenty: X %1  Y %2  Z %3 mm")
        .arg(new_origin.x, 0, 'f', 3).arg(new_origin.y, 0, 'f', 3)
        .arg(new_origin.z, 0, 'f', 3));
}

void AssemblyWorkspaceWindow::end_component_drag() {
    if (!component_drag_document_) return;
    const std::string document_id = component_drag_document_id_;
    auto result = std::move(*component_drag_document_);
    const bool changed = component_drag_changed_;
    component_drag_document_.reset();
    component_drag_document_id_.clear();
    component_drag_occurrence_id_.clear();
    component_drag_changed_ = false;
    if (changed) {
        if (auto* assembly = workspace_.open_assembly(document_id)) {
            assembly->session.commit(std::move(result));
        }
    }
    refresh_tabs();
    refresh_scene();
}

void AssemblyWorkspaceWindow::clear_selected_sketch_geometry() {
    selected_sketch_segment_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_text_id_.clear();
    selected_sketch_external_reference_id_.clear();
    selected_sketch_point_id_.clear();
    for (auto* action : {
             sketch_radius_dimension_action_, sketch_diameter_dimension_action_,
             sketch_ellipse_major_dimension_action_,
             sketch_ellipse_minor_dimension_action_,
             sketch_ellipse_rotation_dimension_action_,
             sketch_fix_point_action_}) {
        if (action != nullptr) action->setEnabled(false);
    }
    const bool sketch_tools_available = !active_sketch_id_.empty();
    for (auto* action : {
             sketch_horizontal_action_, sketch_vertical_action_,
             sketch_dimension_action_, sketch_dimension_x_action_,
             sketch_dimension_y_action_, sketch_point_line_dimension_action_,
             sketch_symmetric_dimension_action_,
             sketch_three_point_angle_dimension_action_,
             sketch_angle_dimension_action_}) {
        if (action != nullptr) action->setEnabled(sketch_tools_available);
    }
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
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                if (!geometry_id.empty()) sketch.remove_geometry(geometry_id);
                else sketch.remove_point(selected_sketch_point_id_);
            })) return false;
        workspace_.synchronize_external_sketch_dependencies();
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

void AssemblyWorkspaceWindow::set_active_sketch_geometry_construction(
    const std::string& geometry_id, bool construction) {
    if (active_sketch_id_.empty() || geometry_id.empty()) return;
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                sketch.set_geometry_construction(geometry_id, construction);
            })) return;
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(construction
            ? tr("Geometrie byla změněna na pomocnou konstrukční geometrii.")
            : tr("Geometrie byla vrácena do obrysu profilu."));
    } catch (const std::exception& error) {
        state_->setText(QString::fromUtf8(error.what()));
    }
}

void AssemblyWorkspaceWindow::remove_sketch_relation(
    const std::string& sketch_id, const std::string& relation_id,
    bool dimension) {
    if (properties_dialog_ != nullptr || sketch_id.empty() || relation_id.empty()) return;
    if (sketch_id != active_sketch_id_ || active_sketch() == nullptr) return;
    try {
        if (!mutate_active_sketch([&](auto& sketch) {
                if (dimension) sketch.remove_dimension(relation_id);
                else sketch.remove_constraint(relation_id);
            })) return;
        workspace_.synchronize_external_sketch_dependencies();
        preserve_view_on_refresh_ = true;
        refresh_tabs();
        refresh_scene();
        state_->setText(dimension
            ? tr("Kóta byla odstraněna. Operaci lze vrátit přes Zpět.")
            : tr("Vazba byla odstraněna. Operaci lze vrátit přes Zpět."));
    } catch (const std::exception& error) {
        QMessageBox::warning(this,
            dimension ? tr("Kótu nelze odstranit") : tr("Vazbu nelze odstranit"),
            error.what());
    }
}

void AssemblyWorkspaceWindow::toggle_part_container_suppressed(
    const std::string& container_id) {
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || properties_dialog_ != nullptr) return;
    try {
        auto next = part->session.document();
        auto* container = next.find_container(container_id);
        if (container != nullptr) {
            container->suppressed = !container->suppressed;
        } else if (auto* construction = next.find_construction(container_id)) {
            construction->suppressed = !construction->suppressed;
        } else {
            const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
                [&](const auto& value) { return value.id == container_id; });
            if (sketch == next.sketches.end()) return;
            sketch->suppressed = !sketch->suppressed;
        }
        const auto& previous = part->session.calculated_boundaries();
        auto calculated = calculate_part(next, &previous);
        next.resolve_constructions(calculated.empty()
            ? zima::kernel::ViewerReferenceGeometry{}
            : calculated.back().mesh.original_references);
        static_cast<void>(refresh_sketch_external_references(next, calculated));
        part->session.commit(std::move(next), std::move(calculated));
        refresh_tabs();
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Potlačení nelze změnit"), error.what());
    }
}

void AssemblyWorkspaceWindow::move_part_container(
    const std::string& container_id, int direction) {
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || properties_dialog_ != nullptr ||
        (direction != -1 && direction != 1)) return;
    try {
        auto next = part->session.document();
        const auto order = std::find_if(next.history_order.begin(),
            next.history_order.end(), [&](const auto& entry) {
                return entry.id == container_id;
            });
        if (order == next.history_order.end()) return;
        const auto order_index = static_cast<std::size_t>(
            std::distance(next.history_order.begin(), order));
        if ((direction < 0 && order_index == 0) ||
            (direction > 0 && order_index + 1 >= next.history_order.size())) return;
        const auto order_target = static_cast<std::size_t>(
            static_cast<std::ptrdiff_t>(order_index) + direction);
        std::swap(next.history_order[order_index], next.history_order[order_target]);
        std::vector<zima::document::HistoryContainer> reordered;
        reordered.reserve(next.history.size());
        for (const auto& entry : next.history_order) {
            if (entry.kind != zima::document::PartHistoryKind::Feature) continue;
            const auto feature = std::find_if(next.history.begin(), next.history.end(),
                [&](const auto& value) { return value.id == entry.id; });
            if (feature != next.history.end()) reordered.push_back(*feature);
        }
        next.history = std::move(reordered);
        auto calculated = calculate_part(next);
        next.resolve_constructions(calculated.empty()
            ? zima::kernel::ViewerReferenceGeometry{}
            : calculated.back().mesh.original_references);
        static_cast<void>(refresh_sketch_external_references(next, calculated));
        part->session.commit(std::move(next), std::move(calculated));
        refresh_tabs();
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Pořadí nelze změnit"), error.what());
    }
}

void AssemblyWorkspaceWindow::delete_part_object(
    const std::string& object_id, const QString& kind) {
    if (properties_dialog_ != nullptr || object_id.empty()) return;
    if (QMessageBox::question(this, tr("Odstranit objekt"),
            tr("Opravdu chcete vybraný objekt odstranit?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
        QMessageBox::Yes) return;
    try {
        if (kind == QStringLiteral("assembly-cut")) {
            auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
            if (assembly == nullptr) return;
            const auto assembly_id = assembly->session.document().document_id;
            workspace_.regenerate_assembly_from_open_dependencies(assembly_id);
            assembly = workspace_.open_assembly(assembly_id);
            if (assembly == nullptr) return;
            auto next = assembly->session.document();
            std::erase_if(next.cuts, [&](const auto& cut) {
                return cut.definition.id == object_id;
            });
            calculate_assembly_cuts(next);
            assembly->session.commit(std::move(next));
        } else if (kind == QStringLiteral("assembly-sketch")) {
            auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
            if (assembly == nullptr) return;
            auto next = assembly->session.document();
            if (std::any_of(next.cuts.begin(), next.cuts.end(), [&](const auto& cut) {
                    const auto& definition = cut.definition;
                    return (definition.feature_kind ==
                                zima::document::FeatureKind::Extrusion &&
                            definition.extrusion.sketch_id == object_id) ||
                        (definition.feature_kind ==
                                zima::document::FeatureKind::Revolution &&
                            definition.revolution.sketch_id == object_id);
                })) {
                throw std::runtime_error("Assembly sketch is still used by a cut");
            }
            std::erase_if(next.sketches,
                [&](const auto& sketch) { return sketch.id == object_id; });
            assembly->session.commit(std::move(next));
        } else if (kind == QStringLiteral("assembly-construction")) {
            auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
            if (assembly == nullptr) return;
            auto next = assembly->session.document();
            const auto document_states = workspace_.documents();
            const bool used_by_external_sketch = std::any_of(
                document_states.begin(), document_states.end(),
                [&](const auto& state) {
                    const auto* part = std::get_if<zima::workspace::PartState>(&state);
                    return part != nullptr && std::any_of(
                        part->session.document().sketches.begin(),
                        part->session.document().sketches.end(), [&](const auto& sketch) {
                            return std::any_of(sketch.external_references.begin(),
                                sketch.external_references.end(), [&](const auto& reference) {
                                    return reference.source_document_id == next.document_id &&
                                        reference.source_owner_id == object_id;
                                });
                        });
                });
            if (std::any_of(next.components.begin(), next.components.end(), [&](const auto& component) {
                    return std::any_of(component.placement_references.begin(),
                        component.placement_references.end(), [&](const auto& row) {
                            return row.component_reference.owner_id == object_id ||
                                row.target_reference.owner_id == object_id;
                        });
                }) || std::any_of(next.constructions.begin(), next.constructions.end(),
                    [&](const auto& construction) {
                        return construction.id != object_id &&
                            std::any_of(construction.references.begin(),
                                construction.references.end(), [&](const auto& reference) {
                                    return reference.owner_id == object_id;
                                });
                    }) || used_by_external_sketch) {
                throw std::runtime_error(
                    "Assembly datum is still used by a placement reference or another datum");
            }
            const auto old_size = next.constructions.size();
            std::erase_if(next.constructions,
                [&](const auto& object) { return object.id == object_id; });
            if (next.constructions.size() == old_size) return;
            next.resolve_constructions();
            next.calculate_placement_references();
            assembly->session.commit(std::move(next));
        } else {
            auto* part = workspace_.open_part(workspace_.active_document_id());
            if (part == nullptr) return;
            auto next = part->session.document();
            if (kind == QStringLiteral("sketch")) {
                if (std::any_of(next.history.begin(), next.history.end(),
                        [&](const auto& container) {
                            return (container.feature_kind ==
                                        zima::document::FeatureKind::Extrusion &&
                                    container.extrusion.sketch_id == object_id) ||
                                (container.feature_kind ==
                                        zima::document::FeatureKind::Revolution &&
                                    container.revolution.sketch_id == object_id);
                        })) {
                    throw std::runtime_error("Sketch is still used by a feature");
                }
                std::erase_if(next.sketches,
                    [&](const auto& sketch) { return sketch.id == object_id; });
            } else if (kind == QStringLiteral("part-construction")) {
                const bool used_by_datum = std::any_of(next.constructions.begin(),
                    next.constructions.end(), [&](const auto& construction) {
                        return construction.id != object_id &&
                            std::any_of(construction.references.begin(),
                                construction.references.end(), [&](const auto& reference) {
                                    return reference.owner_id == object_id;
                                });
                    });
                const bool used_by_feature = std::any_of(next.history.begin(),
                    next.history.end(), [&](const auto& container) {
                        return container.feature_kind ==
                                zima::document::FeatureKind::Extrusion &&
                            container.extrusion.target_face.owner_id == object_id;
                    });
                const bool used_by_sketch = std::any_of(next.sketches.begin(),
                    next.sketches.end(), [&](const auto& sketch) {
                        return std::any_of(sketch.external_references.begin(),
                            sketch.external_references.end(), [&](const auto& reference) {
                                return reference.source_document_id == next.document_id &&
                                    reference.source_owner_id == object_id;
                            });
                    });
                if (used_by_datum || used_by_feature || used_by_sketch) {
                    throw std::runtime_error(
                        "Construction object is still used by another definition");
                }
                std::erase_if(next.constructions,
                    [&](const auto& object) { return object.id == object_id; });
            } else {
                std::erase_if(next.history,
                    [&](const auto& container) { return container.id == object_id; });
            }
            const auto deleted_order = std::find_if(next.history_order.begin(),
                next.history_order.end(),
                [&](const auto& entry) { return entry.id == object_id; });
            const auto deleted_index = deleted_order == next.history_order.end()
                ? next.history_order.size()
                : static_cast<std::size_t>(std::distance(
                    next.history_order.begin(), deleted_order));
            const auto cursor_before_delete = next.effective_history_cursor();
            std::erase_if(next.history_order,
                [&](const auto& entry) { return entry.id == object_id; });
            next.set_history_cursor(cursor_before_delete -
                (deleted_index < cursor_before_delete ? 1U : 0U));
            auto calculated = calculate_part(next);
            next.resolve_constructions(calculated.empty()
                ? zima::kernel::ViewerReferenceGeometry{}
                : calculated.back().mesh.original_references);
            static_cast<void>(refresh_sketch_external_references(next, calculated));
            part->session.commit(std::move(next), std::move(calculated));
            if (active_sketch_id_ == object_id) active_sketch_id_.clear();
            if (selected_sketch_id_ == object_id) selected_sketch_id_.clear();
        }
        refresh_tabs();
        preserve_view_on_refresh_ = true;
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Objekt nelze odstranit"), error.what());
    }
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
    const auto first_id = selected_sketch_point_id_;
    cancel_sketch_segment();
    sketch_point_dimension_active_ = true;
    pending_point_dimension_first_id_ = first_id;
    pending_point_dimension_vertex_id_.clear();
    pending_point_dimension_kind_ = kind;
    const bool point_to_line =
        kind == zima::sketcher::DimensionKind::DistancePointLine ||
        kind == zima::sketcher::DimensionKind::DistanceSymmetric;
    viewer_->set_selection_contract(first_id.empty()
        ? std::vector{zima::viewer::CandidateKind::SketchPoint}
        : point_to_line
        ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                      zima::viewer::CandidateKind::SketchAxis,
                      zima::viewer::CandidateKind::SketchExternalReference}
        : kind == zima::sketcher::DimensionKind::AngleThreePoint
        ? std::vector{zima::viewer::CandidateKind::SketchPoint}
        : kind == zima::sketcher::DimensionKind::Distance
        ? std::vector{zima::viewer::CandidateKind::SketchPoint,
                      zima::viewer::CandidateKind::SketchExternalReference}
        : std::vector{zima::viewer::CandidateKind::SketchPoint,
                      zima::viewer::CandidateKind::SketchExternalReference,
                      zima::viewer::CandidateKind::SketchAxis});
    const auto owner_id = active_sketch_id_;
    viewer_->set_candidate_filter([owner_id, first_id, kind](const auto& candidate) {
        if (candidate.owner_id != owner_id) return false;
        if (first_id.empty()) {
            return candidate.kind == zima::viewer::CandidateKind::SketchPoint &&
                candidate.semantic_key.starts_with("point:");
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
            return (kind == zima::sketcher::DimensionKind::DistanceX &&
                    candidate.semantic_key == "sketch_axis:y") ||
                (kind == zima::sketcher::DimensionKind::DistanceY &&
                 candidate.semantic_key == "sketch_axis:x");
        }
        return candidate.kind ==
                zima::viewer::CandidateKind::SketchExternalReference &&
            candidate.semantic_key.starts_with("external_point:");
    });
    state_->setText(first_id.empty()
        ? tr("Kóta: vyberte první bod. Escape příkaz zruší.")
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
    if (!sketch_point_dimension_active_ ||
        candidate.owner_id != active_sketch_id_) return;
    if (!pending_point_dimension_second_id_.empty()) {
        if (pending_point_dimension_cursor_) {
            finish_pending_linear_dimension(*pending_point_dimension_cursor_);
        }
        return;
    }
    if (pending_point_dimension_first_id_.empty()) {
        if (candidate.kind != zima::viewer::CandidateKind::SketchPoint ||
            !candidate.semantic_key.starts_with("point:")) return;
        pending_point_dimension_first_id_ = candidate.semantic_key.substr(6);
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
               ((pending_point_dimension_kind_ ==
                    zima::sketcher::DimensionKind::DistanceX &&
                 candidate.semantic_key == "sketch_axis:y") ||
                (pending_point_dimension_kind_ ==
                    zima::sketcher::DimensionKind::DistanceY &&
                 candidate.semantic_key == "sketch_axis:x"))) {
        second_id = "sketch_origin";
    }
    if (second_id.empty() || second_id == pending_point_dimension_first_id_) return;
    if (pending_point_dimension_kind_ ==
            zima::sketcher::DimensionKind::Distance) {
        pending_point_dimension_second_id_ = second_id;
        pending_point_dimension_cursor_.reset();
        viewer_->set_candidate_filter({});
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
    const auto* first = sketch->find_point(pending_point_dimension_first_id_);
    const auto* second = sketch->find_point(pending_point_dimension_second_id_);
    auto kind = zima::sketcher::DimensionKind::Distance;
    if (first != nullptr && second != nullptr) {
        kind = zima::sketcher::classify_linear_dimension(
            {first->x, first->y}, {second->x, second->y}, cursor);
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
        ? std::vector{zima::viewer::CandidateKind::SketchSegment}
        : kind ==
            zima::sketcher::DimensionKind::AngleBetween
        ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                      zima::viewer::CandidateKind::SketchAxis}
        : std::vector{zima::viewer::CandidateKind::SketchSegment});
    const auto owner_id = active_sketch_id_;
    viewer_->set_candidate_filter([owner_id, reference_id, kind](const auto& candidate) {
        if (candidate.owner_id != owner_id) return false;
        if (candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
            candidate.semantic_key.starts_with("segment:")) {
            return candidate.semantic_key.substr(8) != reference_id;
        }
        return !reference_id.empty() &&
            kind == zima::sketcher::DimensionKind::AngleBetween &&
            candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
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
        if (selected_axis) return;
        pending_line_dimension_reference_id_ = selected_id;
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(pending_line_dimension_kind_ ==
                zima::sketcher::DimensionKind::AngleBetween
            ? tr("Úhel: vyberte řízenou úsečku nebo osu skici.")
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
        sketch_mirror_active_ || sketch_circle_active_ || sketch_arc_active_ ||
        sketch_ellipse_active_ || sketch_elliptical_arc_active_ ||
        sketch_bspline_active_ ||
        sketch_coincident_active_ || sketch_midpoint_active_ ||
        sketch_symmetric_active_ || sketch_concentric_active_ ||
        sketch_tangent_active_ ||
        sketch_segment_pair_active_ || sketch_point_dimension_active_ ||
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
    if (!edit_mode && placement) initial.placement = *placement;
    const bool segment_dimension_creation = !edit_mode &&
        !selected_sketch_segment_id_.empty() && first_point_id.empty() &&
        first_geometry_id.empty();
    auto* dialog = new SketchDimensionPropertiesDialog(
        std::move(initial), edit_mode,
        [this, sketch_id, edit_mode, creation_kind, active_target,
         segment_dimension_creation](
            zima::sketcher::SketchDimension committed) {
            const auto apply = [&](auto& target) {
                const auto found = std::find_if(target.sketches.begin(),
                    target.sketches.end(), [&](const auto& value) {
                        return value.id == sketch_id;
                    });
                if (found == target.sketches.end()) return false;
                found->apply_dimension(committed);
                found->validate();
                return true;
            };
            bool applied{};
            if (active_target) {
                applied = active_sketch_id_ == sketch_id &&
                    mutate_active_sketch([&](auto& target) {
                        target.apply_dimension(committed);
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
                    pending_point_dimension_kind_ =
                        creation_kind == zima::sketcher::DimensionKind::DistanceX ||
                        creation_kind == zima::sketcher::DimensionKind::DistanceY
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
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) {
        viewer_->set_transient_edges({});
        return;
    }
    if (sketch_polyline_active_ && sketch_polyline_arc_mode_ &&
        !pending_polyline_tangent_geometry_id_.empty()) {
        try {
            auto preview = *sketch;
            const auto start = std::find_if(
                preview.points.begin(), preview.points.end(), [&](const auto& point) {
                    return std::hypot(
                        point.x - (*pending_segment_start_)[0],
                        point.y - (*pending_segment_start_)[1]) <= 1.0e-6;
                });
            if (start == preview.points.end()) {
                viewer_->set_transient_edges({});
                return;
            }
            const auto arc_id = preview.add_tangent_arc(
                start->id, (*position)[0], (*position)[1],
                pending_polyline_tangent_geometry_id_);
            auto mesh = preview.viewer_mesh();
            const auto edge = std::find_if(mesh.edges.begin(), mesh.edges.end(),
                [&](const auto& value) {
                    return value.reference.semantic_key == "arc:" + arc_id;
                });
            if (edge != mesh.edges.end()) {
                auto transient = *edge;
                transient.reference = {};
                viewer_->set_transient_edges({std::move(transient)});
                return;
            }
        } catch (const std::exception&) {
            viewer_->set_transient_edges({});
            return;
        }
    }
    const auto inference = inferred_sketch_segment_end(*position);
    sketch_segment_inference_variant_count_ = inference.variant_count;
    const auto& preview_position = inference.position;
    const auto active_point = sketch->world_point(
        preview_position[0], preview_position[1]);
    std::vector<zima::kernel::ViewerEdge> preview_edges{{{
        sketch->world_point((*pending_segment_start_)[0], (*pending_segment_start_)[1]),
        active_point}, {}}};
    if (sketch_segment_construction_) {
        preview_edges.front().construction = true;
        preview_edges.front().overlay = true;
        preview_edges.front().infinite = true;
        preview_edges.front().dash_dot = true;
    }
    if (!inference.equal_length_reference_id.empty()) {
        const auto reference = std::find_if(
            sketch->segments.begin(), sketch->segments.end(),
            [&](const auto& value) {
                return value.id == inference.equal_length_reference_id;
            });
        if (reference != sketch->segments.end()) {
            const auto* first = sketch->find_point(reference->first_point_id);
            const auto* second = sketch->find_point(reference->second_point_id);
            preview_edges.push_back({{
                sketch->world_point(first->x, first->y),
                sketch->world_point(second->x, second->y)},
                {active_sketch_id_, "inference:reference", {}}});
        }
    }
    if (!inference.symmetry_axis_id.empty()) {
        const auto axis = std::find_if(
            sketch->segments.begin(), sketch->segments.end(),
            [&](const auto& value) {
                return value.id == inference.symmetry_axis_id;
            });
        if (axis != sketch->segments.end()) {
            const auto* first = sketch->find_point(axis->first_point_id);
            const auto* second = sketch->find_point(axis->second_point_id);
            preview_edges.push_back({{
                sketch->world_point(first->x, first->y),
                sketch->world_point(second->x, second->y)},
                {active_sketch_id_, "inference:reference", {}}});
        }
    }
    if (!inference.tangent_reference_id.empty()) {
        const auto source_mesh = sketch->viewer_mesh();
        const auto curve = std::find_if(
            source_mesh.edges.begin(), source_mesh.edges.end(),
            [&](const auto& value) {
                return value.reference.semantic_key ==
                        "circle:" + inference.tangent_reference_id ||
                    value.reference.semantic_key ==
                        "arc:" + inference.tangent_reference_id ||
                    value.reference.semantic_key ==
                        "ellipse:" + inference.tangent_reference_id ||
                    value.reference.semantic_key ==
                        "elliptical_arc:" + inference.tangent_reference_id ||
                    value.reference.semantic_key ==
                        "bspline:" + inference.tangent_reference_id;
            });
        if (curve != source_mesh.edges.end()) {
            auto reference = *curve;
            reference.reference = {
                active_sketch_id_, "inference:reference", {}};
            preview_edges.push_back(std::move(reference));
        }
    }
    if (!inference.perpendicular_reference_id.empty()) {
        const auto source_mesh = sketch->viewer_mesh();
        const auto support_key = inference.perpendicular_reference_id ==
                "sketch_axis:x" ||
                inference.perpendicular_reference_id == "sketch_axis:y"
            ? inference.perpendicular_reference_id
            : std::any_of(sketch->segments.begin(), sketch->segments.end(),
                  [&](const auto& value) {
                      return value.id == inference.perpendicular_reference_id;
                  })
                ? "segment:" + inference.perpendicular_reference_id
                : "external_edge:" + inference.perpendicular_reference_id;
        if (const auto support = std::find_if(
                source_mesh.edges.begin(), source_mesh.edges.end(),
                [&](const auto& value) {
                    return value.reference.semantic_key == support_key ||
                        value.reference.semantic_key ==
                            "external_axis:" +
                                inference.perpendicular_reference_id;
                }); support != source_mesh.edges.end()) {
            auto reference = *support;
            reference.reference = {
                active_sketch_id_, "inference:reference", {}};
            preview_edges.push_back(std::move(reference));
        } else if (const auto axis = std::find_if(
                source_mesh.axes.begin(), source_mesh.axes.end(),
                [&](const auto& value) {
                    return value.reference.semantic_key == support_key;
                }); axis != source_mesh.axes.end()) {
            const double half = axis->display_length * 0.5;
            preview_edges.push_back({{
                {axis->point.x - axis->direction.x * half,
                 axis->point.y - axis->direction.y * half,
                 axis->point.z - axis->direction.z * half},
                {axis->point.x + axis->direction.x * half,
                 axis->point.y + axis->direction.y * half,
                 axis->point.z + axis->direction.z * half}},
                {active_sketch_id_, "inference:reference", {}}});
        }
    }
    if (!inference.parallel_reference_id.empty()) {
        const auto reference = std::find_if(
            sketch->segments.begin(), sketch->segments.end(),
            [&](const auto& value) {
                return value.id == inference.parallel_reference_id;
            });
        if (reference != sketch->segments.end()) {
            const auto* first = sketch->find_point(reference->first_point_id);
            const auto* second = sketch->find_point(reference->second_point_id);
            preview_edges.push_back({{
                sketch->world_point(first->x, first->y),
                sketch->world_point(second->x, second->y)},
                {active_sketch_id_, "inference:reference", {}}});
        }
    }
    if (!inference.midpoint_line_reference_id.empty()) {
        const auto source_mesh = sketch->viewer_mesh();
        const auto support_key = inference.midpoint_line_reference_id ==
                "sketch_axis:x" ||
                inference.midpoint_line_reference_id == "sketch_axis:y"
            ? inference.midpoint_line_reference_id
            : "segment:" + inference.midpoint_line_reference_id;
        if (const auto support = std::find_if(source_mesh.edges.begin(),
                source_mesh.edges.end(), [&](const auto& value) {
                    return value.reference.semantic_key == support_key;
                }); support != source_mesh.edges.end()) {
            auto reference = *support;
            reference.reference = {
                active_sketch_id_, "inference:reference", {}};
            preview_edges.push_back(std::move(reference));
        }
    }
    viewer_->set_transient_edges(std::move(preview_edges));
    viewer_->set_transient_points({active_point});
    std::string marker;
    if (!sketch_skip_candidate_snap_) {
        if (const auto candidate = viewer_->hovered_candidate(); candidate &&
            sketch_candidate_snap_ray(*candidate, origin, direction)) {
            marker = "C";
        }
    }
    if (marker.empty() && inference.kind ==
               zima::sketcher::ConstraintKind::Horizontal) {
        marker = "H";
    } else if (marker.empty() && inference.kind ==
               zima::sketcher::ConstraintKind::Vertical) {
        marker = "V";
    }
    if (!inference.symmetry_axis_id.empty()) {
        const double local_dx = preview_position[0] - (*pending_segment_start_)[0];
        const double local_dy = preview_position[1] - (*pending_segment_start_)[1];
        const double marker_tolerance = viewer_->world_tolerance_for_pixels(
            1.5 * viewer_->devicePixelRatioF());
        marker = std::abs(local_dy) <= marker_tolerance ? "S  H"
            : std::abs(local_dx) <= marker_tolerance ? "S  V" : "S  ⊥";
    }
    std::vector<std::pair<zima::kernel::Vec3, std::string>> markers;
    if (!marker.empty()) markers.push_back({active_point, std::move(marker)});
    if (!inference.tangent_reference_id.empty()) {
        markers.push_back({sketch->world_point(
            (*pending_segment_start_)[0], (*pending_segment_start_)[1]), "C  T"});
    }
    if (!inference.perpendicular_reference_id.empty()) {
        markers.push_back({sketch->world_point(
            (*pending_segment_start_)[0], (*pending_segment_start_)[1]),
            "C  ⊥"});
    }
    if (!inference.equal_length_reference_id.empty()) {
        const auto start_point = sketch->world_point(
            (*pending_segment_start_)[0], (*pending_segment_start_)[1]);
        markers.push_back({
            {(start_point.x + active_point.x) * 0.5,
             (start_point.y + active_point.y) * 0.5,
             (start_point.z + active_point.z) * 0.5},
            inference.parallel_reference_id.empty() ? "=" : "=  ∥"});
    }
    if (!inference.parallel_reference_id.empty() &&
        inference.equal_length_reference_id.empty()) {
        const auto start_point = sketch->world_point(
            (*pending_segment_start_)[0], (*pending_segment_start_)[1]);
        markers.push_back({
            {(start_point.x + active_point.x) * 0.5,
             (start_point.y + active_point.y) * 0.5,
            (start_point.z + active_point.z) * 0.5}, "∥"});
    }
    if (!inference.midpoint_line_reference_id.empty()) {
        const auto start_point = sketch->world_point(
            (*pending_segment_start_)[0], (*pending_segment_start_)[1]);
        markers.push_back({
            {(start_point.x + active_point.x) * 0.5,
             (start_point.y + active_point.y) * 0.5,
             (start_point.z + active_point.z) * 0.5}, "M"});
    }
    if (!markers.empty()) viewer_->set_transient_labels(std::move(markers));
}

bool AssemblyWorkspaceWindow::accept_sketch_rectangle_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_rectangle_active_) return false;
    if (sketch_rectangle_axis_selecting_) return true;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_rectangle_corner_) {
        pending_rectangle_corner_ = *position;
        state_->setText(tr("Obdélník skici: určete protilehlý roh."));
        return true;
    }
    if (pending_rectangle_axis_id_.empty() &&
        (std::abs((*position)[0] - (*pending_rectangle_corner_)[0]) <= 1.0e-9 ||
         std::abs((*position)[1] - (*pending_rectangle_corner_)[1]) <= 1.0e-9)) {
        state_->setText(tr("Obdélník musí mít nenulovou šířku i výšku."));
        return true;
    }
    if (!mutate_active_sketch([&](auto& target) {
            if (pending_rectangle_axis_id_.empty()) {
                static_cast<void>(target.add_rectangle(
                    (*pending_rectangle_corner_)[0], (*pending_rectangle_corner_)[1],
                    (*position)[0], (*position)[1]));
            } else {
                static_cast<void>(target.add_oriented_rectangle(
                    (*pending_rectangle_corner_)[0], (*pending_rectangle_corner_)[1],
                    (*position)[0], (*position)[1], pending_rectangle_axis_id_));
            }
        })) return true;
    pending_rectangle_corner_.reset();
    pending_rectangle_axis_id_.clear();
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
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return;
    const double x0 = (*pending_rectangle_corner_)[0];
    const double y0 = (*pending_rectangle_corner_)[1];
    const double x1 = (*position)[0];
    const double y1 = (*position)[1];
    if (!pending_rectangle_axis_id_.empty()) {
        const auto axis = std::find_if(sketch->segments.begin(), sketch->segments.end(),
            [&](const auto& value) { return value.id == pending_rectangle_axis_id_; });
        if (axis == sketch->segments.end()) return;
        const auto* axis_first = sketch->find_point(axis->first_point_id);
        const auto* axis_second = sketch->find_point(axis->second_point_id);
        const double dx = axis_second->x - axis_first->x;
        const double dy = axis_second->y - axis_first->y;
        const double length = std::hypot(dx, dy);
        if (length <= 1.0e-12) return;
        const double ux = dx / length;
        const double uy = dy / length;
        const double along = (x1 - x0) * ux + (y1 - y0) * uy;
        const double projection =
            (x0 - axis_first->x) * ux + (y0 - axis_first->y) * uy;
        const double foot_x = axis_first->x + projection * ux;
        const double foot_y = axis_first->y + projection * uy;
        const std::array mirrored{2.0 * foot_x - x0, 2.0 * foot_y - y0};
        const std::array far_first{x0 + along * ux, y0 + along * uy};
        const std::array far_mirrored{
            mirrored[0] + along * ux, mirrored[1] + along * uy};
        const auto a = sketch->world_point(x0, y0);
        const auto b = sketch->world_point(far_first[0], far_first[1]);
        const auto c = sketch->world_point(far_mirrored[0], far_mirrored[1]);
        const auto d = sketch->world_point(mirrored[0], mirrored[1]);
        viewer_->set_transient_edges({
            {{a, b}, {}}, {{b, c}, {}}, {{c, d}, {}}, {{d, a}, {}}});
        viewer_->set_transient_points({b, d});
        return;
    }
    const auto a = sketch->world_point(x0, y0);
    const auto b = sketch->world_point(x1, y0);
    const auto c = sketch->world_point(x1, y1);
    const auto d = sketch->world_point(x0, y1);
    viewer_->set_transient_edges({
        {{a, b}, {}}, {{b, c}, {}}, {{c, d}, {}}, {{d, a}, {}}});
    viewer_->set_transient_points({b, d});
}

void AssemblyWorkspaceWindow::accept_sketch_rectangle_axis(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!sketch_rectangle_axis_selecting_ ||
        candidate.owner_id != active_sketch_id_ ||
        candidate.kind != zima::viewer::CandidateKind::SketchSegment ||
        !candidate.semantic_key.starts_with("segment:")) return;
    pending_rectangle_axis_id_ = candidate.semantic_key.substr(8);
    sketch_rectangle_axis_selecting_ = false;
    viewer_->set_candidate_filter({});
    preserve_view_on_refresh_ = true;
    refresh_scene();
    state_->setText(tr(
        "Orientovaný obdélník: určete délku podél vybrané konstrukční osy."));
}

bool AssemblyWorkspaceWindow::accept_sketch_polygon_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_polygon_active_) return false;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
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
        if (!mutate_active_sketch([&](auto& target) {
                static_cast<void>(target.add_regular_polygon(
                    (*pending_polygon_center_)[0], (*pending_polygon_center_)[1],
                    (*position)[0], (*position)[1], sketch_polygon_sides_));
            })) return true;
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
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
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
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return true;
    if (!pending_circle_center_) {
        pending_circle_center_ = *position;
        state_->setText(tr("Kružnice skici: určete bod na obvodu."));
        return true;
    }
    double radius = std::hypot(
        (*position)[0] - (*pending_circle_center_)[0],
        (*position)[1] - (*pending_circle_center_)[1]);
    const auto equal_radius = inferred_sketch_circle_radius(*position);
    const auto tangent = equal_radius
        ? std::optional<std::pair<double, std::string>>{}
        : inferred_sketch_circle_tangent(*position);
    if (equal_radius) radius = equal_radius->first;
    else if (tangent) radius = tangent->first;
    if (radius <= 1.0e-9) {
        state_->setText(tr("Kružnice musí mít nenulový poloměr."));
        return true;
    }
    if (!mutate_active_sketch([&](auto& target) {
            const auto created = target.add_circle(
                (*pending_circle_center_)[0], (*pending_circle_center_)[1], radius);
            if (equal_radius) {
                static_cast<void>(target.add_equal_radius_constraint(
                    equal_radius->second, created));
            } else if (tangent) {
                static_cast<void>(target.add_tangent_constraint(
                    tangent->second, created));
            }
        })) return true;
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
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
    const auto position = sketch->intersect_ray(origin, direction);
    if (!position) return;
    double radius = std::hypot(
        (*position)[0] - (*pending_circle_center_)[0],
        (*position)[1] - (*pending_circle_center_)[1]);
    const auto equal_radius = inferred_sketch_circle_radius(*position);
    const auto tangent = equal_radius
        ? std::optional<std::pair<double, std::string>>{}
        : inferred_sketch_circle_tangent(*position);
    if (equal_radius) radius = equal_radius->first;
    else if (tangent) radius = tangent->first;
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
    if (equal_radius || tangent) {
        const auto rim = sketch->world_point(
            (*pending_circle_center_)[0] + radius,
            (*pending_circle_center_)[1]);
        viewer_->set_transient_labels({{rim, equal_radius ? "=" : "T"}});
    } else {
        viewer_->set_transient_labels({});
    }
}

std::optional<std::pair<double, std::string>>
AssemblyWorkspaceWindow::inferred_sketch_circle_radius(
    const std::array<double, 2>& rim_position) const {
    const auto* sketch = active_sketch();
    if (sketch == nullptr || !pending_circle_center_ || viewer_ == nullptr) {
        return std::nullopt;
    }
    const double requested = std::hypot(
        rim_position[0] - (*pending_circle_center_)[0],
        rim_position[1] - (*pending_circle_center_)[1]);
    const double tolerance = viewer_->world_tolerance_for_pixels(
        16.0 * viewer_->devicePixelRatioF());
    std::optional<std::pair<double, std::string>> best;
    double best_difference = tolerance;
    const auto offer = [&](double radius, const std::string& id) {
        const double difference = std::abs(radius - requested);
        if (radius > 1.0e-12 && difference <= best_difference) {
            best_difference = difference;
            best = std::pair{radius, id};
        }
    };
    for (const auto& circle : sketch->circles) offer(circle.radius, circle.id);
    for (const auto& arc : sketch->arcs) offer(arc.radius, arc.id);
    return best;
}

std::optional<std::pair<double, std::string>>
AssemblyWorkspaceWindow::inferred_sketch_circle_tangent(
    const std::array<double, 2>& rim_position) const {
    const auto* sketch = active_sketch();
    if (sketch == nullptr || !pending_circle_center_ || viewer_ == nullptr) {
        return std::nullopt;
    }
    const double tolerance = viewer_->world_tolerance_for_pixels(
        12.0 * viewer_->devicePixelRatioF());
    std::optional<std::pair<double, std::string>> best;
    double best_distance = tolerance;
    for (const auto& line : sketch->segments) {
        const auto* first = sketch->find_point(line.first_point_id);
        const auto* second = sketch->find_point(line.second_point_id);
        if (first == nullptr || second == nullptr) continue;
        const double dx = second->x - first->x;
        const double dy = second->y - first->y;
        const double length_squared = dx * dx + dy * dy;
        if (length_squared <= 1.0e-18) continue;
        double parameter =
            (((*pending_circle_center_)[0] - first->x) * dx +
             ((*pending_circle_center_)[1] - first->y) * dy) /
            length_squared;
        if (!line.centerline && (parameter < 0.0 || parameter > 1.0)) continue;
        const std::array foot{
            first->x + parameter * dx, first->y + parameter * dy};
        const double cursor_distance = std::hypot(
            rim_position[0] - foot[0], rim_position[1] - foot[1]);
        if (cursor_distance > best_distance) continue;
        const double radius = std::hypot(
            foot[0] - (*pending_circle_center_)[0],
            foot[1] - (*pending_circle_center_)[1]);
        if (radius <= 1.0e-12) continue;
        best_distance = cursor_distance;
        best = std::pair{radius, line.id};
    }
    return best;
}

bool AssemblyWorkspaceWindow::accept_sketch_arc_ray(
    const zima::kernel::Vec3& origin, const zima::kernel::Vec3& direction) {
    if (!sketch_arc_active_) return false;
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
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
        state_->setText(tr(
            "Oblouk skici: určete koncový bod; RMB přepíná směr."));
        return true;
    }
    if (std::hypot((*position)[0] - (*pending_arc_center_)[0],
                   (*position)[1] - (*pending_arc_center_)[1]) <= 1.0e-9) {
        state_->setText(tr("Koncový bod oblouku nesmí ležet ve středu."));
        return true;
    }
    try {
        if (!mutate_active_sketch([&](auto& target) {
                static_cast<void>(target.add_arc(
                    (*pending_arc_center_)[0], (*pending_arc_center_)[1],
                    (*pending_arc_start_)[0], (*pending_arc_start_)[1],
                    (*position)[0], (*position)[1], false, 1.0e-6,
                    sketch_arc_clockwise_));
            })) return true;
        pending_arc_center_.reset();
        pending_arc_start_.reset();
        sketch_arc_clockwise_ = false;
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
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
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
    if (sketch_arc_clockwise_) std::swap(start_angle, end_angle);
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
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
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
        if (!mutate_active_sketch([&](auto& target) {
                static_cast<void>(target.add_ellipse(
                    (*pending_ellipse_center_)[0], (*pending_ellipse_center_)[1],
                    (*pending_ellipse_major_)[0], (*pending_ellipse_major_)[1],
                    (*position)[0], (*position)[1]));
            })) return true;
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
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
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
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return false;
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
        if (!mutate_active_sketch([&](auto& target) {
            static_cast<void>(target.add_elliptical_arc(
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
        })) return true;
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
    const auto* sketch = active_sketch();
    if (sketch == nullptr) return;
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
    if (event->key() == Qt::Key_Escape && placement_reference_drag_document_) {
        placement_reference_drag_document_.reset();
        placement_reference_drag_document_id_.clear();
        placement_reference_drag_occurrence_id_.clear();
        placement_reference_drag_index_ = 0;
        placement_reference_drag_changed_ = false;
        placement_reference_drag_angular_ = false;
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(tr("Tažení reference umístění bylo zrušeno beze změny."));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && component_drag_document_) {
        component_drag_document_.reset();
        component_drag_document_id_.clear();
        component_drag_occurrence_id_.clear();
        component_drag_changed_ = false;
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(tr("Tažení komponenty bylo zrušeno beze změny."));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && edge_treatment_selection_) {
        if (edge_treatment_dialog_ != nullptr) {
            edge_treatment_dialog_->reject();
            event->accept();
            return;
        }
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
        viewer_->hasFocus() && viewer_->confirm_current_pointer()) {
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape &&
        (sketch_external_reference_active_ || sketch_point_active_ ||
         sketch_segment_active_ ||
         sketch_rectangle_active_ || sketch_polygon_active_ || sketch_trim_active_ ||
         sketch_circle_active_ ||
         sketch_mirror_active_ || sketch_arc_active_ || sketch_ellipse_active_ ||
         sketch_elliptical_arc_active_ ||
         sketch_bspline_active_ ||
         sketch_coincident_active_ || sketch_midpoint_active_ ||
         sketch_symmetric_active_ || sketch_concentric_active_ ||
         sketch_tangent_active_ ||
         sketch_segment_pair_active_ || sketch_point_dimension_active_ ||
         sketch_line_pair_dimension_active_ || sketch_corner_fillet_active_)) {
        if (cancel_current_sketch_step()) {
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Escape) {
        // Idle mode (no active command consumed Escape above): mirror the
        // ordinary empty-View-space click contract and clear the confirmed
        // View+Tree selection together.
        viewer_->clear_selection();
        tree_->clearSelection();
        tree_->setCurrentItem(nullptr);
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

std::pair<zima::kernel::Vec3, zima::kernel::Vec3>
AssemblyWorkspaceWindow::active_assembly_local_ray(
    const zima::kernel::Vec3& origin,
    const zima::kernel::Vec3& direction) const {
    const auto* active = workspace_.open_assembly(workspace_.active_document_id());
    const auto* displayed =
        workspace_.open_assembly(workspace_.displayed_document_id());
    if (active == nullptr || displayed == nullptr ||
        active->session.document().document_id ==
            displayed->session.document().document_id ||
        active_occurrence_path_.empty()) return {origin, direction};
    const auto path = zima::assembly::InstancePath::decode(active_occurrence_path_);
    return {
        workspace_.occurrence_point_from_scene(
            displayed->session.document().document_id, path, origin),
        workspace_.occurrence_direction_from_scene(
            displayed->session.document().document_id, path, direction),
    };
}

void AssemblyWorkspaceWindow::regenerate_active_part() {
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || properties_dialog_ != nullptr) return;
    try {
        auto next = part->session.document();
        const auto& previous = part->session.calculated_boundaries();
        auto calculated = calculate_part(next, &previous);
        const auto previous_constructions = next.constructions;
        next.resolve_constructions(calculated.empty()
            ? zima::kernel::ViewerReferenceGeometry{}
            : calculated.back().mesh.original_references);
        const bool references_changed =
            refresh_sketch_external_references(next, calculated) |
            workspace_.refresh_context_external_references(next);
        if (references_changed) calculated = calculate_part(next, &calculated);
        if (references_changed || next.constructions != previous_constructions) {
            part->session.commit(std::move(next), std::move(calculated));
        } else {
            part->session.update_calculated_boundaries(std::move(calculated));
        }
        refresh_tabs();
        preserve_view_on_refresh_ = true;
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
        if (part->session.undo()) {
            workspace_.synchronize_external_sketch_dependencies();
            refresh_scene();
        }
    } else if (auto* assembly = workspace_.open_assembly(workspace_.active_document_id())) {
        if (assembly->session.undo()) refresh_scene();
    }
    refresh_tabs();
}

void AssemblyWorkspaceWindow::redo() {
    if (properties_dialog_ != nullptr) return;
    cancel_sketch_segment();
    if (auto* part = workspace_.open_part(workspace_.active_document_id())) {
        if (part->session.redo()) {
            workspace_.synchronize_external_sketch_dependencies();
            refresh_scene();
        }
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

void AssemblyWorkspaceWindow::update_document_kind_button() {
    if (document_kind_button_ == nullptr) return;
    const std::string displayed = workspace_.displayed_document_id();
    if (displayed.empty()) {
        document_kind_button_->hide();
        return;
    }
    QString label;
    QString tooltip;
    if (const auto* drawing = workspace_.open_drawing(displayed)) {
        bool assembly_source = false;
        if (!drawing->document.sheets.empty() &&
            !drawing->document.sheets.front().views.empty()) {
            const auto& view = drawing->document.sheets.front().views.front();
            assembly_source = workspace_.open_assembly(view.source_document_id) != nullptr ||
                view.source_path.extension() == ".asmz";
        }
        label = assembly_source ? tr("SESTAVA") : tr("DÍL");
        tooltip = assembly_source ? tr("Přejít na zdrojovou sestavu")
                                  : tr("Přejít na zdrojový díl");
    } else if (workspace_.open_part(displayed) != nullptr ||
               workspace_.open_assembly(displayed) != nullptr) {
        label = tr("VÝKRES");
        tooltip = tr("Otevřít nebo vytvořit výkres tohoto dokumentu");
    } else {
        document_kind_button_->hide();
        return;
    }
    document_kind_button_->setText(label);
    document_kind_button_->setToolTip(tooltip);
    document_kind_button_->show();
    QTimer::singleShot(0, this, [this] {
        if (document_kind_button_ == nullptr || !document_kind_button_->isVisible()) return;
        const QSize hint = document_kind_button_->sizeHint();
        const int height = std::min(tree_->header()->height() - 6, hint.height());
        const int width = std::max(72, hint.width() + 8);
        document_kind_button_->setGeometry(
            tree_->header()->width() - width - 5,
            std::max(2, (tree_->header()->height() - height) / 2), width, height);
        document_kind_button_->raise();
    });
}

void AssemblyWorkspaceWindow::navigate_document_kind() {
    const std::string displayed = workspace_.displayed_document_id();
    if (const auto* drawing = workspace_.open_drawing(displayed)) {
        if (drawing->document.sheets.empty() ||
            drawing->document.sheets.front().views.empty()) {
            state_->setText(tr("Výkres zatím nemá zdrojový pohled."));
            return;
        }
        const auto& view = drawing->document.sheets.front().views.front();
        if (workspace_.find(view.source_document_id) == nullptr) {
            if (view.source_path.empty() ||
                !open_document_path(QString::fromStdString(view.source_path.string()))) {
                state_->setText(tr("Zdrojový dokument výkresu nelze otevřít."));
                return;
            }
        } else {
            workspace_.activate(view.source_document_id);
            workspace_.display_top_level(view.source_document_id);
            refresh_tabs();
            refresh_scene();
        }
        return;
    }

    std::filesystem::path source_path;
    QString source_name;
    if (const auto* part = workspace_.open_part(displayed)) {
        source_path = part->path;
        source_name = QString::fromStdString(part->session.document().name);
    } else if (const auto* assembly = workspace_.open_assembly(displayed)) {
        source_path = assembly->path;
        source_name = QString::fromStdString(assembly->session.document().name);
    } else return;
    if (source_path.empty()) {
        state_->setText(tr("Nejprve zdrojový dokument uložte."));
        return;
    }
    auto drawing_path = source_path;
    drawing_path.replace_extension(".drwz");
    if (const auto open = workspace_.document_id_for_path(drawing_path)) {
        workspace_.activate(*open);
        workspace_.display_top_level(*open);
        refresh_tabs(); refresh_scene();
        return;
    }
    if (std::filesystem::is_regular_file(drawing_path)) {
        static_cast<void>(open_document_path(
            QString::fromStdString(drawing_path.string())));
        return;
    }
    auto drawing = zima::drawing::DrawingDocument::create_default();
    drawing.name = source_name.toStdString();
    const std::string drawing_id = drawing.document_id;
    workspace_.add_drawing(std::move(drawing), drawing_path);
    workspace_.activate(drawing_id);
    workspace_.display_top_level(drawing_id);
    refresh_tabs(); refresh_scene();
    state_->setText(tr("Nový výkres vytvořen. Pokračujte příkazem Vložit pohled."));
}

void AssemblyWorkspaceWindow::refresh_scene() {
    QScopedValueRollback refreshing_guard(refreshing_scene_, true);
    update_document_area_visibility();
    tree_->setRootIndex(QModelIndex{});
    tree_->clear();
    update_document_kind_button();
    viewer_->set_transient_point_transform({});
    const auto construction_mesh = [this](const auto& document, double scene_size) {
        auto mesh = construction_preview_mesh_.has_value()
            ? *construction_preview_mesh_
            : document.construction_viewer_mesh({}, scene_size);
        const auto* stored_object = document.find_construction(
            construction_dimension_object_id_);
        const auto* object = construction_parameter_preview_ &&
                construction_parameter_preview_->id ==
                    construction_dimension_object_id_
            ? &*construction_parameter_preview_ : stored_object;
        const auto append_dimension = [&](const std::string& owner,
                const char* key, const char*,
                zima::kernel::Vec3 witness_first,
                zima::kernel::Vec3 witness_second,
                zima::kernel::Vec3 offset, double value) {
            mesh.dimensions.push_back({witness_first, witness_second,
                {witness_first.x + offset.x, witness_first.y + offset.y,
                    witness_first.z + offset.z},
                {witness_second.x + offset.x, witness_second.y + offset.y,
                    witness_second.z + offset.z}, value,
                {owner, std::string("parameter:") + key, {}}, ""});
        };
        if (object != nullptr) {
            if (object->kind == zima::document::ConstructionKind::Point) {
                append_dimension(object->id, "x", "X = ", {0, 0, 0},
                    {object->origin.x, 0, 0}, {0, -8, 0}, object->origin.x);
                append_dimension(object->id, "y", "Y = ",
                    {object->origin.x, 0, 0},
                    {object->origin.x, object->origin.y, 0},
                    {8, 0, 0}, object->origin.y);
                append_dimension(object->id, "z", "Z = ",
                    {object->origin.x, object->origin.y, 0}, object->origin,
                    {8, 8, 0}, object->origin.z);
            } else if (object->kind == zima::document::ConstructionKind::Axis) {
                const auto end = zima::kernel::Vec3{
                    object->origin.x + object->direction.x * object->display_size,
                    object->origin.y + object->direction.y * object->display_size,
                    object->origin.z + object->direction.z * object->display_size};
                append_dimension(object->id, "length", "L = ",
                    object->origin, end, {5, 5, 0}, object->display_size);
            } else {
                append_dimension(object->id, "offset", "Odsazení = ",
                    object->origin, object->entity_origin, {5, 5, 0},
                    object->offset);
            }
        }
        if constexpr (requires { document.history; }) {
            const auto stored_container = std::find_if(document.history.begin(),
                document.history.end(), [&](const auto& value) {
                    return value.id == construction_dimension_object_id_;
                });
            const zima::document::HistoryContainer* container =
                parameter_dimension_preview_ &&
                    parameter_dimension_preview_->id ==
                        construction_dimension_object_id_
                ? &*parameter_dimension_preview_
                : stored_container != document.history.end()
                    ? &*stored_container : nullptr;
            if (container != nullptr) {
                const auto origin = zima::kernel::Vec3{
                    container->placement.x, container->placement.y,
                    container->placement.z};
                const auto local = [&](double x, double y, double z) {
                    // Parameter dimensions intentionally use the persisted
                    // container placement, not OCCT result topology.
                    constexpr double radians = std::numbers::pi / 180.0;
                    const double cx = std::cos(container->placement.rotation_x * radians);
                    const double sx = std::sin(container->placement.rotation_x * radians);
                    const double cy = std::cos(container->placement.rotation_y * radians);
                    const double sy = std::sin(container->placement.rotation_y * radians);
                    const double cz = std::cos(container->placement.rotation_z * radians);
                    const double sz = std::sin(container->placement.rotation_z * radians);
                    const double yx = cx * y - sx * z;
                    const double zx = sx * y + cx * z;
                    const double xy = cy * x + sy * zx;
                    const double zy = -sy * x + cy * zx;
                    return zima::kernel::Vec3{
                        origin.x + cz * xy - sz * yx,
                        origin.y + sz * xy + cz * yx,
                        origin.z + zy};
                };
                const auto linear = [&](const char* key, const char* label,
                        zima::kernel::Vec3 first, zima::kernel::Vec3 second,
                        zima::kernel::Vec3 offset, double value) {
                    append_dimension(container->id, key, label, first, second,
                        offset, value);
                };
                const auto radius = [&](const char* key,
                        zima::kernel::Vec3 center, zima::kernel::Vec3 rim,
                        zima::kernel::Vec3 offset, double value) {
                    append_dimension(container->id, key, "", center, rim,
                        offset, value);
                    mesh.dimensions.back().kind =
                        zima::kernel::ViewerDimensionKind::Radius;
                };
                using zima::document::FeatureKind;
                if (container->feature_kind == FeatureKind::Box) {
                    const double x = container->box.length * 0.5;
                    const double y = container->box.width * 0.5;
                    const double z = container->box.height * 0.5;
                    linear("length", "Délka = ", local(-x,-y,-z), local(x,-y,-z),
                        {0,-8,0}, container->box.length);
                    linear("width", "Šířka = ", local(-x,-y,-z), local(-x,y,-z),
                        {-8,0,0}, container->box.width);
                    linear("height", "Výška = ", local(-x,-y,-z), local(-x,-y,z),
                        {-8,-8,0}, container->box.height);
                } else if (container->feature_kind == FeatureKind::Cylinder) {
                    radius("radius", origin,
                        local(container->cylinder.radius,0,0), {0,6,0},
                        container->cylinder.radius);
                    linear("height", "Výška = ", origin,
                        local(0,0,container->cylinder.height), {8,0,0},
                        container->cylinder.height);
                } else if (container->feature_kind == FeatureKind::Sphere) {
                    radius("radius", origin,
                        local(container->sphere.radius,0,0), {0,6,0},
                        container->sphere.radius);
                } else if (container->feature_kind == FeatureKind::Cone) {
                    radius("bottom_radius", origin,
                        local(container->cone.bottom_radius,0,0), {0,-8,0},
                        container->cone.bottom_radius);
                    radius("top_radius", local(0,0,container->cone.height),
                        local(container->cone.top_radius,0,container->cone.height),
                        {0,8,0}, container->cone.top_radius);
                    linear("height", "Výška = ", origin,
                        local(0,0,container->cone.height), {8,0,0},
                        container->cone.height);
                } else if (container->feature_kind == FeatureKind::Pyramid) {
                    linear("length", "Délka = ", origin,
                        local(container->pyramid.length,0,0), {0,-8,0},
                        container->pyramid.length);
                    linear("width", "Šířka = ", origin,
                        local(0,container->pyramid.width,0), {-8,0,0},
                        container->pyramid.width);
                    linear("height", "Výška = ", origin,
                        local(0,0,container->pyramid.height), {8,8,0},
                        container->pyramid.height);
                } else if (container->feature_kind == FeatureKind::Wedge) {
                    linear("length", "Délka = ", origin,
                        local(container->wedge.length,0,0), {0,-8,0},
                        container->wedge.length);
                    linear("width", "Šířka = ", origin,
                        local(0,container->wedge.width,0), {-8,0,0},
                        container->wedge.width);
                    linear("height", "Výška = ", origin,
                        local(0,0,container->wedge.height), {8,8,0},
                        container->wedge.height);
                    linear("top_offset", "Posun = ", origin,
                        local(container->wedge.top_offset,0,0), {0,8,0},
                        container->wedge.top_offset);
                } else if (container->feature_kind == FeatureKind::Extrusion) {
                    const auto sketch = std::find_if(document.sketches.begin(),
                        document.sketches.end(), [&](const auto& value) {
                            return value.id == container->extrusion.sketch_id;
                        });
                    if (sketch != document.sketches.end()) {
                        const auto start = sketch->resolved_origin;
                        const auto along = [&](double distance) {
                            return zima::kernel::Vec3{
                                start.x + sketch->resolved_normal.x * distance,
                                start.y + sketch->resolved_normal.y * distance,
                                start.z + sketch->resolved_normal.z * distance};
                        };
                        linear("length_forward", "Délka = ", start,
                            along(container->extrusion.length_forward), {8,8,0},
                            container->extrusion.length_forward);
                        if (container->extrusion.extent_mode ==
                                zima::document::ProfileExtentMode::TwoSides) {
                            linear("length_reverse", "Délka 2 = ", start,
                                along(-container->extrusion.length_reverse),
                                {-8,-8,0}, container->extrusion.length_reverse);
                        }
                        const auto base = along(
                            -container->extrusion.profile_plane_offset);
                        linear("profile_offset", "Odsazení = ", base, start,
                            {5,5,0}, container->extrusion.profile_plane_offset);
                    }
                } else if (container->feature_kind == FeatureKind::Revolution) {
                    const auto sketch = std::find_if(document.sketches.begin(),
                        document.sketches.end(), [&](const auto& value) {
                            return value.id == container->revolution.sketch_id;
                        });
                    if (sketch != document.sketches.end()) {
                        const auto segment = std::find_if(sketch->segments.begin(),
                            sketch->segments.end(), [&](const auto& value) {
                                return value.id ==
                                    container->revolution.axis_segment_id;
                            });
                        if (segment != sketch->segments.end()) {
                            const auto* first = sketch->find_point(
                                segment->first_point_id);
                            const auto* second = sketch->find_point(
                                segment->second_point_id);
                            if (first != nullptr && second != nullptr) {
                                const auto vertex = sketch->world_point(first->x, first->y);
                                const auto axis_end = sketch->world_point(second->x, second->y);
                                zima::kernel::Vec3 axis{
                                    axis_end.x - vertex.x, axis_end.y - vertex.y,
                                    axis_end.z - vertex.z};
                                const double axis_length = std::hypot(
                                    std::hypot(axis.x, axis.y), axis.z);
                                axis = {axis.x / axis_length, axis.y / axis_length,
                                        axis.z / axis_length};
                                zima::kernel::Vec3 radial{
                                    axis.y * sketch->resolved_normal.z -
                                        axis.z * sketch->resolved_normal.y,
                                    axis.z * sketch->resolved_normal.x -
                                        axis.x * sketch->resolved_normal.z,
                                    axis.x * sketch->resolved_normal.y -
                                        axis.y * sketch->resolved_normal.x};
                                constexpr double radius = 25.0;
                                const auto ray = zima::kernel::Vec3{
                                    vertex.x + radial.x * radius,
                                    vertex.y + radial.y * radius,
                                    vertex.z + radial.z * radius};
                                const double angle = container->revolution.angle_degrees *
                                    std::numbers::pi / 180.0;
                                const auto perpendicular = zima::kernel::Vec3{
                                    axis.y * radial.z - axis.z * radial.y,
                                    axis.z * radial.x - axis.x * radial.z,
                                    axis.x * radial.y - axis.y * radial.x};
                                const auto other = zima::kernel::Vec3{
                                    vertex.x + radius * (radial.x * std::cos(angle) +
                                        perpendicular.x * std::sin(angle)),
                                    vertex.y + radius * (radial.y * std::cos(angle) +
                                        perpendicular.y * std::sin(angle)),
                                    vertex.z + radius * (radial.z * std::cos(angle) +
                                        perpendicular.z * std::sin(angle))};
                                mesh.dimensions.push_back({vertex, vertex, ray, other,
                                    container->revolution.angle_degrees,
                                    {container->id, "parameter:angle", {}},
                                    "", "°"});
                                auto& angle_dimension = mesh.dimensions.back();
                                angle_dimension.kind =
                                    zima::kernel::ViewerDimensionKind::Angular;
                                angle_dimension.plane_normal = axis;
                                angle_dimension.sweep_degrees =
                                    container->revolution.angle_degrees;
                            }
                        }
                        const auto start = sketch->resolved_origin;
                        const auto base = zima::kernel::Vec3{
                            start.x - sketch->resolved_normal.x *
                                container->revolution.profile_plane_offset,
                            start.y - sketch->resolved_normal.y *
                                container->revolution.profile_plane_offset,
                            start.z - sketch->resolved_normal.z *
                                container->revolution.profile_plane_offset};
                        linear("profile_offset", "Odsazení = ", base, start,
                            {5,5,0}, container->revolution.profile_plane_offset);
                    }
                } else if (container->feature_kind == FeatureKind::Fillet ||
                           container->feature_kind == FeatureKind::Chamfer) {
                    linear("size", container->feature_kind == FeatureKind::Fillet
                            ? "Poloměr = " : "Vzdálenost = ",
                        origin, local(container->edge_treatment.size,0,0),
                        {0,6,0}, container->edge_treatment.size);
                }
            }
        }
        if constexpr (requires { document.cuts; }) {
            const auto cut = std::find_if(document.cuts.begin(),
                document.cuts.end(), [&](const auto& value) {
                    return value.definition.id ==
                        construction_dimension_object_id_;
                });
            if (cut != document.cuts.end()) {
                const auto& definition = cut->definition;
                using zima::document::FeatureKind;
                if (definition.feature_kind == FeatureKind::Extrusion) {
                    const auto sketch = std::find_if(document.sketches.begin(),
                        document.sketches.end(), [&](const auto& value) {
                            return value.id == definition.extrusion.sketch_id;
                        });
                    if (sketch != document.sketches.end()) {
                        const auto start = sketch->resolved_origin;
                        const auto end = zima::kernel::Vec3{
                            start.x + sketch->resolved_normal.x *
                                definition.extrusion.length_forward,
                            start.y + sketch->resolved_normal.y *
                                definition.extrusion.length_forward,
                            start.z + sketch->resolved_normal.z *
                                definition.extrusion.length_forward};
                        append_dimension(definition.id, "length_forward", "Délka = ",
                            start, end, {8,8,0},
                            definition.extrusion.length_forward);
                    }
                } else if (definition.feature_kind == FeatureKind::Revolution) {
                    // Assembly cuts share the same editable angular value;
                    // keep its inspection dimension independent of OCCT body
                    // topology just like Part history containers.
                    zima::kernel::ViewerDimension angle{
                        {definition.placement.x, definition.placement.y,
                         definition.placement.z},
                        {definition.placement.x, definition.placement.y,
                         definition.placement.z},
                        {definition.placement.x + 25.0, definition.placement.y,
                         definition.placement.z},
                        {definition.placement.x,
                         definition.placement.y + 25.0,
                         definition.placement.z},
                        definition.revolution.angle_degrees,
                        {definition.id, "parameter:angle", {}}, "", "°"};
                    angle.kind = zima::kernel::ViewerDimensionKind::Angular;
                    angle.sweep_degrees = definition.revolution.angle_degrees;
                    mesh.dimensions.push_back(std::move(angle));
                } else if (definition.feature_kind == FeatureKind::Fillet ||
                           definition.feature_kind == FeatureKind::Chamfer) {
                    const zima::kernel::Vec3 start{
                        definition.placement.x, definition.placement.y,
                        definition.placement.z};
                    append_dimension(definition.id, "size",
                        definition.feature_kind == FeatureKind::Fillet
                            ? "Poloměr = " : "Vzdálenost = ",
                        start, {start.x + definition.edge_treatment.size,
                                start.y, start.z},
                        {0,6,0}, definition.edge_treatment.size);
                }
            }
        }
        if constexpr (requires { document.sketches; }) {
            const auto sketch = std::find_if(document.sketches.begin(),
                document.sketches.end(), [&](const auto& value) {
                    return value.id == construction_dimension_object_id_ ||
                        value.owner_container_id ==
                            construction_dimension_object_id_;
                });
            if (sketch != document.sketches.end()) {
                const auto sketch_mesh = sketch->viewer_mesh();
                mesh.dimensions.insert(mesh.dimensions.end(),
                    sketch_mesh.dimensions.begin(), sketch_mesh.dimensions.end());
                const auto base = zima::kernel::Vec3{
                    sketch->resolved_origin.x - sketch->resolved_normal.x *
                        sketch->plane_offset,
                    sketch->resolved_origin.y - sketch->resolved_normal.y *
                        sketch->plane_offset,
                    sketch->resolved_origin.z - sketch->resolved_normal.z *
                        sketch->plane_offset};
                append_dimension(sketch->owner_container_id, "profile_offset",
                    "Odsazení = ", base, sketch->resolved_origin,
                    {5,5,0}, sketch->plane_offset);
            }
        }
        return mesh;
    };
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
        sketch_external_profile_action_->setEnabled(false);
        rebuild_application_toolbar();
        return;
    }
    if (auto* drawing =
            workspace_.open_drawing(workspace_.displayed_document_id())) {
        workspace_stack_->setCurrentWidget(drawing_workspace_);
        drawing_workspace_->edit_workspace_document(drawing->document.document_id);
        tree_->setHeaderLabels({tr("VÝKRES")});
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
        // The drawing document name is already shown by its tab. Hide the
        // technical owner row (for example "part" from part.drwz) and make
        // sheets/views the visible tree root, consistently with Part and
        // Assembly workspaces.
        tree_->setRootIndex(tree_->indexFromItem(root));
        active_application_ = ApplicationMode::Drawing;
        insert_action_->setEnabled(false);
        regenerate_action_->setEnabled(false);
        for (auto* action : {box_action_, cylinder_action_, sphere_action_, cone_action_,
                             pyramid_action_, wedge_action_, construction_point_action_,
                             construction_axis_action_, construction_plane_action_,
                             sketch_action_, extrusion_action_, revolution_action_,
                             fillet_action_, chamfer_action_, regenerate_part_action_,
                             sketch_normal_view_action_,
                             sketch_flip_view_action_, sketch_rotate_view_action_,
                             sketch_external_reference_action_,
                             sketch_external_profile_action_, sketch_point_action_,
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
                             sketch_dimensions_action_, finish_sketch_action_}) {
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
        // Explicitly re-assert Modeling mode every time this Part branch
        // runs (not just on first activation): refresh_scene() runs on
        // every tab switch, and active_application_ otherwise keeps
        // whatever value the previously active tab left it at, so
        // rebuild_application_toolbar() below could render e.g. the
        // Assembly toolbar while a Part tab is actually being displayed.
        active_application_ = ApplicationMode::Modeling;
        tree_->setHeaderLabels({tr("DÍL")});
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
        if (!active_sketch_id_.empty()) {
            const auto found = std::find_if(document.sketches.begin(),
                document.sketches.end(), [&](const auto& sketch) {
                    return sketch.id == active_sketch_id_;
                });
            if (found != document.sketches.end()) populate_sketch_tree(*found);
        } else {
            auto* root = new QTreeWidgetItem(
                tree_, {QString::fromStdString(document.name)});
            add_part_tree_children(root, document);
            root->setExpanded(true);
            // Keep the document object as the internal owner, but present its
            // contents as the visible root exactly like the Python history tree.
            tree_->setRootIndex(tree_->indexFromItem(root));
        }
        viewer_->set_selection_contract(!selection_action_->isChecked()
            ? std::vector<zima::viewer::CandidateKind>{}
            : sketch_external_reference_active_
                ? sketch_external_profile_active_
                    ? std::vector{zima::viewer::CandidateKind::Edge}
                    : std::vector{zima::viewer::CandidateKind::Edge,
                              zima::viewer::CandidateKind::Vertex,
                              zima::viewer::CandidateKind::Axis,
                              zima::viewer::CandidateKind::Face}
            : sketch_rectangle_axis_selecting_
                ? std::vector{zima::viewer::CandidateKind::SketchSegment}
            : sketch_trim_active_
                ? std::vector{zima::viewer::CandidateKind::SketchTrimPiece}
            : sketch_corner_fillet_active_
                ? std::vector{zima::viewer::CandidateKind::SketchSegment}
            : sketch_mirror_active_
                ? sketch_mirror_selecting_sources_
                    ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchPoint,
                                  zima::viewer::CandidateKind::SketchCurve}
                    : std::vector{zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchAxis}
            : sketch_coincident_active_
                ? pending_point_pair_constraint_kind_ !=
                        zima::sketcher::ConstraintKind::Coincident
                    ? std::vector{zima::viewer::CandidateKind::SketchPoint}
                    : pending_coincident_point_id_.empty()
                        ? std::vector{zima::viewer::CandidateKind::SketchPoint,
                                      zima::viewer::CandidateKind::SketchExternalReference}
                        : std::vector{zima::viewer::CandidateKind::SketchPoint,
                                      zima::viewer::CandidateKind::SketchSegment,
                                      zima::viewer::CandidateKind::SketchCurve,
                                      zima::viewer::CandidateKind::SketchExternalReference}
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
            : sketch_point_dimension_active_
                ? pending_point_dimension_first_id_.empty()
                    ? std::vector{zima::viewer::CandidateKind::SketchPoint}
                : (pending_point_dimension_kind_ ==
                        zima::sketcher::DimensionKind::DistancePointLine ||
                   pending_point_dimension_kind_ ==
                        zima::sketcher::DimensionKind::DistanceSymmetric)
                    ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchAxis,
                                  zima::viewer::CandidateKind::SketchExternalReference}
                : pending_point_dimension_kind_ ==
                        zima::sketcher::DimensionKind::AngleThreePoint
                    ? std::vector{zima::viewer::CandidateKind::SketchPoint}
                : pending_point_dimension_kind_ ==
                        zima::sketcher::DimensionKind::Distance
                    ? std::vector{zima::viewer::CandidateKind::SketchPoint,
                                  zima::viewer::CandidateKind::SketchExternalReference}
                    : std::vector{zima::viewer::CandidateKind::SketchPoint,
                                  zima::viewer::CandidateKind::SketchExternalReference,
                                  zima::viewer::CandidateKind::SketchAxis}
            : sketch_line_pair_dimension_active_
                ? pending_line_dimension_reference_id_.empty()
                    ? std::vector{zima::viewer::CandidateKind::SketchSegment}
                : pending_line_dimension_kind_ ==
                        zima::sketcher::DimensionKind::AngleBetween
                    ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                                  zima::viewer::CandidateKind::SketchAxis}
                    : std::vector{zima::viewer::CandidateKind::SketchSegment}
            : pending_construction_reference_index_ ||
                    pending_primitive_reference_index_
                ? std::vector{zima::viewer::CandidateKind::Vertex,
                              zima::viewer::CandidateKind::Axis,
                              zima::viewer::CandidateKind::Plane}
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
                              zima::viewer::CandidateKind::SketchAxis,
                              zima::viewer::CandidateKind::Dimension,
                              zima::viewer::CandidateKind::SketchConstraint,
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
        } else if (sketch_coincident_active_) {
            const auto owner_id = active_sketch_id_;
            const auto first_id = pending_coincident_point_id_;
            const auto kind = pending_point_pair_constraint_kind_;
            viewer_->set_candidate_filter(
                [owner_id, first_id, kind](const auto& candidate) {
                    if (candidate.owner_id != owner_id) return false;
                    if (candidate.kind ==
                            zima::viewer::CandidateKind::SketchPoint) {
                        return candidate.semantic_key.starts_with("point:") &&
                            candidate.semantic_key.substr(6) != first_id;
                    }
                    if (kind != zima::sketcher::ConstraintKind::Coincident)
                        return false;
                    if (first_id.empty()) {
                        return candidate.kind == zima::viewer::CandidateKind::
                                SketchExternalReference &&
                            candidate.semantic_key.starts_with("external_point:");
                    }
                    return (candidate.kind ==
                                zima::viewer::CandidateKind::SketchSegment &&
                            candidate.semantic_key.starts_with("segment:")) ||
                        (candidate.kind ==
                                zima::viewer::CandidateKind::SketchCurve &&
                            (candidate.semantic_key.starts_with("circle:") ||
                             candidate.semantic_key.starts_with("arc:") ||
                             candidate.semantic_key.starts_with("ellipse:") ||
                             candidate.semantic_key.starts_with(
                                 "elliptical_arc:") ||
                             candidate.semantic_key.starts_with("bspline:"))) ||
                        (candidate.kind == zima::viewer::CandidateKind::
                                SketchExternalReference &&
                            (candidate.semantic_key.starts_with("external_point:") ||
                             candidate.semantic_key.starts_with("external_edge:") ||
                             candidate.semantic_key.starts_with("external_axis:") ||
                             candidate.semantic_key.starts_with("external_face:")));
                });
        } else if (sketch_corner_fillet_active_) {
            const auto owner_id = active_sketch_id_;
            const auto first_id = pending_corner_fillet_segment_id_;
            viewer_->set_candidate_filter(
                [owner_id, first_id](const auto& candidate) {
                    return candidate.owner_id == owner_id &&
                        candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
                        candidate.semantic_key.starts_with("segment:") &&
                        (first_id.empty() || candidate.semantic_key.substr(8) != first_id);
                });
        } else if (sketch_rectangle_axis_selecting_) {
            const auto* sketch = active_sketch();
            std::set<std::string> construction_axes;
            if (sketch != nullptr) {
                for (const auto& segment : sketch->segments) {
                    if (segment.construction) construction_axes.insert(segment.id);
                }
            }
            const auto owner_id = active_sketch_id_;
            viewer_->set_candidate_filter(
                [owner_id, construction_axes](const auto& candidate) {
                    return candidate.owner_id == owner_id &&
                        candidate.kind == zima::viewer::CandidateKind::SketchSegment &&
                        candidate.semantic_key.starts_with("segment:") &&
                        construction_axes.contains(candidate.semantic_key.substr(8));
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
        } else if (sketch_point_dimension_active_) {
            const auto owner_id = active_sketch_id_;
            const auto first_id = pending_point_dimension_first_id_;
            const auto vertex_id = pending_point_dimension_vertex_id_;
            const auto kind = pending_point_dimension_kind_;
            viewer_->set_candidate_filter(
                [owner_id, first_id, vertex_id, kind](const auto& candidate) {
                    if (candidate.owner_id != owner_id) return false;
                    if (first_id.empty()) {
                        return candidate.kind ==
                                zima::viewer::CandidateKind::SketchPoint &&
                            candidate.semantic_key.starts_with("point:");
                    }
                    if (kind == zima::sketcher::DimensionKind::DistancePointLine ||
                        kind == zima::sketcher::DimensionKind::DistanceSymmetric) {
                        return (candidate.kind ==
                                    zima::viewer::CandidateKind::SketchSegment &&
                                candidate.semantic_key.starts_with("segment:")) ||
                            (candidate.kind ==
                                 zima::viewer::CandidateKind::SketchAxis &&
                             (candidate.semantic_key == "sketch_axis:x" ||
                              candidate.semantic_key == "sketch_axis:y")) ||
                            (candidate.kind == zima::viewer::CandidateKind::
                                 SketchExternalReference &&
                             (candidate.semantic_key.starts_with("external_edge:") ||
                              candidate.semantic_key.starts_with("external_axis:")));
                    }
                    if (candidate.kind ==
                            zima::viewer::CandidateKind::SketchPoint &&
                        candidate.semantic_key.starts_with("point:")) {
                        return candidate.semantic_key.substr(6) != first_id &&
                            candidate.semantic_key.substr(6) != vertex_id;
                    }
                    if (candidate.kind ==
                            zima::viewer::CandidateKind::SketchAxis) {
                        return (kind == zima::sketcher::DimensionKind::DistanceX &&
                                candidate.semantic_key == "sketch_axis:y") ||
                            (kind == zima::sketcher::DimensionKind::DistanceY &&
                             candidate.semantic_key == "sketch_axis:x");
                    }
                    return candidate.kind == zima::viewer::CandidateKind::
                               SketchExternalReference &&
                        candidate.semantic_key.starts_with("external_point:");
                });
        } else if (sketch_line_pair_dimension_active_) {
            const auto owner_id = active_sketch_id_;
            const auto reference_id = pending_line_dimension_reference_id_;
            const auto kind = pending_line_dimension_kind_;
            viewer_->set_candidate_filter(
                [owner_id, reference_id, kind](const auto& candidate) {
                    if (candidate.owner_id != owner_id) return false;
                    if (candidate.kind ==
                            zima::viewer::CandidateKind::SketchSegment &&
                        candidate.semantic_key.starts_with("segment:")) {
                        return reference_id.empty() ||
                            candidate.semantic_key.substr(8) != reference_id;
                    }
                    return !reference_id.empty() &&
                        kind == zima::sketcher::DimensionKind::AngleBetween &&
                        candidate.kind == zima::viewer::CandidateKind::SketchAxis &&
                        (candidate.semantic_key == "sketch_axis:x" ||
                         candidate.semantic_key == "sketch_axis:y");
                });
        }
        if (part_rollback_ &&
            part_rollback_->part_document_id == document.document_id) {
            auto display = part_rollback_->input_body
                ? part_rollback_->input_body->mesh : zima::kernel::ViewerMesh{};
            // Sketcher keeps the complete View context visible. Selection
            // tools narrow what can be confirmed through their candidate
            // contracts; presentation itself is never filtered.
            append_mesh(display, document.origin_viewer_mesh());
            append_mesh(display, construction_mesh(document, 0.0));
            if (primitive_origin_preview_mesh_) {
                append_mesh(display, *primitive_origin_preview_mesh_);
            }
            viewer_->set_mesh(std::move(display), !preserve_view_on_refresh_);
            preserve_view_on_refresh_ = false;
        } else {
            const auto& calculated = part->session.calculated_boundaries();
            std::size_t feature_count{};
            if (document.history_order.empty()) {
                feature_count = calculated.size();
            } else {
                const auto cursor = document.effective_history_cursor();
                for (std::size_t index = 0; index < cursor; ++index) {
                    if (document.history_order[index].kind ==
                        zima::document::PartHistoryKind::Feature) {
                        ++feature_count;
                    }
                }
            }
            const auto cursor_body = part->session.calculated_boundary(
                std::min(feature_count, calculated.size()));
            zima::kernel::ViewerMesh display = cursor_body
                ? cursor_body->mesh : zima::kernel::ViewerMesh{};
            for (const auto& sketch : document.sketches) {
                if (sketch.id == sketch_properties_preview_id_) continue;
                if (sketch.id != active_sketch_id_ &&
                    !sketch_visible_outside_sketcher(document, sketch)) continue;
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
                if (sketch.id != active_sketch_id_) {
                    keep_only_inactive_sketch_profile(sketch_mesh);
                }
                append_mesh(display, std::move(sketch_mesh));
            }
            append_mesh(display, document.origin_viewer_mesh());
            append_mesh(display, construction_mesh(document, 0.0));
            if (primitive_origin_preview_mesh_) {
                append_mesh(display, *primitive_origin_preview_mesh_);
            }
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
        // set_mesh() intentionally resets stale picking state. Dimension
        // inspection, however, still owns this exact persisted container, so
        // restore its cyan confirmation after the rebuilt scene is installed.
        if (!construction_dimension_object_id_.empty()) {
            viewer_->confirm_container(construction_dimension_object_id_);
        }
        state_->setText(document.history.empty()
            ? tr("Nový Part: začněte příkazem Kvádr, jiným tělesem nebo Skica.")
            : tr("Zobrazený Part: %1").arg(QString::fromStdString(document.name)));
        insert_action_->setEnabled(false);
        regenerate_action_->setEnabled(false);
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
        extrusion_action_->setEnabled(true);
        revolution_action_->setEnabled(true);
        sketch_action_->setEnabled(true);
        sketch_normal_view_action_->setEnabled(!active_sketch_id_.empty());
        sketch_flip_view_action_->setEnabled(!active_sketch_id_.empty());
        sketch_rotate_view_action_->setEnabled(!active_sketch_id_.empty());
        sketch_external_reference_action_->setEnabled(!active_sketch_id_.empty());
        sketch_external_profile_action_->setEnabled(!active_sketch_id_.empty());
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
        sketch_horizontal_action_->setEnabled(
            !selected_sketch_segment_id_.empty() || !selected_sketch_point_id_.empty());
        sketch_vertical_action_->setEnabled(
            !selected_sketch_segment_id_.empty() || !selected_sketch_point_id_.empty());
        sketch_coincident_action_->setEnabled(!active_sketch_id_.empty());
        sketch_midpoint_action_->setEnabled(!active_sketch_id_.empty());
        sketch_symmetric_action_->setEnabled(!active_sketch_id_.empty());
        sketch_concentric_action_->setEnabled(!active_sketch_id_.empty());
        sketch_tangent_action_->setEnabled(!active_sketch_id_.empty());
        sketch_parallel_action_->setEnabled(!active_sketch_id_.empty());
        sketch_perpendicular_action_->setEnabled(!active_sketch_id_.empty());
        sketch_equal_length_action_->setEnabled(!active_sketch_id_.empty());
        const bool segment_or_point = !selected_sketch_segment_id_.empty() ||
            !selected_sketch_point_id_.empty();
        const bool automatically_dimensionable = segment_or_point ||
            !selected_sketch_circle_id_.empty() ||
            !selected_sketch_arc_id_.empty() ||
            !selected_sketch_ellipse_id_.empty();
        sketch_dimension_action_->setEnabled(automatically_dimensionable);
        sketch_dimension_x_action_->setEnabled(segment_or_point);
        sketch_dimension_y_action_->setEnabled(segment_or_point);
        sketch_point_line_dimension_action_->setEnabled(
            !selected_sketch_point_id_.empty());
        sketch_symmetric_dimension_action_->setEnabled(
            !selected_sketch_point_id_.empty());
        sketch_three_point_angle_dimension_action_->setEnabled(
            !selected_sketch_point_id_.empty());
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
                    sketch_external_reference_action_, sketch_external_profile_action_,
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
                    sketch_point_line_dimension_action_,
                    sketch_symmetric_dimension_action_,
                    sketch_three_point_angle_dimension_action_,
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
    // Explicitly re-assert Assembly mode every time this branch runs (not
    // just on first activation), matching the Part branch above: otherwise
    // active_application_ keeps whatever the previously active tab left it
    // at, and rebuild_application_toolbar() below can render the wrong
    // (e.g. Part-mode) toolbar while an Assembly tab is actually displayed.
    active_application_ = ApplicationMode::Assembly;
    const auto& document = assembly->session.document();
    const auto* active_part =
        workspace_.open_part(workspace_.active_document_id());
    const bool active_top_assembly_sketch =
        workspace_.active_document_id() == document.document_id &&
        !active_sketch_id_.empty();
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
    const zima::sketcher::Sketch* editing_sketch = nullptr;
    if (!active_sketch_id_.empty()) {
        if (active_part != nullptr) {
            const auto& sketches = active_part->session.document().sketches;
            const auto found = std::find_if(sketches.begin(), sketches.end(),
                [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
            if (found != sketches.end()) editing_sketch = &*found;
        } else if (const auto* active_assembly = workspace_.open_assembly(
                       workspace_.active_document_id())) {
            const auto& sketches = active_assembly->session.document().sketches;
            const auto found = std::find_if(sketches.begin(), sketches.end(),
                [&](const auto& sketch) { return sketch.id == active_sketch_id_; });
            if (found != sketches.end()) editing_sketch = &*found;
        }
    }
    if (editing_sketch != nullptr) {
        populate_sketch_tree(*editing_sketch);
    } else {
        tree_->setHeaderLabels({tr("SESTAVA")});
        auto* root = new QTreeWidgetItem(
            tree_, {QString::fromStdString(document.name)});
        add_assembly_tree_children(root, document.document_id, {});
        root->setExpanded(true);
        // The document remains the internal owner, but its file/name is
        // already visible in the tab. Present the Assembly contents as the
        // tree root, matching Part and the Python history tree, instead of
        // exposing an extra top row such as "part" from part.asmz.
        tree_->setRootIndex(tree_->indexFromItem(root));
    }
    viewer_->set_selection_contract(!selection_action_->isChecked()
        ? std::vector<zima::viewer::CandidateKind>{}
        : (active_part != nullptr || active_top_assembly_sketch) && sketch_trim_active_
            ? std::vector{zima::viewer::CandidateKind::SketchTrimPiece}
        : (active_part != nullptr || active_top_assembly_sketch) &&
                sketch_external_reference_active_
            ? sketch_external_profile_active_
                ? std::vector{zima::viewer::CandidateKind::Edge}
                : std::vector{zima::viewer::CandidateKind::Edge,
                          zima::viewer::CandidateKind::Vertex,
                          zima::viewer::CandidateKind::Axis,
                          zima::viewer::CandidateKind::Face}
        : pending_construction_reference_index_ ||
                pending_primitive_reference_index_
            ? std::vector{zima::viewer::CandidateKind::Vertex,
                          zima::viewer::CandidateKind::Axis,
                          zima::viewer::CandidateKind::Plane}
        : (active_part != nullptr && !active_sketch_id_.empty()) ||
                active_top_assembly_sketch
            ? std::vector{zima::viewer::CandidateKind::SketchSegment,
                          zima::viewer::CandidateKind::SketchPoint,
                          zima::viewer::CandidateKind::Dimension,
                          zima::viewer::CandidateKind::SketchConstraint,
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
    if (active_top_assembly_sketch && sketch_external_reference_active_) {
        const auto top_assembly_id = document.document_id;
        viewer_->set_candidate_filter([this, top_assembly_id](const auto& candidate) {
            if (candidate.geometry !=
                    zima::viewer::CandidateGeometry::OriginalReference ||
                candidate.instance_path.empty()) return false;
            try {
                return workspace_.resolve_occurrence(top_assembly_id,
                    zima::assembly::InstancePath::decode(candidate.instance_path))
                    .has_value();
            } catch (const std::invalid_argument&) {
                return false;
            }
        });
    } else if (active_part != nullptr && sketch_external_reference_active_ &&
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
    if (assembly_cut_rollback_ &&
        assembly_cut_rollback_->assembly_document_id == document.document_id) {
        auto rollback_document = document;
        for (auto& component : rollback_document.components) {
            const auto found = assembly_cut_rollback_->input_component_bodies.find(
                component.occurrence_id);
            if (found != assembly_cut_rollback_->input_component_bodies.end()) {
                component.calculated_source = found->second;
            }
        }
        viewer_->set_mesh(rollback_document.build_scene());
    } else if (part_rollback_ && !part_rollback_->instance_path.empty()) {
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
            auto display = document.build_scene();
            if (workspace_.active_document_id() == document.document_id) {
                for (const auto& sketch : document.sketches) {
                    if (!active_sketch_id_.empty() &&
                        sketch.id != active_sketch_id_) continue;
                    auto mesh = sketch.viewer_mesh();
                    if (sketch.id != active_sketch_id_) {
                        keep_only_inactive_sketch_profile(mesh);
                    }
                    append_mesh(display, std::move(mesh));
                }
            }
            viewer_->set_mesh(std::move(display));
        }
    } else {
        const auto* active_assembly =
            workspace_.open_assembly(workspace_.active_document_id());
        if (active_assembly != nullptr &&
            active_assembly->session.document().document_id != document.document_id &&
            !active_occurrence_path_.empty()) {
            viewer_->set_mesh(workspace_.build_scene_with_assembly_override(
                document.document_id,
                zima::assembly::InstancePath::decode(active_occurrence_path_),
                active_assembly->session.document()));
        } else if (active_part_occurrence && !active_part_occurrence->empty()) {
            zima::kernel::BodyResult live_source;
            if (!active_part->session.calculated_boundaries().empty()) {
                live_source = active_part->session.calculated_boundaries().back();
            }
            for (const auto& sketch : active_part->session.document().sketches) {
                if (sketch.id == sketch_properties_preview_id_) continue;
                if (sketch.id != active_sketch_id_ &&
                    !sketch_visible_outside_sketcher(
                        active_part->session.document(), sketch)) continue;
                const auto* displayed_sketch = sketch_trim_active_ &&
                        sketch_trim_preview_ && sketch.id == active_sketch_id_
                    ? &*sketch_trim_preview_ : &sketch;
                auto sketch_mesh = displayed_sketch->viewer_mesh();
                if (sketch.id != active_sketch_id_) {
                    keep_only_inactive_sketch_profile(sketch_mesh);
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
                active_part->session.document().origin_viewer_mesh(
                    zima::document::viewer_mesh_bounds_diagonal(
                        live_source.mesh)));
            append_mesh(live_source.mesh,
                construction_mesh(active_part->session.document(),
                    zima::document::viewer_mesh_bounds_diagonal(
                        live_source.mesh)));
            if (primitive_origin_preview_mesh_) {
                append_mesh(live_source.mesh, *primitive_origin_preview_mesh_);
            }
            viewer_->set_mesh(workspace_.build_scene_with_part_override(
                document.document_id,
                zima::assembly::InstancePath::decode(*active_part_occurrence),
                std::move(live_source)));
        } else {
            auto display = document.build_scene();
            if (active_top_assembly_sketch) {
                for (const auto& sketch : document.sketches) {
                    if (sketch.id != active_sketch_id_) continue;
                    const auto* shown = sketch_trim_active_ && sketch_trim_preview_
                        ? &*sketch_trim_preview_ : &sketch;
                    append_mesh(display, shown->viewer_mesh());
                }
            }
            viewer_->set_mesh(std::move(display));
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
    // A component may be selected from disk even when no other source
    // document is currently open, matching the Python insertion workflow.
    insert_action_->setEnabled(true);
    regenerate_action_->setEnabled(true);
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
    const bool supports_constructions = active_part != nullptr ||
        workspace_.open_assembly(workspace_.active_document_id()) != nullptr;
    construction_point_action_->setEnabled(supports_constructions);
    construction_axis_action_->setEnabled(supports_constructions);
    construction_plane_action_->setEnabled(supports_constructions);
    const bool active_assembly_owner =
        workspace_.open_assembly(workspace_.active_document_id()) != nullptr;
    const bool profile_feature_owner = active_part != nullptr || active_assembly_owner;
    extrusion_action_->setEnabled(profile_feature_owner);
    revolution_action_->setEnabled(profile_feature_owner);
    sketch_action_->setEnabled(active_part != nullptr || active_assembly_owner);
    const bool has_active_part_sketch =
        (active_part != nullptr || active_assembly_owner) &&
        !active_sketch_id_.empty();
    sketch_normal_view_action_->setEnabled(has_active_part_sketch);
    sketch_flip_view_action_->setEnabled(has_active_part_sketch);
    sketch_rotate_view_action_->setEnabled(has_active_part_sketch);
    sketch_external_reference_action_->setEnabled(has_active_part_sketch);
    sketch_external_profile_action_->setEnabled(has_active_part_sketch);
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
    sketch_horizontal_action_->setEnabled(has_active_part_sketch);
    sketch_vertical_action_->setEnabled(has_active_part_sketch);
    sketch_coincident_action_->setEnabled(has_active_part_sketch);
    sketch_midpoint_action_->setEnabled(has_active_part_sketch);
    sketch_symmetric_action_->setEnabled(has_active_part_sketch);
    sketch_concentric_action_->setEnabled(has_active_part_sketch);
    sketch_tangent_action_->setEnabled(has_active_part_sketch);
    sketch_parallel_action_->setEnabled(has_active_part_sketch);
    sketch_perpendicular_action_->setEnabled(has_active_part_sketch);
    sketch_equal_length_action_->setEnabled(has_active_part_sketch);
    sketch_dimension_action_->setEnabled(has_active_part_sketch);
    sketch_dimension_x_action_->setEnabled(has_active_part_sketch);
    sketch_dimension_y_action_->setEnabled(has_active_part_sketch);
    sketch_point_line_dimension_action_->setEnabled(has_active_part_sketch);
    sketch_symmetric_dimension_action_->setEnabled(has_active_part_sketch);
    sketch_three_point_angle_dimension_action_->setEnabled(has_active_part_sketch);
    sketch_angle_dimension_action_->setEnabled(has_active_part_sketch);
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

void AssemblyWorkspaceWindow::populate_sketch_tree(
    const zima::sketcher::Sketch& sketch) {
    tree_->setHeaderLabels({tr("SKETCHER — %1").arg(
        QString::fromStdString(sketch.name))});
    auto* origin = new QTreeWidgetItem(tree_, {
        tr("Počátek kontejneru — %1").arg(QString::fromStdString(sketch.name))});
    origin->setIcon(0, resource_icon("origin"));
    origin->setFlags(Qt::ItemIsEnabled);
    const std::array origin_children{
        std::pair{QStringLiteral("Lokální počátek"), "point"},
        std::pair{QStringLiteral("X"), "axis"},
        std::pair{QStringLiteral("Y"), "axis"},
        std::pair{QStringLiteral("Z"), "axis"},
        std::pair{QStringLiteral("XY"), "plane"},
        std::pair{QStringLiteral("YZ"), "plane"},
        std::pair{QStringLiteral("XZ"), "plane"}};
    for (std::size_t index = 0; index < origin_children.size(); ++index) {
        const auto& [label, icon] = origin_children[index];
        auto* child = new QTreeWidgetItem(origin, {label});
        child->setIcon(0, resource_icon(icon));
        const QString semantic_key = index == 0
            ? QStringLiteral("external_point:sketch_origin")
            : index == 1 ? QStringLiteral("sketch_axis:x")
            : index == 2 ? QStringLiteral("sketch_axis:y") : QString{};
        if (!semantic_key.isEmpty()) {
            child->setData(0, Qt::UserRole, semantic_key);
            child->setData(0, Qt::UserRole + 3,
                QStringLiteral("sketch-origin-reference"));
            child->setData(0, Qt::UserRole + 4,
                QString::fromStdString(sketch.id));
        } else {
            child->setFlags(Qt::ItemIsEnabled);
        }
    }
    origin->setExpanded(true);

    if (!sketch.external_references.empty()) {
        auto* references = new QTreeWidgetItem(tree_, {tr("Reference")});
        references->setIcon(0, resource_icon("sketch-reference"));
        references->setFlags(Qt::ItemIsEnabled);
        const auto source_display_name = [this](
                const zima::sketcher::SketchExternalReference& reference) {
            QString document_name;
            QString owner_name;
            if (const auto* part = workspace_.open_part(
                    reference.source_document_id)) {
                document_name = QString::fromStdString(
                    part->session.document().name);
                if (const auto* container = part->session.document().find_container(
                        reference.source_owner_id)) {
                    owner_name = QString::fromStdString(container->name);
                } else if (const auto* construction =
                               part->session.document().find_construction(
                                   reference.source_owner_id)) {
                    owner_name = QString::fromStdString(construction->name);
                }
            } else if (const auto* assembly = workspace_.open_assembly(
                           reference.source_document_id)) {
                document_name = QString::fromStdString(
                    assembly->session.document().name);
                if (const auto* construction =
                        assembly->session.document().find_construction(
                            reference.source_owner_id)) {
                    owner_name = QString::fromStdString(construction->name);
                }
            }
            if (document_name.isEmpty()) {
                return QString::fromStdString(reference.source_owner_id);
            }
            return owner_name.isEmpty() || owner_name == document_name
                ? document_name
                : tr("%1 — %2").arg(document_name, owner_name);
        };
        for (const auto& reference : sketch.external_references) {
            const QString kind = reference.kind ==
                    zima::sketcher::ExternalReferenceKind::Face ? tr("plocha")
                : reference.kind == zima::sketcher::ExternalReferenceKind::Edge
                    ? tr("hrana")
                : reference.kind == zima::sketcher::ExternalReferenceKind::Axis
                    ? tr("osa") : tr("bod");
            auto* item = new QTreeWidgetItem(references, {
                source_display_name(reference) +
                QStringLiteral(" · ") + kind +
                (reference.broken ? tr(" — přerušená") : QString{})});
            item->setIcon(0, resource_icon("sketch-reference"));
            item->setData(0, Qt::UserRole, QString::fromStdString(reference.id));
            item->setData(0, Qt::UserRole + 3, "sketch-external-reference");
            item->setData(0, Qt::UserRole + 4, QString::fromStdString(sketch.id));
            item->setSelected(reference.id == selected_sketch_external_reference_id_);
        }
        references->setExpanded(true);
    }

    auto* geometry = new QTreeWidgetItem(tree_, {tr("Geometrie")});
    geometry->setIcon(0, resource_icon("sketch"));
    geometry->setFlags(Qt::ItemIsEnabled);
    auto* constraints = new QTreeWidgetItem(tree_, {tr("Vazby")});
    constraints->setIcon(0, resource_icon("sketch-constraints"));
    constraints->setFlags(Qt::ItemIsEnabled);
    auto* dimensions = new QTreeWidgetItem(tree_, {tr("Kóty")});
    dimensions->setIcon(0, resource_icon("sketch-dimensions"));
    dimensions->setFlags(Qt::ItemIsEnabled);
    const auto geometry_item = [&](const std::string& id, const QString& label,
                                   const QString& icon) {
        auto* item = new QTreeWidgetItem(geometry, {label});
        item->setIcon(0, resource_icon(icon));
        item->setData(0, Qt::UserRole, QString::fromStdString(id));
        item->setData(0, Qt::UserRole + 3, "sketch-geometry");
        item->setData(0, Qt::UserRole + 4, QString::fromStdString(sketch.id));
        return item;
    };
    const auto indexed_geometry_label = [&](const QString& kind, int index,
                                            bool construction) {
        const auto indexed = kind + QStringLiteral("%1").arg(
            index, 3, 10, QLatin1Char('0'));
        return construction
            ? tr("Pomocná geometrie · %1").arg(indexed)
            : indexed;
    };
    int point_index = 0;
    std::unordered_map<std::string, QString> point_labels;
    for (const auto& point : sketch.points) {
        const QString label = indexed_geometry_label(
            tr("Bod"), ++point_index, point.construction);
        point_labels.emplace(point.id, label);
        auto* item = geometry_item(point.id, label, "point");
        item->setSelected(point.id == selected_sketch_point_id_);
    }
    int segment_index = 0;
    for (const auto& segment : sketch.segments) {
        const int index = ++segment_index;
        const QString segment_label = segment.centerline
            ? tr("Konstrukční čára%1").arg(
                index, 3, 10, QLatin1Char('0'))
            : indexed_geometry_label(tr("Úsečka"), index,
                segment.construction);
        auto* item = geometry_item(segment.id,
            segment_label,
            "sketch");
        item->setSelected(segment.id == selected_sketch_segment_id_);
        const auto append_endpoint = [&](const std::string& point_id,
                                         const QString& role) {
            const auto label = point_labels.find(point_id);
            auto* endpoint = new QTreeWidgetItem(item, {
                tr("%1 — %2").arg(role,
                    label == point_labels.end()
                        ? tr("Chybějící bod") : label->second)});
            endpoint->setIcon(0, resource_icon("point"));
            endpoint->setData(
                0, Qt::UserRole, QString::fromStdString(point_id));
            endpoint->setData(0, Qt::UserRole + 3, "sketch-geometry");
            endpoint->setData(
                0, Qt::UserRole + 4, QString::fromStdString(sketch.id));
        };
        append_endpoint(segment.first_point_id, tr("Počáteční bod"));
        append_endpoint(segment.second_point_id, tr("Koncový bod"));
    }
    int circle_index = 0;
    for (const auto& circle : sketch.circles) {
        auto* item = geometry_item(circle.id,
            indexed_geometry_label(tr("Kružnice"), ++circle_index,
                circle.construction), "sketch");
        item->setSelected(circle.id == selected_sketch_circle_id_);
    }
    int arc_index = 0;
    for (const auto& arc : sketch.arcs) geometry_item(arc.id,
        indexed_geometry_label(tr("Oblouk"), ++arc_index, arc.construction), "sketch")
            ->setSelected(arc.id == selected_sketch_arc_id_);
    int ellipse_index = 0;
    for (const auto& ellipse : sketch.ellipses) geometry_item(ellipse.id,
        indexed_geometry_label(tr("Elipsa"), ++ellipse_index,
            ellipse.construction), "sketch")
            ->setSelected(ellipse.id == selected_sketch_ellipse_id_);
    int elliptical_arc_index = 0;
    for (const auto& arc : sketch.elliptical_arcs) geometry_item(arc.id,
        indexed_geometry_label(tr("Eliptický oblouk"),
            ++elliptical_arc_index, arc.construction), "sketch")
            ->setSelected(arc.id == selected_sketch_elliptical_arc_id_);
    int spline_index = 0;
    for (const auto& spline : sketch.bsplines) geometry_item(spline.id,
        indexed_geometry_label(tr("Spline"), ++spline_index,
            spline.construction), "sketch")
            ->setSelected(spline.id == selected_sketch_bspline_id_);
    int text_index = 0;
    for (const auto& text : sketch.texts) geometry_item(text.id,
        tr("Text%1").arg(++text_index, 3, 10, QLatin1Char('0')), "sketch")
            ->setSelected(text.id == selected_sketch_text_id_);
    int constraint_index = 0;
    for (const auto& constraint : sketch.constraints) {
        auto* item = new QTreeWidgetItem(constraints, {
            sketch_constraint_label(constraint.kind) +
            QStringLiteral("%1").arg(++constraint_index, 3, 10, QLatin1Char('0'))});
        item->setIcon(0, resource_icon("sketch-constraints"));
        item->setData(0, Qt::UserRole, QString::fromStdString(constraint.id));
        item->setData(0, Qt::UserRole + 3, "part-sketch-constraint");
        item->setData(0, Qt::UserRole + 4, QString::fromStdString(sketch.id));
    }
    int dimension_index = 0;
    for (const auto& dimension : sketch.dimensions) {
        auto* item = new QTreeWidgetItem(dimensions, {
            sketch_dimension_label(dimension) + QStringLiteral(" · %1")
                .arg(++dimension_index, 3, 10, QLatin1Char('0'))});
        item->setIcon(0, resource_icon("sketch-dimensions"));
        item->setData(0, Qt::UserRole, QString::fromStdString(dimension.id));
        item->setData(0, Qt::UserRole + 3, "part-sketch-dimension");
        item->setData(0, Qt::UserRole + 4, QString::fromStdString(sketch.id));
    }
    geometry->setExpanded(true);
    constraints->setExpanded(true);
    dimensions->setExpanded(true);
}

void AssemblyWorkspaceWindow::add_part_tree_children(
    QTreeWidgetItem* parent,
    const zima::document::PartDocument& document) {
    const auto construction_path = active_occurrence_path_.empty()
        ? zima::assembly::InstancePath{}
        : zima::assembly::InstancePath::decode(active_occurrence_path_);
    add_origin_tree_item(parent, document.document_id, false, construction_path);
    for (std::size_t index = 0; index < document.history.size(); ++index) {
        const auto& container = document.history[index];
        const QString operation = container.combine_mode ==
                zima::document::CombineMode::Subtract
            ? QStringLiteral("− ") : QStringLiteral("+ ");
        auto* item = new QTreeWidgetItem(parent,
            {operation + QString::fromStdString(container.name) +
             (container.suppressed ? tr(" [potlačeno]") : QString{})});
        item->setData(0, Qt::UserRole, QString::fromStdString(container.id));
        item->setData(0, Qt::UserRole + 3, "part-container");
        item->setIcon(0, resource_icon(feature_icon_name(container.feature_kind)));
        const auto owned_sketch = std::find_if(document.sketches.begin(),
            document.sketches.end(), [&](const auto& sketch) {
                return sketch.owner_container_id == container.id;
            });
        add_history_container_tree_children(item, container, construction_path,
            owned_sketch == document.sketches.end() ? nullptr : &*owned_sketch);
        if (container.suppressed) {
            item->setForeground(0, QBrush(QColor(125, 125, 125)));
            auto font = item->font(0);
            font.setItalic(true);
            font.setStrikeOut(true);
            item->setFont(0, font);
        }
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
        if (!sketch.owner_container_id.empty()) continue;
        auto* item = new QTreeWidgetItem(
            parent, {QString::fromStdString(sketch.name) +
                (sketch.suppressed ? tr(" [potlačeno]") : QString{})});
        item->setData(0, Qt::UserRole, QString::fromStdString(sketch.id));
        item->setData(0, Qt::UserRole + 3, "part-sketch");
        item->setSelected(sketch.id == selected_sketch_id_);
        if (sketch.suppressed) {
            auto font = item->font(0);
            font.setItalic(true);
            font.setStrikeOut(true);
            item->setFont(0, font);
            item->setForeground(0, QBrush(QColor(128, 128, 128)));
        }
        for (const auto& constraint : sketch.constraints) {
            auto* child = new QTreeWidgetItem(
                item, {sketch_constraint_label(constraint.kind)});
            child->setData(0, Qt::UserRole,
                QString::fromStdString(constraint.id));
            child->setData(0, Qt::UserRole + 3, "part-sketch-constraint");
            child->setData(0, Qt::UserRole + 4,
                QString::fromStdString(sketch.id));
        }
        for (const auto& dimension : sketch.dimensions) {
            auto* child = new QTreeWidgetItem(
                item, {sketch_dimension_label(dimension)});
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
            parent, {QString::fromStdString(object.name) +
                (object.suppressed ? tr(" [potlačeno]") : QString{})});
        item->setData(0, Qt::UserRole, QString::fromStdString(object.id));
        item->setData(0, Qt::UserRole + 1,
            QString::fromStdString(construction_path.encoded()));
        item->setData(0, Qt::UserRole + 3, "part-construction");
        if (object.suppressed) {
            auto font = item->font(0);
            font.setItalic(true);
            font.setStrikeOut(true);
            item->setFont(0, font);
            item->setForeground(0, QBrush(QColor(128, 128, 128)));
        }
        item->setIcon(0, resource_icon(
            object.kind == zima::document::ConstructionKind::Point ? "point"
                : object.kind == zima::document::ConstructionKind::Axis
                    ? "axis" : "plane"));
        add_construction_tree_children(item, object, construction_path);
    }
    if (!document.history_order.empty()) {
        std::map<std::string, QTreeWidgetItem*> items_by_id;
        for (int index = parent->childCount() - 1; index >= 1; --index) {
            auto* child = parent->child(index);
            const auto role = child->data(0, Qt::UserRole + 3).toString();
            if (role != QStringLiteral("part-container") &&
                role != QStringLiteral("part-sketch") &&
                role != QStringLiteral("part-construction")) continue;
            child = parent->takeChild(index);
            items_by_id.emplace(
                child->data(0, Qt::UserRole).toString().toStdString(), child);
        }
        int insertion = 1;
        for (const auto& entry : document.history_order) {
            const auto found = items_by_id.find(entry.id);
            if (found == items_by_id.end()) continue;
            parent->insertChild(insertion++, found->second);
            items_by_id.erase(found);
        }
        for (const auto& [id, item] : items_by_id) {
            static_cast<void>(id);
            parent->insertChild(insertion++, item);
        }
    }
    const int cursor_position = 1 + static_cast<int>(
        document.effective_history_cursor());
    auto* body = new QTreeWidgetItem({tr("Těleso")});
    body->setIcon(0, resource_icon("result-body"));
    body->setData(0, Qt::UserRole, QString::fromStdString(document.document_id));
    body->setData(0, Qt::UserRole + 3, "part-result-body");
    auto* insert_here = new QTreeWidgetItem({tr("← Vložit zde")});
    insert_here->setData(0, Qt::UserRole + 3, "part-insert-here");
    auto font = insert_here->font(0);
    font.setBold(true);
    insert_here->setFont(0, font);
    insert_here->setForeground(0, QBrush(QColor("#4DD811")));
    parent->insertChild(std::min(cursor_position, parent->childCount()), body);
    parent->insertChild(std::min(cursor_position + 1, parent->childCount()),
        insert_here);
}

void AssemblyWorkspaceWindow::add_assembly_tree_children(
    QTreeWidgetItem* parent,
    const std::string& assembly_document_id,
    const zima::assembly::InstancePath& parent_path,
    bool ancestor_suppressed) {
    const auto* assembly = workspace_.open_assembly(assembly_document_id);
    if (assembly == nullptr) return;
    if (assembly_document_id == workspace_.active_document_id()) {
        add_origin_tree_item(parent, assembly_document_id, true, parent_path);
        for (const auto& sketch : assembly->session.document().sketches) {
            auto* item = new QTreeWidgetItem(
                parent, {QString::fromStdString(sketch.name)});
            item->setData(0, Qt::UserRole, QString::fromStdString(sketch.id));
            item->setData(0, Qt::UserRole + 3, "assembly-sketch");
            item->setSelected(sketch.id == selected_sketch_id_);
        }
        for (std::size_t cut_index = 0;
             cut_index < assembly->session.document().cuts.size(); ++cut_index) {
            const auto& cut = assembly->session.document().cuts[cut_index];
            auto* item = new QTreeWidgetItem(
                parent, {QString::fromStdString(cut.definition.name) +
                    (cut.definition.suppressed
                        ? tr(" [potlačeno]") : QString{})});
            item->setData(0, Qt::UserRole,
                QString::fromStdString(cut.definition.id));
            item->setData(0, Qt::UserRole + 3, "assembly-cut");
            if (cut.definition.suppressed || (assembly_cut_rollback_ &&
                    cut_index > assembly_cut_rollback_->cut_index)) {
                item->setForeground(0, QBrush(QColor(125, 125, 125)));
            } else if (assembly_cut_rollback_ &&
                       cut_index == assembly_cut_rollback_->cut_index) {
                item->setForeground(0, QBrush(QColor(70, 190, 95)));
                QFont font = item->font(0);
                font.setBold(true);
                item->setFont(0, font);
            }
        }
        for (const auto& object : assembly->session.document().constructions) {
            auto* item = new QTreeWidgetItem(
                parent, {QString::fromStdString(object.name)});
            item->setData(0, Qt::UserRole, QString::fromStdString(object.id));
            item->setData(0, Qt::UserRole + 1,
                QString::fromStdString(parent_path.encoded()));
            item->setData(0, Qt::UserRole + 3, "assembly-construction");
            item->setIcon(0, resource_icon(
                object.kind == zima::document::ConstructionKind::Point ? "point"
                    : object.kind == zima::document::ConstructionKind::Axis
                        ? "axis" : "plane"));
            add_construction_tree_children(item, object, parent_path);
        }
    }
    add_snapshot_tree_children(
        parent, assembly->session.document().occurrence_snapshot(),
        assembly_document_id, parent_path, ancestor_suppressed);
    if (assembly_document_id == workspace_.active_document_id() &&
        parent_path.occurrence_ids.empty()) {
        auto* insert_here = new QTreeWidgetItem(parent, {tr("← Vložit zde")});
        insert_here->setData(0, Qt::UserRole + 3, "assembly-insert-here");
        auto font = insert_here->font(0);
        font.setBold(true);
        insert_here->setFont(0, font);
        insert_here->setForeground(0, QBrush(QColor("#4DD811")));
    }
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
            add_origin_tree_item(item, component.source_document_id, true, path);
            const auto* active_source = active_occurrence
                ? workspace_.open_assembly(component.source_document_id) : nullptr;
            if (active_source != nullptr) {
                for (const auto& object :
                     active_source->session.document().constructions) {
                    auto* construction_item = new QTreeWidgetItem(
                        item, {QString::fromStdString(object.name)});
                    construction_item->setData(0, Qt::UserRole,
                        QString::fromStdString(object.id));
                    construction_item->setData(0, Qt::UserRole + 1,
                        QString::fromStdString(path.encoded()));
                    construction_item->setData(
                        0, Qt::UserRole + 3, "assembly-construction");
                }
                add_snapshot_tree_children(
                    item, active_source->session.document().occurrence_snapshot(),
                    component.source_document_id, path,
                    suppressed || !component.visible);
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
        } else {
            add_origin_tree_item(item, component.source_document_id, false, path);
        }
    }
}

void AssemblyWorkspaceWindow::select_container(const std::string& container_id) {
    auto* root = tree_->topLevelItem(0);
    if (root == nullptr) return;
    QTreeWidgetItem* fallback{};
    std::vector<QTreeWidgetItem*> pending{root};
    while (!pending.empty()) {
        auto* item = pending.back();
        pending.pop_back();
        if (item->data(0, Qt::UserRole).toString().toStdString() == container_id) {
            const auto role = item->data(0, Qt::UserRole + 3).toString();
            // A Sketch and its owned Plane intentionally share the Sketch
            // ID in the Tree. View selection of the complete Sketch must
            // synchronize to the Sketch row, not stop on its Plane sibling.
            if (role == QStringLiteral("part-sketch") ||
                role == QStringLiteral("assembly-sketch")) {
                selected_sketch_id_ = container_id;
                tree_->setCurrentItem(item);
                return;
            }
            if (fallback == nullptr) fallback = item;
        }
        for (int index = 0; index < item->childCount(); ++index) {
            pending.push_back(item->child(index));
        }
    }
    if (fallback != nullptr) tree_->setCurrentItem(fallback);
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
            // Apply the embedded placement-reference rows (Python-style,
            // stored directly on the occurrence) using the same solver
            // family as calculate_mates() -- explicit, on-commit only, no
            // auto-regeneration on later unrelated commits.
            next.calculate_placement_references();
            assembly->session.commit(std::move(next));
        }, this);
    dialog->set_reference_request_callback(
        [this](std::size_t index, bool component_side) {
            start_component_placement_reference_selection(index, component_side);
        });
    component_placement_dialog_ = dialog;
    component_placement_assembly_document_id_ = address->owner_assembly_document_id;
    component_placement_occurrence_id_ = address->occurrence_id;
    properties_dialog_ = dialog;
    properties_dialog_instance_path_ = instance_path;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        properties_dialog_instance_path_.clear();
        component_placement_dialog_ = nullptr;
        component_placement_assembly_document_id_.clear();
        component_placement_occurrence_id_.clear();
        pending_component_placement_index_.reset();
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_candidate_filter({});
        viewer_->clear_selection();
        refresh_scene();
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::focus_parameter_dimension_field(
    const std::string& semantic_key) {
    if (properties_dialog_ == nullptr ||
        !semantic_key.starts_with("parameter:")) return;
    const auto key = QString::fromStdString(semantic_key.substr(10));
    const std::map<QString, std::vector<QString>> names{
        {"x", {"constructionX"}}, {"y", {"constructionY"}},
        {"z", {"constructionZ"}}, {"offset", {"constructionOffset"}},
        {"length", {"boxLength", "pyramidLength", "wedgeLength",
            "constructionDisplaySize"}},
        {"width", {"boxWidth", "pyramidWidth", "wedgeWidth"}},
        {"height", {"boxHeight", "cylinderHeight", "coneHeight",
            "pyramidHeight", "wedgeHeight"}},
        {"radius", {"cylinderRadius", "sphereRadius"}},
        {"bottom_radius", {"coneBottomRadius"}},
        {"top_radius", {"coneTopRadius"}},
        {"top_offset", {"wedgeTopOffset"}},
        {"length_forward", {"extrusionHeight"}},
        {"length_reverse", {"extrusionReverseLength"}},
        {"profile_offset", {"profilePlaneOffset", "sketchPlaneOffset"}},
        {"angle", {"revolutionAngle"}},
        {"size", {"edgeTreatmentSize"}}};
    const auto found = names.find(key);
    if (found == names.end()) return;
    for (const auto& name : found->second) {
        if (auto* field = properties_dialog_->findChild<QDoubleSpinBox*>(name);
            field != nullptr && field->isEnabled() && field->isVisible()) {
            field->setFocus();
            field->selectAll();
            return;
        }
    }
}

void AssemblyWorkspaceWindow::show_tree_item_properties(QTreeWidgetItem* item) {
    if (item == nullptr) return;
    const auto kind = item->data(0, Qt::UserRole + 3).toString();
    const auto id = item->data(0, Qt::UserRole).toString().toStdString();
    const auto instance_path =
        item->data(0, Qt::UserRole + 1).toString().toStdString();
    if (kind == QStringLiteral("part-container") ||
        kind == QStringLiteral("part-construction") ||
        kind == QStringLiteral("assembly-construction") ||
        kind == QStringLiteral("part-sketch") ||
        kind == QStringLiteral("assembly-sketch") ||
        kind == QStringLiteral("assembly-cut")) {
        construction_dimension_object_id_ = id;
        preserve_view_on_refresh_ = true;
        refresh_scene();
    }
    if (kind == QStringLiteral("part-container")) {
        const auto* part = workspace_.open_part(workspace_.active_document_id());
        const auto* container = part == nullptr
            ? nullptr : part->session.document().find_container(id);
        if (container != nullptr && container->feature_kind ==
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
        } else if (container != nullptr) {
            show_primitive_properties(container->feature_kind, id);
        }
    } else if (kind == QStringLiteral("assembly-cut")) {
        const auto* assembly =
            workspace_.open_assembly(workspace_.active_document_id());
        const auto* cut = assembly == nullptr
            ? nullptr : assembly->session.document().find_cut(id);
        if (cut != nullptr) show_primitive_properties(cut->definition.feature_kind, id);
    } else if (kind == QStringLiteral("part-construction") ||
               kind == QStringLiteral("assembly-construction")) {
        const auto* part = workspace_.open_part(workspace_.active_document_id());
        const auto* assembly =
            workspace_.open_assembly(workspace_.active_document_id());
        const auto* object = part != nullptr
            ? part->session.document().find_construction(id)
            : assembly != nullptr
                ? assembly->session.document().find_construction(id) : nullptr;
        if (object != nullptr) show_construction_properties(object->kind, id);
    } else if (kind == QStringLiteral("part-sketch") ||
               kind == QStringLiteral("assembly-sketch")) {
        show_sketch_properties(id);
    } else if (kind == QStringLiteral("part-sketch-dimension")) {
        show_sketch_dimension_properties(
            item->data(0, Qt::UserRole + 4).toString().toStdString(), id);
    } else if (!instance_path.empty()) {
        show_component_properties(instance_path);
    }
}

bool AssemblyWorkspaceWindow::activate_occurrence_for_test(
    const std::string& instance_path) {
    const std::string top_assembly_id = workspace_.displayed_document_id();
    std::optional<zima::workspace::OccurrenceAddress> address;
    try {
        address = workspace_.resolve_occurrence(
            top_assembly_id, zima::assembly::InstancePath::decode(instance_path));
    } catch (const std::invalid_argument&) {
        return false;
    }
    if (!address) return false;
    auto* assembly = workspace_.open_assembly(address->owner_assembly_document_id);
    if (assembly == nullptr) return false;
    const auto* occurrence = assembly->session.document().find_occurrence(
        address->occurrence_id);
    if (occurrence == nullptr) return false;
    const bool source_is_assembly =
        address->source_kind == zima::assembly::ComponentSourceKind::Assembly;
    try {
        if (workspace_.find(address->source_document_id) == nullptr) {
            if (occurrence->source_path.empty() ||
                !open_document_path(QString::fromStdString(
                    occurrence->source_path.string()))) {
                return false;
            }
        }
        const auto activated = workspace_.activate_occurrence(
            top_assembly_id, zima::assembly::InstancePath::decode(instance_path));
        if (!activated) return false;
        active_occurrence_path_ = instance_path;
        active_sketch_id_.clear();
        selected_sketch_id_.clear();
        active_application_ = source_is_assembly
            ? ApplicationMode::Assembly : ApplicationMode::Modeling;
        refresh_tabs();
        refresh_scene();
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

void AssemblyWorkspaceWindow::deactivate_active_occurrence_for_test() {
    const std::string displayed = workspace_.displayed_document_id();
    if (workspace_.open_assembly(displayed) == nullptr) return;
    workspace_.activate(displayed);
    active_occurrence_path_.clear();
    active_sketch_id_.clear();
    selected_sketch_id_.clear();
    active_application_ = ApplicationMode::Assembly;
    refresh_tabs();
    refresh_scene();
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
    const bool is_active_occurrence =
        workspace_.active_document_id() == address->source_document_id &&
        active_occurrence_path_ == instance_path;
    const bool source_is_assembly =
        address->source_kind == zima::assembly::ComponentSourceKind::Assembly;
    QMenu menu(this);
    const auto parent_path =
        zima::assembly::InstancePath::decode(instance_path).parent();
    auto* select_parent = parent_path
        ? menu.addAction(tr("Vybrat rodiče")) : nullptr;
    auto* activate_or_deactivate = is_active_occurrence
        ? menu.addAction(tr("Zpět do sestavy"))
        : menu.addAction(source_is_assembly
            ? tr("Aktivovat podsestavu") : tr("Aktivovat komponentu"));
    auto* properties = menu.addAction(tr("Vlastnosti"));
    auto* visibility = menu.addAction(
        occurrence->visible ? tr("Skrýt") : tr("Zobrazit"));
    auto* suppression = menu.addAction(
        occurrence->suppressed ? tr("Obnovit") : tr("Potlačit"));
    auto* grounding = menu.addAction(
        occurrence->grounded ? tr("Uvolnit") : tr("Uzemnit"));
    auto* remove = menu.addAction(tr("Odstranit"));
    const QAction* selected = menu.exec(global_position);
    if (selected == select_parent && parent_path) {
        const std::string encoded = parent_path->encoded();
        viewer_->confirm_occurrence(encoded);
        select_occurrence(encoded);
        return;
    }
    if (selected == activate_or_deactivate) {
        if (is_active_occurrence) {
            deactivate_active_occurrence_for_test();
        } else if (!activate_occurrence_for_test(instance_path)) {
            QMessageBox::critical(this, tr("Aktivace selhala"),
                tr("Zdrojový dokument komponenty se nepodařilo otevřít nebo aktivovat."));
        }
        return;
    }
    if (selected == properties) {
        show_component_properties(instance_path);
        return;
    }
    if (selected != visibility && selected != suppression &&
        selected != grounding && selected != remove) return;
    if (selected == remove && QMessageBox::question(
            this, tr("Odstranit komponentu"),
            tr("Opravdu chcete komponentu ze sestavy odstranit?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
        QMessageBox::Yes) return;
    auto next = assembly->session.document();
    auto found = std::find_if(next.components.begin(), next.components.end(),
        [&](const auto& item) { return item.occurrence_id == address->occurrence_id; });
    if (found == next.components.end()) return;
    if (selected == remove) {
        const auto uses_occurrence = [&](const auto& path) {
            return !path.occurrence_ids.empty() &&
                path.occurrence_ids.front() == address->occurrence_id;
        };
        const bool used_by_placement_reference = std::any_of(
            next.components.begin(), next.components.end(), [&](const auto& component) {
                return std::any_of(component.placement_references.begin(),
                    component.placement_references.end(), [&](const auto& row) {
                        return uses_occurrence(row.component_reference.instance_path) ||
                            uses_occurrence(row.target_reference.instance_path);
                    });
            });
        const bool used_by_dependency = std::any_of(
            next.dependencies.begin(), next.dependencies.end(), [&](const auto& edge) {
                return edge.dependent_occurrence_id == address->occurrence_id ||
                    edge.prerequisite_occurrence_id == address->occurrence_id;
            });
        const bool used_by_sketch = std::any_of(
            next.sketches.begin(), next.sketches.end(), [&](const auto& sketch) {
                return std::any_of(sketch.external_references.begin(),
                    sketch.external_references.end(), [&](const auto& reference) {
                        if (reference.context_assembly_document_id != next.document_id ||
                            reference.source_instance_path.empty()) return false;
                        try {
                            return uses_occurrence(zima::assembly::InstancePath::decode(
                                reference.source_instance_path));
                        } catch (const std::invalid_argument&) {
                            return false;
                        }
                    });
            });
        if (used_by_placement_reference || used_by_dependency || used_by_sketch) {
            QMessageBox::warning(this, tr("Komponentu nelze odstranit"),
                tr("Komponenta je použita vazbou, závislostí nebo "
                   "externí referencí skici."));
            return;
        }
        const auto assembly_id = next.document_id;
        const auto occurrence_id = address->occurrence_id;
        workspace_.regenerate_assembly_from_open_dependencies(assembly_id);
        assembly = workspace_.open_assembly(assembly_id);
        if (assembly == nullptr) return;
        next = assembly->session.document();
        found = std::find_if(next.components.begin(), next.components.end(),
            [&](const auto& item) { return item.occurrence_id == occurrence_id; });
        if (found == next.components.end()) return;
        for (auto& cut : next.cuts) {
            std::erase(cut.target_occurrence_ids, occurrence_id);
            auto& extrusion = cut.definition.extrusion;
            bool lost_extent_target = false;
            if (!extrusion.target_face.instance_path.empty()) {
                try {
                    lost_extent_target = uses_occurrence(
                        zima::assembly::InstancePath::decode(
                            extrusion.target_face.instance_path));
                } catch (const std::invalid_argument&) {
                    lost_extent_target = true;
                }
            }
            if (lost_extent_target &&
                (extrusion.extent == zima::document::ExtrusionExtent::UpToPlane ||
                 extrusion.extent == zima::document::ExtrusionExtent::UpToSurface)) {
                extrusion.extent = zima::document::ExtrusionExtent::Blind;
                extrusion.target_face = {};
                extrusion.target_surface_triangles.clear();
            }
        }
        next.components.erase(found);
        next.calculate_placement_references();
        calculate_assembly_cuts(next);
        assembly->session.commit(std::move(next));
        refresh_tabs();
        refresh_scene();
        return;
    }
    if (selected == visibility) found->visible = !found->visible;
    if (selected == suppression) found->suppressed = !found->suppressed;
    if (selected == grounding) found->grounded = !found->grounded;
    next.calculate_placement_references();
    assembly->session.commit(std::move(next));
    refresh_tabs();
    refresh_scene();
}

}  // namespace zima::app
