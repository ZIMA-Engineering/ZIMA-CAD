#include "component_properties_dialog.hpp"

#include <zima/ui/reference_cell.hpp>

#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>

namespace zima::app {

namespace {

const char* mate_type_label(zima::assembly::MateKind kind) {
    switch (kind) {
    case zima::assembly::MateKind::PlaneCoincident: return "Plocha";
    case zima::assembly::MateKind::AxisCoincident: return "Osa";
    case zima::assembly::MateKind::PointCoincident: return "Bod";
    case zima::assembly::MateKind::AxisAngle: return "Úhel os";
    case zima::assembly::MateKind::PlaneAngle: return "Úhel ploch";
    }
    return "Plocha";
}

bool mate_type_is_angular(zima::assembly::MateKind kind) {
    return kind == zima::assembly::MateKind::AxisAngle ||
        kind == zima::assembly::MateKind::PlaneAngle;
}

// Small modal editor for a single placement-reference row's optional
// lower_limit/upper_limit, opened from the row's "Meze" button. Mirrors
// MatePropertiesDialog's limit fields/validation, but only the limit part --
// the nominal value itself stays editable in the main table's offset field.
// Validation (lower <= upper, current value inside range) is performed
// atomically on OK, matching MatePropertiesDialog::submit(); nothing is
// written back to `row` until the user confirms with valid values.
bool edit_placement_reference_limits(
    QWidget* parent, zima::assembly::ComponentPlacementReference& row) {
    const bool angular = mate_type_is_angular(row.mate_type);
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Meze reference umístění"));
    auto* layout = new QFormLayout(&dialog);
    auto* lower_enabled = new QCheckBox(&dialog);
    auto* lower_field = new QDoubleSpinBox(&dialog);
    auto* upper_enabled = new QCheckBox(&dialog);
    auto* upper_field = new QDoubleSpinBox(&dialog);
    for (auto* field : {lower_field, upper_field}) {
        field->setDecimals(3);
        field->setRange(angular ? -360.0 : -1'000'000'000.0,
            angular ? 360.0 : 1'000'000'000.0);
        field->setSuffix(angular ? QStringLiteral(" °") : QStringLiteral(" mm"));
    }
    lower_enabled->setChecked(row.lower_limit.has_value());
    lower_field->setValue(row.lower_limit.value_or(row.offset));
    lower_field->setEnabled(lower_enabled->isChecked());
    upper_enabled->setChecked(row.upper_limit.has_value());
    upper_field->setValue(row.upper_limit.value_or(row.offset));
    upper_field->setEnabled(upper_enabled->isChecked());
    QObject::connect(lower_enabled, &QCheckBox::toggled, lower_field,
        &QDoubleSpinBox::setEnabled);
    QObject::connect(upper_enabled, &QCheckBox::toggled, upper_field,
        &QDoubleSpinBox::setEnabled);
    auto* lower_row = new QWidget(&dialog);
    auto* lower_row_layout = new QHBoxLayout(lower_row);
    lower_row_layout->setContentsMargins(0, 0, 0, 0);
    lower_row_layout->addWidget(lower_enabled);
    lower_row_layout->addWidget(lower_field, 1);
    layout->addRow(QObject::tr("Dolní mez"), lower_row);
    auto* upper_row = new QWidget(&dialog);
    auto* upper_row_layout = new QHBoxLayout(upper_row);
    upper_row_layout->setContentsMargins(0, 0, 0, 0);
    upper_row_layout->addWidget(upper_enabled);
    upper_row_layout->addWidget(upper_field, 1);
    layout->addRow(QObject::tr("Horní mez"), upper_row);
    auto* error = new QLabel(&dialog);
    error->setStyleSheet("color: #c64b4b;");
    error->setWordWrap(true);
    layout->addRow(error);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const std::optional<double> lower = lower_enabled->isChecked()
            ? std::optional<double>{lower_field->value()} : std::nullopt;
        const std::optional<double> upper = upper_enabled->isChecked()
            ? std::optional<double>{upper_field->value()} : std::nullopt;
        if (lower && upper && *lower > *upper) {
            error->setText(QObject::tr("Dolní mez nesmí být větší než horní mez."));
            return;
        }
        if ((lower && row.offset < *lower) || (upper && row.offset > *upper)) {
            error->setText(QObject::tr("Jmenovitá hodnota musí ležet v zadaných mezích."));
            return;
        }
        row.lower_limit = lower;
        row.upper_limit = upper;
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}

}  // namespace

ComponentPropertiesDialog::ComponentPropertiesDialog(
    const zima::assembly::PartOccurrence& initial,
    CommitCallback commit,
    QWidget* parent)
    : PropertiesSubWindow(tr("Vlastnosti komponenty"), parent),
      initial_(initial), commit_(std::move(commit)),
      placement_references_(initial.placement_references) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(420);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(initial.name), this);
    form->addRow(tr("Název"), name_);
    auto* source = new QLineEdit(QString::fromStdString(initial.source_path.string()), this);
    source->setReadOnly(true);
    form->addRow(tr("Zdroj"), source);
    const auto placement = [this](double value, bool angular) {
        auto* field = new QDoubleSpinBox(this);
        field->setRange(angular ? -360.0 : -1'000'000.0,
                        angular ? 360.0 : 1'000'000.0);
        field->setDecimals(3);
        field->setSingleStep(1.0);
        field->setSuffix(angular ? "°" : " mm");
        field->setValue(value);
        field->setObjectName(
            angular ? "componentRotation" : "componentTranslation");
        return field;
    };
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
    form->addRow(tr("Posunutí X"), translation_[0]);
    form->addRow(tr("Posunutí Y"), translation_[1]);
    form->addRow(tr("Posunutí Z"), translation_[2]);
    form->addRow(tr("Natočení X"), rotation_[0]);
    form->addRow(tr("Natočení Y"), rotation_[1]);
    form->addRow(tr("Natočení Z"), rotation_[2]);
    content_layout()->addLayout(form);

