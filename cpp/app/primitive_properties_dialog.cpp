#include "primitive_properties_dialog.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>

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

QString primitive_label(zima::document::FeatureKind kind) {
    return kind == zima::document::FeatureKind::Cylinder ? QObject::tr("válce")
        : kind == zima::document::FeatureKind::Sphere ? QObject::tr("koule")
        : kind == zima::document::FeatureKind::Cone ? QObject::tr("kužele")
        : kind == zima::document::FeatureKind::Pyramid ? QObject::tr("jehlanu")
        : kind == zima::document::FeatureKind::Wedge ? QObject::tr("klínu")
        : kind == zima::document::FeatureKind::Extrusion
            ? QObject::tr("vytažení")
        : kind == zima::document::FeatureKind::Revolution
            ? QObject::tr("rotace")
        : kind == zima::document::FeatureKind::Fillet
            ? QObject::tr("zaoblení")
        : kind == zima::document::FeatureKind::Chamfer
            ? QObject::tr("sražení")
        : kind == zima::document::FeatureKind::ImportedStep
            ? QObject::tr("importovaného STEP") : QObject::tr("kvádru");
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
    : PropertiesSubWindow(
          edit_mode
              ? tr("Vlastnosti %1").arg(primitive_label(initial.feature_kind))
              : initial.feature_kind == zima::document::FeatureKind::Cylinder
                  ? tr("Nový válec")
                  : initial.feature_kind == zima::document::FeatureKind::Sphere
                      ? tr("Nová koule")
                  : initial.feature_kind == zima::document::FeatureKind::Cone
                      ? tr("Nový kužel")
                  : initial.feature_kind == zima::document::FeatureKind::Pyramid
                      ? tr("Nový jehlan")
                  : initial.feature_kind == zima::document::FeatureKind::Wedge
                      ? tr("Nový klín")
                  : initial.feature_kind == zima::document::FeatureKind::Extrusion
                      ? tr("Nové vytažení")
                  : initial.feature_kind == zima::document::FeatureKind::Revolution
                      ? tr("Nová rotace")
                  : initial.feature_kind == zima::document::FeatureKind::Fillet
                      ? tr("Nové zaoblení")
                  : initial.feature_kind == zima::document::FeatureKind::Chamfer
                      ? tr("Nové sražení")
                  : initial.feature_kind == zima::document::FeatureKind::ImportedStep
                      ? tr("Import STEP") : tr("Nový kvádr"),
          parent),
      initial_(initial), commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(340);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(initial.name), this);
    form->addRow(tr("Název"), name_);
    const bool treatment = initial.feature_kind == zima::document::FeatureKind::Fillet ||
        initial.feature_kind == zima::document::FeatureKind::Chamfer;
    if (!treatment) {
        operation_ = new QComboBox(this);
        if (!assembly_cut_mode) operation_->addItem(tr("Přičíst"), "add");
        if (allow_subtract || assembly_cut_mode) {
            operation_->addItem(tr("Odečíst"), "subtract");
        }
        if (initial.combine_mode == zima::document::CombineMode::Subtract) {
            operation_->setCurrentIndex(operation_->findData("subtract"));
        }
        form->addRow(tr("Operace"), operation_);
        if (assembly_cut_mode) operation_->setEnabled(false);
    }

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
            revolution_axis_ = new QComboBox(this);
            revolution_axis_->setObjectName("revolutionAxis");
            revolution_axis_->addItem(tr("Osa X skici"), "sketch_x");
            revolution_axis_->addItem(tr("Osa Y skici"), "sketch_y");
            revolution_axis_->setCurrentIndex(revolution_axis_->findData(
                initial.revolution.axis == zima::document::RevolutionAxis::SketchX
                    ? "sketch_x" : "sketch_y"));
            form->addRow(tr("Osa"), revolution_axis_);
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
        if (operation_ != nullptr) {
            operation_->hide();
            if (auto* label = form->labelForField(operation_)) label->hide();
            auto* operation_row = new QWidget(this);
            auto* operation_layout = new QHBoxLayout(operation_row);
            operation_layout->setContentsMargins(0, 0, 0, 0);
            add_operation_button_ = new QPushButton(tr("Přičíst"), this);
            subtract_operation_button_ = new QPushButton(tr("Odečíst"), this);
            for (auto* button : {add_operation_button_, subtract_operation_button_}) {
                button->setCheckable(true);
                button->setMinimumHeight(40);
            }
            add_operation_button_->setStyleSheet(
                "QPushButton{border:2px solid #54703a;border-radius:6px;font-weight:700;"
                "padding:7px 14px} QPushButton:checked{background:#80AA1A;color:#101510;"
                "border-color:#a7d52b}");
            subtract_operation_button_->setStyleSheet(
                "QPushButton{border:2px solid #713d3d;border-radius:6px;font-weight:700;"
                "padding:7px 14px} QPushButton:checked{background:#c64b4b;color:white;"
                "border-color:#ed7777}");
            const bool subtract = operation_->currentData() == "subtract";
            add_operation_button_->setChecked(!subtract);
            subtract_operation_button_->setChecked(subtract);
            operation_layout->addWidget(add_operation_button_, 1);
            operation_layout->addWidget(subtract_operation_button_, 1);
            form->addRow(tr("Operace"), operation_row);
            const auto select_operation = [this](bool subtract_selected) {
                operation_->setCurrentIndex(operation_->findData(
                    subtract_selected ? "subtract" : "add"));
                add_operation_button_->setChecked(!subtract_selected);
                subtract_operation_button_->setChecked(subtract_selected);
            };
            connect(add_operation_button_, &QPushButton::clicked, this,
                [select_operation] { select_operation(false); });
            connect(subtract_operation_button_, &QPushButton::clicked, this,
                [select_operation] { select_operation(true); });
        }
        own_sketch_button_ = new QPushButton(QStringLiteral("SKETCH"), this);
        own_sketch_button_->setMinimumHeight(40);
        own_sketch_button_->setStyleSheet(
            "QPushButton{background:#4DD811;color:#102027;font-weight:700;"
            "padding:9px 18px;border-radius:4px}"
            "QPushButton:hover{background:#65ec2c}");
        form->addRow(own_sketch_button_);
        connect(own_sketch_button_, &QPushButton::clicked, this, [this] {
            const std::string sketch_id = initial_.feature_kind ==
                    zima::document::FeatureKind::Extrusion
                ? initial_.extrusion.sketch_id : initial_.revolution.sketch_id;
            if (submit()) {
                accept();
                if (edit_sketch_) edit_sketch_(sketch_id);
            }
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
        QStringList references;
        for (const auto& edge : initial.edge_treatment.edges) {
            references.push_back(QString::fromStdString(
                edge.owner_id + " / " + edge.semantic_key));
        }
        auto* edge_list = new QLabel(references.join("\n"), this);
        edge_list->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(tr("Hrany"), edge_list);
        treatment_size_ = dimension(initial.edge_treatment.size, "edgeTreatmentSize");
        form->addRow(initial.feature_kind == zima::document::FeatureKind::Fillet
            ? tr("Poloměr") : tr("Vzdálenost"), treatment_size_);
    }

    const auto placement = [this](double value, bool angular) {
        auto* field = new QDoubleSpinBox(this);
        field->setRange(angular ? -360.0 : -1'000'000.0,
                        angular ? 360.0 : 1'000'000.0);
        field->setDecimals(3);
        field->setSingleStep(1.0);
        field->setSuffix(angular ? "°" : " mm");
        field->setValue(value);
        field->setObjectName(angular ? "primitiveRotation" : "primitiveTranslation");
        return field;
    };
    if (initial.feature_kind == zima::document::FeatureKind::Box ||
        initial.feature_kind == zima::document::FeatureKind::Cylinder ||
        initial.feature_kind == zima::document::FeatureKind::Sphere ||
        initial.feature_kind == zima::document::FeatureKind::Cone ||
        initial.feature_kind == zima::document::FeatureKind::Pyramid ||
        initial.feature_kind == zima::document::FeatureKind::Wedge ||
        initial.feature_kind == zima::document::FeatureKind::ImportedStep) {
        translation_ = {
            placement(initial.placement.x, false),
            placement(initial.placement.y, false),
            placement(initial.placement.z, false),
        };
        rotation_ = {
            placement(initial.placement.rotation_x, true),
            placement(initial.placement.rotation_y, true),
            placement(initial.placement.rotation_z, true),
        };
        for (auto* input : rotation_) input->setRange(-360'000.0, 360'000.0);
        form->addRow(tr("Posunutí X"), translation_[0]);
        form->addRow(tr("Posunutí Y"), translation_[1]);
        form->addRow(tr("Posunutí Z"), translation_[2]);
        form->addRow(tr("Natočení X"), rotation_[0]);
        form->addRow(tr("Natočení Y"), rotation_[1]);
        form->addRow(tr("Natočení Z"), rotation_[2]);
    }
    content_layout()->addLayout(form);

    // Universal container placement: an origin resolved from a position
    // reference, an optional FRONT/TOP orientation frame, on top of which
    // the RX/RY/RZ fields above act as a manual correction. Currently wired
    // for Box as the first container kind to validate the shared contract;
    // the remaining primitive/feature kinds reuse the plain manual XYZ
    // fields above until they are switched over to this same table.
    if (initial.feature_kind == zima::document::FeatureKind::Box) {
        for (const auto& reference : initial.placement.references) {
            if (reference.orientation_drives_rotation) {
                orientation_references_.push_back(reference);
                orientation_labels_.push_back(
                    readable_placement_reference_kind(reference.semantic_key));
            } else {
                references_.push_back(reference);
                reference_labels_.push_back(
                    readable_placement_reference_kind(reference.semantic_key));
            }
        }
        reference_status_ = new QLabel(this);
        reference_status_->setStyleSheet("color:#80AA1A;font-weight:700;");
        reference_status_->setWordWrap(true);
        content_layout()->addWidget(reference_status_);
        auto* placement_heading = new QLabel(tr("Umístění kontejneru"), this);
        auto heading_font = placement_heading->font();
        heading_font.setBold(true);
        placement_heading->setFont(heading_font);
        content_layout()->addWidget(placement_heading);
        reference_table_ = new QTableWidget(0, 3, this);
        reference_table_->setObjectName("primitiveReferenceTable");
        reference_table_->setHorizontalHeaderLabels(
            {QString(), tr("Reference"), tr("Odsazení")});
        reference_table_->horizontalHeader()->setSectionResizeMode(
            0, QHeaderView::ResizeToContents);
        reference_table_->horizontalHeader()->setSectionResizeMode(
            1, QHeaderView::Stretch);
        reference_table_->horizontalHeader()->setSectionResizeMode(
            2, QHeaderView::ResizeToContents);
        reference_table_->verticalHeader()->setDefaultSectionSize(34);
        reference_table_->verticalHeader()->setMinimumSectionSize(34);
        reference_table_->setFixedHeight(
            reference_table_->horizontalHeader()->sizeHint().height() + 3 * 34 +
            reference_table_->frameWidth() * 2);
        reference_table_->setStyleSheet(
            "QTableWidget::item:selected{background:#00d1ff;color:#102027}");
        content_layout()->addWidget(reference_table_);
        auto* orientation_heading = new QLabel(tr("Orientace kontejneru (FRONT/TOP)"), this);
        auto orientation_heading_font = orientation_heading->font();
        orientation_heading_font.setBold(true);
        orientation_heading->setFont(orientation_heading_font);
        content_layout()->addWidget(orientation_heading);
        orientation_table_ = new QTableWidget(2, 3, this);
        orientation_table_->setObjectName("primitiveOrientationTable");
        orientation_table_->setHorizontalHeaderLabels(
            {QString(), tr("Reference"), tr("FRONT / TOP")});
        orientation_table_->horizontalHeader()->setSectionResizeMode(
            0, QHeaderView::ResizeToContents);
        orientation_table_->horizontalHeader()->setSectionResizeMode(
            1, QHeaderView::Stretch);
        orientation_table_->horizontalHeader()->setSectionResizeMode(
            2, QHeaderView::ResizeToContents);
        orientation_table_->verticalHeader()->setDefaultSectionSize(34);
        orientation_table_->setFixedHeight(
            orientation_table_->horizontalHeader()->sizeHint().height() + 2 * 34 +
            orientation_table_->frameWidth() * 2);
        content_layout()->addWidget(orientation_table_);
        dof_label_ = new QLabel(this);
        content_layout()->addWidget(dof_label_);
        refresh_reference_table();
        refresh_orientation_table();
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
    connect(name_, &QLineEdit::textChanged, this, [this](const QString& name) {
        set_internal_title(name.trimmed().isEmpty()
            ? tr("Vlastnosti %1").arg(primitive_label(initial_.feature_kind))
            : tr("Vlastnosti: %1").arg(name.trimmed()));
        error_->clear();
    });
    for (auto* input : translation_) {
        if (input != nullptr) connect(input, &QDoubleSpinBox::valueChanged,
            this, [this] { notify_preview(); });
    }
    for (auto* input : rotation_) {
        if (input != nullptr) connect(input, &QDoubleSpinBox::valueChanged,
            this, [this] { notify_preview(); });
    }
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
        result.extrusion.profile_source = profile_source_->text() ==
                tr("Vlastní kontejner")
            ? zima::document::ProfileSource::Internal
            : zima::document::ProfileSource::External;
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
        result.revolution.profile_source = profile_source_->text() ==
                tr("Vlastní kontejner")
            ? zima::document::ProfileSource::Internal
            : zima::document::ProfileSource::External;
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
        result.revolution.axis =
            revolution_axis_->currentData().toString() == "sketch_y"
                ? zima::document::RevolutionAxis::SketchY
                : zima::document::RevolutionAxis::SketchX;
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
        result.feature_kind == zima::document::FeatureKind::ImportedStep) {
        result.placement = {
            translation_[0]->value(), translation_[1]->value(), translation_[2]->value(),
            rotation_[0]->value(), rotation_[1]->value(), rotation_[2]->value(),
        };
        if (!references_.empty() || !orientation_references_.empty()) {
            // A position/orientation reference is present: the manual
            // RX/RY/RZ fields become the correction on top of the FRONT/TOP
            // frame (or a no-op passthrough without one), matching
            // resolve_placement()'s manual rotation_offset_* contract.
            result.placement.rotation_offset_x = rotation_[0]->value();
            result.placement.rotation_offset_y = rotation_[1]->value();
            result.placement.rotation_offset_z = rotation_[2]->value();
            result.placement.references = references_;
            result.placement.references.insert(result.placement.references.end(),
                orientation_references_.begin(), orientation_references_.end());
        }
    }
    return result;
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
    std::function<void(std::string)> callback) {
    edit_sketch_ = std::move(callback);
}

