#include "construction_properties_dialog.hpp"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

#include <cmath>

namespace zima::app {

ConstructionPropertiesDialog::ConstructionPropertiesDialog(
    const zima::document::ConstructionObject& initial, bool edit_mode,
    CommitCallback commit, QWidget* parent)
    : PropertiesSubWindow(edit_mode ? tr("Vlastnosti konstrukční geometrie")
                                    : tr("Nová konstrukční geometrie"), parent),
      initial_(initial), commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(340);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(initial.name), this);
    form->addRow(tr("Název"), name_);
    const auto field = [this](double value, const char* name, const QString& suffix) {
        auto* result = new QDoubleSpinBox(this);
        result->setRange(-1'000'000.0, 1'000'000.0);
        result->setDecimals(3);
        result->setSingleStep(1.0);
        result->setSuffix(suffix);
        result->setObjectName(name);
        result->setValue(value);
        return result;
    };
    origin_ = {field(initial.origin.x, "constructionX", " mm"),
               field(initial.origin.y, "constructionY", " mm"),
               field(initial.origin.z, "constructionZ", " mm")};
    form->addRow(tr("X"), origin_[0]);
    form->addRow(tr("Y"), origin_[1]);
    form->addRow(tr("Z"), origin_[2]);
    if (initial.kind != zima::document::ConstructionKind::Point) {
        direction_ = {field(initial.direction.x, "constructionDirectionX", {}),
                      field(initial.direction.y, "constructionDirectionY", {}),
                      field(initial.direction.z, "constructionDirectionZ", {})};
        form->addRow(tr("Směr X"), direction_[0]);
        form->addRow(tr("Směr Y"), direction_[1]);
        form->addRow(tr("Směr Z"), direction_[2]);
    }
    if (initial.kind != zima::document::ConstructionKind::Point) {
        display_size_ = field(initial.display_size, "constructionDisplaySize", " mm");
        display_size_->setRange(0.001, 1'000'000.0);
        form->addRow(initial.kind == zima::document::ConstructionKind::Axis
                         ? tr("Délka zobrazení") : tr("Velikost zobrazení"),
                     display_size_);
    }
    content_layout()->addLayout(form);
    error_ = new QLabel(this);
    error_->setStyleSheet("color: #d96b6b;");
    error_->setWordWrap(true);
    content_layout()->addWidget(error_);
}

bool ConstructionPropertiesDialog::submit() {
    auto value = initial_;
    value.name = name_->text().trimmed().toStdString();
    value.origin = {origin_[0]->value(), origin_[1]->value(), origin_[2]->value()};
    if (direction_[0] != nullptr) {
        value.direction = {direction_[0]->value(), direction_[1]->value(),
                           direction_[2]->value()};
        const double length = std::sqrt(value.direction.x * value.direction.x +
                                        value.direction.y * value.direction.y +
                                        value.direction.z * value.direction.z);
        if (length <= 1e-12) {
            error_->setText(tr("Směr nesmí být nulový."));
            return false;
        }
    }
    if (display_size_ != nullptr) value.display_size = display_size_->value();
    if (value.name.empty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        return false;
    }
    commit_(std::move(value));
    return true;
}

}  // namespace zima::app
