#include "construction_properties_dialog.hpp"

#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include <algorithm>
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
    definition_ = new QComboBox(this);
    definition_->setObjectName("constructionDefinition");
    definition_->addItem(tr("Absolutní souřadnice"),
        static_cast<int>(zima::document::ConstructionDefinition::Absolute));
    if (initial.kind == zima::document::ConstructionKind::Point) {
        definition_->addItem(tr("Podle bodu / vrcholu"),
            static_cast<int>(zima::document::ConstructionDefinition::PointReference));
    } else if (initial.kind == zima::document::ConstructionKind::Axis) {
        definition_->addItem(tr("Podle dvou bodů"),
            static_cast<int>(zima::document::ConstructionDefinition::TwoPointAxis));
        definition_->addItem(tr("Podle osy nebo lineární hrany"),
            static_cast<int>(zima::document::ConstructionDefinition::AxisReference));
    } else {
        definition_->addItem(tr("Podle tří bodů"),
            static_cast<int>(zima::document::ConstructionDefinition::ThreePointPlane));
        definition_->addItem(tr("Rovnoběžně s plochou / rovinou"),
            static_cast<int>(zima::document::ConstructionDefinition::PlaneReference));
    }
    const auto definition_index = definition_->findData(
        static_cast<int>(initial.definition));
    definition_->setCurrentIndex(std::max(0, definition_index));
    form->addRow(tr("Definice"), definition_);
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
    offset_ = field(initial.offset, "constructionOffset", " mm");
    form->addRow(tr("Odsazení"), offset_);
    references_ = initial.references;
    for (std::size_t index = 0; index < reference_buttons_.size(); ++index) {
        auto* button = new QPushButton(this);
        button->setObjectName(QString("constructionReference%1").arg(index + 1));
        button->setText(index < references_.size()
            ? QString::fromStdString(references_[index].owner_id + " / " +
                references_[index].semantic_key)
            : tr("Vybrat referenci…"));
        connect(button, &QPushButton::clicked, this, [this, index] {
            if (reference_request_) reference_request_(index);
        });
        reference_buttons_[index] = button;
        form->addRow(tr("Reference %1").arg(index + 1), button);
    }
    connect(definition_, &QComboBox::currentIndexChanged,
        this, [this] { refresh_definition_fields(); });
    refresh_definition_fields();
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
    connect(name_, &QLineEdit::textChanged, this, [this] { notify_preview(); });
    for (auto* input : origin_) {
        connect(input, &QDoubleSpinBox::valueChanged, this,
            [this] { notify_preview(); });
    }
    for (auto* input : direction_) {
        if (input != nullptr) connect(input, &QDoubleSpinBox::valueChanged,
            this, [this] { notify_preview(); });
    }
    if (display_size_ != nullptr) connect(display_size_,
        &QDoubleSpinBox::valueChanged, this, [this] { notify_preview(); });
    connect(offset_, &QDoubleSpinBox::valueChanged,
        this, [this] { notify_preview(); });
}

void ConstructionPropertiesDialog::set_reference_request_callback(
    ReferenceRequestCallback callback) {
    reference_request_ = std::move(callback);
}

void ConstructionPropertiesDialog::set_preview_callback(PreviewCallback callback) {
    preview_ = std::move(callback);
    notify_preview();
}

void ConstructionPropertiesDialog::set_reference(std::size_t index,
    zima::document::ConstructionReference reference, const QString& label) {
    if (index >= reference_buttons_.size()) return;
    if (references_.size() <= index) references_.resize(index + 1);
    references_[index] = std::move(reference);
    reference_buttons_[index]->setText(label);
    notify_preview();
}

zima::document::ConstructionDefinition
ConstructionPropertiesDialog::current_definition() const {
    return static_cast<zima::document::ConstructionDefinition>(
        definition_->currentData().toInt());
}

void ConstructionPropertiesDialog::refresh_definition_fields() {
    const auto definition = static_cast<zima::document::ConstructionDefinition>(
        definition_->currentData().toInt());
    std::size_t required = 0;
    if (definition == zima::document::ConstructionDefinition::PointReference ||
        definition == zima::document::ConstructionDefinition::AxisReference ||
        definition == zima::document::ConstructionDefinition::PlaneReference) required = 1;
    else if (definition == zima::document::ConstructionDefinition::TwoPointAxis) required = 2;
    else if (definition == zima::document::ConstructionDefinition::ThreePointPlane) required = 3;
    for (std::size_t index = 0; index < reference_buttons_.size(); ++index) {
        reference_buttons_[index]->setVisible(index < required);
    }
    offset_->setVisible(definition ==
        zima::document::ConstructionDefinition::PlaneReference);
    notify_preview();
}

zima::document::ConstructionObject ConstructionPropertiesDialog::current_value() const {
    auto value = initial_;
    value.name = name_->text().trimmed().toStdString();
    value.origin = {origin_[0]->value(), origin_[1]->value(), origin_[2]->value()};
    if (direction_[0] != nullptr) {
        value.direction = {direction_[0]->value(), direction_[1]->value(),
                           direction_[2]->value()};
    }
    if (display_size_ != nullptr) value.display_size = display_size_->value();
    value.definition = current_definition();
    const std::size_t required = value.definition ==
            zima::document::ConstructionDefinition::TwoPointAxis ? 2
        : value.definition == zima::document::ConstructionDefinition::ThreePointPlane ? 3
        : value.definition == zima::document::ConstructionDefinition::Absolute ? 0 : 1;
    value.references.assign(references_.begin(),
        references_.begin() + static_cast<std::ptrdiff_t>(
            std::min(required, references_.size())));
    value.offset = offset_->value();
    return value;
}

void ConstructionPropertiesDialog::notify_preview() {
    if (preview_) preview_(current_value());
}

bool ConstructionPropertiesDialog::submit() {
    auto value = current_value();
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
    const std::size_t required = value.definition ==
            zima::document::ConstructionDefinition::TwoPointAxis ? 2
        : value.definition == zima::document::ConstructionDefinition::ThreePointPlane ? 3
        : value.definition == zima::document::ConstructionDefinition::Absolute ? 0 : 1;
    if (references_.size() < required ||
        std::any_of(references_.begin(), references_.begin() +
                static_cast<std::ptrdiff_t>(required), [](const auto& reference) {
            return reference.owner_id.empty() || reference.semantic_key.empty();
        })) {
        error_->setText(tr("Vyberte všechny požadované reference."));
        return false;
    }
    value.references.assign(references_.begin(), references_.begin() +
        static_cast<std::ptrdiff_t>(required));
    value.offset = offset_->value();
    if (value.name.empty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        return false;
    }
    commit_(std::move(value));
    return true;
}

}  // namespace zima::app
