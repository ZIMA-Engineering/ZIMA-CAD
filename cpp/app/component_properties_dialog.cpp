#include "component_properties_dialog.hpp"

#include <zima/ui/reference_cell.hpp>

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
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

class MateLimitsDialog final : public zima::ui::PropertiesSubWindow {
public:
    using Commit = std::function<void(std::optional<double>, std::optional<double>)>;

    MateLimitsDialog(const zima::assembly::ComponentPlacementReference& row,
        Commit commit, QWidget* parent)
        : PropertiesSubWindow(tr("Meze mate"), parent),
          current_value_(row.offset), commit_(std::move(commit)) {
        setObjectName("mateLimitsDialog");
        const bool angular = mate_type_is_angular(row.mate_type);
        auto* form = new QFormLayout;
        current_ = new QDoubleSpinBox(this);
        lower_enabled_ = new QCheckBox(this);
        lower_ = new QDoubleSpinBox(this);
        upper_enabled_ = new QCheckBox(this);
        upper_ = new QDoubleSpinBox(this);
        for (auto* field : {current_, lower_, upper_}) {
            field->setObjectName(field == current_ ? "mateCurrentValue" :
                field == lower_ ? "mateLowerLimit" : "mateUpperLimit");
            field->setDecimals(3);
            field->setRange(angular ? -360.0 : -1'000'000'000.0,
                angular ? 360.0 : 1'000'000'000.0);
            field->setSuffix(angular ? QStringLiteral(" °") : QStringLiteral(" mm"));
        }
        current_->setValue(row.offset);
        current_->setReadOnly(true);
        current_->setButtonSymbols(QAbstractSpinBox::NoButtons);
        lower_enabled_->setObjectName("mateLowerEnabled");
        upper_enabled_->setObjectName("mateUpperEnabled");
        lower_enabled_->setChecked(row.lower_limit.has_value());
        lower_->setValue(row.lower_limit.value_or(row.offset));
        lower_->setEnabled(lower_enabled_->isChecked());
        upper_enabled_->setChecked(row.upper_limit.has_value());
        upper_->setValue(row.upper_limit.value_or(row.offset));
        upper_->setEnabled(upper_enabled_->isChecked());
        connect(lower_enabled_, &QCheckBox::toggled, lower_,
            &QDoubleSpinBox::setEnabled);
        connect(upper_enabled_, &QCheckBox::toggled, upper_,
            &QDoubleSpinBox::setEnabled);
        form->addRow(tr("Hodnota"), current_);
        form->addRow(tr("Dolní mez"), limit_row(lower_enabled_, lower_));
        form->addRow(tr("Horní mez"), limit_row(upper_enabled_, upper_));
        error_ = new QLabel(this);
        error_->setStyleSheet("color:#c64b4b;");
        error_->setWordWrap(true);
        content_layout()->addLayout(form);
        content_layout()->addWidget(error_);
        set_initial_size({340, 250});
    }

protected:
    bool submit() override {
        const std::optional<double> lower = lower_enabled_->isChecked()
            ? std::optional<double>{lower_->value()} : std::nullopt;
        const std::optional<double> upper = upper_enabled_->isChecked()
            ? std::optional<double>{upper_->value()} : std::nullopt;
        if (lower && upper && *lower > *upper) {
            error_->setText(tr("Dolní mez nesmí být větší než horní mez."));
            return false;
        }
        if ((lower && current_value_ < *lower) ||
            (upper && current_value_ > *upper)) {
            error_->setText(tr("Aktuální hodnota musí ležet v zadaných mezích."));
            return false;
        }
        commit_(lower, upper);
        return true;
    }

private:
    QWidget* limit_row(QCheckBox* enabled, QDoubleSpinBox* value) {
        auto* widget = new QWidget(this);
        auto* layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(enabled);
        layout->addWidget(value, 1);
        return widget;
    }

    double current_value_{};
    Commit commit_;
    QDoubleSpinBox* current_{};
    QCheckBox* lower_enabled_{};
    QDoubleSpinBox* lower_{};
    QCheckBox* upper_enabled_{};
    QDoubleSpinBox* upper_{};
    QLabel* error_{};
};

}  // namespace

