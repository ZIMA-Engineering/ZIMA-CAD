#include "table_entry.hpp"
#include "sketch_button_style.hpp"
#include "construction_properties_dialog.hpp"
#include "sweep_point_order_dialog.hpp"

#include "zima/ui/reference_cell.hpp"

#include <zima/kernel/stable_id.hpp>

#include <QDoubleSpinBox>
#include <QPointer>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QComboBox>
#include <QCheckBox>
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
    return kind == zima::document::ConstructionKind::Curve3D;
}

QString construction_properties_title(
    zima::document::ConstructionKind kind) {
    using zima::document::ConstructionKind;
    switch (kind) {
        case ConstructionKind::Point:
            return QObject::tr("Vlastnosti bodu");
        case ConstructionKind::Curve3D:
            return QObject::tr("Vlastnosti 3D křivky");
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
      curve_points_(initial.curve_points) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    // Keep Point/Axis/Plane properties as compact as Sketch and feature
    // properties. The reference table stretches inside this width; the old
    // 460 px minimum only left a needlessly wide empty middle column.
    setMinimumWidth(340);
    // The numeric column may grow with document precision and application font.
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

        setMinimumWidth(340);
        setMaximumWidth(QWIDGETSIZE_MAX);
        set_initial_size(QSize(460, 650));

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
        curve_rounding_ = new QCheckBox(tr("Zaoblení rohů"), this);
        curve_rounding_->setObjectName("curve3DRounding");
        curve_rounding_->setChecked(initial.curve_rounding_enabled);
        content_layout()->addWidget(curve_rounding_);
        connect(curve_rounding_, &QCheckBox::toggled, this, [this] {
            refresh_curve_points(); notify_preview();
        });

        curve_points_table_ = new QTableWidget(this);
        curve_points_table_->setObjectName(
            "curve3DPoints");
        curve_points_table_->setColumnCount(6);
        curve_points_table_->setHorizontalHeaderLabels({tr("Bod"), tr("Osa směru"), QString{}, tr("Flip"), tr("Směr"), tr("R [mm]")});
        curve_points_table_->horizontalHeader()->setSectionResizeMode(
            0, QHeaderView::Stretch);
        curve_points_table_->horizontalHeader()->setSectionResizeMode(
            1, QHeaderView::Stretch);
        for (int column = 2; column < curve_points_table_->columnCount(); ++column)
            curve_points_table_->horizontalHeader()->setSectionResizeMode(
                column, QHeaderView::Fixed);

        curve_points_table_->setColumnWidth(2, 66);
        curve_points_table_->setColumnWidth(3, 48);
        curve_points_table_->setColumnWidth(4, 52);
        curve_points_table_->setColumnWidth(5, 100);

        curve_points_table_->verticalHeader()->hide();
        curve_points_table_->setSelectionBehavior(
            QAbstractItemView::SelectRows);
        curve_points_table_->setSelectionMode(
            QAbstractItemView::SingleSelection);
        curve_points_table_->setEditTriggers(
            QAbstractItemView::NoEditTriggers);
        zima::ui::install_reference_cell_delegate(curve_points_table_);
        curve_points_table_->setMinimumHeight(150);
        content_layout()->addWidget(curve_points_table_);

        auto* point_buttons = new QHBoxLayout;
        edit_curve_point_ = new QPushButton(tr("Upravit"), this);
        move_curve_point_up_ = new QPushButton(tr("Nahoru"), this);
        move_curve_point_down_ = new QPushButton(tr("Dolů"), this);
        edit_curve_point_->setObjectName("curve3DEditPoint");
        move_curve_point_up_->setObjectName("curve3DMovePointUp");
        move_curve_point_down_->setObjectName("curve3DMovePointDown");
        for (auto* button : {edit_curve_point_, move_curve_point_up_,
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
                if (static_cast<std::size_t>(row)==curve_points_.size()) {
                    active_curve_axis_index_.reset();
                    if(curve_axis_cycle_)curve_axis_cycle_();
                    if(curve_point_edit_request_)curve_point_edit_request_(std::nullopt);
                    return;
                }
                if (initial_.kind ==
                        zima::document::ConstructionKind::Curve3D &&
                    column == 1 &&
                    static_cast<std::size_t>(row) < curve_points_.size() &&
                    curve_points_[static_cast<std::size_t>(row)]
                        .curve_tangent_enabled && curve_axis_request_) {
                    curve_axis_request_(static_cast<std::size_t>(row));
                }
            });
        const auto move_point = [this, selected_curve_point](int direction) {
            const auto index = selected_curve_point();
            if (!index) return;
            const auto destination = static_cast<std::ptrdiff_t>(*index) + direction;
            if (destination < 0 || destination >=
                    static_cast<std::ptrdiff_t>(curve_points_.size())) return;
            std::swap(curve_points_[*index],
                curve_points_[static_cast<std::size_t>(destination)]);

            refresh_curve_points();

            curve_points_table_->selectRow(static_cast<int>(destination));

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
    initialized_ = true;
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
    set_internal_title(tr("Vlastnosti 3D tažení"));
    if (curve_rounding_) curve_rounding_->setToolTip(tr(
        "Vypnuto: samostatné rovné úseky s vlastními profily a kolmými čely. "
        "Zapnuto: souvislé tažení přes zaoblené rohy."));
    initialize_sweep_ui();
    // Sweep adds profile and operation controls below the Curve3D editor.
    set_initial_size(QSize(460, std::max(900, sizeHint().height())));
}

void ConstructionPropertiesDialog::initialize_sweep_ui() {
    if (!initial_sweep_) return;
    content_layout()->removeWidget(error_);
    auto* thin_form = new QFormLayout;
    sweep_result_type_ = new QComboBox(this);
    sweep_result_type_->setObjectName("sweep3DResultType");
    sweep_result_type_->addItems({tr("Těleso"),tr("Thin")});
    sweep_result_type_->setCurrentIndex(initial_sweep_->sweep3d.result_type ==
        zima::document::ProfileResultType::Thin ? 1 : 0);
    thin_form->addRow(tr("Typ výsledku"),sweep_result_type_);
    sweep_thickness_ = new QDoubleSpinBox(this);
    sweep_thickness_->setObjectName("sweep3DThickness");
    sweep_thickness_->setDecimals(offset_->decimals());
    sweep_thickness_->setRange(0.001,1'000'000.0);
    sweep_thickness_->setSuffix(tr(" mm"));
    sweep_thickness_->setValue(initial_sweep_->sweep3d.thickness);
    thin_form->addRow(tr("Tloušťka"),sweep_thickness_);
    sweep_thin_mode_ = new QComboBox(this);
    sweep_thin_mode_->setObjectName("sweep3DThinSide");
    sweep_thin_mode_->addItems({tr("Dovnitř"),tr("Ven"),tr("Symetricky")});
    sweep_thin_mode_->setCurrentIndex(initial_sweep_->sweep3d.thin_mode ==
        zima::document::ThinMode::OneSide ? 0 : initial_sweep_->sweep3d.thin_mode ==
        zima::document::ThinMode::OtherSide ? 1 : 2);
    sweep_thin_mode_->setToolTip(tr("Symetricky: polovina zadané tloušťky na každou stranu profilu. "
        "U otevřené kontury určuje stranu směr jejího obvodu."));
    thin_form->addRow(tr("Strana tloušťky"),sweep_thin_mode_);
    content_layout()->addLayout(thin_form);
    const auto update_thin=[this,thin_form] {
        const bool thin=sweep_result_type_->currentIndex()==1;
        thin_form->setRowVisible(sweep_thickness_,thin);
        thin_form->setRowVisible(sweep_thin_mode_,thin);
        set_initial_size(QSize(460,std::max(900,sizeHint().height())));
    };
    update_thin();
    connect(sweep_result_type_,&QComboBox::currentIndexChanged,this,
        [this,update_thin] { update_thin();notify_preview(); });
    connect(sweep_thickness_,&QDoubleSpinBox::valueChanged,this,[this] { notify_preview(); });
    connect(sweep_thin_mode_,&QComboBox::currentIndexChanged,this,[this] { notify_preview(); });
    auto* title = new QLabel(tr("Profily 3D tažení"), this);
    title->setStyleSheet("color:#9fd7e5;font-weight:700;");
    content_layout()->addWidget(title);
    sweep_profiles_table_ = new QTableWidget(this);
    sweep_profiles_table_->setObjectName("sweep3DProfiles");
    sweep_profiles_table_->setColumnCount(4);
    sweep_profiles_table_->setHorizontalHeaderLabels(
        {tr("Stanice"), tr("Skica profilu"), tr("Pořadí bodů"), tr("Použitý profil")});
    sweep_profiles_table_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    sweep_profiles_table_->verticalHeader()->hide();
    sweep_profiles_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    sweep_profiles_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    sweep_profiles_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sweep_profiles_table_->setMinimumHeight(90);
    content_layout()->addWidget(sweep_profiles_table_);
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

void ConstructionPropertiesDialog::set_origin_selection_mode_callback(
        std::function<void(bool)> callback) {
    if (placement_) placement_->set_origin_selection_mode_callback(std::move(callback));
}

void ConstructionPropertiesDialog::set_origin_selection_mode_active(bool active) {
    if (placement_) placement_->set_origin_selection_mode_active(active);
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

    refresh_curve_points();
    if (index && curve_points_table_ != nullptr) {

        curve_points_table_->selectRow(static_cast<int>(*index));

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

    refresh_curve_points();
    refresh_sweep_profiles();
    if (update_preview) notify_preview();
}

const zima::document::ConstructionObject*
ConstructionPropertiesDialog::curve_point(std::size_t index) const {
    return index < curve_points_.size() ? &curve_points_[index] : nullptr;
}


void ConstructionPropertiesDialog::refresh_sweep_profiles() {
    if (sweep_profiles_table_ == nullptr) return;
    const QSignalBlocker blocked(sweep_profiles_table_);
    sweep_profiles_table_->setRowCount(0);
    auto* row_actions=entry_row_header(sweep_profiles_table_);row_actions->clear_actions();
    zima::document::Curve3DRoute route;
    try { route = zima::document::curve3d_route(current_value()); }
    catch(const std::exception& error) { error_->setText(QString::fromUtf8(error.what())); return; }
    QString inherited_from;
    for (const auto& station : route.stations) {
        const int row = sweep_profiles_table_->rowCount();
        sweep_profiles_table_->insertRow(row);
        auto* item = new QTableWidgetItem(QString::fromStdString(station.label));
        if (initial_sweep_ && current_value().curve_type == zima::document::Curve3DType::Polyline &&
            !current_value().curve_rounding_enabled) {
            auto container=*initial_sweep_;
            container.sweep3d.path=current_value();
            const auto point=std::ranges::find_if(container.sweep3d.path.curve_points,
                [&](const auto& p){return p.id==station.point_id;});
            const auto index=static_cast<std::size_t>(std::distance(container.sweep3d.path.curve_points.begin(),point));
            const bool start=!station.incoming && index+1<container.sweep3d.path.curve_points.size();
            const auto segment=start?index:index-1;
            item->setToolTip(QString::fromStdString(zima::document::sweep3d_cap_label(container,
                zima::document::sweep3d_cap_key(container.sweep3d.path,segment,start))));
        }
        item->setData(Qt::UserRole, QString::fromStdString(station.point_id));
        sweep_profiles_table_->setItem(row, 0, item);
        const auto own = std::ranges::find_if(sweep_profiles_, [&](const auto& profile) {
            return profile.point_id == station.point_id && profile.incoming == station.incoming;
        });
        const bool has_profile = own != sweep_profiles_.end() &&
            zima::document::sweep3d_profile_has_geometry(
                zima::sketcher::Sketch::from_serialized(own->sketch_serialized));
        row_actions->set_action(row,has_profile,[this,station] {
            std::erase_if(sweep_profiles_,[&](const auto& p){return p.point_id==station.point_id&&p.incoming==station.incoming;});
            refresh_sweep_profiles();notify_preview();
        });
        QString status;
        if (!station.active) status = tr("Neaktivní");
        else if (has_profile) {
            inherited_from = QString::fromStdString(station.label);
            status = tr("Vlastní");
        } else if (inherited_from.isEmpty()) status = tr("Vyplňte první profil");
        else status = tr("Z bodu %1").arg(inherited_from);
        sweep_profiles_table_->setItem(row, 3, new QTableWidgetItem(status));
        auto* order=new QPushButton(tr("Pořadí bodů"),sweep_profiles_table_);
        order->setObjectName(QString("sweep3DPointOrder%1").arg(QString::fromStdString(station.label)));
        order->setEnabled(station.active && has_profile);
        order->setToolTip(tr("Určit první bod a zkontrolovat párování obvodu profilu."));
        const auto profile_index=static_cast<std::size_t>(std::distance(sweep_profiles_.begin(),own));
        connect(order,&QPushButton::clicked,this,[this,profile_index] {
            try {
                const auto initial=sweep_profiles_.at(profile_index).correspondence_start_point_id;
                const QPointer<ConstructionPropertiesDialog> self(this);
                const auto preview=[self,profile_index](std::string id) {
                    if(!self)return;
                    self->sweep_profiles_.at(profile_index).correspondence_start_point_id=std::move(id);
                    self->notify_preview();
                };
                auto* dialog=new SweepPointOrderDialog(
                    zima::sketcher::Sketch::from_serialized(sweep_profiles_.at(profile_index).sketch_serialized),
                    initial,preview,parentWidget(),sweep_result_type_->currentIndex()==1);
                connect(dialog,&QDialog::finished,this,[self,preview,initial](int result) {
                    if(!self)return;
                    if(result!=QDialog::Accepted)preview(initial);
                    self->show();self->raise();
                });
                hide();dialog->show();
            } catch(const std::exception& error) { error_->setText(QString::fromUtf8(error.what())); }
        });
        sweep_profiles_table_->setCellWidget(row,2,order);
        auto* button = new QPushButton(tr("Sketch"), sweep_profiles_table_);
        button->setObjectName(QString("sweep3DStationSketch%1").arg(QString::fromStdString(station.label)));
        button->setEnabled(station.active);
        style_sketch_button(button);
        connect(button, &QPushButton::clicked, this, [this, station] {
            auto found=std::ranges::find_if(sweep_profiles_, [&](const auto& profile) {
                return profile.point_id==station.point_id && profile.incoming==station.incoming;
            });
            std::size_t index=static_cast<std::size_t>(std::distance(sweep_profiles_.begin(),found));
            if(found==sweep_profiles_.end()) {
                auto sketch=zima::sketcher::Sketch::create_default();
                sketch.name="Profil "+station.label;
                sketch.owner_container_id=initial_sweep_->id;
                sweep_profiles_.push_back({zima::kernel::make_stable_id(),station.point_id,sketch.id,sketch.serialized(),station.incoming});
            }
            auto pending=pending_sweep_value();
            sweep_profiles_[index]=pending.sweep3d.profiles[index];
            if(sweep_profile_edit_request_)sweep_profile_edit_request_(index);
        });
        sweep_profiles_table_->setCellWidget(row,1,button);
    }
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
    container.sweep3d.result_type = sweep_result_type_->currentIndex()==1 ?
        zima::document::ProfileResultType::Thin : zima::document::ProfileResultType::Solid;
    container.sweep3d.thickness = sweep_thickness_->value();
    container.sweep3d.thin_mode = sweep_thin_mode_->currentIndex()==0 ? zima::document::ThinMode::OneSide
        : sweep_thin_mode_->currentIndex()==1 ? zima::document::ThinMode::OtherSide : zima::document::ThinMode::Symmetric;
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
    if (initial_sweep_ && (owner_id == initial_sweep_->id ||
        owner_id == initial_sweep_->feature_id ||
        owner_id == initial_sweep_->container_origin.id)) return true;
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

bool ConstructionPropertiesDialog::set_curve_point_radius(
    const std::string& point_id, double value) {
    auto pending = current_value();
    for (std::size_t index = 1; index + 1 < pending.curve_points.size(); ++index) {
        if (pending.curve_points[index].id != point_id) continue;
        auto* field = qobject_cast<QDoubleSpinBox*>(
            curve_points_table_->cellWidget(static_cast<int>(index), 5));
        if (field == nullptr || !field->isEnabled()) return false;
        if (!std::isfinite(value) || value < field->minimum() || value > field->maximum())
            throw std::runtime_error("Radius je mimo povolený rozsah.");
        pending.curve_points[index].curve_radius = value;
        static_cast<void>(zima::document::curve3d_route(pending));
        // Use the table's normal change notification: preview and station
        // profiles stay transient until the Properties dialog is confirmed.
        field->setValue(value);
        return true;
    }
    return false;
}

void ConstructionPropertiesDialog::filter_parameter_dimensions(
    std::vector<zima::kernel::ViewerDimension>& dimensions) const {
    std::erase_if(dimensions, [this](const auto& dimension) {
        if (dimension.reference.owner_id != initial_.id) return false;
        std::string_view key = dimension.reference.semantic_key;
        if (!key.starts_with("parameter:")) return false;
        key.remove_prefix(std::string_view("parameter:").size());
        if (key.starts_with("placement:"))
            key.remove_prefix(std::string_view("placement:").size());
        const QDoubleSpinBox* field = nullptr;
        if (key == "x" || key == "y" || key == "z") {
            field = origin_[key == "x" ? 0 : key == "y" ? 1 : 2];
        } else if (key == "rotation_x" || key == "rotation_y" || key == "rotation_z") {
            const auto index = key == "rotation_x" ? 0 : key == "rotation_y" ? 1 : 2;
            field = rotation_[index]->isEnabled()
                ? rotation_[index] : rotation_offset_[index];
        } else if (key == "offset") {
            field = offset_;
        } else if (key == "length") {
            field = display_size_;
        } else {
            return false;
        }
        // Only offer the live editable value, including the correction
        // rather than the read-only absolute angle when a reference owns it.
        return !field || !field->isEnabled() || !field->isVisible() ||
            std::abs(dimension.value - field->value()) >
                0.5 * std::pow(10.0, -field->decimals()) + 1e-9;
    });
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
    if (key == "thickness") return set_field(sweep_thickness_);
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
    return row < curve_points_.size() ? std::optional<std::size_t>{row} : std::nullopt;
}


void ConstructionPropertiesDialog::refresh_curve_points() {
    if (curve_points_table_ == nullptr) return;

    const int selected = curve_points_table_->currentRow();
    const QSignalBlocker blocked(curve_points_table_);
    curve_points_table_->setRowCount(static_cast<int>(curve_points_.size())+1);
    auto* row_actions=entry_row_header(curve_points_table_);row_actions->clear_actions();
    for(std::size_t index=0;index<curve_points_.size();++index)
        row_actions->set_action(static_cast<int>(index),true,[this,index]{erase_curve_point(index);});
    const int offered_row=static_cast<int>(curve_points_.size());
    auto* offered=new zima::ui::ReferenceCellItem(tr("Nový bod…"));
    curve_points_table_->setItem(offered_row,0,offered);
    row_actions->set_action(offered_row,false,{},[this] {
        active_curve_axis_index_.reset();if(curve_axis_cycle_)curve_axis_cycle_();
        if(curve_point_edit_request_)curve_point_edit_request_(std::nullopt);
    });
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
    const bool polyline = curve_type_ && curve_type_->currentData().toInt() ==
        static_cast<int>(zima::document::Curve3DType::Polyline);
    if(curve_rounding_)curve_rounding_->setEnabled(polyline);
    for (std::size_t index = 0; index < curve_points_.size(); ++index) {
        auto* radius = new QDoubleSpinBox(curve_points_table_);
        radius->setObjectName(QString("curve3DRadius%1").arg(index+1));
        radius->setDecimals(offset_->decimals()); radius->setRange(0,1e9);
        const bool enabled=polyline && curve_rounding_ && curve_rounding_->isChecked() && index>0 && index+1<curve_points_.size();
        radius->setEnabled(enabled);
        radius->setValue(enabled?curve_points_[index].curve_radius:0);
        connect(radius,&QDoubleSpinBox::valueChanged,this,[this,index](double value){
            curve_points_[index].curve_radius=value; notify_preview();
        });
        curve_points_table_->setCellWidget(static_cast<int>(index),5,radius);
        auto* point_item = new zima::ui::ReferenceCellItem(
            QString::number(index+1));
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
    const bool has_selection = row >= 0 && static_cast<std::size_t>(row) < curve_points_.size();
    if (edit_curve_point_ != nullptr) edit_curve_point_->setEnabled(has_selection);
    if (move_curve_point_up_ != nullptr)
        move_curve_point_up_->setEnabled(has_selection && row > 0);
    if (move_curve_point_down_ != nullptr)
        move_curve_point_down_->setEnabled(
            has_selection && static_cast<std::size_t>(row + 1) < curve_points_.size());
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
    if(curve_rounding_)value.curve_rounding_enabled=curve_rounding_->isChecked();

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
    if (!initialized_) return;
    refresh_sweep_profiles();
    try {
        const auto value=current_value();
        if(value.kind==zima::document::ConstructionKind::Curve3D)
            static_cast<void>(zima::document::curve3d_route(value));
        error_->clear();
        if (preview_) preview_(value);
    } catch(const std::exception& error) { error_->setText(QString::fromUtf8(error.what())); }
}

bool ConstructionPropertiesDialog::submit() {
    auto value = current_value();
    try { if(value.kind==zima::document::ConstructionKind::Curve3D)static_cast<void>(zima::document::curve3d_route(value)); }
    catch(const std::exception& error){error_->setText(QString::fromUtf8(error.what()));return false;}
    if ((value.kind == zima::document::ConstructionKind::Curve3D) &&
        value.curve_points.size() < 2) {
        error_->setText(tr("3D křivka vyžaduje alespoň dva body."));
        return false;
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
                "3D tažení vyžaduje alespoň jednu profilovou skicu."));
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
