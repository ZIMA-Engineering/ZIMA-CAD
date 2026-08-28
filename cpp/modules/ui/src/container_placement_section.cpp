#include "zima/ui/container_placement_section.hpp"

#include "zima/ui/reference_cell.hpp"

#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace zima::ui {
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

ContainerPlacementSection::ContainerPlacementSection(
    QWidget* parent_widget, QVBoxLayout* layout, bool with_orientation,
    bool position_rows_can_define_rotation, int decimal_places)
    : QObject(parent_widget), parent_widget_(parent_widget),
      with_orientation_(with_orientation),
      position_rows_can_define_rotation_(position_rows_can_define_rotation),
      decimal_places_(std::clamp(decimal_places, 0, 12)) {
    // A section without its own orientation table (e.g. Point) never has
    // set_remaining_rotation_dof() called on it by its owning dialog -- the
    // member's {3} default initializer would then permanently keep
    // refresh_reference_table()'s "(translation + rotation) > 0" check true
    // even once translation is fully constrained, wrongly offering another
    // reference row forever. Only sections that actually expose rotation
    // (with_orientation_) start with 3 DOF pending; others start at 0.
    if (!with_orientation_) remaining_rotation_dof_ = 0;
    reference_status_ = new QLabel(parent_widget_);
    reference_status_->setStyleSheet("color:#80AA1A;font-weight:700;");
    reference_status_->setWordWrap(true);
    layout->addWidget(reference_status_);

    auto* placement_heading = new QLabel(tr("Umístění kontejneru"), parent_widget_);
    auto heading_font = placement_heading->font();
    heading_font.setBold(true);
    placement_heading->setFont(heading_font);
    layout->addWidget(placement_heading);

    reference_table_ = new QTableWidget(0, 3, parent_widget_);
    reference_table_->setObjectName("containerPlacementReferenceTable");
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
    layout->addWidget(reference_table_);
    connect(reference_table_, &QTableWidget::cellClicked, this,
        [this](int row, int column) {
            if (column != 1 || row < 0 ||
                static_cast<std::size_t>(row) >= reference_items_.size())
                return;
            auto* item = reference_items_[static_cast<std::size_t>(row)];
            if (item == nullptr) return;
            if (!item->has_reference()) {
                if (reference_request_) reference_request_(static_cast<std::size_t>(row));
            } else {
                // A populated reference is a persistent value, not an on/off
                // control; clicking it only toggles its viewer highlight,
                // matching Python's `_reference_cell_clicked`.
                toggle_reference_highlight(static_cast<std::size_t>(row));
            }
            reference_table_->clearSelection();
        });

    if (with_orientation_) {
        orientation_table_ = new QTableWidget(2, 4, parent_widget_);
        orientation_table_->setObjectName("containerPlacementOrientationTable");
        orientation_table_->setHorizontalHeaderLabels(
            {QString(), tr("Reference"), tr("Rovina kontejneru"), tr("Obrátit")});
        orientation_table_->horizontalHeader()->setSectionResizeMode(
            0, QHeaderView::ResizeToContents);
        orientation_table_->horizontalHeader()->setSectionResizeMode(
            1, QHeaderView::Stretch);
        orientation_table_->horizontalHeader()->setSectionResizeMode(
            2, QHeaderView::ResizeToContents);
        orientation_table_->horizontalHeader()->setSectionResizeMode(
            3, QHeaderView::ResizeToContents);
        orientation_table_->verticalHeader()->setDefaultSectionSize(34);
        orientation_table_->setFixedHeight(
            orientation_table_->horizontalHeader()->sizeHint().height() + 2 * 34 +
            orientation_table_->frameWidth() * 2);
        connect(orientation_table_, &QTableWidget::cellClicked, this,
            [this](int row, int column) {
                if (column != 1 || row < 0 ||
                    static_cast<std::size_t>(row) >= orientation_items_.size())
                    return;
                auto* item = orientation_items_[static_cast<std::size_t>(row)];
                if (item == nullptr) return;
                if (!item->has_reference()) {
                    if (reference_request_)
                        reference_request_(static_cast<std::size_t>(row) + 3);
                } else {
                    toggle_orientation_highlight(static_cast<std::size_t>(row));
                }
                orientation_table_->clearSelection();
            });
        // Kept as an internal value adapter while old call sites are removed;
        // it is deliberately not presented to the user anymore.
        orientation_table_->hide();
    }

    const auto field = [this](bool angular, const char* object_name) {
        auto* input = new QDoubleSpinBox(parent_widget_);
        input->setRange(angular ? -360'000.0 : -1'000'000.0,
                        angular ? 360'000.0 : 1'000'000.0);
        input->setDecimals(decimal_places_);
        input->setSingleStep(angular ? 5.0 : 1.0);
        input->setSuffix(angular ? tr(" deg") : tr(" mm"));
        input->setObjectName(object_name);
        connect(input, &QDoubleSpinBox::valueChanged, this,
            [this] { notify_changed(); });
        return input;
    };
    translation_ = {field(false, "containerPlacementX"),
                    field(false, "containerPlacementY"),
                    field(false, "containerPlacementZ")};
    auto* coordinates = new QFormLayout;
    coordinates->addRow(tr("X"), translation_[0]);
    coordinates->addRow(tr("Y"), translation_[1]);
    coordinates->addRow(tr("Z"), translation_[2]);
    layout->addLayout(coordinates);

    if (with_orientation_) {
        rotation_ = {field(true, "containerRotationX"),
                     field(true, "containerRotationY"),
                     field(true, "containerRotationZ")};
        rotation_offset_ = {field(true, "containerRotationOffsetX"),
                            field(true, "containerRotationOffsetY"),
                            field(true, "containerRotationOffsetZ")};
        auto* rotation_form = new QFormLayout;
        auto* header = new QWidget(parent_widget_);
        auto* header_layout = new QHBoxLayout(header);
        header_layout->setContentsMargins(0, 0, 0, 0);
        header_layout->addWidget(new QLabel(tr("Absolutní"), parent_widget_));
        header_layout->addWidget(new QLabel(tr("Korekce"), parent_widget_));
        rotation_form->addRow(QString(), header);
        for (std::size_t index = 0; index < 3; ++index) {
            auto* row = new QWidget(parent_widget_);
            auto* row_layout = new QHBoxLayout(row);
            row_layout->setContentsMargins(0, 0, 0, 0);
            row_layout->addWidget(rotation_[index]);
            row_layout->addWidget(rotation_offset_[index]);
            rotation_form->addRow(index == 0 ? tr("RX") : index == 1 ? tr("RY") : tr("RZ"), row);
        }
        layout->addLayout(rotation_form);
    }

    dof_label_ = new QLabel(parent_widget_);
    dof_label_->setObjectName("containerPlacementDofLabel");
    dof_label_->setWordWrap(true);
    dof_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    // A child QWidget without a layout position is still painted by Qt at
    // its default geometry. Keep it hidden until install_dof_label() places
    // it, otherwise a caller that forgets installation gets a floating,
    // clipped "Zbývající stupně volnosti" label over the dialog.
    dof_label_->hide();
}

void ContainerPlacementSection::initialize_numeric_values(
        const zima::document::Placement& placement) {
    const std::array position{placement.x, placement.y, placement.z};
    const bool has_orientation_reference = std::any_of(
        placement.references.begin(), placement.references.end(),
        [](const auto& reference) { return reference.orientation_drives_rotation; });
    const std::array rotation{
        !has_orientation_reference ? placement.absolute_rotation_x : placement.rotation_x,
        !has_orientation_reference ? placement.absolute_rotation_y : placement.rotation_y,
        !has_orientation_reference ? placement.absolute_rotation_z : placement.rotation_z};
    const std::array correction{placement.rotation_offset_x,
        placement.rotation_offset_y, placement.rotation_offset_z};
    for (std::size_t i = 0; i < 3; ++i) {
        translation_[i]->setValue(position[i]);
        if (rotation_[i]) rotation_[i]->setValue(rotation[i]);
        if (rotation_offset_[i]) rotation_offset_[i]->setValue(correction[i]);
    }
    orientation_back_ = placement.orientation_back;
    orientation_quarter_turns_ =
        ((placement.orientation_quarter_turns % 4) + 4) % 4;
}

zima::document::Placement ContainerPlacementSection::numeric_placement() const {
    zima::document::Placement result;
    result.x = translation_[0]->value(); result.y = translation_[1]->value();
    result.z = translation_[2]->value();
    if (rotation_[0]) {
        result.rotation_x = rotation_[0]->value();
        result.rotation_y = rotation_[1]->value();
        result.rotation_z = rotation_[2]->value();
        result.rotation_offset_x = rotation_offset_[0]->value();
        result.rotation_offset_y = rotation_offset_[1]->value();
        result.rotation_offset_z = rotation_offset_[2]->value();
        result.absolute_rotation_x = rotation_[0]->value();
        result.absolute_rotation_y = rotation_[1]->value();
        result.absolute_rotation_z = rotation_[2]->value();
        result.orientation_back = orientation_back_;
        result.orientation_quarter_turns = orientation_quarter_turns_;
    }
    return result;
}

void ContainerPlacementSection::set_translation_constraint_state(
        const zima::document::PointConstraintState& state,
        const zima::kernel::Vec3& solution) {
    set_remaining_translation_dof(state.remaining_dof);
    const std::array values{solution.x, solution.y, solution.z};
    for (std::size_t i = 0; i < 3; ++i) {
        translation_[i]->setEnabled(!state.constrained_axes[i]);
        if (state.constrained_axes[i]) {
            const QSignalBlocker blocker(translation_[i]);
            translation_[i]->setValue(values[i]);
        }
    }
}

void ContainerPlacementSection::set_orientation_base_rotation(
        const zima::kernel::Vec3& value, bool constrained) {
    const std::array values{value.x, value.y, value.z};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!rotation_[i]) continue;
        const QSignalBlocker blocker(rotation_[i]);
        rotation_[i]->setValue(values[i]);
        rotation_[i]->setEnabled(!constrained);
        if (rotation_offset_[i]) {
            rotation_offset_[i]->setEnabled(constrained);
            if (!constrained) {
                const QSignalBlocker offset_blocker(rotation_offset_[i]);
                rotation_offset_[i]->setValue(0.0);
            }
        }
    }
}

