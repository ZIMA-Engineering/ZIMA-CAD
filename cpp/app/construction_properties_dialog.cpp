#include "construction_properties_dialog.hpp"

#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>

#include <algorithm>
#include <cmath>

namespace zima::app {

ConstructionPropertiesDialog::ConstructionPropertiesDialog(
    const zima::document::ConstructionObject& initial, bool edit_mode,
    CommitCallback commit, QWidget* parent)
    : PropertiesSubWindow(
          initial.kind == zima::document::ConstructionKind::Point
              ? tr("Vlastnosti bodu")
              : initial.kind == zima::document::ConstructionKind::Axis
                  ? tr("Vlastnosti osy") : tr("Vlastnosti roviny"), parent),
      initial_(initial), commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(460);
    auto compact_font = font();
    compact_font.setPixelSize(10);
    setFont(compact_font);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(initial.name), this);
    set_internal_title(tr("Vlastnosti – %1").arg(name_->text()));
    connect(name_, &QLineEdit::textChanged, this, [this](const QString& name) {
        set_internal_title(tr("Vlastnosti – %1").arg(name.trimmed()));
    });
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
    definition_->hide();
    auto* container_type = new QComboBox(this);
    container_type->addItem(tr("Bod"));
    container_type->addItem(tr("Osa"));
    container_type->addItem(tr("Rovina"));
    container_type->setCurrentIndex(initial.kind ==
            zima::document::ConstructionKind::Point ? 0
        : initial.kind == zima::document::ConstructionKind::Axis ? 1 : 2);
    container_type->setEnabled(false);
    form->addRow(tr("Typ kontejneru"), container_type);
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
    if (initial.kind != zima::document::ConstructionKind::Point) {
        direction_ = {field(initial.direction.x, "constructionDirectionX", {}),
                      field(initial.direction.y, "constructionDirectionY", {}),
                      field(initial.direction.z, "constructionDirectionZ", {})};
    }
    offset_ = field(initial.offset, "constructionOffset", " mm");
    offset_->hide();
    references_ = initial.references;
    connect(definition_, &QComboBox::currentIndexChanged,
        this, [this] { refresh_definition_fields(); });
    refresh_definition_fields();
    if (initial.kind != zima::document::ConstructionKind::Point) {
        display_size_ = field(initial.display_size, "constructionDisplaySize", " mm");
        display_size_->setRange(0.001, 1'000'000.0);
    }
    content_layout()->addLayout(form);
    reference_status_ = new QLabel(this);
    reference_status_->setStyleSheet("color:#80AA1A;font-weight:700;");
    reference_status_->setWordWrap(true);
    content_layout()->addWidget(reference_status_);
    auto* placement_heading = new QLabel(tr("Umístění konstrukce"), this);
    auto heading_font = placement_heading->font();
    heading_font.setBold(true);
    placement_heading->setFont(heading_font);
    content_layout()->addWidget(placement_heading);
    reference_table_ = new QTableWidget(0, 3, this);
    reference_table_->setObjectName("constructionReferenceTable");
    reference_table_->setHorizontalHeaderLabels(
        {QString(), tr("Reference"), tr("Odsazení")});
    reference_table_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    reference_table_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    reference_table_->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    reference_table_->verticalHeader()->setDefaultSectionSize(34);
    reference_table_->verticalHeader()->setMinimumSectionSize(34);
    reference_table_->setFixedHeight(
        reference_table_->horizontalHeader()->sizeHint().height() + 3 * 34 +
        reference_table_->frameWidth() * 2);
    reference_table_->setStyleSheet(
        "QTableWidget::item:selected{background:#00d1ff;color:#102027}");
    connect(reference_table_, &QTableWidget::cellClicked, this,
        [this](int row, int column) {
            if (column == 1 && row >= 0 && row < 3 && reference_request_) {
                reference_request_(static_cast<std::size_t>(row));
            }
        });
    content_layout()->addWidget(reference_table_);
    refresh_reference_table();
    auto* coordinates = new QFormLayout;
    coordinates->addRow(tr("X"), origin_[0]);
    coordinates->addRow(tr("Y"), origin_[1]);
    coordinates->addRow(tr("Z"), origin_[2]);
    if (initial.kind == zima::document::ConstructionKind::Point) {
        auto* orientation_heading = new QLabel(tr("Orientace objektu"), this);
        auto orientation_font = orientation_heading->font();
        orientation_font.setBold(true);
        orientation_heading->setFont(orientation_font);
        coordinates->addRow(orientation_heading);
        rotation_ = {field(initial.rotation.x, "constructionRotationX", " deg"),
                     field(initial.rotation.y, "constructionRotationY", " deg"),
                     field(initial.rotation.z, "constructionRotationZ", " deg")};
        for (auto* input : rotation_) {
            input->setRange(-360'000.0, 360'000.0);
            input->setSingleStep(5.0);
        }
        coordinates->addRow(tr("RX"), rotation_[0]);
        coordinates->addRow(tr("RY"), rotation_[1]);
        coordinates->addRow(tr("RZ"), rotation_[2]);
    } else {
        coordinates->addRow(tr("Směr X"), direction_[0]);
        coordinates->addRow(tr("Směr Y"), direction_[1]);
        coordinates->addRow(tr("Směr Z"), direction_[2]);
        coordinates->addRow(initial.kind == zima::document::ConstructionKind::Axis
                ? tr("Délka zobrazení") : tr("Velikost zobrazení"),
            display_size_);
    }
    content_layout()->addLayout(coordinates);
    dof_label_ = new QLabel(this);
    content_layout()->addWidget(dof_label_);
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
    for (auto* input : rotation_) {
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
    zima::document::ConstructionReference reference, const QString& label,
    zima::document::ConstructionDefinition definition) {
    if (index >= 3) return;
    if (references_.size() <= index) references_.resize(index + 1);
    references_[index] = std::move(reference);
    definition_->setCurrentIndex(definition_->findData(static_cast<int>(definition)));
    if (auto* item = reference_table_->item(static_cast<int>(index), 1)) {
        item->setText(QStringLiteral("%1. %2").arg(index + 1).arg(label));
    }
    refresh_reference_table();
    notify_preview();
}

