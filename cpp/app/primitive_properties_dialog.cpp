#include "primitive_properties_dialog.hpp"

#include <zima/ui/reference_cell.hpp>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QBrush>
#include <QColor>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QTableWidget>
#include <QTreeWidget>

#include <exception>
#include <algorithm>

namespace zima::app {
namespace {

// Mirrors ConstructionPropertiesDialog's readable_reference_kind() so a
// picked position/orientation reference reads the same way in every
// container's placement UI.
QString readable_placement_reference_kind(const std::string& semantic) {
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

// Every primitive solid shares one universal placement contract (origin +
// FRONT/TOP orientation references, manual RX/RY/RZ correction on top).
bool supports_placement_reference_table(zima::document::FeatureKind kind) {
    using zima::document::FeatureKind;
    return kind == FeatureKind::Box || kind == FeatureKind::Cylinder ||
        kind == FeatureKind::Sphere || kind == FeatureKind::Cone ||
        kind == FeatureKind::Pyramid || kind == FeatureKind::Wedge ||
        kind == FeatureKind::Extrusion || kind == FeatureKind::Revolution ||
        kind == FeatureKind::ImportedStep;
}

QString primitive_label(zima::document::FeatureKind kind) {
    return kind == zima::document::FeatureKind::Cylinder ? QObject::tr("Válec")
        : kind == zima::document::FeatureKind::Sphere ? QObject::tr("Koule")
        : kind == zima::document::FeatureKind::Cone ? QObject::tr("Kužel")
        : kind == zima::document::FeatureKind::Pyramid ? QObject::tr("Jehlan")
        : kind == zima::document::FeatureKind::Wedge ? QObject::tr("Klín")
        : kind == zima::document::FeatureKind::Extrusion
            ? QObject::tr("Vytažení")
        : kind == zima::document::FeatureKind::Revolution
            ? QObject::tr("Rotace")
        : kind == zima::document::FeatureKind::Fillet
            ? QObject::tr("Zaoblení")
        : kind == zima::document::FeatureKind::Chamfer
            ? QObject::tr("Sražení")
        : kind == zima::document::FeatureKind::ImportedStep
            ? QObject::tr("Import STEP") : QObject::tr("Kvádr");
}

}  // namespace

PrimitivePropertiesDialog::PrimitivePropertiesDialog(
    const zima::document::HistoryContainer& initial,
    bool edit_mode,
    bool allow_subtract,
    LegacyCommitCallback commit,
    QWidget* parent)
    : PrimitivePropertiesDialog(
          initial, edit_mode, allow_subtract,
          [commit = std::move(commit)](
              zima::document::HistoryContainer value,
              std::vector<std::string>) mutable {
              commit(std::move(value));
          }, parent) {}

PrimitivePropertiesDialog::PrimitivePropertiesDialog(
    const zima::document::HistoryContainer& initial,
    bool edit_mode,
    bool allow_subtract,
    CommitCallback commit,
    QWidget* parent,
    std::vector<AssemblyTarget> assembly_targets,
    std::vector<std::string> selected_targets,
    bool assembly_cut_mode)
    : PropertiesSubWindow(primitive_label(initial.feature_kind), parent),
      initial_(initial), edit_mode_(edit_mode),
      accepted_target_baseline_(selected_targets), commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(340);
    auto* header_form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(initial.name), this);
    const bool treatment = initial.feature_kind == zima::document::FeatureKind::Fillet ||
        initial.feature_kind == zima::document::FeatureKind::Chamfer;
    if (!treatment) header_form->addRow(tr("Název"), name_);
    else name_->hide();
    if (!treatment) {
        operation_ = new QComboBox(this);
        if (!assembly_cut_mode) operation_->addItem(tr("Přičíst"), "add");
        if (allow_subtract || assembly_cut_mode) {
            operation_->addItem(tr("Odečíst"), "subtract");
        }
        if (initial.combine_mode == zima::document::CombineMode::Subtract) {
            operation_->setCurrentIndex(operation_->findData("subtract"));
        }
        operation_->hide();
        if (assembly_cut_mode) operation_->setEnabled(false);
    }
    content_layout()->addLayout(header_form);

    // Universal container placement (Umístění kontejneru / Orientace
    // kontejneru) is shown immediately below the name/operation header, in
    // the same position as ConstructionPropertiesDialog's Point/Axis/Plane
    // dialogs, before any shape-specific dimension fields.
    if (supports_placement_reference_table(initial.feature_kind)) {
        placement_ = std::make_unique<zima::ui::ContainerPlacementSection>(
            this, content_layout(), /*with_orientation=*/true,
            /*position_rows_can_define_rotation=*/true);
        placement_->initialize_from_references(initial.placement.references,
            [](const std::string& semantic) {
                return readable_placement_reference_kind(semantic);
            });
        placement_->initialize_numeric_values(initial.placement);
        const bool has_orientation_reference = std::any_of(
            initial.placement.references.begin(), initial.placement.references.end(),
            [](const auto& reference) {
                return reference.orientation_drives_rotation;
            });
        placement_->set_orientation_base_rotation(
            {initial.placement.rotation_x, initial.placement.rotation_y,
             initial.placement.rotation_z}, has_orientation_reference);
        translation_ = placement_->translation_fields();
        rotation_ = placement_->rotation_offset_fields();
        for (auto* input : translation_) input->setObjectName("primitiveTranslation");
        for (auto* input : rotation_) input->setObjectName("primitiveRotation");
        reference_status_ = placement_->reference_status_label();
        dof_label_ = placement_->dof_label();
        placement_->reference_table()->setObjectName("primitiveReferenceTable");
        if (placement_->orientation_table() != nullptr) {
            placement_->orientation_table()->setObjectName("primitiveOrientationTable");
        }
        placement_->set_reference_request_callback(
            [this](std::size_t index) {
                if (reference_request_) reference_request_(index);
            });
        placement_->set_changed_callback([this] { notify_preview(); });
        placement_->set_highlights_changed_callback(
            [this] { if (reference_highlights_changed_) reference_highlights_changed_(); });
        placement_->refresh_reference_table();
        placement_->refresh_orientation_table();
        placement_->install_dof_label(content_layout());
    }