ComponentPropertiesDialog::ComponentPropertiesDialog(
    const zima::assembly::PartOccurrence& initial,
    CommitCallback commit,
    QWidget* parent)
    : PropertiesSubWindow(tr("Komponenta"), parent),
      initial_(initial), commit_(std::move(commit)),
      placement_references_(initial.placement_references) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    // The three reference columns must remain readable without horizontal
    // compression; match the proven Python component-properties proportions.
    setMinimumWidth(720);
    resize(820, sizeHint().height());
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
    content_layout()->addLayout(form);

    // Embedded placement reference table -- Python reference design
    // (AssemblyComponentPropertiesDialog): up to 3 rows entered directly in
    // THIS dialog rather than via separate Mate commands + a "Vazby" tree
    // branch. Columns: [indicator, component reference, target reference,
    // mate type, offset/angle, flip].
    auto* heading = new QLabel(tr("Umístění komponenty"), this);
    auto heading_font = heading->font();
    heading_font.setBold(true);
    heading->setFont(heading_font);
    content_layout()->addWidget(heading);

    placement_table_ = new QTableWidget(0, 7, this);
    placement_table_->setObjectName("componentPlacementTable");
    placement_table_->setHorizontalHeaderLabels({QString(), tr("Tento díl"),
        tr("Cíl"), tr("Typ"), tr("Hodnota"), tr("Obrátit"), QString()});
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

    auto* position_form = new QFormLayout;
    position_form->addRow(tr("X"), translation_[0]);
    position_form->addRow(tr("Y"), translation_[1]);
    position_form->addRow(tr("Z"), translation_[2]);
    content_layout()->addLayout(position_form);

    auto* orientation_heading = new QLabel(tr("Orientace komponenty"), this);
    auto orientation_heading_font = orientation_heading->font();
    orientation_heading_font.setBold(true);
    orientation_heading->setFont(orientation_heading_font);
    content_layout()->addWidget(orientation_heading);
    auto* orientation_form = new QFormLayout;
    orientation_form->addRow(tr("RX"), rotation_[0]);
    orientation_form->addRow(tr("RY"), rotation_[1]);
    orientation_form->addRow(tr("RZ"), rotation_[2]);
    content_layout()->addLayout(orientation_form);

    error_ = new QLabel(this);
    error_->setStyleSheet("color: #c64b4b;");
    error_->setWordWrap(true);
    content_layout()->addWidget(error_);
    connect(name_, &QLineEdit::textChanged, this, [this](const QString&) {
        error_->clear();
    });
    for (auto* field : translation_) {
        connect(field, &QDoubleSpinBox::valueChanged, this,
            [this](double) { notify_preview(); });
    }
    for (auto* field : rotation_) {
        connect(field, &QDoubleSpinBox::valueChanged, this,
            [this](double) { notify_preview(); });
    }

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

void ComponentPropertiesDialog::set_preview_callback(PreviewCallback callback) {
    preview_ = std::move(callback);
    notify_preview();
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
    notify_preview();
    static_cast<void>(label);
}