void ConstructionPropertiesDialog::set_reference(std::size_t index,
    zima::document::ConstructionReference reference, const QString& label) {
    set_reference(index, std::move(reference), label, current_definition());
}

zima::document::ConstructionDefinition
ConstructionPropertiesDialog::current_definition() const {
    return static_cast<zima::document::ConstructionDefinition>(
        definition_->currentData().toInt());
}

zima::document::ConstructionKind
ConstructionPropertiesDialog::construction_kind() const {
    return initial_.kind;
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
    offset_->setVisible(definition ==
        zima::document::ConstructionDefinition::PlaneReference);
    refresh_reference_table();
    notify_preview();
}

void ConstructionPropertiesDialog::refresh_reference_table() {
    if (reference_table_ == nullptr) return;
    reference_table_->setRowCount(0);
    const std::size_t visible = std::min<std::size_t>(3, references_.size() + 1);
    for (std::size_t index = 0; index < visible; ++index) {
        reference_table_->insertRow(static_cast<int>(index));
        if (index < references_.size()) {
            auto* remove = new QPushButton(QStringLiteral("×"), reference_table_);
            remove->setFixedSize(30, 30);
            remove->setToolTip(tr("Odstranit referenci"));
            remove->setStyleSheet(
                "QPushButton{color:white;background:#8b2424;border:1px solid #b94a4a;"
                "border-radius:4px;font-weight:700}"
                "QPushButton:hover{background:#b83232;border-color:#ed7777}");
            connect(remove, &QPushButton::clicked, this,
                [this, index] { remove_reference(index); });
            reference_table_->setCellWidget(static_cast<int>(index), 0, remove);
            reference_table_->setItem(static_cast<int>(index), 1,
                new QTableWidgetItem(QStringLiteral("%1. %2 / %3")
                    .arg(index + 1)
                    .arg(QString::fromStdString(references_[index].owner_id))
                    .arg(QString::fromStdString(references_[index].semantic_key))));
            auto* offset = new QDoubleSpinBox(reference_table_);
            offset->setRange(-1'000'000'000.0, 1'000'000'000.0);
            offset->setDecimals(3);
            offset->setSuffix(QStringLiteral(" mm"));
            offset->setValue(references_[index].offset);
            offset->setEnabled(current_definition() ==
                zima::document::ConstructionDefinition::PlaneReference);
            connect(offset, &QDoubleSpinBox::valueChanged, this,
                [this, index](double value) {
                    if (index < references_.size()) {
                        references_[index].offset = value;
                        notify_preview();
                    }
                });
            reference_table_->setCellWidget(static_cast<int>(index), 2, offset);
        } else {
            auto* item = new QTableWidgetItem(
                QStringLiteral("%1. %2").arg(index + 1).arg(tr("Reference")));
            item->setForeground(palette().brush(QPalette::Mid));
            reference_table_->setItem(static_cast<int>(index), 1, item);
            auto* offset = new QDoubleSpinBox(reference_table_);
            offset->setEnabled(false);
            offset->setSuffix(QStringLiteral(" mm"));
            reference_table_->setCellWidget(static_cast<int>(index), 2, offset);
        }
    }
    if (dof_label_ != nullptr) {
        const std::size_t required = current_definition() ==
                zima::document::ConstructionDefinition::TwoPointAxis ? 2
            : current_definition() ==
                zima::document::ConstructionDefinition::ThreePointPlane ? 3
            : current_definition() == zima::document::ConstructionDefinition::Absolute
                ? 0 : 1;
        const int dof = initial_.kind == zima::document::ConstructionKind::Point
            ? std::max(0, 3 - static_cast<int>(references_.size())) + 3
            : static_cast<int>(required > references_.size()
                ? required - references_.size() : 0);
        dof_label_->setText(tr("Zbývající stupně volnosti: %1").arg(dof));
        reference_status_->setText(dof == 0 && required != 0
            ? tr("Plně určené") : QString());
    }
}

void ConstructionPropertiesDialog::remove_reference(std::size_t index) {
    if (index >= references_.size()) return;
    references_.erase(references_.begin() + static_cast<std::ptrdiff_t>(index));
    if (references_.empty()) {
        definition_->setCurrentIndex(definition_->findData(static_cast<int>(
            zima::document::ConstructionDefinition::Absolute)));
    }
    refresh_reference_table();
    notify_preview();
}

zima::document::ConstructionObject ConstructionPropertiesDialog::current_value() const {
    auto value = initial_;
    value.name = name_->text().trimmed().toStdString();
    value.origin = {origin_[0]->value(), origin_[1]->value(), origin_[2]->value()};
    if (rotation_[0] != nullptr) {
        value.rotation = {rotation_[0]->value(), rotation_[1]->value(),
                          rotation_[2]->value()};
    }
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
