#include "component_properties_dialog.hpp"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

#include <exception>

namespace zima::app {

ComponentPropertiesDialog::ComponentPropertiesDialog(
    const zima::assembly::PartOccurrence& initial,
    CommitCallback commit,
    QWidget* parent)
    : PropertiesSubWindow(tr("Vlastnosti komponenty"), parent),
      initial_(initial), commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(340);
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
    try {
        commit_(std::move(result));
    } catch (const std::exception& failure) {
        error_->setText(QString::fromUtf8(failure.what()));
        return false;
    }
    return true;
}

}  // namespace zima::app