    // Embedded placement reference table -- Python reference design
    // (AssemblyComponentPropertiesDialog): up to 3 rows entered directly in
    // THIS dialog rather than via separate Mate commands + a "Vazby" tree
    // branch. Columns: [indicator, component reference, target reference,
    // mate type, offset/angle, flip].
    auto* heading = new QLabel(tr("Vazby umístění"), this);
    auto heading_font = heading->font();
    heading_font.setBold(true);
    heading->setFont(heading_font);
    content_layout()->addWidget(heading);

    placement_table_ = new QTableWidget(0, 7, this);
    placement_table_->setObjectName("componentPlacementTable");
    placement_table_->setHorizontalHeaderLabels({QString(), tr("Tento díl"),
        tr("Cíl"), tr("Typ"), tr("Hodnota"), tr("Obrátit"), tr("Meze")});
    placement_table_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    placement_table_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    placement_table_->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::Stretch);
    placement_table_->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::ResizeToContents);
    placement_table_->horizontalHeader()->setSectionResizeMode(
        4, QHeaderView::ResizeToContents);
    placement_table_->horizontalHeader()->setSectionResizeMode(
        5, QHeaderView::ResizeToContents);
    placement_table_->horizontalHeader()->setSectionResizeMode(
        6, QHeaderView::ResizeToContents);
    placement_table_->verticalHeader()->setDefaultSectionSize(34);
    placement_table_->verticalHeader()->setMinimumSectionSize(34);
    placement_table_->setFixedHeight(
        placement_table_->horizontalHeader()->sizeHint().height() + 3 * 34 +
        placement_table_->frameWidth() * 2);
    placement_table_->setStyleSheet(
        "QTableWidget::item:selected{background:#00d1ff;color:#102027}");
    content_layout()->addWidget(placement_table_);
    connect(placement_table_, &QTableWidget::cellClicked, this,
        [this](int row, int column) {
            if (row < 0 || static_cast<std::size_t>(row) >= component_items_.size())
                return;
            if (column == 1 || column == 2) {
                const bool component_side = column == 1;
                auto* item = component_side
                    ? component_items_[static_cast<std::size_t>(row)]
                    : target_items_[static_cast<std::size_t>(row)];
                if (item == nullptr || item->has_reference()) return;
                if (reference_request_) {
                    reference_request_(static_cast<std::size_t>(row), component_side);
                }
            }
            placement_table_->clearSelection();
        });

    error_ = new QLabel(this);
    error_->setStyleSheet("color: #c64b4b;");
    error_->setWordWrap(true);
    content_layout()->addWidget(error_);
    connect(name_, &QLineEdit::textChanged, this, [this](const QString& name) {
        set_internal_title(name.trimmed().isEmpty()
            ? tr("Vlastnosti komponenty")
            : tr("Vlastnosti: %1").arg(name.trimmed()));
        error_->clear();
    });

    refresh_placement_table();
}

