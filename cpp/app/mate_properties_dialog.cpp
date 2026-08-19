#include "mate_properties_dialog.hpp"

#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QFormLayout>
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
    const bool point_mate = initial_.kind == zima::assembly::MateKind::PointCoincident;
    auto* dependent = new QLineEdit(
        QString::fromStdString(initial_.dependent.semantic_key), this);
    dependent->setReadOnly(true);
    form->addRow(axis_mate ? tr("Pohyblivá osa")
                           : point_mate ? tr("Pohyblivý bod") : tr("Pohyblivá plocha"),
                 dependent);
    auto* prerequisite = new QLineEdit(
        QString::fromStdString(initial_.prerequisite.semantic_key), this);
    prerequisite->setReadOnly(true);
    form->addRow(axis_mate ? tr("Referenční osa")
                           : point_mate ? tr("Referenční bod") : tr("Referenční plocha"),
                 prerequisite);
    offset_ = new QDoubleSpinBox(this);
    offset_->setRange(-1'000'000.0, 1'000'000.0);
    offset_->setDecimals(3);
    offset_->setSuffix(" mm");
    offset_->setObjectName("mateOffset");
    offset_->setValue(initial_.offset);
    offset_->setEnabled(!axis_mate && !point_mate);
    form->addRow(tr("Odsazení"), offset_);
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
    initial_.flipped = flipped_->isChecked();
    try {
        commit_(std::move(initial_));
    } catch (const std::exception& failure) {
        error_->setText(QString::fromUtf8(failure.what()));
        return false;
    }
    return true;
}

}  // namespace zima::app
