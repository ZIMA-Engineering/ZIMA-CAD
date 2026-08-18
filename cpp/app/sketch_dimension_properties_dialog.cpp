#include "sketch_dimension_properties_dialog.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>

namespace zima::app {
namespace {

QDoubleSpinBox* dimension_field(double value, const char* name, QWidget* parent) {
    auto* field = new QDoubleSpinBox(parent);
    field->setObjectName(name);
    field->setRange(-1'000'000.0, 1'000'000.0);
    field->setDecimals(3);
    field->setSingleStep(1.0);
    field->setSuffix(" mm");
    field->setValue(value);
    return field;
}

QWidget* optional_field(
    QCheckBox*& enabled, QDoubleSpinBox*& field, bool checked,
    double value, const char* check_name, const char* field_name, QWidget* parent) {
    auto* widget = new QWidget(parent);
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    enabled = new QCheckBox(widget);
    enabled->setObjectName(check_name);
    enabled->setChecked(checked);
    field = dimension_field(value, field_name, widget);
    field->setEnabled(checked);
    layout->addWidget(enabled);
    layout->addWidget(field, 1);
    return widget;
}

}  // namespace

SketchDimensionPropertiesDialog::SketchDimensionPropertiesDialog(
    zima::sketcher::SketchDimension initial, bool edit_mode,
    CommitCallback commit, QWidget* parent)
    : PropertiesSubWindow(edit_mode ? tr("Vlastnosti kóty") : tr("Nová kóta"), parent),
      initial_(std::move(initial)), commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(340);
    auto* form = new QFormLayout;
    value_ = dimension_field(initial_.value, "sketchDimensionValue", this);
    form->addRow(tr("Jmenovitá hodnota"), value_);
    form->addRow(tr("Dolní mez"), optional_field(
        lower_enabled_, lower_, initial_.lower_limit.has_value(),
        initial_.lower_limit.value_or(0.0), "sketchLowerEnabled", "sketchLowerLimit", this));
    form->addRow(tr("Horní mez"), optional_field(
        upper_enabled_, upper_, initial_.upper_limit.has_value(),
        initial_.upper_limit.value_or(initial_.value),
        "sketchUpperEnabled", "sketchUpperLimit", this));
    content_layout()->addLayout(form);
    error_ = new QLabel(this);
    error_->setStyleSheet("color: #c64b4b;");
    error_->setWordWrap(true);
    content_layout()->addWidget(error_);
    connect(lower_enabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        lower_->setEnabled(enabled);
        if (enabled && !initial_.lower_limit) lower_->setValue(0.0);
        error_->clear();
    });
    connect(upper_enabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        upper_->setEnabled(enabled);
        if (enabled && !initial_.upper_limit) upper_->setValue(value_->value());
        error_->clear();
    });
    connect(value_, qOverload<double>(&QDoubleSpinBox::valueChanged),
        this, [this](double) { error_->clear(); });
}

bool SketchDimensionPropertiesDialog::submit() {
    auto result = initial_;
    result.value = value_->value();
    result.lower_limit = lower_enabled_->isChecked()
        ? std::optional<double>{lower_->value()} : std::nullopt;
    result.upper_limit = upper_enabled_->isChecked()
        ? std::optional<double>{upper_->value()} : std::nullopt;
    if (result.kind == zima::sketcher::DimensionKind::Distance && result.value < 0.0) {
        error_->setText(tr("Délka nesmí být záporná."));
        return false;
    }
    if (result.lower_limit && result.upper_limit &&
        *result.lower_limit > *result.upper_limit) {
        error_->setText(tr("Dolní mez nesmí být větší než horní mez."));
        return false;
    }
    if ((result.lower_limit && result.value < *result.lower_limit) ||
        (result.upper_limit && result.value > *result.upper_limit)) {
        error_->setText(tr("Jmenovitá hodnota musí ležet v zadaných mezích."));
        return false;
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