void ContainerPlacementSection::install_dof_label(QVBoxLayout* layout) {
    if (dof_label_ == nullptr || layout == nullptr) return;
    if (layout->indexOf(dof_label_) < 0) layout->addWidget(dof_label_);
    dof_label_->show();
}

void ContainerPlacementSection::set_orientation_locked(
        bool locked, const QString& source_label) {
    if (!with_orientation_) return;
    orientation_inherited_ = locked;
    orientation_inherited_label_ = source_label;
    refresh_orientation_table();
}

QString ContainerPlacementSection::first_position_reference_label() const {
    if (reference_items_[0] == nullptr) return {};
    const bool populated = !references_.empty() &&
        !(references_[0].owner_id.empty() && references_[0].semantic_key.empty());
    if (!populated) return {};
    const QString text = reference_items_[0]->text();
    const auto separator = text.indexOf(QStringLiteral(". "));
    return separator >= 0 ? text.mid(separator + 2) : text;
}

void ContainerPlacementSection::set_reference_request_callback(
    ReferenceRequestCallback callback) {
    reference_request_ = std::move(callback);
}

void ContainerPlacementSection::set_changed_callback(ChangedCallback callback) {
    changed_ = std::move(callback);
}

void ContainerPlacementSection::set_highlights_changed_callback(
    HighlightsChangedCallback callback) {
    highlights_changed_ = std::move(callback);
}

