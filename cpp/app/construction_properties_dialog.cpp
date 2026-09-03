#include "construction_properties_dialog.hpp"

#include "zima/ui/reference_cell.hpp"

#include <zima/kernel/stable_id.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/kernel/stable_id.hpp>

#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTableWidget>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <unordered_set>

namespace zima::app {
namespace {

bool is_curve_container_kind(zima::document::ConstructionKind kind) {
    return kind == zima::document::ConstructionKind::Curve3D ||
        kind == zima::document::ConstructionKind::Curve3DExperimental;
}

QString construction_properties_title(
    zima::document::ConstructionKind kind) {
    using zima::document::ConstructionKind;
    switch (kind) {
        case ConstructionKind::Point:
            return QObject::tr("Vlastnosti bodu");
        case ConstructionKind::Curve3D:
            return QObject::tr("Vlastnosti 3D křivky");
        case ConstructionKind::Curve3DExperimental:
            return QObject::tr(
                "Vlastnosti 3D trajektorie — EXPERIMENTÁLNÍ");
        case ConstructionKind::Axis:
            return QObject::tr("Vlastnosti osy");
        case ConstructionKind::Plane:
            return QObject::tr("Vlastnosti roviny");
    }
    return QObject::tr("Vlastnosti konstrukčního prvku");
}

QString readable_reference_kind(const std::string& semantic) {
    const auto key = QString::fromStdString(semantic);
    if (key == QStringLiteral("point") || key.contains(QStringLiteral("point")))
        return QObject::tr("Bod");
    if (key.startsWith(QStringLiteral("origin:axis:")))
        return QObject::tr("Osa %1").arg(key.sliced(12).toUpper());
    if (key == QStringLiteral("axis") || key.contains(QStringLiteral("axis")))
        return QObject::tr("Osa");
    if (key.startsWith(QStringLiteral("origin:plane:")))
        return QObject::tr("Rovina %1").arg(key.sliced(13).toUpper());
    if (key == QStringLiteral("plane") || key.contains(QStringLiteral("plane")))
        return QObject::tr("Rovina");
    if (key.contains(QStringLiteral("edge"))) return QObject::tr("Hrana");
    if (key.contains(QStringLiteral("face"))) return QObject::tr("Plocha");
    return QObject::tr("Geometrická reference");
}

void style_curve_switch_button(QToolButton* button, int width) {
    button->setFixedSize(width, 30);
    button->setStyleSheet(
        "QToolButton{color:#dddddd;background:#2f3339;"
        "border:1px solid #4a4f57;border-radius:4px;"
        "font-size:10px;font-weight:700;padding:0}"
        "QToolButton:hover{background:#3c414a;border-color:#6a7078}"
        "QToolButton:checked{color:#102027;background:#00d1ff;"
        "border-color:#00a9d1}"
        "QToolButton:disabled{color:#666666;background:#26282c;"
        "border-color:#35383e}");
}

zima::document::ConstructionObject sweep_dialog_path(
    const zima::document::HistoryContainer& container) {
    auto path = container.sweep3d.path;
    path.name = container.name;
    path.origin = {container.placement.x, container.placement.y,
        container.placement.z};
    path.rotation = {container.placement.rotation_x,
        container.placement.rotation_y, container.placement.rotation_z};
    path.absolute_rotation = {container.placement.absolute_rotation_x,
        container.placement.absolute_rotation_y,
        container.placement.absolute_rotation_z};
    path.rotation_offset_x = container.placement.rotation_offset_x;
    path.rotation_offset_y = container.placement.rotation_offset_y;
    path.rotation_offset_z = container.placement.rotation_offset_z;
    path.orientation_back = container.placement.orientation_back;
    path.orientation_quarter_turns =
        container.placement.orientation_quarter_turns;
    path.references = container.placement.references;
    return path;
}

}  // namespace

ConstructionPropertiesDialog::ConstructionPropertiesDialog(
    const zima::document::ConstructionObject& initial, bool edit_mode,
    CommitCallback commit, QWidget* parent, int decimal_places)
    : PropertiesSubWindow(construction_properties_title(initial.kind), parent),
      initial_(initial), commit_(std::move(commit)),
      curve_points_(initial.curve_points),
      curve_connections_(initial.curve_connections) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    // Keep Point/Axis/Plane properties as compact as Sketch and feature
    // properties. The reference table stretches inside this width; the old
    // 460 px minimum only left a needlessly wide empty middle column.
    setMinimumWidth(340);
    setMaximumWidth(360);
    auto compact_font = font();
    compact_font.setPixelSize(10);
    setFont(compact_font);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(initial.name), this);
    form->addRow(tr("Název"), name_);
    definition_ = new QComboBox(this);
    definition_->setObjectName("constructionDefinition");
    definition_->addItem(tr("Absolutní souřadnice"),
        static_cast<int>(zima::document::ConstructionDefinition::Absolute));
    definition_->addItem(tr("Geometrické reference"),
        static_cast<int>(zima::document::ConstructionDefinition::PointReference));
    if (initial.kind == zima::document::ConstructionKind::Axis) {
        definition_->addItem(tr("Podle dvou bodů"),
            static_cast<int>(zima::document::ConstructionDefinition::TwoPointAxis));
        definition_->addItem(tr("Podle osy nebo lineární hrany"),
            static_cast<int>(zima::document::ConstructionDefinition::AxisReference));
    } else if (initial.kind == zima::document::ConstructionKind::Plane) {
        definition_->addItem(tr("Podle tří bodů"),
            static_cast<int>(zima::document::ConstructionDefinition::ThreePointPlane));
        definition_->addItem(tr("Rovnoběžně s plochou / rovinou"),
            static_cast<int>(zima::document::ConstructionDefinition::PlaneReference));
    }
    const auto definition_index = definition_->findData(
        static_cast<int>(initial.definition));
    definition_->setCurrentIndex(std::max(0, definition_index));
    definition_->hide();
    auto* container_type = new QComboBox(this);
    container_type->addItem(tr("Bod"));
    container_type->addItem(tr("3D křivka"));
    container_type->addItem(tr("3D trajektorie — EXPERIMENTÁLNÍ"));
    container_type->addItem(tr("Osa"));
    container_type->addItem(tr("Rovina"));
    container_type->setCurrentIndex(initial.kind ==
            zima::document::ConstructionKind::Point ? 0
        : initial.kind == zima::document::ConstructionKind::Curve3D ? 1
        : initial.kind ==
                zima::document::ConstructionKind::Curve3DExperimental ? 2
        : initial.kind == zima::document::ConstructionKind::Axis ? 3 : 4);
    container_type->setEnabled(false);
    form->addRow(tr("Typ kontejneru"), container_type);
    const int precision = std::clamp(decimal_places, 0, 12);
    const auto field = [this, precision](
            double value, const char* name, const QString& suffix) {
        auto* result = new QDoubleSpinBox(this);
        result->setRange(-1'000'000.0, 1'000'000.0);
        result->setDecimals(precision);
        result->setSingleStep(1.0);
        result->setSuffix(suffix);
        result->setObjectName(name);
        result->setValue(value);
        return result;
    };
    offset_ = field(initial.offset, "constructionOffset", " mm");
    if (initial.kind != zima::document::ConstructionKind::Plane) {
        offset_->hide();
    }
    connect(definition_, &QComboBox::currentIndexChanged,
        this, [this] { refresh_definition_fields(); });
    refresh_definition_fields();
    if (initial.kind == zima::document::ConstructionKind::Axis) {
        display_size_ = field(initial.display_size, "constructionDisplaySize", " mm");
        display_size_->setRange(0.001, 1'000'000.0);
    }
    content_layout()->addLayout(form);
    placement_ = std::make_unique<zima::ui::ContainerPlacementSection>(
        this, content_layout(),
        /*with_orientation=*/true,
        // A Point fixes only the Container Origin translation.  Its local
        // frame still owns three rotational degrees of freedom and may be
        // oriented by one/two following plane or axis references, exactly
        // like every other container.
        /*position_rows_can_define_rotation=*/true,
        precision);
    zima::document::Placement numeric;
    numeric.x = initial.origin.x; numeric.y = initial.origin.y; numeric.z = initial.origin.z;
    const auto initial_absolute = initial.absolute_rotation;
    numeric.rotation_x = initial_absolute.x;
    numeric.rotation_y = initial_absolute.y;
    numeric.rotation_z = initial_absolute.z;
    numeric.absolute_rotation_x = initial_absolute.x;
    numeric.absolute_rotation_y = initial_absolute.y;
    numeric.absolute_rotation_z = initial_absolute.z;
    numeric.orientation_back = initial.orientation_back;
    numeric.orientation_quarter_turns = initial.orientation_quarter_turns;
    numeric.rotation_offset_x = initial.rotation_offset_x;
    numeric.rotation_offset_y = initial.rotation_offset_y;
    numeric.rotation_offset_z = initial.rotation_offset_z;
    placement_->initialize_numeric_values(numeric);
    const bool has_orientation_reference = std::any_of(
        initial.references.begin(), initial.references.end(),
        [](const auto& reference) { return reference.orientation_drives_rotation; });
    placement_->set_orientation_base_rotation(
        has_orientation_reference ? initial.rotation_base
                                  : initial.absolute_rotation,
        has_orientation_reference);
    origin_ = placement_->translation_fields();
    rotation_ = placement_->rotation_fields();
    rotation_offset_ = placement_->rotation_offset_fields();
    origin_[0]->setObjectName("constructionX");
    origin_[1]->setObjectName("constructionY");
    origin_[2]->setObjectName("constructionZ");
    rotation_[0]->setObjectName("constructionRotationX");
    rotation_[1]->setObjectName("constructionRotationY");
    rotation_[2]->setObjectName("constructionRotationZ");
    rotation_offset_[0]->setObjectName("constructionRotationOffsetX");
    rotation_offset_[1]->setObjectName("constructionRotationOffsetY");
    rotation_offset_[2]->setObjectName("constructionRotationOffsetZ");
    placement_->initialize_from_references(initial.references,
        [](const std::string& semantic) { return readable_reference_kind(semantic); });
    reference_status_ = placement_->reference_status_label();
    dof_label_ = placement_->dof_label();
    placement_->reference_table()->setObjectName("constructionReferenceTable");
    if (placement_->orientation_table() != nullptr) {
        placement_->orientation_table()->setObjectName("constructionOrientationTable");
    }
    placement_->set_reference_request_callback(
        [this](std::size_t index) {
            if (reference_request_) reference_request_(index);
        });
    placement_->set_changed_callback([this] {
        refresh_offset_enabled_state();
        notify_preview();
    });
    placement_->set_highlights_changed_callback(
        [this] { if (reference_highlights_changed_) reference_highlights_changed_(); });
    placement_->refresh_reference_table();
    if (initial.kind == zima::document::ConstructionKind::Plane) {
        placement_->refresh_orientation_table();
    }
    const bool has_direction =
        initial.kind == zima::document::ConstructionKind::Axis ||
        initial.kind == zima::document::ConstructionKind::Plane;
    auto* rotation_form = new QFormLayout;
    if (has_direction) {
        direction_combo_ = new QComboBox(this);
        direction_combo_->setObjectName("constructionDirection");
        if (initial.kind == zima::document::ConstructionKind::Axis) {
            direction_combo_->addItem(QStringLiteral("X"), QStringLiteral("x"));
            direction_combo_->addItem(QStringLiteral("Y"), QStringLiteral("y"));
            direction_combo_->addItem(QStringLiteral("Z"), QStringLiteral("z"));
            const auto index = direction_combo_->findData(
                QString::fromStdString(initial.direction_axis));
            direction_combo_->setCurrentIndex(index >= 0 ? index : 1);
            rotation_form->addRow(tr("Směr"), direction_combo_);
        } else {
            direction_combo_->addItem(QStringLiteral("XY"), QStringLiteral("xy"));
            direction_combo_->addItem(QStringLiteral("YZ"), QStringLiteral("yz"));
            direction_combo_->addItem(QStringLiteral("XZ"), QStringLiteral("xz"));
            direction_combo_->hide();
        }
        if (initial.kind == zima::document::ConstructionKind::Axis) {
            rotation_form->addRow(tr("Délka zobrazení"), display_size_);
        }
    }
    content_layout()->addLayout(rotation_form);

    placement_->install_dof_label(content_layout());
    if (initial.kind == zima::document::ConstructionKind::Plane) {
        // "Work plane offset" is only meaningful once the user has chosen
        // what the Plane is parallel to / anchored on, so keep it at the
        // very bottom of the Plane dialog, right before the OK/Cancel row,
        // instead of splitting it away from the reference/orientation
        // controls at the top.
        auto* offset_form = new QFormLayout;
        base_plane_combo_ = new QComboBox(this);
        base_plane_combo_->setObjectName("constructionBasePlane");
        base_plane_combo_->setSizeAdjustPolicy(
            QComboBox::AdjustToMinimumContentsLengthWithIcon);
        base_plane_combo_->setMinimumContentsLength(18);
        base_plane_combo_->setSizePolicy(
            QSizePolicy::Ignored, QSizePolicy::Fixed);
        base_plane_combo_->addItem(
            tr("Počátek kontejneru — Rovina XY"), QStringLiteral("xy"));
        base_plane_combo_->addItem(
            tr("Počátek kontejneru — Rovina YZ"), QStringLiteral("yz"));
        base_plane_combo_->addItem(
            tr("Počátek kontejneru — Rovina XZ"), QStringLiteral("xz"));
        const auto base_plane_key = initial.base_plane ==
                zima::document::LocalDatumPlane::XY ? QStringLiteral("xy")
            : initial.base_plane == zima::document::LocalDatumPlane::XZ
                ? QStringLiteral("xz") : QStringLiteral("yz");
        base_plane_combo_->setCurrentIndex(
            base_plane_combo_->findData(base_plane_key));
        base_plane_combo_->setToolTip(tr(
            "Rovina XY, YZ nebo XZ lokálního počátku kontejneru, se kterou "
            "bude výsledná rovina rovnoběžná."));
        offset_form->addRow(tr("Výchozí rovina"), base_plane_combo_);
        offset_form->addRow(tr("Odsazení roviny"), offset_);
        content_layout()->addLayout(offset_form);
    }
    if (is_curve_container_kind(initial.kind)) {
        const bool experimental = initial.kind ==
            zima::document::ConstructionKind::Curve3DExperimental;
        setMinimumWidth(experimental ? 720 : 520);
        setMaximumWidth(experimental ? 820 : 590);
        if (!experimental) {
            auto* curve_form = new QFormLayout;
            curve_type_ = new QComboBox(this);
            curve_type_->setObjectName("curve3DType");
            curve_type_->addItem(tr("Lomená čára"),
                static_cast<int>(zima::document::Curve3DType::Polyline));
            curve_type_->addItem(tr("Interpolační spline"),
                static_cast<int>(
                    zima::document::Curve3DType::InterpolatingSpline));
            curve_type_->setCurrentIndex(curve_type_->findData(
                static_cast<int>(initial.curve_type)));
            curve_form->addRow(tr("Typ celé 3D křivky"), curve_type_);
            content_layout()->addLayout(curve_form);
        } else {
            auto* experimental_label = new QLabel(tr(
                "Každá mezera mezi dvěma body má vlastní typ spojení. "
                "Směr patří bodu; úsečka jej ignoruje, spline a generované "
                "spojení jej mohou použít."),
                this);
            experimental_label->setWordWrap(true);
            experimental_label->setStyleSheet("color:#9fd7e5;");
            content_layout()->addWidget(experimental_label);
        }
        curve_points_table_ = new QTableWidget(this);
        curve_points_table_->setObjectName(
            experimental ? "curve3DExperimentalRows" : "curve3DPoints");
        curve_points_table_->setColumnCount(experimental ? 6 : 5);
        curve_points_table_->setHorizontalHeaderLabels(experimental
            ? QStringList{tr("Entita"), tr("Úsek"), tr("Osa směru"),
                  QString{}, tr("Flip"), tr("Směr")}
            : QStringList{tr("Bod"), tr("Osa směru"), QString{},
                  tr("Flip"), tr("Směr")});
        curve_points_table_->horizontalHeader()->setSectionResizeMode(
            0, QHeaderView::Stretch);
        curve_points_table_->horizontalHeader()->setSectionResizeMode(
            1, QHeaderView::Stretch);
        for (int column = 2; column < curve_points_table_->columnCount(); ++column)
            curve_points_table_->horizontalHeader()->setSectionResizeMode(
                column, QHeaderView::Fixed);
        if (experimental) {
            curve_points_table_->setColumnWidth(2, 84);
            curve_points_table_->setColumnWidth(3, 66);
            curve_points_table_->setColumnWidth(4, 48);
            curve_points_table_->setColumnWidth(5, 52);
        } else {
            curve_points_table_->setColumnWidth(2, 66);
            curve_points_table_->setColumnWidth(3, 48);
            curve_points_table_->setColumnWidth(4, 52);
        }
        curve_points_table_->verticalHeader()->hide();
        curve_points_table_->setSelectionBehavior(
            QAbstractItemView::SelectRows);
        curve_points_table_->setSelectionMode(
            QAbstractItemView::SingleSelection);
        curve_points_table_->setEditTriggers(
            QAbstractItemView::NoEditTriggers);
        zima::ui::install_reference_cell_delegate(curve_points_table_);
        curve_points_table_->setMinimumHeight(experimental ? 230 : 115);
        content_layout()->addWidget(curve_points_table_);

        auto* point_buttons = new QHBoxLayout;
        add_curve_point_ = new QPushButton(tr("Přidat"), this);
        edit_curve_point_ = new QPushButton(tr("Upravit"), this);
        delete_curve_point_ = new QPushButton(tr("Smazat"), this);
        move_curve_point_up_ = new QPushButton(tr("Nahoru"), this);
        move_curve_point_down_ = new QPushButton(tr("Dolů"), this);
        add_curve_point_->setObjectName("curve3DAddPoint");
        edit_curve_point_->setObjectName("curve3DEditPoint");
        delete_curve_point_->setObjectName("curve3DDeletePoint");
        move_curve_point_up_->setObjectName("curve3DMovePointUp");
        move_curve_point_down_->setObjectName("curve3DMovePointDown");
        for (auto* button : {add_curve_point_, edit_curve_point_,
                 delete_curve_point_, move_curve_point_up_,
                 move_curve_point_down_}) {
            point_buttons->addWidget(button);
        }
        content_layout()->addLayout(point_buttons);
        const auto selected_curve_point = [this] {
            return selected_curve_point_index();
        };
        const auto finish_curve_axis_selection = [this] {
            if (!active_curve_axis_index_) return;
            active_curve_axis_index_.reset();
            if (curve_axis_cycle_) curve_axis_cycle_();
            refresh_curve_points();
        };
        connect(add_curve_point_, &QPushButton::clicked, this,
            [this, finish_curve_axis_selection] {
            finish_curve_axis_selection();
            if (curve_point_edit_request_) curve_point_edit_request_(std::nullopt);
        });
        connect(edit_curve_point_, &QPushButton::clicked, this,
            [this, selected_curve_point, finish_curve_axis_selection] {
                if (const auto index = selected_curve_point();
                    index && curve_point_edit_request_) {
                    finish_curve_axis_selection();
                    curve_point_edit_request_(index);
                }
            });
        connect(curve_points_table_, &QTableWidget::cellDoubleClicked, this,
            [this, finish_curve_axis_selection](int row, int column) {
                if (row < 0 || column != 0 || !curve_point_edit_request_) return;
                curve_points_table_->setCurrentCell(row, column);
                if (const auto index = selected_curve_point_index()) {
                    finish_curve_axis_selection();
                    curve_point_edit_request_(index);
                }
            });
        connect(curve_points_table_, &QTableWidget::cellClicked, this,
            [this](int row, int column) {
                if (row < 0) return;
                curve_points_table_->setCurrentCell(row, column);
                if (initial_.kind ==
                        zima::document::ConstructionKind::Curve3D &&
                    column == 1 &&
                    static_cast<std::size_t>(row) < curve_points_.size() &&
                    curve_points_[static_cast<std::size_t>(row)]
                        .curve_tangent_enabled && curve_axis_request_) {
                    curve_axis_request_(static_cast<std::size_t>(row));
                } else if (initial_.kind ==
                               zima::document::ConstructionKind::
                                   Curve3DExperimental &&
                           column == 2 &&
                           static_cast<std::size_t>(row) <
                               curve_table_rows_.size()) {
                    const auto& table_row =
                        curve_table_rows_[static_cast<std::size_t>(row)];
                    if (table_row.kind == CurveTableRowKind::Point &&
                        table_row.index < curve_points_.size() &&
                        curve_points_[table_row.index].curve_tangent_enabled &&
                        curve_axis_request_) {
                        curve_axis_request_(table_row.index);
                    }
                }
            });
        connect(delete_curve_point_, &QPushButton::clicked, this,
            [this, selected_curve_point] {
                const auto index = selected_curve_point();
                if (!index || *index >= curve_points_.size()) return;
                const auto removed_id = curve_points_[*index].id;
                curve_points_.erase(curve_points_.begin() +
                    static_cast<std::ptrdiff_t>(*index));
                std::erase_if(sweep_profiles_, [&](const auto& profile) {
                    return profile.point_id == removed_id;
                });
                synchronize_experimental_connections();
                refresh_curve_points();
                refresh_sweep_profiles();
                notify_preview();
            });
        const auto move_point = [this, selected_curve_point](int direction) {
            const auto index = selected_curve_point();
            if (!index) return;
            const auto destination = static_cast<std::ptrdiff_t>(*index) + direction;
            if (destination < 0 || destination >=
                    static_cast<std::ptrdiff_t>(curve_points_.size())) return;
            std::swap(curve_points_[*index],
                curve_points_[static_cast<std::size_t>(destination)]);
            synchronize_experimental_connections();
            refresh_curve_points();
            if (initial_.kind ==
                    zima::document::ConstructionKind::Curve3DExperimental) {
                for (std::size_t row = 0; row < curve_table_rows_.size(); ++row) {
                    if (curve_table_rows_[row].kind ==
                            CurveTableRowKind::Point &&
                        curve_table_rows_[row].index ==
                            static_cast<std::size_t>(destination)) {
                        curve_points_table_->selectRow(static_cast<int>(row));
                        break;
                    }
                }
            } else {
                curve_points_table_->selectRow(static_cast<int>(destination));
            }
            notify_preview();
        };
        connect(move_curve_point_up_, &QPushButton::clicked, this,
            [move_point] { move_point(-1); });
        connect(move_curve_point_down_, &QPushButton::clicked, this,
            [move_point] { move_point(1); });
        connect(curve_points_table_, &QTableWidget::currentCellChanged,
            this, [this](int, int, int, int) { refresh_curve_points(); });
        if (curve_type_ != nullptr) connect(curve_type_,
            &QComboBox::currentIndexChanged, this, [this] {
                refresh_curve_points();
                notify_preview();
            });
        synchronize_experimental_connections();
        refresh_curve_points();
    }
    refresh_offset_enabled_state();

    error_ = new QLabel(this);
    error_->setStyleSheet("color: #d96b6b;");
    error_->setWordWrap(true);
    content_layout()->addWidget(error_);
    connect(name_, &QLineEdit::textChanged, this, [this] { notify_preview(); });
    for (auto* input : origin_) {
        connect(input, &QDoubleSpinBox::valueChanged, this,
            [this] { notify_preview(); });
    }
    for (auto* input : rotation_) {
        if (input != nullptr) connect(input, &QDoubleSpinBox::valueChanged,
            this, [this] {
                if (updating_rotation_fields_) return;
                notify_preview();
            });
    }
    for (auto* input : rotation_offset_) {
        if (input != nullptr) connect(input, &QDoubleSpinBox::valueChanged,
            this, [this] { notify_preview(); });
    }
    if (display_size_ != nullptr) connect(display_size_,
        &QDoubleSpinBox::valueChanged, this, [this] { notify_preview(); });
    if (direction_combo_ != nullptr) connect(direction_combo_,
        &QComboBox::currentIndexChanged, this, [this] { notify_preview(); });
    if (base_plane_combo_ != nullptr) connect(base_plane_combo_,
        &QComboBox::currentIndexChanged, this, [this] { notify_preview(); });
    connect(offset_, &QDoubleSpinBox::valueChanged,
        this, [this] { notify_preview(); });
}

