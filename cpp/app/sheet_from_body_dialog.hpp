#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

namespace zima::app {
class SheetFromBodyDialog final : public ui::PropertiesSubWindow {
public:
    using Commit=std::function<void(kernel::FaceReference,double)>;
    std::function<void()> changed;
    SheetFromBodyDialog(double thickness,Commit commit,QWidget* parent)
        :PropertiesSubWindow(tr("Plech z tělesa"),parent),commit_(std::move(commit)) {
        setObjectName("sheetFromBodyDialog");setAttribute(Qt::WA_DeleteOnClose);
        setProperty("originSelectionBound",true);
        auto* form=new QFormLayout;
        table_=new QTableWidget(1,2,this);table_->setObjectName("sheetSourceFace");
        table_->setHorizontalHeaderLabels({tr("Výchozí rovinná plocha"),QString{}});
        table_->verticalHeader()->hide();table_->setSelectionMode(QAbstractItemView::NoSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
        table_->setColumnWidth(1,32);table_->setFixedHeight(65);ui::install_reference_cell_delegate(table_);
        field_=new ui::ReferenceCellItem;table_->setItem(0,0,field_);
        eye_=ui::build_reference_inspection_button(false,false,[this](bool value){inspected_=value;refresh();});
        table_->setCellWidget(0,1,ui::centered_cell_widget(eye_));form->addRow(table_);
        thickness_=new QDoubleSpinBox(this);thickness_->setObjectName("sheetSourceThickness");
        thickness_->setDecimals(ui::numeric_decimal_places(this));thickness_->setRange(.001,1000000);thickness_->setSuffix(" mm");thickness_->setValue(thickness);
        form->addRow(tr("Tloušťka"),thickness_);content_layout()->addLayout(form);
        auto* note=new QLabel(tr("Vytvoří samostatné tabule a ohyby v aktivním prázdném tělese. Zdroj zůstane beze změny. Složité přechody mohou vyžadovat ruční dokončení."),this);
        note->setWordWrap(true);content_layout()->addWidget(note);set_initial_size({400,255});
        connect(table_,&QTableWidget::cellClicked,this,[this](int,int column){if(column==0){active_=true;refresh();}});
        refresh();
    }
    bool active()const{return active_;}
    bool inspected()const{return inspected_;}
    const std::optional<kernel::FaceReference>& selected()const{return selected_;}
    void end_entry(){active_=inspected_=false;refresh();}
    void set_reference(kernel::FaceReference face,double inferred_thickness) {
        selected_=std::move(face);if(inferred_thickness>=.001)thickness_->setValue(inferred_thickness);
        active_=false;refresh();
    }
private:
    bool submit()override {
        if(!selected_)throw std::invalid_argument(tr("Select a planar face of another Body.").toStdString());
        commit_(*selected_,thickness_->value());return true;
    }
    void refresh() {
        field_->setText(selected_?tr("Výchozí rovinná plocha"):tr("Vyberte rovinnou plochu ve View."));
        if(selected_)field_->set_reference(QString::fromStdString(selected_->semantic_key));else field_->clear_reference();
        field_->set_active_input(active_);field_->set_inspected(inspected_);
        eye_->setEnabled(selected_.has_value());eye_->setChecked(inspected_);table_->viewport()->update();if(changed)changed();
    }
    Commit commit_;std::optional<kernel::FaceReference> selected_;
    QTableWidget* table_{};ui::ReferenceCellItem* field_{};QToolButton* eye_{};QDoubleSpinBox* thickness_{};
    bool active_{true},inspected_{};
};
}