void ContainerPlacementSection::initialize_from_references(
    const std::vector<zima::document::ConstructionReference>& references,
    const std::function<QString(const std::string&)>& label_for_semantic) {
    // `orientation_drives_rotation` alone does NOT mean "belongs in the
    // FRONT/TOP table": Point/Axis position references can drive rotation
    // while still remaining ordinary placement rows. Only the dedicated
    // orientation-table entries carry `orientation_only == true`; route by
    // that field exclusively so reopened dialogs keep every positional row
    // in "Umístění kontejneru" and only the user-picked FRONT/TOP entries
    // in "Orientace kontejneru". FRONT and TOP are fixed semantic slots;
    // never compact them, because a document may intentionally contain TOP
    // without an explicit FRONT override.
    std::vector<zima::document::ConstructionReference> orientation_candidates(2);
    std::vector<QString> orientation_candidate_labels(2);
    for (const auto& reference : references) {
        const auto label = label_for_semantic
            ? label_for_semantic(reference.semantic_key)
            : readable_reference_kind(reference.semantic_key);
        // Route purely by orientation_only now (see the field's doc comment
        // in part_document.hpp): a position-table copy of a Plane/Axis
        // front/top reference has orientation_drives_rotation == true but
        // orientation_only == false, and must load back into the position
        // table like any other row -- only the dedicated, orientation-only
        // twin belongs in orientation_candidates. Each entry now carries
        // its own correct orientation_only flag, so no further pairing
        // reconstruction is needed (unlike before this field existed).
        if (with_orientation_ && reference.orientation_only) {
            const bool secondary = reference.orientation_role == "top" ||
                reference.orientation_role == "bottom" ||
                reference.orientation_role == "left" ||
                reference.orientation_role == "right";
            const std::size_t slot = secondary ? 1 : 0;
            orientation_candidates[slot] = reference;
            orientation_candidate_labels[slot] = label;
        } else {
            references_.push_back(reference);
            reference_labels_.push_back(label);
        }
    }
    orientation_references_ = std::move(orientation_candidates);
    orientation_labels_ = std::move(orientation_candidate_labels);
}