ConstructionPropertiesDialog::ConstructionPropertiesDialog(
    const zima::document::HistoryContainer& initial, bool edit_mode,
    bool allow_subtract, SweepCommitCallback commit, QWidget* parent,
    int decimal_places)
    : ConstructionPropertiesDialog(sweep_dialog_path(initial), edit_mode,
          [](zima::document::ConstructionObject) {}, parent, decimal_places) {
    if (initial.feature_kind != zima::document::FeatureKind::Sweep3D) {
        throw std::invalid_argument(
            "ConstructionPropertiesDialog Sweep constructor requires Sweep3D");
    }
    initial_sweep_ = initial;
    sweep_commit_ = std::move(commit);
    sweep_profiles_ = initial.sweep3d.profiles;
    sweep_combine_mode_ = initial.combine_mode;
    allow_sweep_subtract_ = allow_subtract;
    set_internal_title(tr("Vlastnosti 3D Sweepu"));
    initialize_sweep_ui();
}

void ConstructionPropertiesDialog::initialize_sweep_ui() {
    if (!initial_sweep_) return;
    content_layout()->removeWidget(error_);
    auto* title = new QLabel(tr("Profily Sweepu"), this);
    title->setStyleSheet("color:#9fd7e5;font-weight:700;");
    content_layout()->addWidget(title);
    sweep_profiles_table_ = new QTableWidget(this);
    sweep_profiles_table_->setObjectName("sweep3DProfiles");
    sweep_profiles_table_->setColumnCount(2);
    sweep_profiles_table_->setHorizontalHeaderLabels(
        {tr("Skica"), tr("Bod trajektorie")});
    sweep_profiles_table_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    sweep_profiles_table_->verticalHeader()->hide();
    sweep_profiles_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    sweep_profiles_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    sweep_profiles_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sweep_profiles_table_->setMinimumHeight(90);
    content_layout()->addWidget(sweep_profiles_table_);
    auto* buttons = new QHBoxLayout;
    add_sweep_profile_ = new QPushButton(tr("Přidat skicu"), this);
    edit_sweep_profile_ = new QPushButton(tr("Upravit"), this);
    delete_sweep_profile_ = new QPushButton(tr("Smazat"), this);
    reassign_sweep_profile_ = new QPushButton(tr("Jiný bod"), this);
    add_sweep_profile_->setObjectName("sweep3DAddProfile");
    edit_sweep_profile_->setObjectName("sweep3DEditProfile");
    delete_sweep_profile_->setObjectName("sweep3DDeleteProfile");
    reassign_sweep_profile_->setObjectName("sweep3DReassignProfile");
    for (auto* button : {add_sweep_profile_, edit_sweep_profile_,
             delete_sweep_profile_, reassign_sweep_profile_}) {
        buttons->addWidget(button);
    }
    content_layout()->addLayout(buttons);

    auto* operation_row = new QWidget(this);
    auto* operation_layout = new QHBoxLayout(operation_row);
    operation_layout->setContentsMargins(0, 0, 0, 0);
    operation_layout->setSpacing(8);
    add_sweep_operation_ = new QPushButton(tr("Přičíst"), this);
    subtract_sweep_operation_ = new QPushButton(tr("Odečíst"), this);
    for (auto* button : {add_sweep_operation_, subtract_sweep_operation_}) {
        button->setCheckable(true);
        button->setMinimumHeight(40);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    add_sweep_operation_->setObjectName("sweep3DAddOperation");
    subtract_sweep_operation_->setObjectName("sweep3DSubtractOperation");
    add_sweep_operation_->setStyleSheet(
        "QPushButton{border:2px solid #2d5670;border-radius:6px;font-weight:700;"
        "padding:7px 14px} QPushButton:checked{background:#00d1ff;color:#101510;"
        "border-color:#6fe3ff}");
    subtract_sweep_operation_->setStyleSheet(
        "QPushButton{border:2px solid #713d3d;border-radius:6px;font-weight:700;"
        "padding:7px 14px} QPushButton:checked{background:#c64b4b;color:#ffffff;"
        "border-color:#ed7777}");
    const bool subtract = sweep_combine_mode_ ==
        zima::document::CombineMode::Subtract;
    add_sweep_operation_->setChecked(!subtract);
    subtract_sweep_operation_->setChecked(subtract);
    subtract_sweep_operation_->setEnabled(allow_sweep_subtract_ || subtract);
    operation_layout->addWidget(add_sweep_operation_);
    operation_layout->addWidget(subtract_sweep_operation_);
    auto* operation_form = new QFormLayout;
    operation_form->addRow(tr("Operace"), operation_row);
    content_layout()->addLayout(operation_form);

    content_layout()->addWidget(error_);
    connect(add_sweep_profile_, &QPushButton::clicked, this, [this] {
        if (sweep_profile_point_request_)
            sweep_profile_point_request_(std::nullopt);
    });
    connect(edit_sweep_profile_, &QPushButton::clicked, this, [this] {
        const auto index = selected_sweep_profile_index();
        if (index && sweep_profile_edit_request_)
            sweep_profile_edit_request_(*index);
    });
    connect(reassign_sweep_profile_, &QPushButton::clicked, this, [this] {
        const auto index = selected_sweep_profile_index();
        if (index && sweep_profile_point_request_)
            sweep_profile_point_request_(index);
    });
    connect(delete_sweep_profile_, &QPushButton::clicked, this, [this] {
        const auto index = selected_sweep_profile_index();
        if (!index || *index >= sweep_profiles_.size()) return;
        sweep_profiles_.erase(sweep_profiles_.begin() +
            static_cast<std::ptrdiff_t>(*index));
        refresh_sweep_profiles();
        notify_preview();
    });
    connect(sweep_profiles_table_, &QTableWidget::cellDoubleClicked,
        this, [this](int row, int) {
            if (row >= 0 && static_cast<std::size_t>(row) <
                    sweep_profiles_.size() && sweep_profile_edit_request_) {
                sweep_profile_edit_request_(static_cast<std::size_t>(row));
            }
        });
    connect(sweep_profiles_table_, &QTableWidget::currentCellChanged,
        this, [this](int, int, int, int) { refresh_sweep_profiles(); });
    const auto select_operation = [this](bool subtract_selected) {
        sweep_combine_mode_ = subtract_selected
            ? zima::document::CombineMode::Subtract
            : zima::document::CombineMode::Add;
        add_sweep_operation_->setChecked(!subtract_selected);
        subtract_sweep_operation_->setChecked(subtract_selected);
        notify_preview();
    };
    connect(add_sweep_operation_, &QPushButton::clicked, this,
        [select_operation] { select_operation(false); });
    connect(subtract_sweep_operation_, &QPushButton::clicked, this,
        [select_operation] { select_operation(true); });
    refresh_sweep_profiles();
}

void ConstructionPropertiesDialog::set_reference_request_callback(
    ReferenceRequestCallback callback) {
    reference_request_ = std::move(callback);
}

void ConstructionPropertiesDialog::set_reference_highlights_changed_callback(
    ReferenceHighlightsChangedCallback callback) {
    reference_highlights_changed_ = std::move(callback);
}

std::set<std::string>
ConstructionPropertiesDialog::highlighted_reference_owner_ids() const {
    return placement_->highlighted_reference_owner_ids();
}

std::vector<zima::document::ConstructionReference>
ConstructionPropertiesDialog::highlighted_reference_entries() const {
    return placement_->highlighted_reference_entries();
}

void ConstructionPropertiesDialog::set_preview_callback(PreviewCallback callback) {
    preview_ = std::move(callback);
    notify_preview();
}

void ConstructionPropertiesDialog::set_curve_point_edit_request_callback(
    CurvePointEditRequestCallback callback) {
    curve_point_edit_request_ = std::move(callback);
}

void ConstructionPropertiesDialog::set_curve_axis_request_callback(
    CurveAxisRequestCallback callback) {
    curve_axis_request_ = std::move(callback);
}

void ConstructionPropertiesDialog::set_curve_axis_cycle_callback(
    CurveAxisCycleCallback callback) {
    curve_axis_cycle_ = std::move(callback);
}

void ConstructionPropertiesDialog::set_curve_sketch_edit_request_callback(
    CurveSketchEditRequestCallback callback) {
    curve_sketch_edit_request_ = std::move(callback);
}

void ConstructionPropertiesDialog::set_sweep_profile_point_request_callback(
    SweepProfilePointRequestCallback callback) {
    sweep_profile_point_request_ = std::move(callback);
}

void ConstructionPropertiesDialog::set_sweep_profile_edit_request_callback(
    SweepProfileEditRequestCallback callback) {
    sweep_profile_edit_request_ = std::move(callback);
}

void ConstructionPropertiesDialog::set_curve_axis_active(
    std::optional<std::size_t> index) {
    active_curve_axis_index_ = index;
    refresh_curve_points();
}

void ConstructionPropertiesDialog::set_curve_point_tangent(
    std::size_t index, zima::document::Curve3DTangentMode tangent) {
    if (index >= curve_points_.size()) return;
    curve_points_[index].curve_tangent = tangent;
    curve_points_[index].curve_tangent_enabled =
        tangent != zima::document::Curve3DTangentMode::Automatic;
    active_curve_axis_index_.reset();
    refresh_curve_points();
    notify_preview();
}

void ConstructionPropertiesDialog::set_curve_point(
    std::optional<std::size_t> index,
    zima::document::ConstructionObject point, bool update_preview) {
    if (!is_curve_container_kind(initial_.kind) ||
        point.kind != zima::document::ConstructionKind::Point) return;
    point.parent_construction_id = initial_.id;
    if (index && *index < curve_points_.size()) {
        curve_points_[*index] = std::move(point);
    } else {
        if (point.name == "Bod001") {
            point.name = tr("Bod %1").arg(curve_points_.size() + 1,
                3, 10, QLatin1Char('0')).toStdString();
        }
        curve_points_.push_back(std::move(point));
        index = curve_points_.size() - 1;
    }
    synchronize_experimental_connections();
    refresh_curve_points();
    if (index && curve_points_table_ != nullptr) {
        if (initial_.kind ==
                zima::document::ConstructionKind::Curve3DExperimental) {
            for (std::size_t row = 0; row < curve_table_rows_.size(); ++row) {
                if (curve_table_rows_[row].kind == CurveTableRowKind::Point &&
                    curve_table_rows_[row].index == *index) {
                    curve_points_table_->selectRow(static_cast<int>(row));
                    break;
                }
            }
        } else {
            curve_points_table_->selectRow(static_cast<int>(*index));
        }
    }
    if (update_preview) notify_preview();
}

void ConstructionPropertiesDialog::erase_curve_point(
    std::size_t index, bool update_preview) {
    if (index >= curve_points_.size()) return;
    const auto removed_id = curve_points_[index].id;
    curve_points_.erase(curve_points_.begin() +
        static_cast<std::ptrdiff_t>(index));
    std::erase_if(sweep_profiles_, [&](const auto& profile) {
        return profile.point_id == removed_id;
    });
    synchronize_experimental_connections();
    refresh_curve_points();
    refresh_sweep_profiles();
    if (update_preview) notify_preview();
}

const zima::document::ConstructionObject*
ConstructionPropertiesDialog::curve_point(std::size_t index) const {
    return index < curve_points_.size() ? &curve_points_[index] : nullptr;
}

const zima::document::Curve3DConnection*
ConstructionPropertiesDialog::curve_connection(std::size_t index) const {
    return index < curve_connections_.size() ? &curve_connections_[index] : nullptr;
}

void ConstructionPropertiesDialog::set_curve_connection_sketch(
    std::size_t index, const zima::sketcher::Sketch& sketch,
    std::string start_point_id, std::string end_point_id, bool plane_valid) {
    if (index >= curve_connections_.size()) return;
    auto& connection = curve_connections_[index];
    connection.type = zima::document::Curve3DConnectionType::Sketch;
    connection.generator_id = connection.id;
    connection.sketch_id = sketch.id;
    connection.sketch_start_point_id = std::move(start_point_id);
    connection.sketch_end_point_id = std::move(end_point_id);
    connection.sketch_serialized = sketch.serialized();
    connection.sketch_plane_valid = plane_valid;
    refresh_curve_points();
    notify_preview();
}

std::optional<std::size_t>
ConstructionPropertiesDialog::selected_sweep_profile_index() const {
    if (sweep_profiles_table_ == nullptr) return std::nullopt;
    const int row = sweep_profiles_table_->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= sweep_profiles_.size())
        return std::nullopt;
    return static_cast<std::size_t>(row);
}

