#pragma once
#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

namespace zima::app {
class CylinderAxisDialog final : public ui::PropertiesSubWindow {
public:
    using Object = document::ConstructionObject;
    std::function<void()> changed;
    CylinderAxisDialog(Object value, std::function<void(Object)> commit, QWidget* parent)
        : PropertiesSubWindow(tr("Osa z válcové plochy"), parent), value_(std::move(value)), commit_(std::move(commit)) {
        setObjectName("cylinderAxisDialog"); setAttribute(Qt::WA_DeleteOnClose);
        setMinimumWidth(350);
        auto* form = new QFormLayout;
        name_ = new QLineEdit(QString::fromStdString(value_.name), this);
        form->addRow(tr("Název"), name_);
        table_ = new QTableWidget(1, 2, this); table_->setObjectName("cylinderAxisReference");
        table_->setHorizontalHeaderLabels({tr("Válcová plocha"), QString()});
        table_->verticalHeader()->hide(); table_->setSelectionMode(QAbstractItemView::NoSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table_->setColumnWidth(1, 32); table_->setFixedHeight(65);
        ui::install_reference_cell_delegate(table_);
        field_ = new ui::ReferenceCellItem; table_->setItem(0, 0, field_);
        eye_ = ui::build_reference_inspection_button(false, false, [this](bool checked) {
            inspected_ = checked; refresh(); notify();
        });
        table_->setCellWidget(0, 1, ui::centered_cell_widget(eye_));
        form->addRow(table_); content_layout()->addLayout(form);
        active_ = value_.references.empty();
        connect(table_, &QTableWidget::cellClicked, this, [this](int, int column) {
            if (column == 0) { active_ = true; refresh(); notify(); }
        });
        connect(name_, &QLineEdit::textChanged, this, [this] { notify(); });
        refresh();
    }
    Object pending() const { auto result = value_; result.name = name_->text().trimmed().toStdString(); return result; }
    bool active() const { return active_; }
    bool inspected() const { return inspected_; }
    void end_entry() { active_ = inspected_ = false; refresh(); notify(); }
    void set_reference(document::ConstructionReference reference) {
        value_.references = {std::move(reference)}; active_ = false; refresh(); notify();
    }
protected:
    bool submit() override {
        auto value = pending();
        if (value.name.empty()) throw std::runtime_error(tr("Zadejte název osy.").toStdString());
        if (value.references.empty()) throw std::runtime_error(tr("Vyberte válcovou plochu.").toStdString());
        commit_(std::move(value)); return true;
    }
private:
    Object value_; std::function<void(Object)> commit_;
    QLineEdit* name_{}; QTableWidget* table_{}; ui::ReferenceCellItem* field_{}; QToolButton* eye_{};
    bool active_{}, inspected_{};
    void notify() { if (changed) changed(); }
    void refresh() {
        const auto text = value_.references.empty() ? QString() : QString::fromStdString(value_.references.front().semantic_key);
        field_->setText(text.isEmpty() ? tr("Vyberte válcovou plochu.") : tr("Válcová plocha"));
        field_->setToolTip(text);
        if(text.isEmpty())field_->clear_reference();else field_->set_reference(text);
        field_->set_active_input(active_); field_->set_inspected(inspected_);
        eye_->setEnabled(!value_.references.empty()); eye_->setChecked(inspected_); table_->viewport()->update();
    }
};
}
