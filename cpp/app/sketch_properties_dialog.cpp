#include "sketch_properties_dialog.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

namespace zima::app {

SketchPropertiesDialog::SketchPropertiesDialog(
    zima::sketcher::Sketch initial, bool edit_mode,
    CommitCallback commit, QWidget* parent)
    : PropertiesSubWindow(edit_mode ? tr("Vlastnosti skici") : tr("Nová skica"), parent),
      initial_(std::move(initial)), commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(320);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(initial_.name), this);
    name_->setObjectName("sketchName");
    plane_ = new QComboBox(this);
    plane_->setObjectName("sketchPlane");
    plane_->addItem("XY", static_cast<int>(zima::sketcher::SketchPlane::XY));
    plane_->addItem("XZ", static_cast<int>(zima::sketcher::SketchPlane::XZ));
    plane_->addItem("YZ", static_cast<int>(zima::sketcher::SketchPlane::YZ));
    plane_->setCurrentIndex(plane_->findData(static_cast<int>(initial_.plane)));
    offset_ = new QDoubleSpinBox(this);
    offset_->setObjectName("sketchPlaneOffset");
    offset_->setRange(-1'000'000.0, 1'000'000.0);
    offset_->setDecimals(3);
    offset_->setSuffix(" mm");
    offset_->setValue(initial_.plane_offset);
    form->addRow(tr("Název"), name_);
    form->addRow(tr("Rovina"), plane_);
    form->addRow(tr("Odsazení roviny"), offset_);
    content_layout()->addLayout(form);
    error_ = new QLabel(this);
    error_->setStyleSheet("color: #c64b4b;");
    content_layout()->addWidget(error_);
    connect(name_, &QLineEdit::textChanged, this, [this](const QString& value) {
        set_internal_title(value.trimmed().isEmpty()
            ? tr("Vlastnosti skici") : tr("Vlastnosti: %1").arg(value.trimmed()));
        error_->clear();
    });
}

bool SketchPropertiesDialog::submit() {
    const auto name = name_->text().trimmed();
    if (name.isEmpty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        return false;
    }
    auto result = initial_;
    result.name = name.toStdString();
    result.plane = static_cast<zima::sketcher::SketchPlane>(plane_->currentData().toInt());
    result.plane_offset = offset_->value();
    try {
        result.validate();
        commit_(std::move(result));
    } catch (const std::exception& failure) {
        error_->setText(QString::fromUtf8(failure.what()));
        return false;
    }
    return true;
}

}  // namespace zima::app