void ConstructionPropertiesDialog::refresh_sweep_profiles() {
    if (sweep_profiles_table_ == nullptr) return;
    const int selected = sweep_profiles_table_->currentRow();
    const QSignalBlocker blocked(sweep_profiles_table_);
    sweep_profiles_table_->setRowCount(
        static_cast<int>(sweep_profiles_.size()));
    for (std::size_t index = 0; index < sweep_profiles_.size(); ++index) {
        const auto& profile = sweep_profiles_[index];
        QString sketch_name = tr("Skica %1").arg(index + 1);
        try {
            sketch_name = QString::fromStdString(
                zima::sketcher::Sketch::from_serialized(
                    profile.sketch_serialized).name);
        } catch (const std::exception&) {
            sketch_name = tr("Neplatná skica");
        }
        QString point_name = tr("Chybějící bod");
        const auto point = std::find_if(curve_points_.begin(),
            curve_points_.end(), [&](const auto& value) {
                return value.id == profile.point_id;
            });
        if (point != curve_points_.end())
            point_name = QString::fromStdString(point->name);
        auto* sketch_item = new QTableWidgetItem(sketch_name);
        sketch_item->setData(Qt::UserRole,
            QString::fromStdString(profile.id));
        auto* point_item = new QTableWidgetItem(point_name);
        point_item->setData(Qt::UserRole,
            QString::fromStdString(profile.point_id));
        sweep_profiles_table_->setItem(static_cast<int>(index), 0, sketch_item);
        sweep_profiles_table_->setItem(static_cast<int>(index), 1, point_item);
    }
    if (selected >= 0 && selected < sweep_profiles_table_->rowCount())
        sweep_profiles_table_->selectRow(selected);
    const bool has_selection = selected_sweep_profile_index().has_value();
    edit_sweep_profile_->setEnabled(has_selection);
    delete_sweep_profile_->setEnabled(has_selection);
    reassign_sweep_profile_->setEnabled(has_selection);
}