bool ContainerPlacementSection::set_reference(std::size_t index,
    zima::document::ConstructionReference reference, const QString& label,
    QString* error_text) {
    const auto duplicate = [&](const auto& existing) {
        return existing.instance_path == reference.instance_path &&
            existing.owner_id == reference.owner_id &&
            existing.semantic_key == reference.semantic_key;
    };
    if (index >= 3) {
        const auto orientation_index = index - 3;
        if (!with_orientation_ || orientation_index >= 2) return false;
        if (std::any_of(orientation_references_.begin(),
                orientation_references_.end(), duplicate)) {
            if (error_text) *error_text = tr("Stejnou referenci nelze zadat vícekrát.");
            return false;
        }
        reference.orientation_drives_rotation = true;
        reference.orientation_role = orientation_index == 0 ? "front" : "top";
        reference.orientation_only = true;
        if (orientation_references_.size() <= orientation_index)
            orientation_references_.resize(orientation_index + 1);
        if (orientation_labels_.size() <= orientation_index)
            orientation_labels_.resize(orientation_index + 1);
        orientation_references_[orientation_index] = std::move(reference);
        orientation_labels_[orientation_index] = label;
        if (error_text) error_text->clear();
        refresh_orientation_table();
        notify_changed();
        return true;
    }
    for (std::size_t existing_index = 0;
         existing_index < references_.size(); ++existing_index) {
        if (existing_index != index && duplicate(references_[existing_index])) {
            if (error_text) *error_text = tr("Stejnou referenci nelze zadat vícekrát.");
            return false;
        }
    }
    if (error_text) error_text->clear();
    if (references_.size() <= index) references_.resize(index + 1);
    if (reference_labels_.size() <= index) reference_labels_.resize(index + 1);
    references_[index] = std::move(reference);
    reference_labels_[index] = label.trimmed().isEmpty()
        ? readable_reference_kind(references_[index].semantic_key) : label;
    // Plane properties mirror the first two planar placement references
    // into the independent container-orientation slots, matching the
    // reference implementation's _record_automatic_container_orientation().
    // The mirrored descriptors keep the same stable source geometry but are
    // persisted as orientation-only mappings, so position and rotation
    // remain separate concerns and either mapping can later be replaced.
    if (with_orientation_ && references_[index].supports_offset) {
        if (orientation_references_.size() < 2) orientation_references_.resize(2);
        if (orientation_labels_.size() < 2) orientation_labels_.resize(2);
        const auto same_source = [&](const auto& existing) {
            return !existing.owner_id.empty() && duplicate(existing);
        };
        const bool already_mirrored = std::any_of(
            orientation_references_.begin(), orientation_references_.end(),
            same_source);
        if (!already_mirrored) {
            const auto empty = std::find_if(orientation_references_.begin(),
                orientation_references_.end(), [](const auto& existing) {
                    return existing.owner_id.empty() && existing.semantic_key.empty();
                });
            if (empty != orientation_references_.end()) {
                const auto slot = static_cast<std::size_t>(std::distance(
                    orientation_references_.begin(), empty));
                *empty = references_[index];
                empty->orientation_drives_rotation = true;
                empty->orientation_role = slot == 0 ? "front" : "top";
                empty->orientation_only = true;
                // In the identity frame the XZ plane's geometric normal is
                // -Y (X cross Z). FRONT is local +Y, so an automatically
                // mirrored XZ datum used as FRONT must be inverted. This is
                // what keeps X→X, Y→Y and Z→Z after whole-Origin bulk-fill.
                if (slot == 0 &&
                    references_[index].semantic_key.ends_with("plane:xz")) {
                    empty->flip = !empty->flip;
                }
                orientation_labels_[slot] = reference_labels_[index];
                refresh_orientation_table();
            }
        }
    }
    refresh_reference_table();
    notify_changed();
    // Removing a populated row is itself an explicit request to replace
    // that reference. notify_changed() may rebuild the shared View and thus
    // clears its command filter; arm the same row afterwards so hover works
    // immediately for the replacement pick.
    if (reference_request_) reference_request_(index);
    return true;
}

