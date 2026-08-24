#include "orientation_dialog.hpp"

#include <zima/ui/reference_cell.hpp>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QTableWidget>

#include <algorithm>

namespace zima::app {
namespace {

QString standard_view_label(const std::string& key) {
    if (key == "default") return QObject::tr("Výchozí (izometrický)");
    if (key == "front") return QObject::tr("Zepředu");
    if (key == "back") return QObject::tr("Zezadu");
    if (key == "top") return QObject::tr("Shora");
    if (key == "bottom") return QObject::tr("Zdola");
    if (key == "left") return QObject::tr("Zleva");
    if (key == "right") return QObject::tr("Zprava");
    return QString::fromStdString(key);
}

}  // namespace

OrientationDialog::OrientationDialog(
    std::vector<OrientationSavedView> custom_views, QWidget* parent)
    : PropertiesSubWindow(tr("Pohledy"), parent),
      custom_views_(std::move(custom_views)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(410);
    content_layout()->addWidget(new QLabel(
        tr("Vyberte plochu nebo rovinu pro první (FRONT) a volitelně druhý "
           "(orientační) směr pohledu."), this));

    reference_table_ = new QTableWidget(2, 4, this);
    reference_table_->setObjectName("orientationReferenceTable");
    reference_table_->setSelectionMode(QAbstractItemView::NoSelection);
    reference_table_->setHorizontalHeaderLabels({QString(),
        tr("Reference"), tr("Směr"), tr("Obrátit")});
    reference_table_->verticalHeader()->hide();
    reference_table_->setStyleSheet(
        "QTableWidget::item:selected{background:#00d1ff;color:#102027}");
    auto* header = reference_table_->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    reference_table_->verticalHeader()->setDefaultSectionSize(34);
    for (int row = 0; row < reference_table_->rowCount(); ++row)
        reference_table_->setRowHeight(row, 34);
    for (std::size_t row = 0; row < 2; ++row) {
        auto* indicator = zima::ui::build_reference_row_indicator(
            [this, row] { remove_row(row); });
        row_indicators_[row] = indicator;
        reference_table_->setCellWidget(static_cast<int>(row), 0,
            zima::ui::centered_cell_widget(indicator));

        auto* reference = new zima::ui::ReferenceCellItem(
            tr("Vybrat referenci"));
        reference->set_placeholder_style(palette().color(QPalette::Mid));
        reference_items_[row] = reference;
        reference_table_->setItem(static_cast<int>(row), 1, reference);

        auto* role = new QComboBox(reference_table_);
        for (const char* value : {"FRONT", "BACK", "TOP", "BOTTOM", "LEFT",
                 "RIGHT"}) {
            role->addItem(QString::fromLatin1(value),
                QString::fromLatin1(value).toLower());
        }
        if (row == 1) role->setCurrentIndex(role->findData("top"));
        connect(role, &QComboBox::currentIndexChanged, this,
            [this](int) { notify_rows_changed(); });
        role_combos_[row] = role;
        reference_table_->setCellWidget(static_cast<int>(row), 2, role);

        auto* flip = new QCheckBox(reference_table_);
        connect(flip, &QCheckBox::toggled, this,
            [this](bool) { notify_rows_changed(); });
        flip_checks_[row] = flip;
        reference_table_->setCellWidget(static_cast<int>(row), 3, flip);

        reference_table_->setRowHidden(static_cast<int>(row), row != 0);
    }
    connect(reference_table_, &QTableWidget::cellClicked, this,
        [this](int row, int column) {
            handle_reference_cell_clicked(row, column);
        });
    reference_table_->setFixedHeight(
        header->sizeHint().height() + 2 * 34 + reference_table_->frameWidth() * 2);
    content_layout()->addWidget(reference_table_);

    content_layout()->addWidget(new QLabel(tr("Uložené pohledy"), this));
    view_list_ = new QListWidget(this);
    view_list_->setObjectName("orientationViewList");
    {
        std::vector<OrientationSavedView> standard_views;
        for (const char* key : {"default", "front", "back", "top", "bottom",
                 "left", "right"}) {
            OrientationSavedView standard_view;
            standard_view.name = standard_view_label(key);
            standard_view.standard = key;
            standard_views.push_back(std::move(standard_view));
        }
        standard_views.insert(standard_views.end(),
            std::make_move_iterator(custom_views_.begin()),
            std::make_move_iterator(custom_views_.end()));
        custom_views_ = std::move(standard_views);
    }
    refresh_view_list();
    connect(view_list_, &QListWidget::itemActivated, this,
        [this](QListWidgetItem* item) { handle_view_item(item); });
    connect(view_list_, &QListWidget::itemClicked, this,
        [this](QListWidgetItem* item) { handle_view_item(item); });
    content_layout()->addWidget(view_list_);

    auto* name_row = new QHBoxLayout;
    name_row->addWidget(new QLabel(tr("Název"), this));
    name_edit_ = new QLineEdit(this);
    name_edit_->setObjectName("orientationViewName");
    name_row->addWidget(name_edit_, 1);
    auto* save_button = new QPushButton(tr("Uložit"), this);
    save_button->setObjectName("orientationSaveViewButton");
    connect(save_button, &QPushButton::clicked, this,
        [this] { handle_save_clicked(); });
    name_row->addWidget(save_button);
    auto* delete_button = new QPushButton(tr("Odstranit"), this);
    delete_button->setObjectName("orientationDeleteViewButton");
    connect(delete_button, &QPushButton::clicked, this,
        [this] { handle_delete_clicked(); });
    name_row->addWidget(delete_button);
    content_layout()->addLayout(name_row);

    activate_row(0);
}

void OrientationDialog::set_reference_request_callback(
    ReferenceRequestCallback callback) {
    reference_request_ = std::move(callback);
}

void OrientationDialog::set_rows_changed_callback(RowsChangedCallback callback) {
    rows_changed_ = std::move(callback);
}

void OrientationDialog::set_view_requested_callback(
    ViewRequestedCallback callback) {
    view_requested_ = std::move(callback);
}

void OrientationDialog::set_save_view_callback(SaveViewCallback callback) {
    save_view_ = std::move(callback);
}

void OrientationDialog::set_delete_view_callback(DeleteViewCallback callback) {
    delete_view_ = std::move(callback);
}

void OrientationDialog::activate_row(std::size_t row) {
    if (row >= 2) return;
    active_row_ = row;
    reference_table_->setRowHidden(static_cast<int>(row), false);
    if (reference_request_) reference_request_(row);
}

void OrientationDialog::handle_reference_cell_clicked(int row, int column) {
    if (column != 1 || row < 0 || static_cast<std::size_t>(row) >= 2) return;
    auto* reference = reference_items_[static_cast<std::size_t>(row)];
    if (reference == nullptr) return;
    if (!reference->has_reference()) {
        activate_row(static_cast<std::size_t>(row));
        return;
    }
    // A populated reference is a persistent value, not an on/off control;
    // clicking it only toggles its viewer highlight, matching the reference
    // table used by "Umístit kontejner".
    reference->set_checked(true);
    const auto index = static_cast<std::size_t>(row);
    if (highlighted_rows_.count(index)) {
        highlighted_rows_.erase(index);
    } else {
        highlighted_rows_.insert(index);
    }
    update_highlights();
    reference_table_->clearSelection();
}

bool OrientationDialog::references_independent(
    const std::string& candidate_descriptor) const {
    if (!independence_check_) return true;
    for (std::size_t index = 0; index < 2; ++index) {
        const auto* reference = reference_items_[index];
        if (reference == nullptr || !reference->has_reference()) continue;
        if (!independence_check_(
                reference->reference().toStdString(), candidate_descriptor)) {
            return false;
        }
    }
    return true;
}

void OrientationDialog::set_independence_check_callback(
    IndependenceCheckCallback callback) {
    independence_check_ = std::move(callback);
}

void OrientationDialog::accept_reference(
    const std::string& descriptor, const QString& label) {
    if (active_row_ >= 2) return;
    if (!references_independent(descriptor)) {
        if (reference_rejected_) reference_rejected_();
        return;
    }
    auto* reference = reference_items_[active_row_];
    reference->set_reference(QString::fromStdString(descriptor));
    reference->setText(label);
    reference->set_checked(true);
    reference->setForeground(QBrush());
    // Mirrors Python's OrientationDialog: the camera only reorients once the
    // *last* row has been filled in (selectionCompleted), not after the
    // first reference alone -- the second reference affects the final roll,
    // so applying the view after just one reference would show an
    // intermediate, not-yet-final orientation.
    const bool was_last_row = active_row_ + 1 >= 2;
    if (!was_last_row) {
        activate_row(active_row_ + 1);
    }
    update_highlights();
    if (was_last_row) notify_rows_changed();
}

void OrientationDialog::remove_row(std::size_t row) {
    if (row >= 2) return;
    auto rows = orientation_rows();
    if (row < rows.size()) rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(row));
    updating_rows_ = true;
    highlighted_rows_.clear();
    for (std::size_t index = 0; index < 2; ++index) {
        auto* reference = reference_items_[index];
        if (index < rows.size()) {
            reference->set_reference(QString::fromStdString(rows[index].reference));
            reference->setText(rows[index].label);
            reference->set_checked(true);
            reference->setForeground(QBrush());
            role_combos_[index]->setCurrentIndex(std::max(0,
                role_combos_[index]->findData(
                    QString::fromStdString(rows[index].role))));
            flip_checks_[index]->setChecked(rows[index].flip);
        } else {
            reference->clear_reference();
            reference->setText(tr("Vybrat referenci"));
            reference->set_checked(false);
            reference->set_placeholder_style(palette().color(QPalette::Mid));
            flip_checks_[index]->setChecked(false);
        }
        reference_table_->setRowHidden(static_cast<int>(index),
            index > rows.size());
    }
    updating_rows_ = false;
    active_row_ = std::min<std::size_t>(rows.size(), 1);
    reference_table_->setRowHidden(static_cast<int>(active_row_), false);
    update_highlights();
    notify_rows_changed();
}