void ConstructionPropertiesDialog::add_sweep_profile(
    std::string point_id, const zima::sketcher::Sketch& sketch) {
    if (!initial_sweep_ || point_id.empty() ||
        std::ranges::any_of(sweep_profiles_, [&](const auto& profile) {
            return profile.point_id == point_id;
        })) return;
    sweep_profiles_.push_back({zima::kernel::make_stable_id(),
        std::move(point_id), sketch.id, sketch.serialized()});
    refresh_sweep_profiles();
    sweep_profiles_table_->selectRow(
        static_cast<int>(sweep_profiles_.size() - 1));
    notify_preview();
}

void ConstructionPropertiesDialog::set_sweep_profile_sketch(
    std::size_t index, const zima::sketcher::Sketch& sketch) {
    if (index >= sweep_profiles_.size()) return;
    sweep_profiles_[index].sketch_id = sketch.id;
    sweep_profiles_[index].sketch_serialized = sketch.serialized();
    refresh_sweep_profiles();
    sweep_profiles_table_->selectRow(static_cast<int>(index));
    notify_preview();
}

bool ConstructionPropertiesDialog::reassign_sweep_profile(
    std::size_t index, std::string point_id) {
    if (index >= sweep_profiles_.size() || point_id.empty() ||
        std::ranges::any_of(sweep_profiles_, [&](const auto& profile) {
            return profile.point_id == point_id &&
                profile.id != sweep_profiles_[index].id;
        })) return false;
    sweep_profiles_[index].point_id = std::move(point_id);
    refresh_sweep_profiles();
    sweep_profiles_table_->selectRow(static_cast<int>(index));
    notify_preview();
    return true;
}

const zima::document::Sweep3DProfile*
ConstructionPropertiesDialog::sweep_profile(std::size_t index) const {
    return index < sweep_profiles_.size() ? &sweep_profiles_[index] : nullptr;
}

zima::document::ConstructionObject
ConstructionPropertiesDialog::pending_value() const {
    return current_value();
}