void ContainerPlacementSection::set_remaining_translation_dof(int dof) {
    remaining_translation_dof_ = std::clamp(dof, 0, 3);
    if (dof_label_ != nullptr) {
        const int total_dof = remaining_translation_dof_ + remaining_rotation_dof_;
        dof_label_->setText(tr("Zbývající stupně volnosti: %1").arg(total_dof));
        if (reference_status_ != nullptr) {
            reference_status_->setText(total_dof == 0 ? tr("Plně určené") : QString());
        }
    }
    // Always rebuild the row visibility, not just when translation itself
    // changed: set_remaining_rotation_dof() below re-enters this same
    // setter with an unchanged translation value right after mutating
    // remaining_rotation_dof_, so any "did dof change" comparison here
    // races against that mutation and can miss the update that needs to
    // hide/show the last reference row once the combined total reaches (or
    // leaves) zero. Rebuilding the table is cheap (a handful of rows).
    if (reference_table_ != nullptr) refresh_reference_table();
}

void ContainerPlacementSection::set_remaining_rotation_dof(int dof) {
    remaining_rotation_dof_ = std::clamp(dof, 0, 3);
    set_remaining_translation_dof(remaining_translation_dof_);
}

std::vector<zima::document::ConstructionReference>
ContainerPlacementSection::populated_references() const {
    std::vector<zima::document::ConstructionReference> result;
    for (const auto& reference : references_) {
        if (reference.owner_id.empty() && reference.semantic_key.empty()) continue;
        result.push_back(reference);
    }
    return result;
}

std::size_t ContainerPlacementSection::first_empty_position_index() const {
    for (std::size_t index = 0; index < 3; ++index) {
        if (index >= references_.size() ||
            (references_[index].owner_id.empty() &&
             references_[index].semantic_key.empty())) return index;
    }
    return 3;
}

std::vector<zima::document::ConstructionReference>
ContainerPlacementSection::combined_references(std::size_t required) const {
    const auto populated = populated_references();
    std::vector<zima::document::ConstructionReference> result(populated.begin(),
        populated.begin() + static_cast<std::ptrdiff_t>(
            std::min(required, populated.size())));
    for (const auto& reference : orientation_references_) {
        if (reference.owner_id.empty() && reference.semantic_key.empty()) continue;
        result.push_back(reference);
    }
    return result;
}

std::vector<zima::document::ConstructionReference>
ContainerPlacementSection::references_without(std::size_t index) const {
    auto result = populated_references();
    if (index < 3 && index < references_.size() &&
        !(references_[index].owner_id.empty() &&
          references_[index].semantic_key.empty())) {
        const auto removed = std::find(result.begin(), result.end(),
            references_[index]);
        if (removed != result.end()) result.erase(removed);
    }
    if (index >= 3) {
        const auto orientation_index = index - 3;
        if (orientation_index < orientation_references_.size()) {
            auto orientations = orientation_references_;
            orientations.erase(orientations.begin() +
                static_cast<std::ptrdiff_t>(orientation_index));
            for (const auto& reference : orientations) {
                if (reference.owner_id.empty() && reference.semantic_key.empty()) continue;
                result.push_back(reference);
            }
            return result;
        }
    }
    for (const auto& reference : orientation_references_) {
        if (reference.owner_id.empty() && reference.semantic_key.empty()) continue;
        result.push_back(reference);
    }
    return result;
}

std::set<std::string> ContainerPlacementSection::highlighted_reference_owner_ids() const {
    std::set<std::string> owner_ids;
    for (const auto row : highlighted_reference_rows_) {
        if (row < references_.size()) owner_ids.insert(references_[row].owner_id);
    }
    for (const auto row : highlighted_orientation_rows_) {
        if (row < orientation_references_.size())
            owner_ids.insert(orientation_references_[row].owner_id);
    }
    return owner_ids;
}