void ComponentPropertiesDialog::set_live_translation(double x, double y, double z) {
    translation_[0]->blockSignals(true);
    translation_[1]->blockSignals(true);
    translation_[2]->blockSignals(true);
    translation_[0]->setValue(x);
    translation_[1]->setValue(y);
    translation_[2]->setValue(z);
    translation_[0]->blockSignals(false);
    translation_[1]->blockSignals(false);
    translation_[2]->blockSignals(false);
}

void ComponentPropertiesDialog::set_reference_request_callback(
    ReferenceRequestCallback callback) {
    reference_request_ = std::move(callback);
}

void ComponentPropertiesDialog::set_placement_reference(
    std::size_t index, bool component_side,
    zima::assembly::MateReference reference, const QString& label) {
    if (index >= 3) return;
    if (placement_references_.size() <= index) {
        placement_references_.resize(index + 1);
    }
    auto& row = placement_references_[index];
    if (component_side) row.component_reference = std::move(reference);
    else row.target_reference = std::move(reference);
    refresh_placement_table();
    static_cast<void>(label);
}

void ComponentPropertiesDialog::remove_placement_reference(std::size_t index) {
    if (index >= placement_references_.size()) return;
    placement_references_.erase(placement_references_.begin() +
        static_cast<std::ptrdiff_t>(index));
    refresh_placement_table();
}

