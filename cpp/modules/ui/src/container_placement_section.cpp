#include "zima/ui/container_placement_section.hpp"

#include "zima/ui/reference_cell.hpp"

#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
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
    QWidget* parent_widget, QVBoxLayout* layout, bool with_orientation)
    : QObject(parent_widget), parent_widget_(parent_widget),
      with_orientation_(with_orientation) {
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
        auto* orientation_heading = new QLabel(tr("Orientace kontejneru"), parent_widget_);
        auto orientation_heading_font = orientation_heading->font();
        orientation_heading_font.setBold(true);
        orientation_heading->setFont(orientation_heading_font);
        layout->addWidget(orientation_heading);

        orientation_table_ = new QTableWidget(2, 3, parent_widget_);
        orientation_table_->setObjectName("containerPlacementOrientationTable");
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
        layout->addWidget(orientation_table_);
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
    }

    dof_label_ = new QLabel(parent_widget_);
    layout->addWidget(dof_label_);
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
    for (const auto& reference : references) {
        const auto label = label_for_semantic
            ? label_for_semantic(reference.semantic_key)
            : readable_reference_kind(reference.semantic_key);
        if (reference.orientation_drives_rotation) {
            orientation_references_.push_back(reference);
            orientation_labels_.push_back(label);
        } else {
            references_.push_back(reference);
            reference_labels_.push_back(label);
        }
    }
}

bool ContainerPlacementSection::set_reference(std::size_t index,
    zima::document::ConstructionReference reference, const QString& label,
    bool mirror_first_two_into_orientation, QString* error_text) {
    const auto duplicate = [&](const auto& existing) {
        return existing.instance_path == reference.instance_path &&
            existing.owner_id == reference.owner_id &&
            existing.semantic_key == reference.semantic_key;
    };
    if (index >= 3) {
        const auto orientation_index = index - 3;
        if (!with_orientation_ || orientation_index >= 2) return false;
        if (std::any_of(references_.begin(), references_.end(), duplicate) ||
            std::any_of(orientation_references_.begin(),
                orientation_references_.end(), duplicate)) {
            if (error_text) *error_text = tr("Stejnou referenci nelze zadat vícekrát.");
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
    if (mirror_first_two_into_orientation && with_orientation_ && index < 2) {
        auto oriented = references_[index];
        oriented.orientation_drives_rotation = true;
        oriented.orientation_role = index == 0 ? "front" : "top";
        if (orientation_references_.size() <= index)
            orientation_references_.resize(index + 1);
        if (orientation_labels_.size() <= index)
            orientation_labels_.resize(index + 1);
        orientation_references_[index] = std::move(oriented);
        orientation_labels_[index] = reference_labels_[index];
        refresh_orientation_table();
    }
    refresh_reference_table();
    notify_changed();
    return true;
}

void ContainerPlacementSection::set_remaining_translation_dof(int dof) {
    const int previous = remaining_translation_dof_;
    remaining_translation_dof_ = std::clamp(dof, 0, 3);
    if (dof_label_ != nullptr) {
        const int total_dof = remaining_translation_dof_ + remaining_rotation_dof_;
        dof_label_->setText(tr("Zbývající stupně volnosti: %1").arg(total_dof));
        if (reference_status_ != nullptr) {
            reference_status_->setText(total_dof == 0 ? tr("Plně určené") : QString());
        }
    }
    if (previous != remaining_translation_dof_ && reference_table_ != nullptr)
        refresh_reference_table();
}

void ContainerPlacementSection::set_remaining_rotation_dof(int dof) {
    remaining_rotation_dof_ = std::clamp(dof, 0, 3);
    set_remaining_translation_dof(remaining_translation_dof_);
}

std::vector<zima::document::ConstructionReference>
ContainerPlacementSection::combined_references(std::size_t required) const {
    std::vector<zima::document::ConstructionReference> result(references_.begin(),
        references_.begin() + static_cast<std::ptrdiff_t>(
            std::min(required, references_.size())));
    result.insert(result.end(), orientation_references_.begin(),
        orientation_references_.end());
    return result;
}

std::vector<zima::document::ConstructionReference>
ContainerPlacementSection::references_without(std::size_t index) const {
    auto result = references_;
    if (index < 3 && index < result.size()) {
        result.erase(result.begin() + static_cast<std::ptrdiff_t>(index));
    }
    if (index >= 3) {
        const auto orientation_index = index - 3;
        if (orientation_index < orientation_references_.size()) {
            auto orientations = orientation_references_;
            orientations.erase(orientations.begin() +
                static_cast<std::ptrdiff_t>(orientation_index));
            result.insert(result.end(), orientations.begin(), orientations.end());
            return result;
        }
    }
    result.insert(result.end(), orientation_references_.begin(),
        orientation_references_.end());
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
    references_.erase(references_.begin() + static_cast<std::ptrdiff_t>(index));
    if (index < reference_labels_.size()) {
        reference_labels_.erase(reference_labels_.begin() +
            static_cast<std::ptrdiff_t>(index));
    }
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
    refresh_reference_table();
    notify_changed();
}

void ContainerPlacementSection::refresh_reference_table() {
    if (reference_table_ == nullptr) return;
    reference_items_.fill(nullptr);
    reference_indicators_.fill(nullptr);
    highlighted_reference_rows_.clear();
    reference_table_->setRowCount(0);
    const std::size_t visible = std::min<std::size_t>(3, references_.size() +
        (remaining_translation_dof_ > 0 ? 1 : 0));
    const auto palette = parent_widget_->palette();
    for (std::size_t index = 0; index < visible; ++index) {
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
        offset->setDecimals(3);
        offset->setSuffix(QStringLiteral(" mm"));
        if (index < references_.size()) {
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
                        // For a Plane container the first two position rows
                        // are mirrored into a separate orientation-table
                        // copy (see set_reference()). Keep that copy's
                        // offset synchronised, otherwise adjusting the
                        // offset field appears to do nothing: the actual
                        // placement equation solved in resolve_construction
                        // ()/resolve_placement() reads the mirrored copy.
                        if (index < orientation_references_.size() &&
                            orientation_references_[index].owner_id ==
                                references_[index].owner_id &&
                            orientation_references_[index].semantic_key ==
                                references_[index].semantic_key &&
                            orientation_references_[index].instance_path ==
                                references_[index].instance_path) {
                            orientation_references_[index].offset = value;
                        }
                        notify_changed();
                    }
                });
        } else {
            reference->set_placeholder_style(palette.color(QPalette::Mid));
            offset->setEnabled(false);
        }
        reference_table_->setCellWidget(static_cast<int>(index), 2, offset);
        zima::ui::set_reference_row_populated(indicator, index < references_.size());
    }
}