std::vector<zima::document::ConstructionReference>
ContainerPlacementSection::highlighted_reference_entries() const {
    std::vector<zima::document::ConstructionReference> entries;
    for (const auto row : highlighted_reference_rows_) {
        if (row < references_.size()) entries.push_back(references_[row]);
    }
    for (const auto row : highlighted_orientation_rows_) {
        if (row < orientation_references_.size())
            entries.push_back(orientation_references_[row]);
    }
    return entries;
}

void ContainerPlacementSection::toggle_reference_highlight(std::size_t row) {
    if (highlighted_reference_rows_.count(row)) highlighted_reference_rows_.erase(row);
    else highlighted_reference_rows_.insert(row);
    update_reference_highlight_styles();
    if (highlights_changed_) highlights_changed_();
}

void ContainerPlacementSection::toggle_orientation_highlight(std::size_t row) {
    if (highlighted_orientation_rows_.count(row)) highlighted_orientation_rows_.erase(row);
    else highlighted_orientation_rows_.insert(row);
    update_reference_highlight_styles();
    if (highlights_changed_) highlights_changed_();
}

void ContainerPlacementSection::update_reference_highlight_styles() {
    const auto palette = parent_widget_->palette();
    for (std::size_t index = 0; index < reference_items_.size(); ++index) {
        auto* item = reference_items_[index];
        if (item == nullptr) continue;
        const bool highlighted = highlighted_reference_rows_.count(index) != 0;
        item->setBackground(highlighted ? QBrush(QColor("#00d1ff")) : QBrush());
        item->setForeground(highlighted
            ? QBrush(QColor("#102027"))
            : (item->has_reference() ? QBrush() : QBrush(palette.color(QPalette::Mid))));
    }
    for (std::size_t index = 0; index < orientation_items_.size(); ++index) {
        auto* item = orientation_items_[index];
        if (item == nullptr) continue;
        const bool highlighted = highlighted_orientation_rows_.count(index) != 0;
        item->setBackground(highlighted ? QBrush(QColor("#00d1ff")) : QBrush());
        item->setForeground(highlighted
            ? QBrush(QColor("#102027"))
            : (item->has_reference() ? QBrush() : QBrush(palette.color(QPalette::Mid))));
    }
}

void ContainerPlacementSection::notify_changed() {
    if (changed_) changed_();
}

void ContainerPlacementSection::remove_reference(std::size_t index) {
    if (index >= references_.size()) return;
    const auto removed = references_[index];
    // Empty the row in place instead of erase()-ing it: shifting every
    // later row up by one silently changes their meaning. Row order is
    // semantically significant -- 1st position reference = origin, 2nd =
    // direction, 3rd = plane-completing point (the "2 points define an
    // axis"/"3 points define a plane" history-order shortcut). Deleting
    // row 0 used to silently promote the old row 1 (a "direction" pick)
    // into row 0 (an "origin" pick) without the user ever choosing that.
    // It also desynchronised whatever row a bulk "Počátek" re-fill assumed
    // was empty, since the emptied slot no longer matched its own index.
    references_[index] = zima::document::ConstructionReference{};
    if (index < reference_labels_.size()) reference_labels_[index].clear();
    const auto matching_orientation = std::find_if(orientation_references_.begin(),
        orientation_references_.end(), [&](const auto& reference) {
            return reference.owner_id == removed.owner_id &&
                reference.semantic_key == removed.semantic_key &&
                reference.instance_path == removed.instance_path;
        });
    if (with_orientation_ && matching_orientation != orientation_references_.end()) {
        const auto orientation_index = static_cast<std::size_t>(std::distance(
            orientation_references_.begin(), matching_orientation));
        orientation_references_.erase(matching_orientation);
        if (orientation_index < orientation_labels_.size())
            orientation_labels_.erase(orientation_labels_.begin() +
                static_cast<std::ptrdiff_t>(orientation_index));
        for (std::size_t role = 0; role < orientation_references_.size(); ++role)
            orientation_references_[role].orientation_role =
                role == 0 ? "front" : "top";
        refresh_orientation_table();
    }
    // Trailing empty rows carry no information -- drop them so the table
    // still shows only genuinely populated rows plus the usual single
    // trailing "pick next" placeholder, matching refresh_reference_table()'s
    // existing size-based visibility rule.
    while (!references_.empty() && references_.back().owner_id.empty() &&
           references_.back().semantic_key.empty()) {
        references_.pop_back();
        if (!reference_labels_.empty()) reference_labels_.pop_back();
    }
    refresh_reference_table();
    notify_changed();
}