void ComponentPropertiesDialog::refresh_placement_table() {
    component_items_.fill(nullptr);
    target_items_.fill(nullptr);
    mate_type_combos_.fill(nullptr);
    offset_fields_.fill(nullptr);
    flip_buttons_.fill(nullptr);
    limit_buttons_.fill(nullptr);
    placement_table_->setRowCount(0);
    // Offer one more empty row than currently populated, capped at 3 rows,
    // matching Python's _retained_mate_rows(value[:3]) row cap.
    const std::size_t visible = std::min<std::size_t>(3,
        placement_references_.size() + 1);
    for (std::size_t index = 0; index < visible; ++index) {
        placement_table_->insertRow(static_cast<int>(index));
        const bool populated = index < placement_references_.size();
        auto* indicator = zima::ui::build_reference_row_indicator(
            [this, index] { remove_placement_reference(index); });
        placement_table_->setCellWidget(static_cast<int>(index), 0,
            zima::ui::centered_cell_widget(indicator));
        zima::ui::set_reference_row_populated(indicator, populated);

        const auto& row = populated
            ? placement_references_[index] : zima::assembly::ComponentPlacementReference{};

        auto* component_item = new zima::ui::ReferenceCellItem(
            tr("Vybrat referenci"));
        if (populated && !row.component_reference.semantic_key.empty()) {
            component_item->set_reference(
                QString::fromStdString(row.component_reference.semantic_key));
            component_item->setText(
                QString::fromStdString(row.component_reference.semantic_key));
            component_item->set_checked(true);
        } else {
            component_item->set_placeholder_style(palette().color(QPalette::Mid));
        }
        component_items_[index] = component_item;
        placement_table_->setItem(static_cast<int>(index), 1, component_item);

        auto* target_item = new zima::ui::ReferenceCellItem(tr("Vybrat referenci"));
        if (populated && !row.target_reference.semantic_key.empty()) {
            target_item->set_reference(
                QString::fromStdString(row.target_reference.semantic_key));
            target_item->setText(
                QString::fromStdString(row.target_reference.semantic_key));
            target_item->set_checked(true);
        } else {
            target_item->set_placeholder_style(palette().color(QPalette::Mid));
        }
        target_items_[index] = target_item;
        placement_table_->setItem(static_cast<int>(index), 2, target_item);

        auto* mate_type = new QComboBox(placement_table_);
        for (const auto kind : {zima::assembly::MateKind::PlaneCoincident,
                zima::assembly::MateKind::AxisCoincident,
                zima::assembly::MateKind::PointCoincident,
                zima::assembly::MateKind::AxisAngle,
                zima::assembly::MateKind::PlaneAngle}) {
            mate_type->addItem(tr(mate_type_label(kind)),
                static_cast<int>(kind));
        }
        mate_type->setCurrentIndex(mate_type->findData(
            static_cast<int>(row.mate_type)));
        mate_type->setEnabled(populated);
        mate_type_combos_[index] = mate_type;
        placement_table_->setCellWidget(static_cast<int>(index), 3, mate_type);
        connect(mate_type, &QComboBox::currentIndexChanged, this,
            [this, index, mate_type] {
                if (index >= placement_references_.size()) return;
                placement_references_[index].mate_type =
                    static_cast<zima::assembly::MateKind>(
                        mate_type->currentData().toInt());
            });

        auto* offset = new QDoubleSpinBox(placement_table_);
        const bool angular = mate_type_is_angular(row.mate_type);
        offset->setRange(angular ? -360.0 : -1'000'000'000.0,
            angular ? 360.0 : 1'000'000'000.0);
        offset->setDecimals(3);
        offset->setSuffix(angular ? QStringLiteral(" °") : QStringLiteral(" mm"));
        offset->setValue(row.offset);
        offset->setEnabled(populated);
        offset_fields_[index] = offset;
        placement_table_->setCellWidget(static_cast<int>(index), 4, offset);
        connect(offset, &QDoubleSpinBox::valueChanged, this,
            [this, index](double value) {
                if (index < placement_references_.size()) {
                    placement_references_[index].offset = value;
                }
            });

        // FLIP only has an effect on an orientation-driving row (Axis/Plane
        // coincident or angle -- all of them resolve a direction/normal);
        // PointCoincident carries the field but it is always a no-op there,
        // matching ConstructionReference::flip's semantics.
        const bool flip_enabled = populated;
        auto* flip_button = zima::ui::build_reference_row_flip_button(
            flip_enabled, row.flip,
            [this, index](bool value) {
                if (index < placement_references_.size()) {
                    placement_references_[index].flip = value;
                }
            });
        flip_buttons_[index] = flip_button;
        placement_table_->setCellWidget(static_cast<int>(index), 5,
            zima::ui::centered_cell_widget(flip_button));

        auto* limit_button = new QPushButton(tr("Meze…"), placement_table_);
        limit_button->setEnabled(populated);
        limit_buttons_[index] = limit_button;
        placement_table_->setCellWidget(static_cast<int>(index), 6,
            zima::ui::centered_cell_widget(limit_button));
        connect(limit_button, &QPushButton::clicked, this, [this, index] {
            if (index >= placement_references_.size()) return;
            if (edit_placement_reference_limits(this, placement_references_[index])) {
                refresh_placement_table();
            }
        });
    }
}

bool ComponentPropertiesDialog::submit() {
    const QString name = name_->text().trimmed();
    if (name.isEmpty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        name_->setFocus();
        return false;
    }
    auto result = initial_;
    result.name = name.toStdString();
    result.placement = {
        translation_[0]->value(), translation_[1]->value(), translation_[2]->value(),
        rotation_[0]->value(), rotation_[1]->value(), rotation_[2]->value(),
    };
    // Only retain fully-populated rows (both sides picked); an in-progress
    // partially-filled row is not persisted, matching Python's row cap and
    // discard-incomplete-rows behavior.
    result.placement_references.clear();
    for (const auto& row : placement_references_) {
        if (row.component_reference.owner_id.empty() ||
            row.component_reference.semantic_key.empty() ||
            row.target_reference.owner_id.empty() ||
            row.target_reference.semantic_key.empty()) {
            continue;
        }
        result.placement_references.push_back(row);
    }
    try {
        commit_(std::move(result));
    } catch (const std::exception& failure) {
        error_->setText(QString::fromUtf8(failure.what()));
        return false;
    }
    return true;
}

}  // namespace zima::app