zima::document::HistoryContainer
ConstructionPropertiesDialog::pending_sweep_value() const {
    if (!initial_sweep_) return {};
    auto container = *initial_sweep_;
    auto dialog_path = current_value();
    container.name = dialog_path.name;
    container.combine_mode = sweep_combine_mode_;
    container.placement = placement_->numeric_placement();
    container.placement.references = dialog_path.references;
    auto stored_path = std::move(dialog_path);
    stored_path.name = initial_sweep_->sweep3d.path.name;
    stored_path.parent_construction_id = container.id;
    // The upper dialog section edits the history container placement. Child
    // Points remain in the embedded Curve's local frame, so the path itself
    // must not apply that placement a second time during body calculation.
    stored_path.origin = {};
    stored_path.entity_origin = {};
    stored_path.rotation = {};
    stored_path.absolute_rotation = {};
    stored_path.rotation_offset_x = 0.0;
    stored_path.rotation_offset_y = 0.0;
    stored_path.rotation_offset_z = 0.0;
    stored_path.orientation_back = false;
    stored_path.orientation_quarter_turns = 0;
    stored_path.references.clear();
    for (auto& point : stored_path.curve_points)
        point.parent_construction_id = stored_path.id;
    container.sweep3d.path = std::move(stored_path);
    container.sweep3d.profiles = sweep_profiles_;
    for (std::size_t index = 0;
         index < container.sweep3d.profiles.size(); ++index) {
        static_cast<void>(
            zima::document::PartDocument::reframe_sweep3d_profile(
                container, index));
    }
    return container;
}

void ConstructionPropertiesDialog::refresh_preview() {
    notify_preview();
}

bool ConstructionPropertiesDialog::set_reference(std::size_t index,
    zima::document::ConstructionReference reference, const QString& label,
    zima::document::ConstructionDefinition definition) {
    const bool first_plane_reference = initial_.kind ==
            zima::document::ConstructionKind::Plane && index == 0 &&
        reference.supports_offset;
    QString error;
    // A new Plane's position rows keep the same automatically assigned
    // front/top flags every other container kind gets (see
    // assign_automatic_orientation_role() in assembly_workspace_window.cpp):
    // row 0 is FRONT -- the plane the new Plane is offset from -- and row 1
    // is TOP; row 2 never drives orientation. PartDocument::resolve_construction()
    // reads row 0's role to inherit that reference's own full plane frame
    // (parallel-copy + offset) whenever it resolves to a real plane, exactly
    // matching the "FRONT rovina je ta, od které se odsadí skutečná rovina"
    // contract; a non-planar row 0 (axis/point) falls back to the generic
    // FRONT/TOP composition shared with Point/Axis.
    if (!placement_->set_reference(index, std::move(reference), label, &error)) {
        if (!error.isEmpty()) error_->setText(error);
        return false;
    }
    error_->clear();
    if (index < 3) {
        definition_->setCurrentIndex(definition_->findData(static_cast<int>(definition)));
    }
    if (first_plane_reference && base_plane_combo_ != nullptr) {
        // FRONT maps the picked plane normal onto the container's local Y
        // axis. The local XZ datum is therefore the plane that is parallel
        // to that first picked plane and is the correct source for offset.
        // Name the choice after the real picked source so the UI does not
        // misleadingly look as though the offset still starts at an
        // unrelated default Container-Origin plane.
        const int xz_index = base_plane_combo_->findData(QStringLiteral("xz"));
        if (xz_index >= 0) {
            base_plane_combo_->setItemText(xz_index,
                tr("Podle první reference — %1").arg(label));
            base_plane_combo_->setCurrentIndex(xz_index);
        }
    }
    return true;
}

bool ConstructionPropertiesDialog::set_reference(std::size_t index,
    zima::document::ConstructionReference reference, const QString& label) {
    const auto definition = current_definition() ==
            zima::document::ConstructionDefinition::Absolute && index < 3
        ? zima::document::ConstructionDefinition::PointReference
        : current_definition();
    return set_reference(index, std::move(reference), label, definition);
}

zima::document::ConstructionDefinition
ConstructionPropertiesDialog::current_definition() const {
    return static_cast<zima::document::ConstructionDefinition>(
        definition_->currentData().toInt());
}

zima::document::ConstructionKind
ConstructionPropertiesDialog::construction_kind() const {
    return initial_.kind;
}

const std::string& ConstructionPropertiesDialog::construction_id() const {
    return initial_.id;
}

double ConstructionPropertiesDialog::plane_offset() const {
    return offset_ == nullptr ? 0.0 : offset_->value();
}

bool ConstructionPropertiesDialog::orientation_back() const {
    return placement_ != nullptr &&
        placement_->numeric_placement().orientation_back;
}

void ConstructionPropertiesDialog::set_plane_offset_and_orientation(
        double offset, bool back) {
    if (offset_ == nullptr || placement_ == nullptr) return;
    const QSignalBlocker blocker(offset_);
    offset_->setValue(std::abs(offset));
    placement_->set_orientation_back(back);
    notify_preview();
}

bool ConstructionPropertiesDialog::owns_reference_owner(
    const std::string& owner_id) const {
    if (owner_id == initial_.id || owner_id == initial_.entity_id ||
        owner_id == initial_.container_origin.id ||
        (!initial_.parent_construction_id.empty() &&
         owner_id == initial_.parent_construction_id)) return true;
    return std::any_of(curve_points_.begin(), curve_points_.end(),
        [&](const auto& point) {
            return owner_id == point.id || owner_id == point.entity_id ||
                owner_id == point.container_origin.id;
        });
}

std::vector<zima::document::ConstructionReference>
ConstructionPropertiesDialog::references_without(std::size_t index) const {
    return placement_->references_without(index);
}

std::size_t ConstructionPropertiesDialog::first_empty_position_index() const {
    return placement_->first_empty_position_index();
}

void ConstructionPropertiesDialog::set_active_reference_index(
    std::optional<std::size_t> index) {
    placement_->set_active_reference_index(index);
}

void ConstructionPropertiesDialog::set_reference_inspected(
    std::size_t index, bool inspected) {
    placement_->set_reference_inspected(index, inspected);
}

void ConstructionPropertiesDialog::clear_reference_highlights() {
    placement_->clear_reference_highlights();
}

std::vector<zima::document::ConstructionReference>
ConstructionPropertiesDialog::populated_references() const {
    return placement_->populated_references();
}

void ConstructionPropertiesDialog::set_orientation_base_rotation(
    const zima::kernel::Vec3& base_rotation, bool has_orientation_references) {
    if (updating_rotation_fields_) return;
    has_orientation_base_rotation_ = has_orientation_references;
    updating_rotation_fields_ = true;
    placement_->set_orientation_base_rotation(
        base_rotation, has_orientation_references);
    updating_rotation_fields_ = false;
}

void ConstructionPropertiesDialog::set_resolved_rotation(
        const zima::kernel::Vec3& rotation, bool valid) {
    placement_->set_resolved_rotation(rotation, valid);
}

void ConstructionPropertiesDialog::set_orientation_inherited_from_reference(
        bool inherited) {
    if (placement_ == nullptr) return;
    placement_->set_orientation_locked(inherited,
        inherited ? placement_->first_position_reference_label() : QString());
}

void ConstructionPropertiesDialog::set_remaining_translation_dof(int dof) {
    placement_->set_remaining_translation_dof(dof);
    remaining_translation_dof_ = placement_->remaining_translation_dof();
}

void ConstructionPropertiesDialog::set_remaining_rotation_dof(int dof) {
    placement_->set_remaining_rotation_dof(dof);
    remaining_rotation_dof_ = placement_->remaining_rotation_dof();
}

void ConstructionPropertiesDialog::set_rotation_constraint_state(
        const zima::document::OrientationConstraintState& state) {
    placement_->set_rotation_constraint_state(state);
    remaining_rotation_dof_ = placement_->remaining_rotation_dof();
}

void ConstructionPropertiesDialog::set_translation_constraint_state(
    const zima::document::PointConstraintState& state,
    const zima::kernel::Vec3& solution) {
    set_remaining_translation_dof(state.remaining_dof);
    const std::array values{solution.x, solution.y, solution.z};
    for (std::size_t index = 0; index < origin_.size(); ++index) {
        if (origin_[index] == nullptr) continue;
        origin_[index]->setEnabled(!state.constrained_axes[index]);
        if (state.constrained_axes[index]) {
            const QSignalBlocker blocker(origin_[index]);
            origin_[index]->setValue(values[index]);
        }
    }
}

bool ConstructionPropertiesDialog::set_inline_parameter_value(
    std::string_view key, double value) {
    constexpr std::string_view placement_prefix{"placement:"};
    if (key.starts_with(placement_prefix)) key.remove_prefix(
        placement_prefix.size());
    const auto set_field = [value](QDoubleSpinBox* field) {
        if (field == nullptr || !field->isEnabled() || !field->isVisible())
            return false;
        field->setValue(value);
        return true;
    };
    if (key == "x") return set_field(origin_[0]);
    if (key == "y") return set_field(origin_[1]);
    if (key == "z") return set_field(origin_[2]);
    if (key == "rotation_x" || key == "rotation_y" ||
        key == "rotation_z") {
        const std::size_t index = key == "rotation_x" ? 0
            : key == "rotation_y" ? 1 : 2;
        if (set_field(rotation_[index])) return true;
        return set_field(rotation_offset_[index]);
    }
    if (key == "offset") return set_field(offset_);
    if (key == "length") return set_field(display_size_);
    constexpr std::string_view prefix{"reference_offset:"};
    if (!key.starts_with(prefix)) return false;
    const auto suffix = key.substr(prefix.size());
    if (suffix.empty()) return false;
    std::size_t index{};
    for (const char digit : suffix) {
        if (digit < '0' || digit > '9') return false;
        index = index * 10 + static_cast<std::size_t>(digit - '0');
    }
    return placement_ != nullptr &&
        placement_->set_reference_offset(index, value);
}

void ConstructionPropertiesDialog::refresh_definition_fields() {
    offset_->setVisible(
        initial_.kind == zima::document::ConstructionKind::Plane);
    refresh_offset_enabled_state();
    if (placement_) placement_->refresh_reference_table();
    notify_preview();
}

void ConstructionPropertiesDialog::refresh_offset_enabled_state() {
    if (offset_ == nullptr) return;
    if (initial_.kind != zima::document::ConstructionKind::Plane) {
        offset_->setEnabled(false);
        return;
    }
    // A Plane's work-plane offset always has a well-defined direction to
    // move along: resolve_construction() falls back to the identity normal
    // (+X) whenever no orientation-driving reference is present, exactly
    // like the un-referenced X/Y/Z origin fields above. So the offset field
    // must stay editable even with zero position references -- disabling it
    // until a reference exists was a leftover restriction with no actual
    // mathematical dependency behind it.
    offset_->setEnabled(true);
}