    auto* form = new QFormLayout;

    const auto dimension = [this](double value, const char* object_name) {
        auto* field = new QDoubleSpinBox(this);
        field->setRange(0.001, 1'000'000.0);
        field->setDecimals(3);
        field->setSingleStep(1.0);
        field->setSuffix(" mm");
        field->setObjectName(object_name);
        field->setValue(value);
        return field;
    };
    if (initial.feature_kind == zima::document::FeatureKind::Box) {
        length_ = dimension(initial.box.length, "boxLength");
        width_ = dimension(initial.box.width, "boxWidth");
        height_ = dimension(initial.box.height, "boxHeight");
        form->addRow(tr("Délka"), length_);
        form->addRow(tr("Šířka"), width_);
        form->addRow(tr("Výška"), height_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Cylinder) {
        radius_ = dimension(initial.cylinder.radius, "cylinderRadius");
        height_ = dimension(initial.cylinder.height, "cylinderHeight");
        form->addRow(tr("Poloměr"), radius_);
        form->addRow(tr("Výška"), height_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Sphere) {
        radius_ = dimension(initial.sphere.radius, "sphereRadius");
        form->addRow(tr("Poloměr"), radius_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Cone) {
        radius_ = dimension(initial.cone.bottom_radius, "coneBottomRadius");
        top_radius_ = dimension(std::max(initial.cone.top_radius, 0.001), "coneTopRadius");
        top_radius_->setRange(0.0, 1'000'000.0);
        top_radius_->setValue(initial.cone.top_radius);
        height_ = dimension(initial.cone.height, "coneHeight");
        form->addRow(tr("Dolní poloměr"), radius_);
        form->addRow(tr("Horní poloměr"), top_radius_);
        form->addRow(tr("Výška"), height_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Pyramid) {
        length_ = dimension(initial.pyramid.length, "pyramidLength");
        width_ = dimension(initial.pyramid.width, "pyramidWidth");
        height_ = dimension(initial.pyramid.height, "pyramidHeight");
        form->addRow(tr("Délka základny"), length_);
        form->addRow(tr("Šířka základny"), width_);
        form->addRow(tr("Výška"), height_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Wedge) {
        length_ = dimension(initial.wedge.length, "wedgeLength");
        width_ = dimension(initial.wedge.width, "wedgeWidth");
        height_ = dimension(initial.wedge.height, "wedgeHeight");
        top_offset_ = dimension(std::max(initial.wedge.top_offset, 0.001), "wedgeTopOffset");
        top_offset_->setRange(0.0, initial.wedge.length);
        top_offset_->setValue(initial.wedge.top_offset);
        connect(length_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            top_offset_, &QDoubleSpinBox::setMaximum);
        form->addRow(tr("Délka"), length_);
        form->addRow(tr("Šířka"), width_);
        form->addRow(tr("Výška"), height_);
        form->addRow(tr("Odsazení horní hrany"), top_offset_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Extrusion ||
               initial.feature_kind == zima::document::FeatureKind::Revolution) {
        const bool revolve = initial.feature_kind ==
            zima::document::FeatureKind::Revolution;
        const auto profile_source = revolve ? initial.revolution.profile_source
                                             : initial.extrusion.profile_source;
        const auto& sketch_id = revolve ? initial.revolution.sketch_id
                                        : initial.extrusion.sketch_id;
        const auto result_type = revolve ? initial.revolution.result_type
                                         : initial.extrusion.result_type;
        const double thin_thickness = revolve ? initial.revolution.thin_thickness
                                              : initial.extrusion.thin_thickness;
        const auto thin_mode = revolve ? initial.revolution.thin_mode
                                       : initial.extrusion.thin_mode;
        const auto extent_mode = revolve ? initial.revolution.extent_mode
                                         : initial.extrusion.extent_mode;
        const auto direction_mode = revolve ? initial.revolution.direction
                                            : initial.extrusion.direction;
        const auto cycle_combo = [](QComboBox* combo) {
            combo->setCurrentIndex((combo->currentIndex() + 1) % combo->count());
        };
        auto* source_row = new QWidget(this);
        auto* source_layout = new QHBoxLayout(source_row);
        source_layout->setContentsMargins(0, 0, 0, 0);
        profile_source_ = new QLineEdit(this);
        profile_source_->setReadOnly(true);
        profile_source_->setText(profile_source ==
                zima::document::ProfileSource::Internal
            ? tr("Vlastní kontejner")
            : QString::fromStdString(sketch_id));
        profile_pick_button_ = new QPushButton(tr("Vybrat"), this);
        profile_pick_button_->setCheckable(true);
        profile_pick_button_->setToolTip(tr("Vybrat zdrojovou skicu"));
        profile_reset_button_ = new QPushButton(QStringLiteral("↶"), this);
        profile_reset_button_->setToolTip(tr("Použít vlastní skicu"));
        source_layout->addWidget(profile_source_, 1);
        source_layout->addWidget(profile_pick_button_);
        source_layout->addWidget(profile_reset_button_);
        form->addRow(tr("Zdroj profilu"), source_row);
        // Extrusion/Revolution always own their Sketch.  A model command must
        // never silently attach the new history container to a Sketch owned
        // by another container.
        source_row->setVisible(false);
        connect(profile_pick_button_, &QPushButton::toggled, this,
            [this](bool checked) {
                if (profile_pick_request_) profile_pick_request_(checked);
            });
        connect(profile_reset_button_, &QPushButton::clicked, this, [this] {
            profile_source_->setText(tr("Vlastní kontejner"));
            profile_pick_button_->setChecked(false);
            notify_preview();
        });
        profile_status_ = new QLineEdit(this);
        profile_status_->setReadOnly(true);
        profile_status_->setText(tr("Uzavřený"));
        form->addRow(tr("Stav profilu"), profile_status_);
        profile_plane_offset_ = dimension(
            revolve ? initial.revolution.profile_plane_offset
                    : initial.extrusion.profile_plane_offset,
            "profilePlaneOffset");
        profile_plane_offset_->setRange(-1'000'000.0, 1'000'000.0);
        profile_plane_offset_->setValue(
            revolve ? initial.revolution.profile_plane_offset
                    : initial.extrusion.profile_plane_offset);
        form->addRow(tr("Odsazení roviny"), profile_plane_offset_);
        connect(profile_plane_offset_,
            qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this] { notify_preview(); });
        result_type_ = new QComboBox(this);
        result_type_->addItem(tr("Těleso"), "solid");
        result_type_->addItem(tr("Thin"), "thin");
        result_type_->setCurrentIndex(result_type_->findData(
            result_type == zima::document::ProfileResultType::Thin
                ? "thin" : "solid"));
        auto* result_row = new QWidget(this);
        auto* result_layout = new QHBoxLayout(result_row);
        result_layout->setContentsMargins(0, 0, 0, 0);
        result_type_switch_button_ = new QPushButton(tr("Přepnout"), this);
        result_layout->addWidget(result_type_, 1);
        result_layout->addWidget(result_type_switch_button_);
        form->addRow(tr("Typ výsledku"), result_row);
        connect(result_type_switch_button_, &QPushButton::clicked, this,
            [result_type = result_type_, cycle_combo] { cycle_combo(result_type); });
        thin_thickness_ = dimension(
            thin_thickness, "extrusionThinThickness");
        form->addRow(tr("Tloušťka"), thin_thickness_);
        thin_mode_ = new QComboBox(this);
        thin_mode_->addItem(tr("Jedna strana"), "one_side");
        thin_mode_->addItem(tr("Na druhou stranu"), "other_side");
        thin_mode_->addItem(tr("Symetricky"), "symmetric");
        thin_mode_->setCurrentIndex(thin_mode_->findData(
            thin_mode == zima::document::ThinMode::OtherSide
                ? "other_side"
            : thin_mode == zima::document::ThinMode::Symmetric
                ? "symmetric" : "one_side"));
        auto* thin_row = new QWidget(this);
        auto* thin_layout = new QHBoxLayout(thin_row);
        thin_layout->setContentsMargins(0, 0, 0, 0);
        thin_mode_switch_button_ = new QPushButton(tr("Přepnout"), this);
        thin_layout->addWidget(thin_mode_, 1);
        thin_layout->addWidget(thin_mode_switch_button_);
        form->addRow(tr("Strana tloušťky"), thin_row);
        connect(thin_mode_switch_button_, &QPushButton::clicked, this,
            [thin_mode = thin_mode_, cycle_combo] { cycle_combo(thin_mode); });
        extent_mode_ = new QComboBox(this);
        extent_mode_->setObjectName("extrusionExtentMode");
        extent_mode_->addItem(tr("Jedna strana"), "one_side");
        extent_mode_->addItem(tr("Obě strany"), "two_sides");
        extent_mode_->addItem(tr("Symetricky"), "symmetric");
        extent_mode_->setCurrentIndex(extent_mode_->findData(
            extent_mode == zima::document::ProfileExtentMode::TwoSides
                ? "two_sides"
            : extent_mode == zima::document::ProfileExtentMode::Symmetric
                ? "symmetric" : "one_side"));
        auto* extent_row = new QWidget(this);
        auto* extent_layout = new QHBoxLayout(extent_row);
        extent_layout->setContentsMargins(0, 0, 0, 0);
        extent_switch_button_ = new QPushButton(tr("Přepnout"), this);
        extent_layout->addWidget(extent_mode_, 1);
        extent_layout->addWidget(extent_switch_button_);
        form->addRow(tr("Rozsah"), extent_row);
        connect(extent_switch_button_, &QPushButton::clicked, this,
            [extent = extent_mode_, cycle_combo] { cycle_combo(extent); });
        extrusion_direction_ = new QComboBox(this);
        extrusion_direction_->setObjectName("extrusionDirection");
        extrusion_direction_->addItem(revolve ? QStringLiteral("↻") : QStringLiteral("↑"),
                                      "forward");
        extrusion_direction_->addItem(revolve ? QStringLiteral("↺") : QStringLiteral("↓"),
                                      "reverse");
        const char* direction = direction_mode ==
                zima::document::ExtrusionDirection::Reverse ? "reverse" : "forward";
        extrusion_direction_->setCurrentIndex(
            extrusion_direction_->findData(direction));
        auto* direction_row = new QWidget(this);
        auto* direction_layout = new QHBoxLayout(direction_row);
        direction_layout->setContentsMargins(0, 0, 0, 0);
        direction_flip_button_ = new QPushButton(tr("Obrátit"), this);
        direction_layout->addWidget(extrusion_direction_, 1);
        direction_layout->addWidget(direction_flip_button_);
        form->addRow(tr("Směr"), direction_row);
        connect(direction_flip_button_, &QPushButton::clicked, this,
            [this] {
                extrusion_direction_->setCurrentIndex(
                    extrusion_direction_->currentData() == "forward" ? 1 : 0);
                if (extent_mode_->currentData() == "two_sides") {
                    const double forward = forward_length_->value();
                    forward_length_->setValue(reverse_length_->value());
                    reverse_length_->setValue(forward);
                }
            });
        forward_length_ = dimension(revolve ? initial.revolution.angle_degrees
                                            : initial.extrusion.length_forward,
                                    revolve ? "revolutionAngle" : "extrusionHeight");
        reverse_length_ = dimension(revolve ? initial.revolution.angle_reverse
                                            : initial.extrusion.length_reverse,
                                    revolve ? "revolutionReverseAngle"
                                            : "extrusionReverseLength");
        if (revolve) {
            for (auto* spin : {forward_length_, reverse_length_}) {
                spin->setRange(0.001, 360.0);
                spin->setSuffix(QStringLiteral("°"));
            }
            auto* axis_hint = new QLabel(
                tr("Zelená konstrukční osa ve skici"), this);
            axis_hint->setObjectName("revolutionAxisHint");
            axis_hint->setWordWrap(true);
            form->addRow(tr("Osa rotace"), axis_hint);
        }
        const auto end_condition = [this](zima::document::EndCondition selected,
                                          const char* name) {
            auto* combo = new QComboBox(this);
            combo->setObjectName(name);
            combo->addItem(tr("Na délku"), "length");
            combo->addItem(tr("Až k…"), "up_to");
            combo->addItem(tr("Skrz vše"), "through_all");
            combo->setCurrentIndex(combo->findData(
                selected == zima::document::EndCondition::UpTo ? "up_to"
                : selected == zima::document::EndCondition::ThroughAll
                    ? "through_all" : "length"));
            return combo;
        };
        const auto end_row = [this, &end_condition](const char* side, QComboBox*& combo,
                QDoubleSpinBox* length, QLineEdit*& target, QPushButton*& collection,
                zima::document::EndCondition condition,
                const std::vector<zima::document::ExtrusionParameters::EndTarget>& targets) {
            auto* row = new QWidget(this);
            auto* layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            combo = end_condition(condition,
                side == std::string_view("forward")
                    ? "extrusionForwardEndCondition" : "extrusionReverseEndCondition");
            target = new QLineEdit(this);
            target->setReadOnly(true);
            target->setPlaceholderText(tr("Vyberte bod, rovinu nebo rovinnou plochu…"));
            if (!targets.empty()) target->setText(QString::fromStdString(targets.front().label));
            collection = new QPushButton(QStringLiteral("…"), this);
            layout->addWidget(combo, 1);
            layout->addWidget(length, 1);
            layout->addWidget(target, 1);
            layout->addWidget(collection);
            const auto refresh = [combo, length, target, collection] {
                const auto value = combo->currentData().toString();
                length->setVisible(value == "length");
                target->setVisible(value == "up_to");
                collection->setVisible(value == "up_to");
            };
            connect(combo, &QComboBox::currentIndexChanged, this,
                [this, refresh](int) { refresh(); notify_preview(); });
            connect(target, &QLineEdit::selectionChanged, this, [this, side] {
                active_end_target_side_ = side;
                if (extrusion_target_request_) extrusion_target_request_();
            });
            connect(collection, &QPushButton::clicked, this, [this, side] {
                active_end_target_side_ = side;
                if (extrusion_target_request_) extrusion_target_request_();
            });
            refresh();
            return row;
        };
        if (!revolve) {
            auto* forward_row = end_row("forward", forward_end_condition_,
                forward_length_, forward_end_target_, forward_end_targets_button_,
                initial.extrusion.end_condition_forward,
                initial.extrusion.end_targets_forward);
            reverse_end_row_ = end_row("reverse", reverse_end_condition_,
                reverse_length_, reverse_end_target_, reverse_end_targets_button_,
                initial.extrusion.end_condition_reverse,
                initial.extrusion.end_targets_reverse);
            form->addRow(tr("Zakončení"), forward_row);
            form->addRow(tr("Zpětné zakončení"), reverse_end_row_);
        } else {
            form->addRow(tr("Úhel"), forward_length_);
            reverse_end_row_ = reverse_length_;
            form->addRow(tr("Zpětný úhel"), reverse_length_);
        }
        own_sketch_button_ = new QPushButton(QStringLiteral("SKETCH"), this);
        own_sketch_button_->setObjectName("primitiveOwnSketchButton");
        own_sketch_button_->setMinimumHeight(40);
        own_sketch_button_->setStyleSheet(
            "QPushButton{background:#4DD811;color:#102027;font-weight:700;"
            "padding:9px 18px;border-radius:4px}"
            "QPushButton:hover{background:#65ec2c}");
        form->addRow(own_sketch_button_);
        connect(own_sketch_button_, &QPushButton::clicked, this, [this] {
            auto pending = values();
            accept();
            if (edit_sketch_) edit_sketch_(std::move(pending));
        });
        connect(result_type_, &QComboBox::currentIndexChanged, this, [this](int) {
            const bool thin = result_type_->currentData() == "thin";
            thin_thickness_->setVisible(thin);
            thin_mode_->setVisible(thin);
            notify_preview();
        });
        const auto refresh_extent = [this] {
            const auto mode = extent_mode_->currentData().toString();
            const bool reverse = mode == "two_sides";
            reverse_end_row_->setEnabled(reverse);
            reverse_end_row_->setVisible(true);
            if (mode == "symmetric") {
                reverse_length_->setValue(forward_length_->value());
            }
            notify_preview();
        };
        if (revolve) {
            revolution_previous_extent_index_ = extent_mode_->currentIndex();
            revolution_extent_values_[revolution_previous_extent_index_] = {
                forward_length_->value(), reverse_length_->value()};
            connect(extent_mode_, &QComboBox::currentIndexChanged, this,
                [this, refresh_extent](int index) {
                    revolution_extent_values_[revolution_previous_extent_index_] = {
                        forward_length_->value(), reverse_length_->value()};
                    const auto values = revolution_extent_values_[index];
                    forward_length_->setValue(values[0]);
                    reverse_length_->setValue(values[1]);
                    revolution_previous_extent_index_ = index;
                    refresh_extent();
                });
        } else {
            connect(extent_mode_, &QComboBox::currentIndexChanged, this,
                [refresh_extent](int) { refresh_extent(); });
        }
        connect(forward_length_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                if (extent_mode_->currentData() == "symmetric") {
                    reverse_length_->setValue(value);
                }
                notify_preview();
            });
        connect(reverse_length_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this] { notify_preview(); });
        connect(extrusion_direction_, &QComboBox::currentIndexChanged,
            this, [this] { notify_preview(); });
        if (operation_ != nullptr) {
            connect(operation_, &QComboBox::currentIndexChanged,
                this, [this] { notify_preview(); });
        }
        const bool initial_thin = result_type_->currentData() == "thin";
        thin_thickness_->setVisible(initial_thin);
        thin_mode_->setVisible(initial_thin);
        refresh_extent();
    } else if (initial.feature_kind == zima::document::FeatureKind::ImportedStep) {
        auto* source = new QLabel(
            QString::fromStdString(initial.imported_step.source_path), this);
        source->setTextInteractionFlags(Qt::TextSelectableByMouse);
        source->setWordWrap(true);
        form->addRow(tr("Zdrojový soubor"), source);
    } else {
        treatment_size_ = dimension(initial.edge_treatment.size, "edgeTreatmentSize");
        form->addRow(initial.feature_kind == zima::document::FeatureKind::Fillet
            ? tr("Poloměr") : tr("Vzdálenost"), treatment_size_);
        auto* edges_label = new QLabel(tr("Vybrané hrany"), this);
        auto label_font = edges_label->font(); label_font.setBold(true);
        edges_label->setFont(label_font);
        form->addRow(edges_label);
        edge_list_ = new QTreeWidget(this);
        edge_list_->setObjectName("edgeTreatmentEdges");
        edge_list_->setColumnCount(2);
        edge_list_->setHeaderLabels({tr("Trasa"), tr("Objekty")});
        edge_list_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        edge_list_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        edge_list_->setMinimumHeight(90);
        form->addRow(edge_list_);
        remove_edge_button_ = new QPushButton(tr("Odebrat vybranou hranu"), this);
        restore_route_button_ = new QPushButton(tr("Obnovit celou trasu"), this);
        form->addRow(remove_edge_button_); form->addRow(restore_route_button_);
        connect(remove_edge_button_, &QPushButton::clicked, this, [this] {
            const auto* item = edge_list_->currentItem();
            if (item == nullptr || !remove_edge_) return;
            const auto group = item->data(0, Qt::UserRole).toUInt();
            const auto member_data = item->data(0, Qt::UserRole + 1);
            remove_edge_(group, member_data.isValid()
                ? std::optional<std::size_t>{member_data.toUInt()} : std::nullopt);
        });
        connect(restore_route_button_, &QPushButton::clicked, this, [this] {
            const auto* item = edge_list_->currentItem();
            if (item != nullptr && restore_route_)
                restore_route_(item->data(0, Qt::UserRole).toUInt());
        });
        set_edge_groups([&] {
            std::vector<EdgeGroup> groups;
            for (const auto& edge : initial.edge_treatment.edges) groups.push_back({edge});
            return groups;
        }());
    }

    content_layout()->addLayout(form);

    // Shared bottom operation row, matching Python's _build_operation_form
    // for primitives, Extrusion and Revolution. The hidden combo remains an
    // internal value adapter only; it is no longer part of the visible UI.
    if (operation_ != nullptr) {
        auto* operation_row = new QWidget(this);
        auto* operation_layout = new QHBoxLayout(operation_row);
        operation_layout->setContentsMargins(0, 0, 0, 0);
        operation_layout->setSpacing(8);
        add_operation_button_ = new QPushButton(tr("Přičíst"), this);
        subtract_operation_button_ = new QPushButton(tr("Odečíst"), this);
        for (auto* button : {add_operation_button_, subtract_operation_button_}) {
            button->setCheckable(true);
            button->setMinimumHeight(40);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        }
        add_operation_button_->setObjectName("primitiveAddOperation");
        subtract_operation_button_->setObjectName("primitiveSubtractOperation");
        add_operation_button_->setStyleSheet(
            "QPushButton{border:2px solid #2d5670;border-radius:6px;font-weight:700;"
            "padding:7px 14px} QPushButton:checked{background:#00d1ff;color:#101510;"
            "border-color:#6fe3ff}");
        subtract_operation_button_->setStyleSheet(
            "QPushButton{border:2px solid #713d3d;border-radius:6px;font-weight:700;"
            "padding:7px 14px} QPushButton:checked{background:#c64b4b;color:#ffffff;"
            "border-color:#ed7777}");
        const bool subtract = operation_->currentData() == "subtract";
        add_operation_button_->setChecked(!subtract);
        subtract_operation_button_->setChecked(subtract);
        operation_layout->addWidget(add_operation_button_);
        operation_layout->addWidget(subtract_operation_button_);
        auto* operation_form = new QFormLayout;
        operation_form->addRow(tr("Operace"), operation_row);
        content_layout()->addLayout(operation_form);
        const auto select_operation = [this](bool subtract_selected) {
            operation_->setCurrentIndex(operation_->findData(
                subtract_selected ? "subtract" : "add"));
            add_operation_button_->setChecked(!subtract_selected);
            subtract_operation_button_->setChecked(subtract_selected);
            notify_preview();
        };
        connect(add_operation_button_, &QPushButton::clicked, this,
            [select_operation] { select_operation(false); });
        connect(subtract_operation_button_, &QPushButton::clicked, this,
            [select_operation] { select_operation(true); });
        if (assembly_cut_mode) {
            add_operation_button_->setEnabled(false);
            subtract_operation_button_->setEnabled(false);
        }
    }

    if (assembly_cut_mode) {
        assembly_targets_ = new QListWidget(this);
        assembly_targets_->setObjectName("assemblyCutTargets");
        assembly_targets_->setMinimumHeight(100);
        for (const auto& [id, label] : assembly_targets) {
            auto* item = new QListWidgetItem(QString::fromStdString(label),
                assembly_targets_);
            item->setData(Qt::UserRole, QString::fromStdString(id));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(std::find(selected_targets.begin(),
                selected_targets.end(), id) != selected_targets.end()
                    ? Qt::Checked : Qt::Unchecked);
        }
        content_layout()->addWidget(new QLabel(
            tr("Cílové komponenty sestavy"), this));
        content_layout()->addWidget(assembly_targets_);
    }

    error_ = new QLabel(this);
    error_->setStyleSheet("color: #c64b4b;");
    error_->setWordWrap(true);
    content_layout()->addWidget(error_);
    connect(name_, &QLineEdit::textChanged, this, [this](const QString&) {
        error_->clear();
    });
    // Basic-solid dimensions drive the analytical wire preview immediately.
    // The OCCT body is still calculated only when OK commits the dialog.
    for (auto* input : {length_, width_, height_, radius_, top_radius_, top_offset_}) {
        if (input != nullptr) connect(input, &QDoubleSpinBox::valueChanged,
            this, [this] { notify_preview(); });
    }
    for (auto* input : translation_) {
        if (input != nullptr) connect(input, &QDoubleSpinBox::valueChanged,
            this, [this] { notify_preview(); });
    }
    for (auto* input : rotation_) {
        if (input != nullptr) connect(input, &QDoubleSpinBox::valueChanged,
            this, [this] { notify_preview(); });
    }
    // Compare edits against the values represented by the fully constructed
    // controls, rather than against the raw persisted object. Some widgets
    // normalize equivalent values while loading (for example a placement or
    // extent combo). Pressing OK without a user-visible change must therefore
    // remain a no-op and must not create an Undo revision.
    accepted_baseline_ = values();
}

zima::document::HistoryContainer PrimitivePropertiesDialog::values() const {
    const QString name = name_->text().trimmed();
    auto result = initial_;
    result.name = name.toStdString();
    if (operation_ != nullptr) {
        result.combine_mode = operation_->currentData().toString() == "subtract"
            ? zima::document::CombineMode::Subtract
            : zima::document::CombineMode::Add;
    }
    if (result.feature_kind == zima::document::FeatureKind::Box) {
        result.box = {length_->value(), width_->value(), height_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Cylinder) {
        result.cylinder = {radius_->value(), height_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Sphere) {
        result.sphere = {radius_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Cone) {
        result.cone = {radius_->value(), top_radius_->value(), height_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Pyramid) {
        result.pyramid = {length_->value(), width_->value(), height_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Wedge) {
        result.wedge = {length_->value(), width_->value(), height_->value(),
                        top_offset_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Extrusion) {
        result.extrusion.profile_plane_offset = profile_plane_offset_->value();
        result.extrusion.profile_source =
            zima::document::ProfileSource::Internal;
        result.extrusion.result_type = result_type_->currentData() == "thin"
            ? zima::document::ProfileResultType::Thin
            : zima::document::ProfileResultType::Solid;
        result.extrusion.thin_thickness = thin_thickness_->value();
        result.extrusion.thin_mode = thin_mode_->currentData() == "other_side"
            ? zima::document::ThinMode::OtherSide
            : thin_mode_->currentData() == "symmetric"
                ? zima::document::ThinMode::Symmetric
                : zima::document::ThinMode::OneSide;
        result.extrusion.extent_mode = extent_mode_->currentData() == "two_sides"
            ? zima::document::ProfileExtentMode::TwoSides
            : extent_mode_->currentData() == "symmetric"
                ? zima::document::ProfileExtentMode::Symmetric
                : zima::document::ProfileExtentMode::OneSide;
        result.extrusion.length_forward = forward_length_->value();
        result.extrusion.length_reverse = reverse_length_->value();
        const auto condition = [](const QComboBox* combo) {
            return combo->currentData() == "up_to"
                ? zima::document::EndCondition::UpTo
                : combo->currentData() == "through_all"
                    ? zima::document::EndCondition::ThroughAll
                    : zima::document::EndCondition::Length;
        };
        result.extrusion.end_condition_forward = condition(forward_end_condition_);
        result.extrusion.end_condition_reverse = condition(reverse_end_condition_);
        const QString direction =
            extrusion_direction_->currentData().toString();
        result.extrusion.direction = direction == "reverse"
            ? zima::document::ExtrusionDirection::Reverse
            : zima::document::ExtrusionDirection::Forward;
        result.extrusion.height = result.extrusion.extent_mode ==
                zima::document::ProfileExtentMode::OneSide
            ? result.extrusion.length_forward
            : result.extrusion.length_forward + result.extrusion.length_reverse;
    } else if (result.feature_kind == zima::document::FeatureKind::Revolution) {
        result.revolution.profile_plane_offset = profile_plane_offset_->value();
        result.revolution.profile_source =
            zima::document::ProfileSource::Internal;
        result.revolution.result_type = result_type_->currentData() == "thin"
            ? zima::document::ProfileResultType::Thin
            : zima::document::ProfileResultType::Solid;
        result.revolution.thin_thickness = thin_thickness_->value();
        result.revolution.thin_mode = thin_mode_->currentData() == "other_side"
            ? zima::document::ThinMode::OtherSide
            : thin_mode_->currentData() == "symmetric"
                ? zima::document::ThinMode::Symmetric
                : zima::document::ThinMode::OneSide;
        result.revolution.extent_mode = extent_mode_->currentData() == "two_sides"
            ? zima::document::ProfileExtentMode::TwoSides
            : extent_mode_->currentData() == "symmetric"
                ? zima::document::ProfileExtentMode::Symmetric
                : zima::document::ProfileExtentMode::OneSide;
        result.revolution.direction = extrusion_direction_->currentData() == "reverse"
            ? zima::document::ExtrusionDirection::Reverse
            : zima::document::ExtrusionDirection::Forward;
        result.revolution.angle_degrees = forward_length_->value();
        result.revolution.angle_reverse = reverse_length_->value();
    } else if (result.feature_kind != zima::document::FeatureKind::ImportedStep) {
        result.edge_treatment.size = treatment_size_->value();
    }
    if (result.feature_kind == zima::document::FeatureKind::Box ||
        result.feature_kind == zima::document::FeatureKind::Cylinder ||
        result.feature_kind == zima::document::FeatureKind::Sphere ||
        result.feature_kind == zima::document::FeatureKind::Cone ||
        result.feature_kind == zima::document::FeatureKind::Pyramid ||
        result.feature_kind == zima::document::FeatureKind::Wedge ||
        result.feature_kind == zima::document::FeatureKind::Extrusion ||
        result.feature_kind == zima::document::FeatureKind::Revolution ||
        result.feature_kind == zima::document::FeatureKind::ImportedStep) {
        result.placement = placement_->numeric_placement();
        const auto placement_references = placement_
            ? placement_->combined_references(3)
            : std::vector<zima::document::ConstructionReference>{};
        result.placement.references = placement_references;
    }
    return result;
}

void PrimitivePropertiesDialog::add_edge_reference(
    const zima::kernel::EdgeReference& edge) {
    if (!edge.valid()) return;
    auto& edges = initial_.edge_treatment.edges;
    if (std::find(edges.begin(), edges.end(), edge) == edges.end()) {
        edges.push_back(edge);
    }
    if (edge_list_ != nullptr) set_edge_groups([&] {
        std::vector<EdgeGroup> groups;
        for (const auto& selected : edges) groups.push_back({selected});
        return groups;
    }());
    notify_preview();
}

void PrimitivePropertiesDialog::set_edge_references(
    std::vector<zima::kernel::EdgeReference> edges) {
    initial_.edge_treatment.edges = std::move(edges);
    if (edge_list_ != nullptr && edge_groups_.empty()) set_edge_groups([&] {
        std::vector<EdgeGroup> groups;
        for (const auto& selected : initial_.edge_treatment.edges)
            groups.push_back({selected});
        return groups;
    }());
    notify_preview();
}

void PrimitivePropertiesDialog::set_edge_groups(std::vector<EdgeGroup> groups) {
    edge_groups_ = std::move(groups);
    initial_.edge_treatment.edges.clear();
    if (edge_list_ == nullptr) return;
    edge_list_->clear();
    for (std::size_t group_index = 0; group_index < edge_groups_.size(); ++group_index) {
        const auto& group = edge_groups_[group_index];
        if (group.empty()) continue;
        auto* route = new QTreeWidgetItem(edge_list_, {
            tr("Trasa %1").arg(group_index + 1),
            tr("%1 objektů").arg(group.size())});
        route->setData(0, Qt::UserRole, static_cast<uint>(group_index));
        for (std::size_t member = 0; member < group.size(); ++member) {
            const auto& edge = group[member];
            initial_.edge_treatment.edges.push_back(edge);
            auto* child = new QTreeWidgetItem(route, {QString{},
                QString::fromStdString(edge.owner_id + " / " + edge.semantic_key)});
            child->setData(0, Qt::UserRole, static_cast<uint>(group_index));
            child->setData(0, Qt::UserRole + 1, static_cast<uint>(member));
        }
        route->setExpanded(true);
    }
    const bool available = !initial_.edge_treatment.edges.empty();
    if (remove_edge_button_) remove_edge_button_->setEnabled(available);
    if (restore_route_button_) restore_route_button_->setEnabled(available);
    notify_preview();
}

void PrimitivePropertiesDialog::set_edge_group_callbacks(
    std::function<void(std::size_t, std::optional<std::size_t>)> remove,
    std::function<void(std::size_t)> restore) {
    remove_edge_ = std::move(remove);
    restore_route_ = std::move(restore);
}

void PrimitivePropertiesDialog::set_extrusion_target(
    zima::kernel::FaceReference reference, zima::kernel::Vec3 origin,
    zima::kernel::Vec3 normal) {
    zima::document::ExtrusionParameters::EndTarget target;
    target.kind = zima::document::EndTargetKind::Plane;
    target.reference = std::move(reference);
    target.label = target.reference.owner_id + " / " + target.reference.semantic_key;
    target.fallback_origin = origin;
    target.fallback_normal = normal;
    auto& targets = active_end_target_side_ == "reverse"
        ? initial_.extrusion.end_targets_reverse
        : initial_.extrusion.end_targets_forward;
    targets = {target};
    auto* edit = active_end_target_side_ == "reverse"
        ? reverse_end_target_ : forward_end_target_;
    if (edit != nullptr) edit->setText(QString::fromStdString(target.label));
    notify_preview();
}

void PrimitivePropertiesDialog::set_extrusion_surface_target(
    zima::kernel::FaceReference reference,
    std::vector<zima::kernel::Vec3> triangles) {
    zima::document::ExtrusionParameters::EndTarget target;
    target.kind = zima::document::EndTargetKind::Face;
    target.reference = std::move(reference);
    target.label = target.reference.owner_id + " / " + target.reference.semantic_key;
    target.fallback_triangles = std::move(triangles);
    auto& targets = active_end_target_side_ == "reverse"
        ? initial_.extrusion.end_targets_reverse
        : initial_.extrusion.end_targets_forward;
    targets = {target};
    auto* edit = active_end_target_side_ == "reverse"
        ? reverse_end_target_ : forward_end_target_;
    if (edit != nullptr) edit->setText(QString::fromStdString(target.label));
    notify_preview();
}

void PrimitivePropertiesDialog::set_extrusion_target_request(
    std::function<void()> callback) {
    extrusion_target_request_ = std::move(callback);
}

void PrimitivePropertiesDialog::set_profile_pick_request(
    std::function<void(bool)> callback) {
    profile_pick_request_ = std::move(callback);
}

void PrimitivePropertiesDialog::set_edit_sketch_callback(
    std::function<void(zima::document::HistoryContainer)> callback) {
    edit_sketch_ = std::move(callback);
}

void PrimitivePropertiesDialog::set_commit_required(bool required) {
    commit_required_ = required;
}

void PrimitivePropertiesDialog::set_preview_callback(
    std::function<void(const zima::document::HistoryContainer&)> callback) {
    preview_ = std::move(callback);
    notify_preview();
}

double PrimitivePropertiesDialog::profile_plane_offset() const {
    return profile_plane_offset_ == nullptr ? 0.0 : profile_plane_offset_->value();
}

double PrimitivePropertiesDialog::forward_extent_length() const {
    return forward_length_ == nullptr ? 0.0 : forward_length_->value();
}

bool PrimitivePropertiesDialog::extrusion_direction_reversed() const {
    return extrusion_direction_ != nullptr &&
        extrusion_direction_->currentData() == "reverse";
}

void PrimitivePropertiesDialog::set_profile_offset_and_forward_length(
    double offset, double length) {
    if (profile_plane_offset_ == nullptr || forward_length_ == nullptr) return;
    const QSignalBlocker offset_blocker(profile_plane_offset_);
    const QSignalBlocker length_blocker(forward_length_);
    profile_plane_offset_->setValue(offset);
    forward_length_->setValue(std::max(0.001, length));
    notify_preview();
}

void PrimitivePropertiesDialog::set_forward_extent_length(double length) {
    if (forward_length_ == nullptr) return;
    forward_length_->setValue(std::max(0.001, length));
}

void PrimitivePropertiesDialog::notify_preview() {
    if (preview_) preview_(values());
}

bool PrimitivePropertiesDialog::submit() {
    if (name_->text().trimmed().isEmpty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        name_->setFocus();
        return false;
    }
    auto result = values();
    std::vector<std::string> selected_targets;
    if (assembly_targets_ != nullptr) {
        for (int row = 0; row < assembly_targets_->count(); ++row) {
            const auto* item = assembly_targets_->item(row);
            if (item->checkState() == Qt::Checked) {
                selected_targets.push_back(
                    item->data(Qt::UserRole).toString().toStdString());
            }
        }
        if (selected_targets.empty()) {
            error_->setText(tr("Vyberte alespoň jednu cílovou komponentu sestavy."));
            return false;
        }
        result.combine_mode = zima::document::CombineMode::Subtract;
    }
    if (result.feature_kind == zima::document::FeatureKind::Extrusion) {
        const auto missing_target = [](zima::document::EndCondition condition,
                                       const auto& targets) {
            return condition == zima::document::EndCondition::UpTo &&
                (targets.empty() || !targets.front().reference.valid());
        };
        if (missing_target(result.extrusion.end_condition_forward,
                           result.extrusion.end_targets_forward) ||
            (result.extrusion.extent_mode ==
                 zima::document::ProfileExtentMode::TwoSides &&
             missing_target(result.extrusion.end_condition_reverse,
                            result.extrusion.end_targets_reverse))) {
            error_->setText(tr("Vyberte cílový bod, rovinu nebo rovinnou plochu."));
            return false;
        }
        if (result.combine_mode != zima::document::CombineMode::Subtract &&
            (result.extrusion.end_condition_forward ==
                 zima::document::EndCondition::ThroughAll ||
             (result.extrusion.extent_mode ==
                  zima::document::ProfileExtentMode::TwoSides &&
              result.extrusion.end_condition_reverse ==
                  zima::document::EndCondition::ThroughAll))) {
            error_->setText(tr("Skrz vše je dostupné pouze pro řez."));
            return false;
        }
    }
    if (!commit_required_ && edit_mode_ && accepted_baseline_ &&
        result == *accepted_baseline_ &&
        selected_targets == accepted_target_baseline_) {
        return true;
    }
    try {
        commit_(std::move(result), std::move(selected_targets));
    } catch (const std::exception& failure) {
        error_->setText(QString::fromUtf8(failure.what()));
        return false;
    }
    return true;
}

void PrimitivePropertiesDialog::set_reference_request_callback(
    ReferenceRequestCallback callback) {
    reference_request_ = std::move(callback);
}

void PrimitivePropertiesDialog::set_reference_highlights_changed_callback(
    ReferenceHighlightsChangedCallback callback) {
    reference_highlights_changed_ = std::move(callback);
}

std::set<std::string>
PrimitivePropertiesDialog::highlighted_reference_owner_ids() const {
    return placement_ ? placement_->highlighted_reference_owner_ids()
                       : std::set<std::string>{};
}

std::vector<zima::document::ConstructionReference>
PrimitivePropertiesDialog::highlighted_reference_entries() const {
    return placement_ ? placement_->highlighted_reference_entries()
                       : std::vector<zima::document::ConstructionReference>{};
}

bool PrimitivePropertiesDialog::set_reference(std::size_t index,
    zima::document::ConstructionReference reference, const QString& label) {
    if (!placement_) return false;
    QString error;
    if (!placement_->set_reference(index, std::move(reference), label, &error)) {
        if (!error.isEmpty()) error_->setText(error);
        return false;
    }
    error_->clear();
    return true;
}

const std::string& PrimitivePropertiesDialog::container_id() const {
    return initial_.id;
}

bool PrimitivePropertiesDialog::owns_reference_owner(
    const std::string& owner_id) const {
    return owner_id == initial_.id || owner_id == initial_.container_origin.id;
}

std::vector<zima::document::ConstructionReference>
PrimitivePropertiesDialog::references_without(std::size_t index) const {
    return placement_ ? placement_->references_without(index)
                       : std::vector<zima::document::ConstructionReference>{};
}

std::size_t PrimitivePropertiesDialog::first_empty_position_index() const {
    return placement_ ? placement_->first_empty_position_index() : 3;
}

void PrimitivePropertiesDialog::set_remaining_translation_dof(int dof) {
    if (!placement_) return;
    placement_->set_remaining_translation_dof(dof);
    remaining_translation_dof_ = placement_->remaining_translation_dof();
}

void PrimitivePropertiesDialog::set_remaining_rotation_dof(int dof) {
    if (!placement_) return;
    placement_->set_remaining_rotation_dof(dof);
    remaining_rotation_dof_ = placement_->remaining_rotation_dof();
}

void PrimitivePropertiesDialog::set_translation_constraint_state(
    const zima::document::PointConstraintState& state,
    const zima::kernel::Vec3& solution) {
    set_remaining_translation_dof(state.remaining_dof);
    placement_->set_translation_constraint_state(state, solution);
}

void PrimitivePropertiesDialog::set_orientation_base_rotation(
    const zima::kernel::Vec3& rotation, bool constrained) {
    if (!placement_) return;
    placement_->set_orientation_base_rotation(rotation, constrained);
}

}  // namespace zima::app