void PrimitivePropertiesDialog::set_preview_callback(
    std::function<void(const zima::document::HistoryContainer&)> callback) {
    preview_ = std::move(callback);
    notify_preview();
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

bool PrimitivePropertiesDialog::set_reference(std::size_t index,
    zima::document::ConstructionReference reference, const QString& label) {
    const auto duplicate = [&](const auto& existing) {
        return existing.instance_path == reference.instance_path &&
            existing.owner_id == reference.owner_id &&
            existing.semantic_key == reference.semantic_key;
    };
    if (index >= 3) {
        const auto orientation_index = index - 3;
        if (orientation_index >= 2) return false;
        if (std::any_of(references_.begin(), references_.end(), duplicate) ||
            std::any_of(orientation_references_.begin(),
                orientation_references_.end(), duplicate)) {
            error_->setText(tr("Stejnou referenci nelze zadat vícekrát."));
            return false;
        }
        reference.orientation_drives_rotation = true;
        reference.orientation_role = orientation_index == 0 ? "front" : "top";
        if (orientation_references_.size() <= orientation_index)
            orientation_references_.resize(orientation_index + 1);
        if (orientation_labels_.size() <= orientation_index)
            orientation_labels_.resize(orientation_index + 1);
        orientation_references_[orientation_index] = std::move(reference);
        orientation_labels_[orientation_index] = label;
        error_->clear();
        refresh_orientation_table();
        notify_preview();
        return true;
    }
    for (std::size_t existing_index = 0;
         existing_index < references_.size(); ++existing_index) {
        if (existing_index != index && duplicate(references_[existing_index])) {
            error_->setText(tr("Stejnou referenci nelze zadat vícekrát."));
            return false;
        }
    }
    error_->clear();
    if (references_.size() <= index) references_.resize(index + 1);
    if (reference_labels_.size() <= index) reference_labels_.resize(index + 1);
    references_[index] = std::move(reference);
    reference_labels_[index] = label.trimmed().isEmpty()
        ? readable_placement_reference_kind(references_[index].semantic_key) : label;
    refresh_reference_table();
    notify_preview();
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
    auto result = references_;
    if (index < 3 && index < result.size()) {
        result.erase(result.begin() + static_cast<std::ptrdiff_t>(index));
    }
    if (index >= 3) {
        const auto orientation_index = index - 3;
        if (orientation_index < orientation_references_.size()) {
            auto orientations = orientation_references_;
            orientations.erase(orientations.begin() +
                static_cast<std::ptrdiff_t>(orientation_index));
            result.insert(result.end(), orientations.begin(), orientations.end());
            return result;
        }
    }
    result.insert(result.end(), orientation_references_.begin(),
        orientation_references_.end());
    return result;
}

void PrimitivePropertiesDialog::set_remaining_translation_dof(int dof) {
    const int previous = remaining_translation_dof_;
    remaining_translation_dof_ = std::clamp(dof, 0, 3);
    if (dof_label_ != nullptr) {
        const int total_dof = remaining_translation_dof_ + remaining_rotation_dof_;
        dof_label_->setText(tr("Zbývající stupně volnosti: %1").arg(total_dof));
        if (reference_status_ != nullptr) {
            reference_status_->setText(total_dof == 0 ? tr("Plně určené") : QString());
        }
    }
    if (previous != remaining_translation_dof_ && reference_table_ != nullptr)
        refresh_reference_table();
}

void PrimitivePropertiesDialog::set_remaining_rotation_dof(int dof) {
    remaining_rotation_dof_ = std::clamp(dof, 0, 3);
    set_remaining_translation_dof(remaining_translation_dof_);
}

void PrimitivePropertiesDialog::set_translation_constraint_state(
    const zima::document::PointConstraintState& state,
    const zima::kernel::Vec3& solution) {
    set_remaining_translation_dof(state.remaining_dof);
    const std::array values{solution.x, solution.y, solution.z};
    for (std::size_t index = 0; index < translation_.size(); ++index) {
        if (translation_[index] == nullptr) continue;
        translation_[index]->setEnabled(!state.constrained_axes[index]);
        if (state.constrained_axes[index]) {
            const QSignalBlocker blocker(translation_[index]);
            translation_[index]->setValue(values[index]);
        }
    }
}

void PrimitivePropertiesDialog::refresh_reference_table() {
    if (reference_table_ == nullptr) return;
    reference_buttons_.clear();
    reference_table_->setRowCount(0);
    const std::size_t visible = std::min<std::size_t>(3, references_.size() +
        (remaining_translation_dof_ > 0 ? 1 : 0));
    reference_buttons_.resize(visible, nullptr);
    for (std::size_t index = 0; index < visible; ++index) {
        reference_table_->insertRow(static_cast<int>(index));
        if (index < references_.size()) {
            auto* remove = new QPushButton(QStringLiteral("×"), reference_table_);
            remove->setFixedSize(30, 30);
            remove->setToolTip(tr("Odstranit referenci"));
            remove->setStyleSheet(
                "QPushButton{color:white;background:#8b2424;border:1px solid #b94a4a;"
                "border-radius:4px;font-weight:700}"
                "QPushButton:hover{background:#b83232;border-color:#ed7777}");
            connect(remove, &QPushButton::clicked, this,
                [this, index] { remove_reference(index); });
            reference_table_->setCellWidget(static_cast<int>(index), 0, remove);
            auto* reference = new QPushButton(
                QStringLiteral("%1. %2").arg(index + 1).arg(
                    index < reference_labels_.size()
                        ? reference_labels_[index]
                        : readable_placement_reference_kind(
                            references_[index].semantic_key)),
                reference_table_);
            reference->setObjectName(
                QStringLiteral("primitiveReferenceButton%1").arg(index));
            reference->setToolTip(tr("Vybrat nebo nahradit referenci ve 3D pohledu"));
            connect(reference, &QPushButton::clicked, this, [this, index] {
                if (reference_request_) reference_request_(index);
            });
            reference_buttons_[index] = reference;
            reference_table_->setCellWidget(static_cast<int>(index), 1, reference);
            auto* offset = new QDoubleSpinBox(reference_table_);
            offset->setRange(-1'000'000'000.0, 1'000'000'000.0);
            offset->setDecimals(3);
            offset->setSuffix(QStringLiteral(" mm"));
            offset->setValue(references_[index].offset);
            offset->setEnabled(references_[index].supports_offset);
            connect(offset, &QDoubleSpinBox::valueChanged, this,
                [this, index](double value) {
                    if (index < references_.size()) {
                        references_[index].offset = value;
                        notify_preview();
                    }
                });
            reference_table_->setCellWidget(static_cast<int>(index), 2, offset);
        } else {
            auto* reference = new QPushButton(
                QStringLiteral("%1. %2").arg(index + 1).arg(tr("Vybrat referenci")),
                reference_table_);
            reference->setObjectName(
                QStringLiteral("primitiveReferenceButton%1").arg(index));
            reference->setToolTip(tr("Vybrat referenci ve 3D pohledu"));
            connect(reference, &QPushButton::clicked, this, [this, index] {
                if (reference_request_) reference_request_(index);
            });
            reference_buttons_[index] = reference;
            reference_table_->setCellWidget(static_cast<int>(index), 1, reference);
            auto* offset = new QDoubleSpinBox(reference_table_);
            offset->setEnabled(false);
            offset->setSuffix(QStringLiteral(" mm"));
            reference_table_->setCellWidget(static_cast<int>(index), 2, offset);
        }
    }
}

void PrimitivePropertiesDialog::refresh_orientation_table() {
    if (orientation_table_ == nullptr) return;
    for (std::size_t index = 0; index < 2; ++index) {
        if (index < orientation_references_.size() &&
            !orientation_references_[index].owner_id.empty()) {
            auto* remove = new QPushButton(QStringLiteral("×"), orientation_table_);
            remove->setFixedSize(30, 30);
            remove->setStyleSheet(
                "QPushButton{color:white;background:#8b2424;border:1px solid #b94a4a;"
                "border-radius:4px;font-weight:700}");
            connect(remove, &QPushButton::clicked, this, [this, index] {
                orientation_references_.erase(orientation_references_.begin() +
                    static_cast<std::ptrdiff_t>(index));
                if (index < orientation_labels_.size())
                    orientation_labels_.erase(orientation_labels_.begin() +
                        static_cast<std::ptrdiff_t>(index));
                for (std::size_t role = 0; role < orientation_references_.size(); ++role)
                    orientation_references_[role].orientation_role =
                        role == 0 ? "front" : "top";
                refresh_orientation_table();
                notify_preview();
            });
            orientation_table_->setCellWidget(static_cast<int>(index), 0, remove);
        } else {
            orientation_table_->setCellWidget(static_cast<int>(index), 0, nullptr);
        }
        auto* select = new QPushButton(
            index < orientation_labels_.size() && !orientation_labels_[index].isEmpty()
                ? orientation_labels_[index] : tr("Vybrat orientační referenci"),
            orientation_table_);
        select->setObjectName(
            QStringLiteral("primitiveOrientationReference%1").arg(index));
        connect(select, &QPushButton::clicked, this, [this, index] {
            if (reference_request_) reference_request_(index + 3);
        });
        orientation_table_->setCellWidget(static_cast<int>(index), 1, select);
        auto* role = new QComboBox(orientation_table_);
        role->addItem(QStringLiteral("FRONT"), QStringLiteral("front"));
        role->addItem(QStringLiteral("TOP"), QStringLiteral("top"));
        const auto stored_role = index < orientation_references_.size()
            ? QString::fromStdString(orientation_references_[index].orientation_role)
            : (index == 0 ? QStringLiteral("front") : QStringLiteral("top"));
        role->setCurrentIndex(std::max(0, role->findData(stored_role)));
        connect(role, &QComboBox::currentIndexChanged, this, [this, index, role] {
            if (index < orientation_references_.size()) {
                orientation_references_[index].orientation_role =
                    role->currentData().toString().toStdString();
                notify_preview();
            }
        });
        orientation_roles_[index] = role;
        orientation_table_->setCellWidget(static_cast<int>(index), 2, role);
    }
    set_remaining_translation_dof(remaining_translation_dof_);
}

void PrimitivePropertiesDialog::remove_reference(std::size_t index) {
    if (index >= references_.size()) return;
    references_.erase(references_.begin() + static_cast<std::ptrdiff_t>(index));
    if (index < reference_labels_.size()) {
        reference_labels_.erase(reference_labels_.begin() +
            static_cast<std::ptrdiff_t>(index));
    }
    refresh_reference_table();
    notify_preview();
}

}  // namespace zima::app

