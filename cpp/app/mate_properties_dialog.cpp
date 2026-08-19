#include "mate_properties_dialog.hpp"

#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>

#include <exception>

namespace zima::app {

MatePropertiesDialog::MatePropertiesDialog(
    zima::assembly::AssemblyMate initial,
    CommitCallback commit,
    QWidget* parent)
    : PropertiesSubWindow(tr("Vlastnosti vazby"), parent),
      initial_(std::move(initial)), commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(380);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(initial_.name), this);
    form->addRow(tr("Název"), name_);
    const bool axis_mate = initial_.kind == zima::assembly::MateKind::AxisCoincident;
    const bool axis_angle = initial_.kind == zima::assembly::MateKind::AxisAngle;
    const bool plane_angle = initial_.kind == zima::assembly::MateKind::PlaneAngle;
    const bool angle_mate = axis_angle || plane_angle;
    const bool point_mate = initial_.kind == zima::assembly::MateKind::PointCoincident;
    auto* dependent = new QLineEdit(
        QString::fromStdString(initial_.dependent.semantic_key), this);
    dependent->setReadOnly(true);
    form->addRow((axis_mate || axis_angle) ? tr("Pohyblivá osa")
                           : point_mate ? tr("Pohyblivý bod") : tr("Pohyblivá plocha"),
                 dependent);
    auto* prerequisite = new QLineEdit(
        QString::fromStdString(initial_.prerequisite.semantic_key), this);
    prerequisite->setReadOnly(true);
    form->addRow((axis_mate || axis_angle) ? tr("Referenční osa")
                           : point_mate ? tr("Referenční bod") : tr("Referenční plocha"),
                 prerequisite);
    offset_ = new QDoubleSpinBox(this);
    offset_->setRange(-1'000'000.0, 1'000'000.0);
    offset_->setDecimals(3);
    offset_->setSuffix(" mm");
    offset_->setObjectName("mateOffset");
    offset_->setValue(initial_.offset);
    offset_->setEnabled(!axis_mate && !point_mate && !angle_mate);
    form->addRow(tr("Odsazení"), offset_);
    angle_ = new QDoubleSpinBox(this);
    angle_->setRange(0.0, 180.0);
    angle_->setDecimals(3);
    angle_->setSuffix(" °");
    angle_->setObjectName("mateAngle");
    angle_->setValue(initial_.angle_degrees);
    angle_->setEnabled(angle_mate);
    form->addRow(tr("Úhel"), angle_);
    const bool limited_value = angle_mate ||
        initial_.kind == zima::assembly::MateKind::PlaneCoincident;
    const auto add_limit = [&](const QString& label, QCheckBox*& enabled,
                               QDoubleSpinBox*& field, bool lower) {
        auto* widget = new QWidget(this);
        auto* layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        enabled = new QCheckBox(widget);
        enabled->setObjectName(lower ? "mateLowerEnabled" : "mateUpperEnabled");
        const auto& limit = lower ? initial_.lower_limit : initial_.upper_limit;
        enabled->setChecked(limit.has_value());
        enabled->setEnabled(limited_value);
        field = new QDoubleSpinBox(widget);
        field->setObjectName(lower ? "mateLowerLimit" : "mateUpperLimit");
        field->setDecimals(3);
        field->setRange(angle_mate ? 0.0 : -1'000'000.0,
                        angle_mate ? 180.0 : 1'000'000.0);
        field->setSuffix(angle_mate ? " °" : " mm");
        const double current = angle_mate ? initial_.angle_degrees : initial_.offset;
        field->setValue(limit.value_or(lower ? 0.0 : current));
        field->setEnabled(limited_value && limit.has_value());
        const bool had_limit = limit.has_value();
        connect(enabled, &QCheckBox::toggled, this,
            [this, field, lower, had_limit, angle_mate](bool checked) {
            field->setEnabled(checked);
            if (checked && !had_limit) {
                field->setValue(lower ? 0.0
                    : angle_mate ? angle_->value() : offset_->value());
            }
        });
        layout->addWidget(enabled);
        layout->addWidget(field, 1);
        form->addRow(label, widget);
    };
    add_limit(tr("Dolní mez"), lower_enabled_, lower_, true);
    add_limit(tr("Horní mez"), upper_enabled_, upper_, false);
    flipped_ = new QCheckBox(tr("Obrátit orientaci reference"), this);
    flipped_->setChecked(initial_.flipped);
    flipped_->setEnabled(!point_mate);
    flipped_->setObjectName("mateFlipped");
    form->addRow(tr("Orientace"), flipped_);
    content_layout()->addLayout(form);
    error_ = new QLabel(this);
    error_->setStyleSheet("color: #c64b4b;");
    error_->setWordWrap(true);
    content_layout()->addWidget(error_);
}

bool MatePropertiesDialog::submit() {
    const auto name = name_->text().trimmed();
    if (name.isEmpty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        return false;
    }
    initial_.name = name.toStdString();
    initial_.offset = offset_->value();
    initial_.angle_degrees = angle_->value();
    initial_.lower_limit = lower_enabled_->isChecked()
        ? std::optional<double>{lower_->value()} : std::nullopt;
    initial_.upper_limit = upper_enabled_->isChecked()
        ? std::optional<double>{upper_->value()} : std::nullopt;
    initial_.flipped = flipped_->isChecked();
    const bool angular = initial_.kind == zima::assembly::MateKind::AxisAngle ||
        initial_.kind == zima::assembly::MateKind::PlaneAngle;
    const double value = angular ? initial_.angle_degrees : initial_.offset;
    if (initial_.lower_limit && initial_.upper_limit &&
        *initial_.lower_limit > *initial_.upper_limit) {
        error_->setText(tr("Dolní mez nesmí být větší než horní mez."));
        return false;
    }
    if ((initial_.lower_limit && value < *initial_.lower_limit) ||
        (initial_.upper_limit && value > *initial_.upper_limit)) {
        error_->setText(tr("Hodnota vazby musí ležet v zadaných mezích."));
        return false;
    }
    try {
        commit_(std::move(initial_));
    } catch (const std::exception& failure) {
        error_->setText(QString::fromUtf8(failure.what()));
        return false;
    }
    return true;
}

}  // namespace zima::app