void ComponentPropertiesDialog::remove_placement_reference(std::size_t index) {
    if (index >= placement_references_.size()) return;
    placement_references_.erase(placement_references_.begin() +
        static_cast<std::ptrdiff_t>(index));
    refresh_placement_table();
    notify_preview();
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
                auto& row = placement_references_[index];
                const auto next = static_cast<zima::assembly::MateKind>(
                    mate_type->currentData().toInt());
                if (mate_type_is_angular(row.mate_type) !=
                    mate_type_is_angular(next)) {
                    row.lower_limit.reset();
                    row.upper_limit.reset();
                }
                row.mate_type = next;
                if (auto* field = offset_fields_[index]) {
                    const QSignalBlocker blocked(field);
                    const bool angular = mate_type_is_angular(next);
                    field->setRange(angular ? -360.0 : -1'000'000'000.0,
                        angular ? 360.0 : 1'000'000'000.0);
                    field->setSuffix(angular ? QStringLiteral(" °") :
                        QStringLiteral(" mm"));
                    field->setValue(row.offset);
                }
                notify_preview();
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
            [this, index, offset](double value) {
                if (index < placement_references_.size()) {
                    auto& row = placement_references_[index];
                    if (row.lower_limit) value = std::max(value, *row.lower_limit);
                    if (row.upper_limit) value = std::min(value, *row.upper_limit);
                    if (std::abs(offset->value() - value) > 1.0e-12) {
                        const QSignalBlocker blocked(offset);
                        offset->setValue(value);
                    }
                    row.offset = value;
                    notify_preview();
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
                    notify_preview();
                }
            });
        flip_buttons_[index] = flip_button;
        placement_table_->setCellWidget(static_cast<int>(index), 5,
            zima::ui::centered_cell_widget(flip_button));

        auto* limit_button = new QToolButton(placement_table_);
        limit_button->setObjectName(
            QStringLiteral("mateLimitsButton%1").arg(index));
        limit_button->setIcon(style()->standardIcon(
            QStyle::SP_FileDialogDetailedView));
        limit_button->setToolTip(tr("Hodnota a meze mate"));
        limit_button->setAutoRaise(true);
        limit_button->setProperty("limitsEnabled",
            row.lower_limit.has_value() || row.upper_limit.has_value());
        limit_button->setEnabled(populated);
        limit_buttons_[index] = limit_button;
        placement_table_->setCellWidget(static_cast<int>(index), 6,
            zima::ui::centered_cell_widget(limit_button));
        connect(limit_button, &QToolButton::clicked, this, [this, index] {
            if (index >= placement_references_.size()) return;
            QWidget* owner = parentWidget() != nullptr ? parentWidget() : this;
            auto* dialog = new MateLimitsDialog(placement_references_[index],
                [this, index](std::optional<double> lower,
                    std::optional<double> upper) {
                    if (index >= placement_references_.size()) return;
                    auto& row = placement_references_[index];
                    row.lower_limit = lower;
                    row.upper_limit = upper;
                    if (auto* button = limit_buttons_[index]) {
                        button->setProperty("limitsEnabled", lower || upper);
                        button->setToolTip(lower || upper
                            ? tr("Mate má nastavené meze")
                            : tr("Hodnota a meze mate"));
                    }
                    notify_preview();
                }, owner);
            dialog->setAttribute(Qt::WA_DeleteOnClose, true);
            connect(this, &QObject::destroyed, dialog, &QWidget::close);
            dialog->show();
            dialog->raise();
        });
    }
}

zima::assembly::PartOccurrence ComponentPropertiesDialog::current_value() const {
    auto result = initial_;
    result.name = name_->text().trimmed().toStdString();
    result.placement = {
        translation_[0]->value(), translation_[1]->value(), translation_[2]->value(),
        rotation_[0]->value(), rotation_[1]->value(), rotation_[2]->value(),
    };
    result.placement_references.clear();
    for (const auto& row : placement_references_) {
        if (row.component_reference.owner_id.empty() ||
            row.component_reference.semantic_key.empty() ||
            row.target_reference.owner_id.empty() ||
            row.target_reference.semantic_key.empty()) continue;
        result.placement_references.push_back(row);
    }
    return result;
}

void ComponentPropertiesDialog::notify_preview() {
    if (preview_) preview_(current_value());
}

bool ComponentPropertiesDialog::submit() {
    const QString name = name_->text().trimmed();
    if (name.isEmpty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        name_->setFocus();
        return false;
    }
    auto result = current_value();
    try {
        commit_(std::move(result));
    } catch (const std::exception& failure) {
        error_->setText(QString::fromUtf8(failure.what()));
        return false;
    }
    return true;
}

}  // namespace zima::app
