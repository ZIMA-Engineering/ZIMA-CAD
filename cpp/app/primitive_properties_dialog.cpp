#include "primitive_properties_dialog.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

#include <exception>

namespace zima::app {
namespace {

QString primitive_label(zima::document::FeatureKind kind) {
    return kind == zima::document::FeatureKind::Cylinder
        ? QObject::tr("válce") : QObject::tr("kvádru");
}

}  // namespace

PrimitivePropertiesDialog::PrimitivePropertiesDialog(
    const zima::document::HistoryContainer& initial,
    bool edit_mode,
    bool allow_subtract,
    CommitCallback commit,
    QWidget* parent)
    : PropertiesSubWindow(
          edit_mode
              ? tr("Vlastnosti %1").arg(primitive_label(initial.feature_kind))
              : initial.feature_kind == zima::document::FeatureKind::Cylinder
                  ? tr("Nový válec") : tr("Nový kvádr"),
          parent),
      initial_(initial), commit_(std::move(commit)) {
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
    } else {
        radius_ = dimension(initial.cylinder.radius, "cylinderRadius");
        height_ = dimension(initial.cylinder.height, "cylinderHeight");
        form->addRow(tr("Poloměr"), radius_);
        form->addRow(tr("Výška"), height_);
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
            ? tr("Vlastnosti %1").arg(primitive_label(initial_.feature_kind))
            : tr("Vlastnosti: %1").arg(name.trimmed()));
        error_->clear();
    });
}

bool PrimitivePropertiesDialog::submit() {
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
    if (result.feature_kind == zima::document::FeatureKind::Box) {
        result.box = {length_->value(), width_->value(), height_->value()};
    } else {
        result.cylinder = {radius_->value(), height_->value()};
    }
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