std::optional<std::size_t>
ConstructionPropertiesDialog::selected_curve_point_index() const {
    if (curve_points_table_ == nullptr ||
        curve_points_table_->currentRow() < 0) return std::nullopt;
    const auto row =
        static_cast<std::size_t>(curve_points_table_->currentRow());
    if (initial_.kind !=
            zima::document::ConstructionKind::Curve3DExperimental) {
        return row < curve_points_.size()
            ? std::optional<std::size_t>{row} : std::nullopt;
    }
    if (row >= curve_table_rows_.size() ||
        curve_table_rows_[row].kind != CurveTableRowKind::Point) {
        return std::nullopt;
    }
    return curve_table_rows_[row].index;
}

void ConstructionPropertiesDialog::merge_experimental_spline_generators() {
    if (initial_.kind !=
            zima::document::ConstructionKind::Curve3DExperimental) return;
    std::unordered_set<std::string> used;
    for (std::size_t index = 0; index < curve_connections_.size();) {
        auto& connection = curve_connections_[index];
        if (connection.type !=
                zima::document::Curve3DConnectionType::InterpolatingSpline) {
            connection.generator_id = connection.id;
            ++index;
            continue;
        }
        std::size_t end = index + 1;
        while (end < curve_connections_.size() &&
               curve_connections_[end].type ==
                   zima::document::Curve3DConnectionType::InterpolatingSpline) {
            ++end;
        }
        std::string generator;
        for (std::size_t candidate = index; candidate < end; ++candidate) {
            const auto& existing = curve_connections_[candidate].generator_id;
            if (!existing.empty() && !used.contains(existing)) {
                generator = existing;
                break;
            }
        }
        if (generator.empty()) generator = zima::kernel::make_stable_id();
        used.insert(generator);
        for (std::size_t candidate = index; candidate < end; ++candidate)
            curve_connections_[candidate].generator_id = generator;
        index = end;
    }
}

void ConstructionPropertiesDialog::synchronize_experimental_connections() {
    if (initial_.kind !=
            zima::document::ConstructionKind::Curve3DExperimental) return;
    const auto key = [](const std::string& start, const std::string& end) {
        return start + '\n' + end;
    };
    std::unordered_map<std::string, zima::document::Curve3DConnection> existing;
    for (auto connection : curve_connections_) {
        existing.emplace(key(connection.start_point_id,
            connection.end_point_id), std::move(connection));
    }
    std::vector<zima::document::Curve3DConnection> synchronized;
    if (curve_points_.size() >= 2)
        synchronized.reserve(curve_points_.size() - 1);
    for (std::size_t index = 0; index + 1 < curve_points_.size(); ++index) {
        const auto identity =
            key(curve_points_[index].id, curve_points_[index + 1].id);
        auto found = existing.find(identity);
        zima::document::Curve3DConnection connection;
        if (found != existing.end()) {
            connection = std::move(found->second);
        } else {
            connection.id = zima::kernel::make_stable_id();
            connection.generator_id = connection.id;
            connection.type = zima::document::Curve3DConnectionType::Line;
        }
        connection.parent_construction_id = initial_.id;
        connection.start_point_id = curve_points_[index].id;
        connection.end_point_id = curve_points_[index + 1].id;
        connection.start_tangent = curve_points_[index].curve_tangent;
        connection.start_tangent_enabled =
            curve_points_[index].curve_tangent_enabled;
        connection.end_tangent = curve_points_[index + 1].curve_tangent;
        connection.end_tangent_enabled =
            curve_points_[index + 1].curve_tangent_enabled;
        synchronized.push_back(std::move(connection));
    }
    curve_connections_ = std::move(synchronized);
    merge_experimental_spline_generators();
}

void ConstructionPropertiesDialog::refresh_experimental_curve_rows() {
    if (curve_points_table_ == nullptr) return;
    synchronize_experimental_connections();

    std::optional<CurveTableRow> previous;
    const int selected = curve_points_table_->currentRow();
    if (selected >= 0 &&
        static_cast<std::size_t>(selected) < curve_table_rows_.size()) {
        previous = curve_table_rows_[static_cast<std::size_t>(selected)];
    }

    const QSignalBlocker blocked(curve_points_table_);
    curve_points_table_->clearContents();
    curve_points_table_->clearSpans();
    curve_table_rows_.clear();
    curve_table_rows_.reserve(
        curve_points_.size() + curve_connections_.size());
    for (std::size_t index = 0; index < curve_points_.size(); ++index) {
        curve_table_rows_.push_back({CurveTableRowKind::Point, index});
        if (index < curve_connections_.size()) {
            curve_table_rows_.push_back({CurveTableRowKind::Connection, index});
        }
    }
    curve_points_table_->setRowCount(
        static_cast<int>(curve_table_rows_.size()));

    const auto tangent_label = [this](
            zima::document::Curve3DTangentMode mode, bool enabled) {
        if (!enabled) return tr("Automaticky");
        using zima::document::Curve3DTangentMode;
        switch (mode) {
            case Curve3DTangentMode::PositiveX: return QStringLiteral("+X");
            case Curve3DTangentMode::NegativeX: return QStringLiteral("−X");
            case Curve3DTangentMode::PositiveY: return QStringLiteral("+Y");
            case Curve3DTangentMode::NegativeY: return QStringLiteral("−Y");
            case Curve3DTangentMode::PositiveZ: return QStringLiteral("+Z");
            case Curve3DTangentMode::NegativeZ: return QStringLiteral("−Z");
            case Curve3DTangentMode::Automatic: return tr("Automaticky");
        }
        return tr("Automaticky");
    };
    const auto negative_tangent = [](zima::document::Curve3DTangentMode mode) {
        using zima::document::Curve3DTangentMode;
        return mode == Curve3DTangentMode::NegativeX ||
            mode == Curve3DTangentMode::NegativeY ||
            mode == Curve3DTangentMode::NegativeZ;
    };
    const auto cycle_tangent = [](zima::document::Curve3DTangentMode& mode) {
        using zima::document::Curve3DTangentMode;
        switch (mode) {
            case Curve3DTangentMode::PositiveX:
                mode = Curve3DTangentMode::PositiveY; break;
            case Curve3DTangentMode::PositiveY:
                mode = Curve3DTangentMode::PositiveZ; break;
            case Curve3DTangentMode::PositiveZ:
                mode = Curve3DTangentMode::PositiveX; break;
            case Curve3DTangentMode::NegativeX:
                mode = Curve3DTangentMode::NegativeY; break;
            case Curve3DTangentMode::NegativeY:
                mode = Curve3DTangentMode::NegativeZ; break;
            case Curve3DTangentMode::NegativeZ:
                mode = Curve3DTangentMode::NegativeX; break;
            case Curve3DTangentMode::Automatic:
                mode = Curve3DTangentMode::PositiveX; break;
        }
    };
    const auto flip_tangent = [](zima::document::Curve3DTangentMode& mode,
                                 bool flipped) {
        using zima::document::Curve3DTangentMode;
        switch (mode) {
            case Curve3DTangentMode::PositiveX:
            case Curve3DTangentMode::NegativeX:
                mode = flipped ? Curve3DTangentMode::NegativeX
                               : Curve3DTangentMode::PositiveX; break;
            case Curve3DTangentMode::PositiveY:
            case Curve3DTangentMode::NegativeY:
                mode = flipped ? Curve3DTangentMode::NegativeY
                               : Curve3DTangentMode::PositiveY; break;
            case Curve3DTangentMode::PositiveZ:
            case Curve3DTangentMode::NegativeZ:
                mode = flipped ? Curve3DTangentMode::NegativeZ
                               : Curve3DTangentMode::PositiveZ; break;
            case Curve3DTangentMode::Automatic:
                mode = flipped ? Curve3DTangentMode::NegativeX
                               : Curve3DTangentMode::PositiveX; break;
        }
    };

    int row = 0;
    for (std::size_t point_index = 0;
         point_index < curve_points_.size(); ++point_index) {
        auto& point = curve_points_[point_index];
        auto* point_item = new zima::ui::ReferenceCellItem(
            QStringLiteral("● %1").arg(QString::fromStdString(point.name)));
        point_item->set_reference(QString::fromStdString(point.id));
        point_item->setToolTip(tr(
            "Bod trajektorie. Jeho lokální Origin určuje volitelný směr "
            "spline nebo generovaného spojení."));
        curve_points_table_->setItem(row, 0, point_item);
        curve_points_table_->setItem(row, 1, new QTableWidgetItem(tr("Bod")));

        auto* axis = new zima::ui::ReferenceCellItem(
            tangent_label(point.curve_tangent, point.curve_tangent_enabled));
        axis->set_reference(axis->text());
        axis->setToolTip(point.curve_tangent_enabled
            ? tr("Kliknutím lze vybrat osu lokálního Origin tohoto bodu.")
            : tr("Směr je automatický. Zapněte tlačítko SMĚR."));
        if (!point.curve_tangent_enabled) {
            axis->setFlags(axis->flags() & ~Qt::ItemIsEnabled);
        }
        curve_points_table_->setItem(row, 2, axis);

        auto* cycle = new QToolButton(curve_points_table_);
        cycle->setObjectName(QStringLiteral("curve3DExperimentalCycle%1")
            .arg(point_index));
        cycle->setText(QStringLiteral("SWITCH"));
        cycle->setEnabled(point.curve_tangent_enabled);
        style_curve_switch_button(cycle, 58);
        connect(cycle, &QToolButton::clicked, this,
            [this, point_index, cycle_tangent] {
                if (point_index >= curve_points_.size()) return;
                cycle_tangent(curve_points_[point_index].curve_tangent);
                synchronize_experimental_connections();
                refresh_curve_points();
                notify_preview();
            });
        curve_points_table_->setCellWidget(row, 3,
            zima::ui::centered_cell_widget(cycle));

        auto* flip_cell = zima::ui::build_reference_row_flip_button(
            point.curve_tangent_enabled,
            negative_tangent(point.curve_tangent),
            [this, point_index, flip_tangent](bool flipped) {
                if (point_index >= curve_points_.size()) return;
                flip_tangent(curve_points_[point_index].curve_tangent, flipped);
                synchronize_experimental_connections();
                refresh_curve_points();
                notify_preview();
            });
        auto* flip = qobject_cast<QToolButton*>(flip_cell);
        if (flip == nullptr) flip = flip_cell->findChild<QToolButton*>();
        if (flip != nullptr) {
            flip->setText(QStringLiteral("FLIP"));
            flip->setFixedWidth(40);
            flip->setObjectName(QStringLiteral("curve3DExperimentalFlip%1")
                .arg(point_index));
        }
        curve_points_table_->setCellWidget(row, 4, flip_cell);

        auto* direction = new QToolButton(curve_points_table_);
        direction->setObjectName(
            QStringLiteral("curve3DExperimentalDirection%1").arg(point_index));
        direction->setText(QStringLiteral("SMĚR"));
        direction->setCheckable(true);
        direction->setChecked(point.curve_tangent_enabled);
        style_curve_switch_button(direction, 44);
        connect(direction, &QToolButton::toggled, this,
            [this, point_index](bool checked) {
                if (point_index >= curve_points_.size()) return;
                auto& point = curve_points_[point_index];
                point.curve_tangent_enabled = checked;
                if (checked && point.curve_tangent ==
                        zima::document::Curve3DTangentMode::Automatic) {
                    point.curve_tangent =
                        zima::document::Curve3DTangentMode::PositiveX;
                }
                synchronize_experimental_connections();
                refresh_curve_points();
                notify_preview();
            });
        curve_points_table_->setCellWidget(row, 5,
            zima::ui::centered_cell_widget(direction));
        curve_points_table_->setRowHeight(row, 32);
        ++row;

        if (point_index >= curve_connections_.size()) continue;
        auto& connection = curve_connections_[point_index];
        curve_points_table_->setItem(row, 0, new QTableWidgetItem(
            tr("↳ %1 → %2")
                .arg(QString::fromStdString(curve_points_[point_index].name),
                     QString::fromStdString(curve_points_[point_index + 1].name))));

        auto* type = new QComboBox(curve_points_table_);
        type->setObjectName(
            QStringLiteral("curve3DExperimentalConnectionType%1")
                .arg(point_index));
        type->addItem(tr("Úsečka"), static_cast<int>(
            zima::document::Curve3DConnectionType::Line));
        type->addItem(tr("Spline"), static_cast<int>(
            zima::document::Curve3DConnectionType::InterpolatingSpline));
        type->addItem(tr("Sketch"), static_cast<int>(
            zima::document::Curve3DConnectionType::Sketch));
        type->addItem(tr("Dvojitý rádius"), static_cast<int>(
            zima::document::Curve3DConnectionType::Biarc));
        type->setCurrentIndex(type->findData(static_cast<int>(connection.type)));
        connect(type, &QComboBox::currentIndexChanged, this,
            [this, point_index, type] {
                if (point_index >= curve_connections_.size()) return;
                auto& connection = curve_connections_[point_index];
                connection.type =
                    static_cast<zima::document::Curve3DConnectionType>(
                        type->currentData().toInt());
                merge_experimental_spline_generators();
                refresh_curve_points();
                notify_preview();
            });
        curve_points_table_->setCellWidget(row, 1, type);

        curve_points_table_->setSpan(row, 2, 1, 4);
        if (connection.type ==
                zima::document::Curve3DConnectionType::Sketch) {
            auto* edit_sketch = new QPushButton(
                connection.sketch_serialized.empty()
                    ? tr("VYTVOŘIT SKETCH") : tr("UPRAVIT SKETCH"),
                curve_points_table_);
            edit_sketch->setObjectName(
                QStringLiteral("curve3DExperimentalSketch%1").arg(point_index));
            edit_sketch->setToolTip(tr(
                "Otevře běžný Skicář. START a END jsou systémové body "
                "patřící sousedním bodům trajektorie."));
            connect(edit_sketch, &QPushButton::clicked, this,
                [this, point_index] {
                    if (curve_sketch_edit_request_)
                        curve_sketch_edit_request_(point_index);
                });
            curve_points_table_->setCellWidget(row, 2, edit_sketch);
        } else {
            QString status;
            if (connection.type ==
                    zima::document::Curve3DConnectionType::InterpolatingSpline) {
                status = tr("Souvislé sousední řádky = jedna globální spline");
            } else if (connection.type ==
                    zima::document::Curve3DConnectionType::Biarc) {
                status = tr("Vyžaduje řešitelné směry na obou koncích");
            } else {
                status = tr("Směr bodů se pro úsečku ignoruje");
            }
            auto* status_item = new QTableWidgetItem(status);
            status_item->setFlags(status_item->flags() & ~Qt::ItemIsEditable);
            status_item->setForeground(QColor(QStringLiteral("#9fd7e5")));
            curve_points_table_->setItem(row, 2, status_item);
        }
        curve_points_table_->setRowHeight(row, 34);
        ++row;
    }

    if (previous) {
        for (std::size_t candidate = 0;
             candidate < curve_table_rows_.size(); ++candidate) {
            if (curve_table_rows_[candidate].kind == previous->kind &&
                curve_table_rows_[candidate].index == previous->index) {
                curve_points_table_->selectRow(static_cast<int>(candidate));
                break;
            }
        }
    }
    const auto selected_point = selected_curve_point_index();
    if (edit_curve_point_ != nullptr)
        edit_curve_point_->setEnabled(selected_point.has_value());
    if (delete_curve_point_ != nullptr)
        delete_curve_point_->setEnabled(selected_point.has_value());
    if (move_curve_point_up_ != nullptr)
        move_curve_point_up_->setEnabled(selected_point && *selected_point > 0);
    if (move_curve_point_down_ != nullptr)
        move_curve_point_down_->setEnabled(
            selected_point && *selected_point + 1 < curve_points_.size());
}