std::vector<OrientationReferenceRow> OrientationDialog::orientation_rows() const {
    std::vector<OrientationReferenceRow> result;
    for (std::size_t index = 0; index < 2; ++index) {
        const auto* reference = reference_items_[index];
        if (reference == nullptr || !reference->has_reference()) continue;
        OrientationReferenceRow row;
        row.reference = reference->reference().toStdString();
        row.label = reference->text();
        row.role = role_combos_[index]->currentData().toString().toStdString();
        row.flip = flip_checks_[index]->isChecked();
        result.push_back(std::move(row));
    }
    return result;
}

void OrientationDialog::refresh_reference_table() {
    for (std::size_t index = 0; index < 2; ++index) {
        const bool populated = reference_items_[index]->has_reference();
        reference_table_->setRowHidden(static_cast<int>(index),
            index != 0 && !populated && index != active_row_);
    }
}

void OrientationDialog::update_highlights() {
    for (std::size_t index = 0; index < 2; ++index) {
        auto* reference = reference_items_[index];
        const bool highlighted = highlighted_rows_.count(index) != 0;
        reference->setBackground(highlighted
            ? QBrush(QColor("#00d1ff")) : QBrush());
        reference->setForeground(highlighted
            ? QBrush(QColor("#102027"))
            : (reference->has_reference()
                  ? QBrush() : QBrush(palette().color(QPalette::Mid))));
        zima::ui::set_reference_row_populated(
            row_indicators_[index], reference->has_reference());
    }
}

