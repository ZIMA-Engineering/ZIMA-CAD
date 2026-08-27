#include "construction_properties_dialog.hpp"

#include <zima/ui/reference_cell.hpp>

#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
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
    if (initial.kind == zima::document::ConstructionKind::Point)
        remaining_rotation_dof_ = 0;
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(460);
    auto compact_font = font();
    compact_font.setPixelSize(10);
    setFont(compact_font);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(initial.name), this);
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
    offset_ = field(initial.offset, "constructionOffset", " mm");
    if (initial.kind != zima::document::ConstructionKind::Plane) {
        offset_->hide();
    }
    connect(definition_, &QComboBox::currentIndexChanged,
        this, [this] { refresh_definition_fields(); });
    refresh_definition_fields();
    if (initial.kind != zima::document::ConstructionKind::Point) {
        display_size_ = field(initial.display_size, "constructionDisplaySize", " mm");
        display_size_->setRange(0.001, 1'000'000.0);
    }
    content_layout()->addLayout(form);
    placement_ = std::make_unique<zima::ui::ContainerPlacementSection>(
        this, content_layout(),
        /*with_orientation=*/true,
        /*position_rows_can_define_rotation=*/
            initial.kind != zima::document::ConstructionKind::Point);
    zima::document::Placement numeric;
    numeric.x = initial.origin.x; numeric.y = initial.origin.y; numeric.z = initial.origin.z;
    const auto initial_absolute = initial.absolute_rotation;
    numeric.rotation_x = initial_absolute.x;
    numeric.rotation_y = initial_absolute.y;
    numeric.rotation_z = initial_absolute.z;
    numeric.absolute_rotation_x = initial_absolute.x;
    numeric.absolute_rotation_y = initial_absolute.y;
    numeric.absolute_rotation_z = initial_absolute.z;
    numeric.orientation_back = initial.orientation_back;
    numeric.orientation_quarter_turns = initial.orientation_quarter_turns;
    numeric.rotation_offset_x = initial.rotation_offset_x;
    numeric.rotation_offset_y = initial.rotation_offset_y;
    numeric.rotation_offset_z = initial.rotation_offset_z;
    placement_->initialize_numeric_values(numeric);
    const bool has_orientation_reference = std::any_of(
        initial.references.begin(), initial.references.end(),
        [](const auto& reference) { return reference.orientation_drives_rotation; });
    placement_->set_orientation_base_rotation(
        has_orientation_reference ? initial.rotation_base : initial.rotation,
        has_orientation_reference);
    origin_ = placement_->translation_fields();
    rotation_ = placement_->rotation_fields();
    rotation_offset_ = placement_->rotation_offset_fields();
    origin_[0]->setObjectName("constructionX");
    origin_[1]->setObjectName("constructionY");
    origin_[2]->setObjectName("constructionZ");
    rotation_[0]->setObjectName("constructionRotationX");
    rotation_[1]->setObjectName("constructionRotationY");
    rotation_[2]->setObjectName("constructionRotationZ");
    rotation_offset_[0]->setObjectName("constructionRotationOffsetX");
    rotation_offset_[1]->setObjectName("constructionRotationOffsetY");
    rotation_offset_[2]->setObjectName("constructionRotationOffsetZ");
    placement_->initialize_from_references(initial.references,
        [](const std::string& semantic) { return readable_reference_kind(semantic); });
    reference_status_ = placement_->reference_status_label();
    dof_label_ = placement_->dof_label();
    placement_->reference_table()->setObjectName("constructionReferenceTable");
    if (placement_->orientation_table() != nullptr) {
        placement_->orientation_table()->setObjectName("constructionOrientationTable");
    }
    placement_->set_reference_request_callback(
        [this](std::size_t index) {
            if (reference_request_) reference_request_(index);
        });
    placement_->set_changed_callback([this] {
        refresh_offset_enabled_state();
        notify_preview();
    });
    placement_->set_highlights_changed_callback(
        [this] { if (reference_highlights_changed_) reference_highlights_changed_(); });
    placement_->refresh_reference_table();
    if (initial.kind == zima::document::ConstructionKind::Plane) {
        placement_->refresh_orientation_table();
    }
    const bool is_point = initial.kind == zima::document::ConstructionKind::Point;
    auto* rotation_form = new QFormLayout;
    if (!is_point) {
        direction_combo_ = new QComboBox(this);
        direction_combo_->setObjectName("constructionDirection");
        if (initial.kind == zima::document::ConstructionKind::Axis) {
            direction_combo_->addItem(QStringLiteral("X"), QStringLiteral("x"));
            direction_combo_->addItem(QStringLiteral("Y"), QStringLiteral("y"));
            direction_combo_->addItem(QStringLiteral("Z"), QStringLiteral("z"));
            const auto index = direction_combo_->findData(
                QString::fromStdString(initial.direction_axis));
            direction_combo_->setCurrentIndex(index >= 0 ? index : 1);
            rotation_form->addRow(tr("Směr"), direction_combo_);
        } else {
            direction_combo_->addItem(QStringLiteral("XY"), QStringLiteral("xy"));
            direction_combo_->addItem(QStringLiteral("YZ"), QStringLiteral("yz"));
            direction_combo_->addItem(QStringLiteral("XZ"), QStringLiteral("xz"));
            direction_combo_->hide();
        }
        rotation_form->addRow(initial.kind == zima::document::ConstructionKind::Axis
                ? tr("Délka zobrazení") : tr("Velikost zobrazení"),
            display_size_);
    }
    content_layout()->addLayout(rotation_form);

    placement_->install_dof_label(content_layout());
    if (initial.kind == zima::document::ConstructionKind::Plane) {
        // "Work plane offset" is only meaningful once the user has chosen
        // what the Plane is parallel to / anchored on, so keep it at the
        // very bottom of the Plane dialog, right before the OK/Cancel row,
        // instead of splitting it away from the reference/orientation
        // controls at the top.
        auto* offset_form = new QFormLayout;
        base_plane_combo_ = new QComboBox(this);
        base_plane_combo_->setObjectName("constructionBasePlane");
        base_plane_combo_->addItem(
            tr("Počátek kontejneru — Rovina XY"), QStringLiteral("xy"));
        base_plane_combo_->addItem(
            tr("Počátek kontejneru — Rovina YZ"), QStringLiteral("yz"));
        base_plane_combo_->addItem(
            tr("Počátek kontejneru — Rovina XZ"), QStringLiteral("xz"));
        const auto base_plane_key = initial.base_plane ==
                zima::document::LocalDatumPlane::XY ? QStringLiteral("xy")
            : initial.base_plane == zima::document::LocalDatumPlane::XZ
                ? QStringLiteral("xz") : QStringLiteral("yz");
        base_plane_combo_->setCurrentIndex(
            base_plane_combo_->findData(base_plane_key));
        base_plane_combo_->setToolTip(tr(
            "Rovina XY, YZ nebo XZ lokálního počátku kontejneru, se kterou "
            "bude výsledná rovina rovnoběžná."));
        offset_form->addRow(tr("Výchozí rovina"), base_plane_combo_);
        offset_form->addRow(tr("Odsazení roviny"), offset_);
        content_layout()->addLayout(offset_form);
    }
    refresh_offset_enabled_state();

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
            this, [this] {
                if (updating_rotation_fields_) return;
                notify_preview();
            });
    }
    for (auto* input : rotation_offset_) {
        if (input != nullptr) connect(input, &QDoubleSpinBox::valueChanged,
            this, [this] { notify_preview(); });
    }
    if (display_size_ != nullptr) connect(display_size_,
        &QDoubleSpinBox::valueChanged, this, [this] { notify_preview(); });
    if (direction_combo_ != nullptr) connect(direction_combo_,
        &QComboBox::currentIndexChanged, this, [this] { notify_preview(); });
    if (base_plane_combo_ != nullptr) connect(base_plane_combo_,
        &QComboBox::currentIndexChanged, this, [this] { notify_preview(); });
    connect(offset_, &QDoubleSpinBox::valueChanged,
        this, [this] { notify_preview(); });
}

