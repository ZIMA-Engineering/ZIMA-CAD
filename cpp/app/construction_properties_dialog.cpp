#include "construction_properties_dialog.hpp"

#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace zima::app {
namespace {

QString readable_reference_kind(const std::string& semantic) {
    const auto key = QString::fromStdString(semantic);
    if (key == QStringLiteral("point") || key.contains(QStringLiteral("point")))
        return QObject::tr("Bod");
    if (key.startsWith(QStringLiteral("origin:axis:")))
        return QObject::tr("Osa %1").arg(key.sliced(12).toUpper());
    if (key == QStringLiteral("axis") || key.contains(QStringLiteral("axis")))
        return QObject::tr("Osa");
    if (key.startsWith(QStringLiteral("origin:plane:")))
        return QObject::tr("Rovina %1").arg(key.sliced(13).toUpper());
    if (key == QStringLiteral("plane") || key.contains(QStringLiteral("plane")))
        return QObject::tr("Rovina");
    if (key.contains(QStringLiteral("edge"))) return QObject::tr("Hrana");
    if (key.contains(QStringLiteral("face"))) return QObject::tr("Plocha");
    return QObject::tr("Geometrická reference");
}

}  // namespace

ConstructionPropertiesDialog::ConstructionPropertiesDialog(
    const zima::document::ConstructionObject& initial, bool edit_mode,
    CommitCallback commit, QWidget* parent)
    : PropertiesSubWindow(
          initial.kind == zima::document::ConstructionKind::Point
              ? tr("Bod")
              : initial.kind == zima::document::ConstructionKind::Axis
                  ? tr("Osa") : tr("Rovina"), parent),
      initial_(initial), commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(460);
    auto compact_font = font();
    compact_font.setPixelSize(10);
    setFont(compact_font);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(initial.name), this);
    set_internal_title(name_->text());
    connect(name_, &QLineEdit::textChanged, this, [this](const QString& name) {
        set_internal_title(name.trimmed());
    });
    form->addRow(tr("Název"), name_);
    definition_ = new QComboBox(this);
    definition_->setObjectName("constructionDefinition");
    definition_->addItem(tr("Absolutní souřadnice"),
        static_cast<int>(zima::document::ConstructionDefinition::Absolute));
    definition_->addItem(tr("Geometrické reference"),
        static_cast<int>(zima::document::ConstructionDefinition::PointReference));
    if (initial.kind == zima::document::ConstructionKind::Axis) {
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
    offset_ = field(initial.offset, "constructionOffset", " mm");
    offset_->hide();
    for (const auto& reference : initial.references) {
        if (reference.orientation_drives_rotation) {
            orientation_references_.push_back(reference);
            orientation_labels_.push_back(
                readable_reference_kind(reference.semantic_key));
        } else {
            references_.push_back(reference);
            reference_labels_.push_back(
                readable_reference_kind(reference.semantic_key));
        }
    }
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
    content_layout()->addWidget(reference_table_);
    refresh_reference_table();
    if (initial.kind == zima::document::ConstructionKind::Plane) {
        auto* orientation_heading = new QLabel(tr("Orientace roviny"), this);
        auto orientation_heading_font = orientation_heading->font();
        orientation_heading_font.setBold(true);
        orientation_heading->setFont(orientation_heading_font);
        content_layout()->addWidget(orientation_heading);
        orientation_table_ = new QTableWidget(2, 3, this);
        orientation_table_->setObjectName("constructionOrientationTable");
        orientation_table_->setHorizontalHeaderLabels(
            {QString(), tr("Reference"), tr("FRONT / TOP")});
        orientation_table_->horizontalHeader()->setSectionResizeMode(
            0, QHeaderView::ResizeToContents);
        orientation_table_->horizontalHeader()->setSectionResizeMode(
            1, QHeaderView::Stretch);
        orientation_table_->horizontalHeader()->setSectionResizeMode(
            2, QHeaderView::ResizeToContents);
        orientation_table_->verticalHeader()->setDefaultSectionSize(34);
        orientation_table_->setFixedHeight(
            orientation_table_->horizontalHeader()->sizeHint().height() + 2 * 34 +
            orientation_table_->frameWidth() * 2);
        content_layout()->addWidget(orientation_table_);
        refresh_orientation_table();
    }
    auto* coordinates = new QFormLayout;
    coordinates->addRow(tr("X"), origin_[0]);
    coordinates->addRow(tr("Y"), origin_[1]);
    coordinates->addRow(tr("Z"), origin_[2]);
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
    if (initial.kind != zima::document::ConstructionKind::Point) {
        direction_combo_ = new QComboBox(this);
        direction_combo_->setObjectName("constructionDirection");
        if (initial.kind == zima::document::ConstructionKind::Axis) {
            direction_combo_->addItem(QStringLiteral("X"), QStringLiteral("x"));
            direction_combo_->addItem(QStringLiteral("Y"), QStringLiteral("y"));
            direction_combo_->addItem(QStringLiteral("Z"), QStringLiteral("z"));
            const std::array components{std::abs(initial.direction.x),
                std::abs(initial.direction.y), std::abs(initial.direction.z)};
            const auto dominant = static_cast<int>(std::distance(components.begin(),
                std::max_element(components.begin(), components.end())));
            direction_combo_->setCurrentIndex(dominant);
            coordinates->addRow(tr("Směr"), direction_combo_);
        } else {
            direction_combo_->addItem(QStringLiteral("XY"), QStringLiteral("xy"));
            direction_combo_->addItem(QStringLiteral("YZ"), QStringLiteral("yz"));
            direction_combo_->addItem(QStringLiteral("XZ"), QStringLiteral("xz"));
            direction_combo_->hide();
        }
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
    for (auto* input : rotation_) {
        if (input != nullptr) connect(input, &QDoubleSpinBox::valueChanged,
            this, [this] { notify_preview(); });
    }
    if (display_size_ != nullptr) connect(display_size_,
        &QDoubleSpinBox::valueChanged, this, [this] { notify_preview(); });
    if (direction_combo_ != nullptr) connect(direction_combo_,
        &QComboBox::currentIndexChanged, this, [this] { notify_preview(); });
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

bool ConstructionPropertiesDialog::set_reference(std::size_t index,
    zima::document::ConstructionReference reference, const QString& label,
    zima::document::ConstructionDefinition definition) {
    if (index >= 3) {
        const auto orientation_index = index - 3;
        if (initial_.kind != zima::document::ConstructionKind::Plane ||
            orientation_index >= 2) return false;
        const auto duplicate = [&](const auto& existing) {
            return existing.instance_path == reference.instance_path &&
                existing.owner_id == reference.owner_id &&
                existing.semantic_key == reference.semantic_key;
        };
        if (std::any_of(references_.begin(), references_.end(), duplicate) ||
            std::any_of(orientation_references_.begin(),
                orientation_references_.end(), duplicate)) {
            error_->setText(tr("Stejnou referenci nelze zadat vícekrát."));
            return false;
        }
        reference.orientation_drives_rotation = true;
        reference.orientation_role = orientation_index == 0 ? "front" : "top";
        if (orientation_references_.size() <= orientation_index)
            orientation_references_.resize(orientation_index + 1);
        if (orientation_labels_.size() <= orientation_index)
            orientation_labels_.resize(orientation_index + 1);
        orientation_references_[orientation_index] = std::move(reference);
        orientation_labels_[orientation_index] = label;
        error_->clear();
        refresh_orientation_table();
        notify_preview();
        return true;
    }
    for (std::size_t existing_index = 0;
         existing_index < references_.size(); ++existing_index) {
        const auto& existing = references_[existing_index];
        if (existing_index != index &&
            existing.instance_path == reference.instance_path &&
            existing.owner_id == reference.owner_id &&
            existing.semantic_key == reference.semantic_key) {
            error_->setText(tr("Stejnou referenci nelze zadat vícekrát."));
            return false;
        }
    }
    error_->clear();
    if (references_.size() <= index) references_.resize(index + 1);
    if (reference_labels_.size() <= index) reference_labels_.resize(index + 1);
    references_[index] = std::move(reference);
    reference_labels_[index] = label.trimmed().isEmpty()
        ? readable_reference_kind(references_[index].semantic_key) : label;
    definition_->setCurrentIndex(definition_->findData(static_cast<int>(definition)));
    if (auto* item = reference_table_->item(static_cast<int>(index), 1)) {
        item->setText(QStringLiteral("%1. %2").arg(index + 1).arg(label));
    }
    refresh_reference_table();
    notify_preview();
    return true;
}

bool ConstructionPropertiesDialog::set_reference(std::size_t index,
    zima::document::ConstructionReference reference, const QString& label) {
    return set_reference(index, std::move(reference), label, current_definition());
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

const std::string& ConstructionPropertiesDialog::construction_id() const {
    return initial_.id;
}

void ConstructionPropertiesDialog::set_remaining_translation_dof(int dof) {
    remaining_translation_dof_ = std::clamp(dof, 0, 3);
    if (dof_label_ != nullptr) {
        int rotation_dof = 0;
        if (initial_.kind != zima::document::ConstructionKind::Point) {
            const auto orientation_references = initial_.kind ==
                    zima::document::ConstructionKind::Plane
                ? orientation_references_.size()
                : static_cast<std::size_t>(std::count_if(references_.begin(),
                    references_.end(), [](const auto& reference) {
                        return reference.semantic_key.find("axis") != std::string::npos ||
                            reference.semantic_key.find("edge") != std::string::npos ||
                            reference.semantic_key.find("plane") != std::string::npos ||
                            reference.semantic_key.find("face") != std::string::npos;
                    }));
            rotation_dof = orientation_references == 0 ? 3
                : orientation_references == 1 ? 1 : 0;
        }
        const int total_dof = remaining_translation_dof_ + rotation_dof;
        dof_label_->setText(tr("Zbývající stupně volnosti: %1")
            .arg(total_dof));
        reference_status_->setText(total_dof == 0
            ? tr("Plně určené") : QString());
    }
}

void ConstructionPropertiesDialog::set_translation_constraint_state(
    const zima::document::PointConstraintState& state,
    const zima::kernel::Vec3& solution) {
    set_remaining_translation_dof(state.remaining_dof);
    const std::array values{solution.x, solution.y, solution.z};
    for (std::size_t index = 0; index < origin_.size(); ++index) {
        if (origin_[index] == nullptr) continue;
        origin_[index]->setEnabled(!state.constrained_axes[index]);
        if (state.constrained_axes[index]) {
            const QSignalBlocker blocker(origin_[index]);
            origin_[index]->setValue(values[index]);
        }
    }
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
    reference_buttons_.fill(nullptr);
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
            auto* reference = new QPushButton(
                QStringLiteral("%1. %2").arg(index + 1).arg(
                    index < reference_labels_.size()
                        ? reference_labels_[index]
                        : readable_reference_kind(
                            references_[index].semantic_key)),
                reference_table_);
            reference->setObjectName(
                QStringLiteral("constructionReferenceButton%1").arg(index));
            reference->setToolTip(tr("Vybrat nebo nahradit referenci ve 3D pohledu"));
            connect(reference, &QPushButton::clicked, this, [this, index] {
                if (reference_request_) reference_request_(index);
            });
            reference_buttons_[index] = reference;
            reference_table_->setCellWidget(static_cast<int>(index), 1, reference);
            auto* offset = new QDoubleSpinBox(reference_table_);
            offset->setRange(-1'000'000'000.0, 1'000'000'000.0);
            offset->setDecimals(3);
            offset->setSuffix(QStringLiteral(" mm"));
            offset->setValue(references_[index].offset);
            offset->setEnabled(references_[index].supports_offset);
            connect(offset, &QDoubleSpinBox::valueChanged, this,
                [this, index](double value) {
                    if (index < references_.size()) {
                        references_[index].offset = value;
                        notify_preview();
                    }
                });
            reference_table_->setCellWidget(static_cast<int>(index), 2, offset);
        } else {
            auto* reference = new QPushButton(
                QStringLiteral("%1. %2").arg(index + 1).arg(tr("Vybrat referenci")),
                reference_table_);
            reference->setObjectName(
                QStringLiteral("constructionReferenceButton%1").arg(index));
            reference->setToolTip(tr("Vybrat referenci ve 3D pohledu"));
            connect(reference, &QPushButton::clicked, this, [this, index] {
                if (reference_request_) reference_request_(index);
            });
            reference_buttons_[index] = reference;
            reference_table_->setCellWidget(static_cast<int>(index), 1, reference);
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
        set_remaining_translation_dof(remaining_translation_dof_);
    }
}

void ConstructionPropertiesDialog::refresh_orientation_table() {
    if (orientation_table_ == nullptr) return;
    for (std::size_t index = 0; index < 2; ++index) {
        if (index < orientation_references_.size() &&
            !orientation_references_[index].owner_id.empty()) {
            auto* remove = new QPushButton(QStringLiteral("×"), orientation_table_);
            remove->setFixedSize(30, 30);
            remove->setStyleSheet(
                "QPushButton{color:white;background:#8b2424;border:1px solid #b94a4a;"
                "border-radius:4px;font-weight:700}");
            connect(remove, &QPushButton::clicked, this, [this, index] {
                orientation_references_.erase(orientation_references_.begin() +
                    static_cast<std::ptrdiff_t>(index));
                if (index < orientation_labels_.size())
                    orientation_labels_.erase(orientation_labels_.begin() +
                        static_cast<std::ptrdiff_t>(index));
                for (std::size_t role = 0; role < orientation_references_.size(); ++role)
                    orientation_references_[role].orientation_role =
                        role == 0 ? "front" : "top";
                refresh_orientation_table();
                notify_preview();
            });
            orientation_table_->setCellWidget(static_cast<int>(index), 0, remove);
        } else {
            orientation_table_->setCellWidget(static_cast<int>(index), 0, nullptr);
        }
        auto* select = new QPushButton(
            index < orientation_labels_.size() && !orientation_labels_[index].isEmpty()
                ? orientation_labels_[index] : tr("Vybrat orientační referenci"),
            orientation_table_);
        select->setObjectName(
            QStringLiteral("constructionOrientationReference%1").arg(index));
        connect(select, &QPushButton::clicked, this, [this, index] {
            if (reference_request_) reference_request_(index + 3);
        });
        orientation_table_->setCellWidget(static_cast<int>(index), 1, select);
        auto* role = new QComboBox(orientation_table_);
        role->addItem(QStringLiteral("FRONT"), QStringLiteral("front"));
        role->addItem(QStringLiteral("TOP"), QStringLiteral("top"));
        const auto stored_role = index < orientation_references_.size()
            ? QString::fromStdString(orientation_references_[index].orientation_role)
            : (index == 0 ? QStringLiteral("front") : QStringLiteral("top"));
        role->setCurrentIndex(std::max(0, role->findData(stored_role)));
        connect(role, &QComboBox::currentIndexChanged, this, [this, index, role] {
            if (index < orientation_references_.size()) {
                orientation_references_[index].orientation_role =
                    role->currentData().toString().toStdString();
                notify_preview();
            }
        });
        orientation_roles_[index] = role;
        orientation_table_->setCellWidget(static_cast<int>(index), 2, role);
    }
    set_remaining_translation_dof(remaining_translation_dof_);
}

void ConstructionPropertiesDialog::remove_reference(std::size_t index) {
    if (index >= references_.size()) return;
    references_.erase(references_.begin() + static_cast<std::ptrdiff_t>(index));
    if (index < reference_labels_.size()) {
        reference_labels_.erase(reference_labels_.begin() +
            static_cast<std::ptrdiff_t>(index));
    }
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
    if (direction_combo_ != nullptr) {
        const auto key = direction_combo_->currentData().toString();
        zima::kernel::Vec3 local = key == QStringLiteral("x") ||
                key == QStringLiteral("yz")
            ? zima::kernel::Vec3{1.0, 0.0, 0.0}
            : key == QStringLiteral("y") || key == QStringLiteral("xz")
                ? zima::kernel::Vec3{0.0, 1.0, 0.0}
                : zima::kernel::Vec3{0.0, 0.0, 1.0};
        constexpr double radians = std::numbers::pi / 180.0;
        const double cx = std::cos(value.rotation.x * radians);
        const double sx = std::sin(value.rotation.x * radians);
        const double cy = std::cos(value.rotation.y * radians);
        const double sy = std::sin(value.rotation.y * radians);
        const double cz = std::cos(value.rotation.z * radians);
        const double sz = std::sin(value.rotation.z * radians);
        local = {local.x, cx * local.y - sx * local.z,
            sx * local.y + cx * local.z};
        local = {cy * local.x + sy * local.z, local.y,
            -sy * local.x + cy * local.z};
        value.direction = {cz * local.x - sz * local.y,
            sz * local.x + cz * local.y, local.z};
    }
    if (display_size_ != nullptr) value.display_size = display_size_->value();
    value.definition = current_definition();
    const std::size_t required =
            value.definition == zima::document::ConstructionDefinition::PointReference
        ? references_.size()
        : value.definition ==
            zima::document::ConstructionDefinition::TwoPointAxis ? 2
        : value.definition == zima::document::ConstructionDefinition::ThreePointPlane ? 3
        : value.definition == zima::document::ConstructionDefinition::Absolute ? 0 : 1;
    value.references.assign(references_.begin(),
        references_.begin() + static_cast<std::ptrdiff_t>(
            std::min(required, references_.size())));
    value.references.insert(value.references.end(),
        orientation_references_.begin(), orientation_references_.end());
    value.offset = offset_->value();
    return value;
}

void ConstructionPropertiesDialog::notify_preview() {
    if (preview_) preview_(current_value());
}

bool ConstructionPropertiesDialog::submit() {
    auto value = current_value();
    if (direction_combo_ != nullptr) {
        const double length = std::sqrt(value.direction.x * value.direction.x +
                                        value.direction.y * value.direction.y +
                                        value.direction.z * value.direction.z);
        if (length <= 1e-12) {
            error_->setText(tr("Směr nesmí být nulový."));
            return false;
        }
    }
    if (display_size_ != nullptr) value.display_size = display_size_->value();
    const std::size_t required =
            value.definition == zima::document::ConstructionDefinition::PointReference
        ? std::max<std::size_t>(1, references_.size())
        : value.definition ==
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
    value.references.insert(value.references.end(),
        orientation_references_.begin(), orientation_references_.end());
    value.offset = offset_->value();
    if (value.name.empty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        return false;
    }
    commit_(std::move(value));
    return true;
}

}  // namespace zima::app