void ContainerPlacementSection::refresh_orientation_table() {
    if (orientation_table_ == nullptr) return;
    highlighted_orientation_rows_.clear();
    const auto palette = parent_widget_->palette();
    for (std::size_t index = 0; index < 2; ++index) {
        auto* indicator = zima::ui::build_reference_row_indicator(
            [this, index] {
                orientation_references_.erase(orientation_references_.begin() +
                    static_cast<std::ptrdiff_t>(index));
                if (index < orientation_labels_.size())
                    orientation_labels_.erase(orientation_labels_.begin() +
                        static_cast<std::ptrdiff_t>(index));
                for (std::size_t role = 0; role < orientation_references_.size(); ++role)
                    orientation_references_[role].orientation_role =
                        role == 0 ? "front" : "top";
                refresh_orientation_table();
                notify_changed();
            });
        orientation_indicators_[index] = indicator;
        orientation_table_->setCellWidget(static_cast<int>(index), 0,
            zima::ui::centered_cell_widget(indicator));

        const bool populated = index < orientation_references_.size() &&
            !orientation_references_[index].owner_id.empty();
        auto* reference = new zima::ui::ReferenceCellItem(
            tr("Vybrat orientační referenci"));
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
                notify_changed();
            }
        });
        orientation_table_->setCellWidget(static_cast<int>(index), 2, role);
    }
    set_remaining_translation_dof(remaining_translation_dof_);
}

}  // namespace zima::ui