void ConstructionPropertiesDialog::set_reference_request_callback(
    ReferenceRequestCallback callback) {
    reference_request_ = std::move(callback);
}

void ConstructionPropertiesDialog::set_reference_highlights_changed_callback(
    ReferenceHighlightsChangedCallback callback) {
    reference_highlights_changed_ = std::move(callback);
}

std::set<std::string>
ConstructionPropertiesDialog::highlighted_reference_owner_ids() const {
    return placement_->highlighted_reference_owner_ids();
}

std::vector<zima::document::ConstructionReference>
ConstructionPropertiesDialog::highlighted_reference_entries() const {
    return placement_->highlighted_reference_entries();
}

void ConstructionPropertiesDialog::set_preview_callback(PreviewCallback callback) {
    preview_ = std::move(callback);
    notify_preview();
}

bool ConstructionPropertiesDialog::set_reference(std::size_t index,
    zima::document::ConstructionReference reference, const QString& label,
    zima::document::ConstructionDefinition definition) {
    QString error;
    // A new Plane's position rows keep the same automatically assigned
    // front/top flags every other container kind gets (see
    // assign_automatic_orientation_role() in assembly_workspace_window.cpp):
    // row 0 is FRONT -- the plane the new Plane is offset from -- and row 1
    // is TOP; row 2 never drives orientation. PartDocument::resolve_construction()
    // reads row 0's role to inherit that reference's own full plane frame
    // (parallel-copy + offset) whenever it resolves to a real plane, exactly
    // matching the "FRONT rovina je ta, od které se odsadí skutečná rovina"
    // contract; a non-planar row 0 (axis/point) falls back to the generic
    // FRONT/TOP composition shared with Point/Axis.
    if (!placement_->set_reference(index, std::move(reference), label, &error)) {
        if (!error.isEmpty()) error_->setText(error);
        return false;
    }
    error_->clear();
    if (index < 3) {
        definition_->setCurrentIndex(definition_->findData(static_cast<int>(definition)));
    }
    return true;
}

