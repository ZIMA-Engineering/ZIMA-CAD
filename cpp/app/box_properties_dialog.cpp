#include "box_properties_dialog.hpp"

#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

namespace zima::app {

BoxPropertiesDialog::BoxPropertiesDialog(
    const zima::document::HistoryContainer& initial,
    bool edit_mode,
    bool allow_subtract,
    CommitCallback commit,
    QWidget* parent)
    : PropertiesSubWindow(
          edit_mode ? tr("Vlastnosti kvádru") : tr("Nový kvádr"), parent),
      initial_(initial),
      commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(340);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(initial.name), this);
    form->addRow(tr("Název"), name_);
    operation_ = new QComboBox(this);
    operation_->addItem(tr("Přičíst"), "add");
    if (allow_subtract) operation_->addItem(tr("Odečíst"), "subtract");
    if (initial.combine_mode == zima::document::CombineMode::Subtract) {
        operation_->setCurrentIndex(operation_->findData("subtract"));
    }
    form->addRow(tr("Operace"), operation_);

    auto make_dimension = [this](double value) {
        auto* field = new QDoubleSpinBox(this);
        field->setRange(0.001, 1'000'000.0);
        field->setDecimals(3);
        field->setSingleStep(1.0);
        field->setSuffix(" mm");
        field->setObjectName("boxDimension");
        field->setValue(value);
        return field;
    };
    length_ = make_dimension(initial.box.length);
    width_ = make_dimension(initial.box.width);
    height_ = make_dimension(initial.box.height);
    form->addRow(tr("Délka"), length_);
    form->addRow(tr("Šířka"), width_);
    form->addRow(tr("Výška"), height_);

    auto make_placement = [this](double value, bool angular) {
        auto* field = new QDoubleSpinBox(this);
        field->setRange(angular ? -360.0 : -1'000'000.0,
                        angular ? 360.0 : 1'000'000.0);
        field->setDecimals(3);
        field->setSingleStep(1.0);
        field->setSuffix(angular ? "°" : " mm");
        field->setValue(value);
        field->setObjectName(angular ? "boxRotation" : "boxTranslation");
        return field;
    };
    translation_ = {
        make_placement(initial.placement.x, false),
        make_placement(initial.placement.y, false),
        make_placement(initial.placement.z, false),
    };
    rotation_ = {
        make_placement(initial.placement.rotation_x, true),
        make_placement(initial.placement.rotation_y, true),
        make_placement(initial.placement.rotation_z, true),
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
            ? tr("Vlastnosti kvádru")
            : tr("Vlastnosti: %1").arg(name.trimmed()));
        error_->clear();
    });
}

bool BoxPropertiesDialog::submit() {
    const QString name = name_->text().trimmed();
    if (name.isEmpty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        name_->setFocus();
        return false;
    }
    auto result = initial_;
    result.name = name.toStdString();
    result.combine_mode = operation_->currentData().toString() == "subtract"
        ? zima::document::CombineMode::Subtract
        : zima::document::CombineMode::Add;
    result.box.length = length_->value();
    result.box.width = width_->value();
    result.box.height = height_->value();
    result.placement = {
        translation_[0]->value(), translation_[1]->value(),
        translation_[2]->value(), rotation_[0]->value(),
        rotation_[1]->value(), rotation_[2]->value(),
    };
    commit_(std::move(result));
    return true;
}

}  // namespace zima::app
