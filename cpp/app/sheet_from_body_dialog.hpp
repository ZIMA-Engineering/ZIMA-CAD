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
        if(auto* outer=qobject_cast<QVBoxLayout*>(layout()))outer->setStretch(outer->indexOf(content_layout()),1);
        auto* fields=new QWidget(this);
        fields->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
        auto* form=new QFormLayout(fields);
        form->setContentsMargins(0,0,0,0);
        form->setFormAlignment(Qt::AlignTop);
        table_=new QTableWidget(1,3,this);table_->setObjectName("sheetSourceFace");
        table_->setHorizontalHeaderLabels({QString{},tr("Výchozí rovinná plocha"),QString{}});
        table_->verticalHeader()->show();table_->setSelectionMode(QAbstractItemView::NoSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Fixed);
        table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Fixed);
        table_->setColumnWidth(0,32);table_->setColumnWidth(2,32);table_->setFixedHeight(65);ui::install_reference_cell_delegate(table_);
        indicator_=ui::build_reference_row_indicator([this]{selected_.reset();inspected_=false;active_=true;refresh();});
        table_->setCellWidget(0,0,indicator_);
        field_=new ui::ReferenceCellItem;table_->setItem(0,1,field_);
        eye_=ui::build_reference_inspection_button(false,false,[this](bool value){inspected_=value;refresh();});
        table_->setCellWidget(0,2,ui::centered_cell_widget(eye_));form->addRow(table_);
        thickness_=new QDoubleSpinBox(this);thickness_->setObjectName("sheetSourceThickness");
        thickness_->setDecimals(ui::numeric_decimal_places(this));thickness_->setRange(.001,1000000);thickness_->setSuffix(" mm");thickness_->setValue(thickness);
        form->addRow(tr("Tloušťka"),thickness_);content_layout()->addWidget(fields);
        auto* note=new QLabel(tr("Vytvoří samostatné tabule a ohyby v aktivním prázdném tělese. Zdroj zůstane beze změny. Složité přechody mohou vyžadovat ruční dokončení."),this);
        note->setWordWrap(true);note->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);content_layout()->addWidget(note);content_layout()->addStretch(1);set_initial_size({400,255});
        connect(table_,&QTableWidget::cellClicked,this,[this](int,int column){if(column==1){active_=true;refresh();}});
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
        ui::set_reference_row_populated(indicator_,selected_.has_value());
        eye_->setEnabled(selected_.has_value());eye_->setChecked(inspected_);table_->viewport()->update();if(changed)changed();
    }
    Commit commit_;std::optional<kernel::FaceReference> selected_;
    QTableWidget* table_{};ui::ReferenceCellItem* field_{};QWidget* indicator_{};QToolButton* eye_{};QDoubleSpinBox* thickness_{};
    bool active_{true},inspected_{};
};
}