bool ConstructionPropertiesDialog::set_reference(std::size_t index,
    zima::document::ConstructionReference reference, const QString& label) {
    const auto definition = current_definition() ==
            zima::document::ConstructionDefinition::Absolute && index < 3
        ? zima::document::ConstructionDefinition::PointReference
        : current_definition();
    return set_reference(index, std::move(reference), label, definition);
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

bool ConstructionPropertiesDialog::owns_reference_owner(
    const std::string& owner_id) const {
    return owner_id == initial_.id || owner_id == initial_.entity_id ||
        owner_id == initial_.container_origin.id;
}

std::vector<zima::document::ConstructionReference>
ConstructionPropertiesDialog::references_without(std::size_t index) const {
    return placement_->references_without(index);
}

std::size_t ConstructionPropertiesDialog::first_empty_position_index() const {
    return placement_->first_empty_position_index();
}

std::vector<zima::document::ConstructionReference>
ConstructionPropertiesDialog::populated_references() const {
    return placement_->populated_references();
}

void ConstructionPropertiesDialog::set_orientation_base_rotation(
    const zima::kernel::Vec3& base_rotation, bool has_orientation_references) {
    if (updating_rotation_fields_) return;
    has_orientation_base_rotation_ = has_orientation_references;
    updating_rotation_fields_ = true;
    placement_->set_orientation_base_rotation(
        base_rotation, has_orientation_references);
    updating_rotation_fields_ = false;
}

void ConstructionPropertiesDialog::set_orientation_inherited_from_reference(
        bool inherited) {
    if (placement_ == nullptr) return;
    placement_->set_orientation_locked(inherited,
        inherited ? placement_->first_position_reference_label() : QString());
}

void ConstructionPropertiesDialog::set_remaining_translation_dof(int dof) {
    placement_->set_remaining_translation_dof(dof);
    remaining_translation_dof_ = placement_->remaining_translation_dof();
}

void ConstructionPropertiesDialog::set_remaining_rotation_dof(int dof) {
    placement_->set_remaining_rotation_dof(dof);
    remaining_rotation_dof_ = placement_->remaining_rotation_dof();
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
    offset_->setVisible(
        initial_.kind == zima::document::ConstructionKind::Plane);
    refresh_offset_enabled_state();
    if (placement_) placement_->refresh_reference_table();
    notify_preview();
}

void ConstructionPropertiesDialog::refresh_offset_enabled_state() {
    if (offset_ == nullptr) return;
    if (initial_.kind != zima::document::ConstructionKind::Plane) {
        offset_->setEnabled(false);
        return;
    }
    // A Plane's work-plane offset always has a well-defined direction to
    // move along: resolve_construction() falls back to the identity normal
    // (+X) whenever no orientation-driving reference is present, exactly
    // like the un-referenced X/Y/Z origin fields above. So the offset field
    // must stay editable even with zero position references -- disabling it
    // until a reference exists was a leftover restriction with no actual
    // mathematical dependency behind it.
    offset_->setEnabled(true);
}

zima::document::ConstructionObject ConstructionPropertiesDialog::current_value() const {
    auto value = initial_;
    value.name = name_->text().trimmed().toStdString();
    value.origin = {origin_[0]->value(), origin_[1]->value(), origin_[2]->value()};
    if (rotation_[0] != nullptr) {
        value.absolute_rotation = {rotation_[0]->value(), rotation_[1]->value(),
                                   rotation_[2]->value()};
        value.rotation = value.absolute_rotation;
    }
    const auto numeric = placement_->numeric_placement();
    value.orientation_back = numeric.orientation_back;
    value.orientation_quarter_turns = numeric.orientation_quarter_turns;
    if (rotation_offset_[0] != nullptr) {
        value.rotation_offset_x = rotation_offset_[0]->value();
        value.rotation_offset_y = rotation_offset_[1]->value();
        value.rotation_offset_z = rotation_offset_[2]->value();
    }
    if (direction_combo_ != nullptr) {
        const auto key = direction_combo_->currentData().toString();
        if (initial_.kind == zima::document::ConstructionKind::Axis) {
            value.direction_axis = key.toStdString();
        }
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
    if (base_plane_combo_ != nullptr) {
        const auto key = base_plane_combo_->currentData().toString();
        value.base_plane = key == QStringLiteral("xy")
            ? zima::document::LocalDatumPlane::XY
            : key == QStringLiteral("xz")
                ? zima::document::LocalDatumPlane::XZ
                : zima::document::LocalDatumPlane::YZ;
    }
    value.definition = current_definition();
    const auto populated = placement_->populated_references();
    const std::size_t required =
            value.definition == zima::document::ConstructionDefinition::PointReference
        ? populated.size()
        : value.definition ==
            zima::document::ConstructionDefinition::TwoPointAxis ? 2
        : value.definition == zima::document::ConstructionDefinition::ThreePointPlane ? 3
        : value.definition == zima::document::ConstructionDefinition::Absolute ? 0 : 1;
    value.references = placement_->combined_references(required);
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
    const auto populated = placement_->populated_references();
    const std::size_t required =
            value.definition == zima::document::ConstructionDefinition::PointReference
        ? populated.size()
        : value.definition ==
            zima::document::ConstructionDefinition::TwoPointAxis ? 2
        : value.definition == zima::document::ConstructionDefinition::ThreePointPlane ? 3
        : value.definition == zima::document::ConstructionDefinition::Absolute ? 0 : 1;
    if (populated.size() < required ||
        std::any_of(populated.begin(), populated.begin() +
                static_cast<std::ptrdiff_t>(required), [](const auto& reference) {
            return reference.owner_id.empty() || reference.semantic_key.empty();
        })) {
        error_->setText(tr("Vyberte všechny požadované reference."));
        return false;
    }
    value.references = placement_->combined_references(required);
    value.offset = offset_->value();
    if (value.name.empty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        return false;
    }
    commit_(std::move(value));
    return true;
}

}  // namespace zima::app