void ConstructionPropertiesDialog::refresh_curve_points() {
    if (curve_points_table_ == nullptr) return;
    if (initial_.kind ==
            zima::document::ConstructionKind::Curve3DExperimental) {
        refresh_experimental_curve_rows();
        return;
    }
    const int selected = curve_points_table_->currentRow();
    const QSignalBlocker blocked(curve_points_table_);
    curve_points_table_->setRowCount(static_cast<int>(curve_points_.size()));
    const auto tangent = [this](zima::document::Curve3DTangentMode mode) {
        using zima::document::Curve3DTangentMode;
        switch (mode) {
            case Curve3DTangentMode::PositiveX: return QStringLiteral("+X");
            case Curve3DTangentMode::NegativeX: return QStringLiteral("−X");
            case Curve3DTangentMode::PositiveY: return QStringLiteral("+Y");
            case Curve3DTangentMode::NegativeY: return QStringLiteral("−Y");
            case Curve3DTangentMode::PositiveZ: return QStringLiteral("+Z");
            case Curve3DTangentMode::NegativeZ: return QStringLiteral("−Z");
            case Curve3DTangentMode::Automatic: return tr("Automaticky");
        }
        return tr("Automaticky");
    };
    for (std::size_t index = 0; index < curve_points_.size(); ++index) {
        auto* point_item = new zima::ui::ReferenceCellItem(
            QString::fromStdString(curve_points_[index].name));
        point_item->set_reference(QString::fromStdString(curve_points_[index].id));
        curve_points_table_->setItem(static_cast<int>(index), 0, point_item);
        auto* axis_item = new zima::ui::ReferenceCellItem(
            tangent(curve_points_[index].curve_tangent));
        axis_item->set_reference(axis_item->text());
        axis_item->set_active_input(
            curve_points_[index].curve_tangent_enabled &&
            active_curve_axis_index_ && *active_curve_axis_index_ == index);
        if (!curve_points_[index].curve_tangent_enabled) {
            axis_item->setFlags(axis_item->flags() & ~Qt::ItemIsEnabled);
            axis_item->setToolTip(tr(
                "Směr je vypnutý; tečnu v tomto bodě určí interpolace automaticky."));
        }
        curve_points_table_->setItem(static_cast<int>(index), 1, axis_item);
        auto* cycle_axis = new QToolButton(curve_points_table_);
        cycle_axis->setObjectName(
            QStringLiteral("curve3DCycleAxis%1").arg(index));
        cycle_axis->setText(QStringLiteral("SWITCH"));
        cycle_axis->setEnabled(curve_points_[index].curve_tangent_enabled);
        style_curve_switch_button(cycle_axis, 58);
        cycle_axis->setToolTip(tr(
            "Přepnout osu směru X → Y → Z. Znaménko směru zůstane zachováno."));
        connect(cycle_axis, &QToolButton::clicked, this, [this, index] {
            if (index >= curve_points_.size()) return;
            using zima::document::Curve3DTangentMode;
            auto& mode = curve_points_[index].curve_tangent;
            switch (mode) {
                case Curve3DTangentMode::PositiveX:
                    mode = Curve3DTangentMode::PositiveY; break;
                case Curve3DTangentMode::PositiveY:
                    mode = Curve3DTangentMode::PositiveZ; break;
                case Curve3DTangentMode::PositiveZ:
                    mode = Curve3DTangentMode::PositiveX; break;
                case Curve3DTangentMode::NegativeX:
                    mode = Curve3DTangentMode::NegativeY; break;
                case Curve3DTangentMode::NegativeY:
                    mode = Curve3DTangentMode::NegativeZ; break;
                case Curve3DTangentMode::NegativeZ:
                    mode = Curve3DTangentMode::NegativeX; break;
                case Curve3DTangentMode::Automatic:
                    mode = Curve3DTangentMode::PositiveX; break;
            }
            active_curve_axis_index_.reset();
            if (curve_axis_cycle_) curve_axis_cycle_();
            refresh_curve_points();
            notify_preview();
        });
        curve_points_table_->setCellWidget(
            static_cast<int>(index), 2,
            zima::ui::centered_cell_widget(cycle_axis));

        using zima::document::Curve3DTangentMode;
        const bool negative = curve_points_[index].curve_tangent ==
                Curve3DTangentMode::NegativeX ||
            curve_points_[index].curve_tangent == Curve3DTangentMode::NegativeY ||
            curve_points_[index].curve_tangent == Curve3DTangentMode::NegativeZ;
        auto* flip_cell = zima::ui::build_reference_row_flip_button(
            curve_points_[index].curve_tangent_enabled, negative,
            [this, index](bool flipped) {
                if (index >= curve_points_.size() ||
                    !curve_points_[index].curve_tangent_enabled) return;
                using zima::document::Curve3DTangentMode;
                auto& mode = curve_points_[index].curve_tangent;
                switch (mode) {
                    case Curve3DTangentMode::PositiveX:
                    case Curve3DTangentMode::NegativeX:
                        mode = flipped ? Curve3DTangentMode::NegativeX
                                       : Curve3DTangentMode::PositiveX; break;
                    case Curve3DTangentMode::PositiveY:
                    case Curve3DTangentMode::NegativeY:
                        mode = flipped ? Curve3DTangentMode::NegativeY
                                       : Curve3DTangentMode::PositiveY; break;
                    case Curve3DTangentMode::PositiveZ:
                    case Curve3DTangentMode::NegativeZ:
                        mode = flipped ? Curve3DTangentMode::NegativeZ
                                       : Curve3DTangentMode::PositiveZ; break;
                    case Curve3DTangentMode::Automatic:
                        mode = flipped ? Curve3DTangentMode::NegativeX
                                       : Curve3DTangentMode::PositiveX; break;
                }
                active_curve_axis_index_.reset();
                if (curve_axis_cycle_) curve_axis_cycle_();
                refresh_curve_points();
                notify_preview();
            });
        auto* flip = qobject_cast<QToolButton*>(flip_cell);
        if (flip == nullptr) flip = flip_cell->findChild<QToolButton*>();
        if (flip != nullptr) {
            flip->setObjectName(QStringLiteral("curve3DFlip%1").arg(index));
            flip->setText(QStringLiteral("FLIP"));
            flip->setFixedWidth(40);
            flip->setToolTip(tr("Obrátit znaménko vybrané osy + ↔ −"));
        }
        curve_points_table_->setCellWidget(
            static_cast<int>(index), 3, flip_cell);

        auto* direction_enabled = new QToolButton(curve_points_table_);
        direction_enabled->setObjectName(
            QStringLiteral("curve3DDirectionEnabled%1").arg(index));
        direction_enabled->setText(QStringLiteral("SMĚR"));
        direction_enabled->setCheckable(true);
        direction_enabled->setChecked(
            curve_points_[index].curve_tangent_enabled);
        style_curve_switch_button(direction_enabled, 44);
        direction_enabled->setToolTip(tr(
            "Zapnout nebo vypnout řízení tečny lokální osou bodu"));
        connect(direction_enabled, &QToolButton::toggled, this,
            [this, index](bool enabled) {
                if (index >= curve_points_.size()) return;
                auto& point = curve_points_[index];
                point.curve_tangent_enabled = enabled;
                if (enabled && point.curve_tangent ==
                        zima::document::Curve3DTangentMode::Automatic) {
                    point.curve_tangent =
                        zima::document::Curve3DTangentMode::PositiveX;
                }
                active_curve_axis_index_.reset();
                if (curve_axis_cycle_) curve_axis_cycle_();
                refresh_curve_points();
                notify_preview();
            });
        curve_points_table_->setCellWidget(
            static_cast<int>(index), 4,
            zima::ui::centered_cell_widget(direction_enabled));
    }
    if (selected >= 0 && selected < curve_points_table_->rowCount())
        curve_points_table_->selectRow(selected);
    const int row = curve_points_table_->currentRow();
    const bool has_selection = row >= 0 && row < curve_points_table_->rowCount();
    if (edit_curve_point_ != nullptr) edit_curve_point_->setEnabled(has_selection);
    if (delete_curve_point_ != nullptr) delete_curve_point_->setEnabled(has_selection);
    if (move_curve_point_up_ != nullptr)
        move_curve_point_up_->setEnabled(has_selection && row > 0);
    if (move_curve_point_down_ != nullptr)
        move_curve_point_down_->setEnabled(
            has_selection && row + 1 < curve_points_table_->rowCount());
}