void ContainerPlacementSection::refresh_reference_table() {
    if (reference_table_ == nullptr) return;
    reference_items_.fill(nullptr);
    reference_indicators_.fill(nullptr);
    reference_offset_fields_.fill(nullptr);
    highlighted_reference_rows_.clear();
    reference_table_->setRowCount(0);
    // Offer another empty row whenever ANY degree of freedom (position OR
    // rotation) is still open, not only translation: a plain point reference
    // fully fixes X/Y/Z immediately, but a 2nd/3rd point still carries real
    // information -- it is what lets an Axis/Plane derive its direction/
    // normal from "2 points define an axis"/"3 points define a plane" (the
    // classic history-order-dependent shortcut: 1st point = origin, 2nd =
    // direction, 3rd = plane-completing point), which would otherwise be
    // impossible to enter once translation alone reaches 0.
    const int position_dof = remaining_translation_dof_ +
        (position_rows_can_define_rotation_ ? remaining_rotation_dof_ : 0);
    const std::size_t visible = std::min<std::size_t>(3, references_.size() +
        (position_dof > 0 ? 1 : 0));
    const auto palette = parent_widget_->palette();
    for (std::size_t index = 0; index < visible; ++index) {
        // A middle row can be an empty "hole" left by remove_reference()
        // (only a trailing hole is popped off references_ entirely) --
        // treat it exactly like the trailing not-yet-picked placeholder row.
        const bool populated = index < references_.size() &&
            !(references_[index].owner_id.empty() &&
              references_[index].semantic_key.empty());
        reference_table_->insertRow(static_cast<int>(index));
        auto* indicator = zima::ui::build_reference_row_indicator(
            [this, index] { remove_reference(index); });
        reference_indicators_[index] = indicator;
        reference_table_->setCellWidget(static_cast<int>(index), 0,
            zima::ui::centered_cell_widget(indicator));

        auto* reference = new zima::ui::ReferenceCellItem(
            QStringLiteral("%1. %2").arg(index + 1).arg(tr("Vybrat referenci")));
        reference_items_[index] = reference;
        reference_table_->setItem(static_cast<int>(index), 1, reference);

        auto* offset = new QDoubleSpinBox(reference_table_);
        offset->setRange(-1'000'000'000.0, 1'000'000'000.0);
        offset->setDecimals(decimal_places_);
        offset->setSuffix(QStringLiteral(" mm"));
        if (populated) {
            reference->set_reference(
                QString::fromStdString(references_[index].semantic_key));
            reference->setText(QStringLiteral("%1. %2").arg(index + 1).arg(
                index < reference_labels_.size()
                    ? reference_labels_[index]
                    : readable_reference_kind(references_[index].semantic_key)));
            reference->set_checked(true);
            reference->setForeground(QBrush());
            offset->setValue(references_[index].offset);
            offset->setEnabled(references_[index].supports_offset);
            connect(offset, &QDoubleSpinBox::valueChanged, this,
                [this, index](double value) {
                    if (index < references_.size()) {
                        references_[index].offset = value;
                        notify_changed();
                    }
                });
        } else {
            reference->set_placeholder_style(palette.color(QPalette::Mid));
            offset->setEnabled(false);
        }
        reference_offset_fields_[index] = offset;
        reference_table_->setCellWidget(static_cast<int>(index), 2, offset);
        zima::ui::set_reference_row_populated(indicator, populated);
    }
}

bool ContainerPlacementSection::set_reference_offset(
    std::size_t populated_index, double value) {
    std::size_t current{};
    for (std::size_t row = 0; row < references_.size() &&
            row < reference_offset_fields_.size(); ++row) {
        if (references_[row].owner_id.empty() &&
            references_[row].semantic_key.empty()) continue;
        if (current++ != populated_index) continue;
        auto* field = reference_offset_fields_[row];
        if (field == nullptr || !field->isEnabled()) return false;
        field->setValue(value);
        return true;
    }
    return false;
}