void OrientationDialog::notify_rows_changed() {
    if (updating_rows_) return;
    if (rows_changed_) rows_changed_(orientation_rows());
}

void OrientationDialog::refresh_view_list() {
    view_list_->clear();
    for (const auto& view : custom_views_) {
        auto* item = new QListWidgetItem(view.name, view_list_);
        item->setData(Qt::UserRole, view.is_custom());
        view_list_->addItem(item);
    }
}

void OrientationDialog::handle_view_item(QListWidgetItem* item) {
    if (item == nullptr) return;
    const int row = view_list_->row(item);
    if (row < 0 || static_cast<std::size_t>(row) >= custom_views_.size()) return;
    if (view_requested_) view_requested_(custom_views_[static_cast<std::size_t>(row)]);
}

void OrientationDialog::handle_save_clicked() {
    const auto name = name_edit_->text().trimmed();
    if (name.isEmpty()) return;
    if (save_view_) save_view_(name);
    name_edit_->clear();
}

void OrientationDialog::handle_delete_clicked() {
    auto* item = view_list_->currentItem();
    if (item == nullptr) return;
    const int row = view_list_->row(item);
    if (row < 0 || static_cast<std::size_t>(row) >= custom_views_.size()) return;
    auto& view = custom_views_[static_cast<std::size_t>(row)];
    if (!view.is_custom()) return;
    if (delete_view_) delete_view_(view.name);
    remove_saved_view(view.name);
}

void OrientationDialog::append_saved_view(OrientationSavedView view) {
    // Replace an existing entry with the same name (Save re-uses the slot),
    // matching Python's save_view() dedupe-by-name behavior.
    const auto found = std::find_if(custom_views_.begin(), custom_views_.end(),
        [&](const auto& existing) {
            return existing.is_custom() && existing.name == view.name;
        });
    if (found != custom_views_.end()) {
        *found = std::move(view);
    } else {
        custom_views_.push_back(std::move(view));
    }
    refresh_view_list();
}

void OrientationDialog::remove_saved_view(const QString& name) {
    custom_views_.erase(std::remove_if(custom_views_.begin(), custom_views_.end(),
        [&](const auto& existing) {
            return existing.is_custom() && existing.name == name;
        }), custom_views_.end());
    refresh_view_list();
}

bool OrientationDialog::submit() { return true; }

}  // namespace zima::app