zima::document::ConstructionObject ConstructionPropertiesDialog::current_value() const {
    auto value = initial_;
    value.name = name_->text().trimmed().toStdString();
    value.origin = {origin_[0]->value(), origin_[1]->value(), origin_[2]->value()};
    const auto numeric = placement_->numeric_placement();
    if (rotation_[0] != nullptr) {
        value.absolute_rotation = {numeric.absolute_rotation_x,
            numeric.absolute_rotation_y, numeric.absolute_rotation_z};
        value.rotation = {numeric.rotation_x, numeric.rotation_y,
            numeric.rotation_z};
    }
    value.orientation_back = numeric.orientation_back;
    value.orientation_quarter_turns = numeric.orientation_quarter_turns;
    if (rotation_offset_[0] != nullptr) {
        value.rotation_offset_x = rotation_offset_[0]->value();
        value.rotation_offset_y = rotation_offset_[1]->value();
        value.rotation_offset_z = rotation_offset_[2]->value();
    }
    if (direction_combo_ != nullptr) {
        const auto key = direction_combo_->currentData().toString();
        if (initial_.kind == zima::document::ConstructionKind::Axis) {
            value.direction_axis = key.toStdString();
        }
        zima::kernel::Vec3 local = key == QStringLiteral("x") ||
                key == QStringLiteral("yz")
            ? zima::kernel::Vec3{1.0, 0.0, 0.0}
            : key == QStringLiteral("y") || key == QStringLiteral("xz")
                ? zima::kernel::Vec3{0.0, 1.0, 0.0}
                : zima::kernel::Vec3{0.0, 0.0, 1.0};
        constexpr double radians = std::numbers::pi / 180.0;
        const double cx = std::cos(value.rotation.x * radians);
        const double sx = std::sin(value.rotation.x * radians);
        const double cy = std::cos(value.rotation.y * radians);
        const double sy = std::sin(value.rotation.y * radians);
        const double cz = std::cos(value.rotation.z * radians);
        const double sz = std::sin(value.rotation.z * radians);
        local = {local.x, cx * local.y - sx * local.z,
            sx * local.y + cx * local.z};
        local = {cy * local.x + sy * local.z, local.y,
            -sy * local.x + cy * local.z};
        value.direction = {cz * local.x - sz * local.y,
            sz * local.x + cz * local.y, local.z};
    }
    if (display_size_ != nullptr) value.display_size = display_size_->value();
    if (base_plane_combo_ != nullptr) {
        const auto key = base_plane_combo_->currentData().toString();
        value.base_plane = key == QStringLiteral("xy")
            ? zima::document::LocalDatumPlane::XY
            : key == QStringLiteral("xz")
                ? zima::document::LocalDatumPlane::XZ
                : zima::document::LocalDatumPlane::YZ;
    }
    value.definition = current_definition();
    const auto populated = placement_->populated_references();
    const std::size_t required =
            value.definition == zima::document::ConstructionDefinition::PointReference
        ? populated.size()
        : value.definition ==
            zima::document::ConstructionDefinition::TwoPointAxis ? 2
        : value.definition == zima::document::ConstructionDefinition::ThreePointPlane ? 3
        : value.definition == zima::document::ConstructionDefinition::Absolute ? 0 : 1;
    value.references = placement_->combined_references(required);
    value.offset = offset_->value();
    value.curve_points = curve_points_;
    value.curve_connections = curve_connections_;
    if (curve_type_ != nullptr) {
        value.curve_type = static_cast<zima::document::Curve3DType>(
            curve_type_->currentData().toInt());
    }
    if (curve_tangent_ != nullptr) {
        value.curve_tangent =
            static_cast<zima::document::Curve3DTangentMode>(
                curve_tangent_->currentData().toInt());
    }
    return value;
}

void ConstructionPropertiesDialog::notify_preview() {
    if (preview_) preview_(current_value());
}

bool ConstructionPropertiesDialog::submit() {
    auto value = current_value();
    if ((value.kind == zima::document::ConstructionKind::Curve3D ||
         value.kind == zima::document::ConstructionKind::Curve3DExperimental) &&
        value.curve_points.size() < 2) {
        error_->setText(tr("3D křivka vyžaduje alespoň dva body."));
        return false;
    }
    if (value.kind ==
            zima::document::ConstructionKind::Curve3DExperimental) {
        if (value.curve_connections.size() + 1 !=
                value.curve_points.size() ||
            std::ranges::any_of(value.curve_connections,
                [](const auto& connection) {
                    return connection.type ==
                        zima::document::Curve3DConnectionType::Undefined;
                })) {
            error_->setText(tr(
                "Vyberte typ každého spojení experimentální trajektorie."));
            return false;
        }
        const auto solution =
            zima::document::solve_experimental_curve3d(value);
        if (!solution.valid) {
            error_->setText(tr("Trajektorii nelze vypočítat: %1")
                .arg(QString::fromStdString(solution.error)));
            return false;
        }
    }
    if (direction_combo_ != nullptr) {
        const double length = std::sqrt(value.direction.x * value.direction.x +
                                        value.direction.y * value.direction.y +
                                        value.direction.z * value.direction.z);
        if (length <= 1e-12) {
            error_->setText(tr("Směr nesmí být nulový."));
            return false;
        }
    }
    if (display_size_ != nullptr) value.display_size = display_size_->value();
    const auto populated = placement_->populated_references();
    const std::size_t required =
            value.definition == zima::document::ConstructionDefinition::PointReference
        ? populated.size()
        : value.definition ==
            zima::document::ConstructionDefinition::TwoPointAxis ? 2
        : value.definition == zima::document::ConstructionDefinition::ThreePointPlane ? 3
        : value.definition == zima::document::ConstructionDefinition::Absolute ? 0 : 1;
    if (populated.size() < required ||
        std::any_of(populated.begin(), populated.begin() +
                static_cast<std::ptrdiff_t>(required), [](const auto& reference) {
            return reference.owner_id.empty() || reference.semantic_key.empty();
        })) {
        error_->setText(tr("Vyberte všechny požadované reference."));
        return false;
    }
    value.references = placement_->combined_references(required);
    value.offset = offset_->value();
    if (value.name.empty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        return false;
    }
    if (initial_sweep_) {
        if (sweep_profiles_.empty()) {
            error_->setText(tr(
                "3D Sweep vyžaduje alespoň jednu uzavřenou profilovou skicu."));
            return false;
        }
        auto sweep = pending_sweep_value();
        for (std::size_t index = 0;
             index < sweep.sweep3d.profiles.size(); ++index) {
            if (!zima::document::PartDocument::reframe_sweep3d_profile(
                    sweep, index)) {
                error_->setText(tr(
                    "Profil nelze umístit: vybraný bod nebo tečna trajektorie nejsou platné."));
                return false;
            }
        }
        if (!sweep_commit_) return false;
        sweep_commit_(std::move(sweep));
        return true;
    }
    commit_(std::move(value));
    return true;
}

}  // namespace zima::app