void ContainerPlacementSection::refresh_orientation_table() {
    if (orientation_table_ == nullptr) return;
    highlighted_orientation_rows_.clear();
    // The table is always enabled and clickable -- matching Python's
    // `_container_orientation_references`/`_activate_container_orientation_row()`,
    // which never disables the FRONT/TOP table just because row 0 of
    // Umístění kontejneru already supplies a default orientation. An empty
    // row shows a "derived from reference 1" placeholder label when
    // the frame is inherited, but clicking it still arms picking
    // exactly like any other empty row, letting the user override the
    // automatic default with their own reference.
    orientation_table_->setEnabled(true);
    const auto palette = parent_widget_->palette();
    const QString derived_label = orientation_inherited_label_.isEmpty()
        ? tr("první roviny umístění") : orientation_inherited_label_;
    orientation_table_->setToolTip(orientation_inherited_
        ? tr("Lokální roviny FRONT a TOP jsou ve výchozím stavu vypočtené "
             "z umístění kontejneru (%1). Každé z nich můžete kdykoliv "
             "přiřadit vlastní orientační referenci.")
              .arg(derived_label)
        : QString());
    for (std::size_t index = 0; index < 2; ++index) {
        const bool populated = index < orientation_references_.size() &&
            !orientation_references_[index].owner_id.empty();
        auto* indicator = zima::ui::build_reference_row_indicator(
            populated
                ? std::function<void()>([this, index] {
                      orientation_references_[index] = {};
                      if (index < orientation_labels_.size())
                          orientation_labels_[index].clear();
                      refresh_orientation_table();
                      notify_changed();
                      if (reference_request_) reference_request_(index + 3);
                  })
                : std::function<void()>({}));
        orientation_indicators_[index] = indicator;
        orientation_table_->setCellWidget(static_cast<int>(index), 0,
            zima::ui::centered_cell_widget(indicator));

        auto* reference = new zima::ui::ReferenceCellItem(
            orientation_inherited_
                ? tr("Vypočteno z umístění (%1)").arg(derived_label)
                : tr("Vybrat orientační referenci"));
        if (populated) {
            reference->set_reference(
                QString::fromStdString(orientation_references_[index].owner_id));
            reference->setText(index < orientation_labels_.size() &&
                    !orientation_labels_[index].isEmpty()
                ? orientation_labels_[index] : tr("Orientační reference"));
            reference->set_checked(true);
            reference->setForeground(QBrush());
        } else {
            reference->set_placeholder_style(palette.color(QPalette::Mid));
        }
        orientation_items_[index] = reference;
        orientation_table_->setItem(static_cast<int>(index), 1, reference);
        zima::ui::set_reference_row_populated(indicator, populated);

        auto* role = new QComboBox(orientation_table_);
        if (index == 0) {
            role->addItem(QStringLiteral("FRONT"), QStringLiteral("front"));
            role->addItem(QStringLiteral("BACK"), QStringLiteral("back"));
        } else {
            role->addItem(QStringLiteral("TOP"), QStringLiteral("top"));
            role->addItem(QStringLiteral("BOTTOM"), QStringLiteral("bottom"));
            role->addItem(QStringLiteral("LEFT"), QStringLiteral("left"));
            role->addItem(QStringLiteral("RIGHT"), QStringLiteral("right"));
        }
        const auto stored_role = populated
            ? QString::fromStdString(orientation_references_[index].orientation_role)
            : (index == 0 ? QStringLiteral("front") : QStringLiteral("top"));
        role->setCurrentIndex(std::max(0, role->findData(stored_role)));
        role->setEnabled(populated);
        connect(role, &QComboBox::currentIndexChanged, this, [this, index, role] {
            if (index < orientation_references_.size() &&
                !orientation_references_[index].owner_id.empty()) {
                orientation_references_[index].orientation_role =
                    role->currentData().toString().toStdString();
                notify_changed();
            }
        });
        orientation_table_->setCellWidget(static_cast<int>(index), 2, role);

        auto* flip_button = zima::ui::build_reference_row_flip_button(
            populated, populated && index < orientation_references_.size() &&
                orientation_references_[index].flip,
            [this, index](bool value) {
                if (index < orientation_references_.size()) {
                    orientation_references_[index].flip = value;
                    notify_changed();
                }
            });
        orientation_table_->setCellWidget(static_cast<int>(index), 3,
            zima::ui::centered_cell_widget(flip_button));
    }
    set_remaining_translation_dof(remaining_translation_dof_);
}

}  // namespace zima::ui
