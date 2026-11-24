#pragma once
#include <zima/document/wrapping_parameters.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include "profile_operation_button.hpp"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <functional>

namespace zima::app {
class WrappingParameterPanel final:public QWidget {
public:
    std::function<void(bool)> pick_requested;
    std::function<void()> changed,inspection_changed,layout_changed;
    std::function<QString(const kernel::FaceReference&)> face_label;
    explicit WrappingParameterPanel(document::WrappingParameters initial,QWidget* parent)
        :QWidget(parent),pending_(std::move(initial)) {
        setObjectName("wrappingParameters");auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        form_=new QFormLayout;layout->addLayout(form_);
        output_=new QComboBox(this);output_->setObjectName("wrappingOutput");output_->addItems({tr("Křivky"),tr("Plocha")});
        output_->setCurrentIndex(std::min(1,static_cast<int>(pending_.output)));form_->addRow(tr("Výsledek"),output_);
        target_=new QTableWidget(1,4,this);target_->setObjectName("wrappingTarget");target_->verticalHeader()->hide();
        target_->setSelectionMode(QAbstractItemView::NoSelection);target_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui::install_reference_cell_delegate(target_);target_->setRowHeight(0,34);target_->setFixedHeight(72);
        for(int col:{0,1,3}){target_->horizontalHeader()->setSectionResizeMode(col,QHeaderView::Fixed);target_->setColumnWidth(col,34);}
        target_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);layout->addWidget(target_);
        details_=new QFormLayout;layout->addLayout(details_);
        height_=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);height_->setObjectName("wrappingHeight");
        height_->setRange(.000001,1000000);height_->setValue(pending_.height);displayed_height_=height_->value();initial_height_=pending_.height;details_->addRow(tr("Korekce"),height_);
        operations_=new QWidget(this);auto* operations=new QHBoxLayout(operations_);operations->setContentsMargins(0,0,0,0);
        group_=new QButtonGroup(this);
        for(int i=0;i<2;++i){auto* b=new ProfileOperationButton(i==0,i==0?tr("Přičíst"):tr("Odečíst"),operations_);b->setCheckable(true);group_->addButton(b,i);operations->addWidget(b);}
        group_->button(0)->setChecked(true);details_->addRow(tr("Operace"),operations_);
        reverse_u_=new QCheckBox(tr("Reverse horizontal direction"),this);reverse_u_->setObjectName("wrappingReverseU");reverse_u_->setChecked(pending_.reverse_u);
        reverse_v_=new QCheckBox(tr("Reverse vertical direction"),this);reverse_v_->setObjectName("wrappingReverseV");reverse_v_->setChecked(pending_.reverse_v);
        layout->addWidget(reverse_u_);layout->addWidget(reverse_v_);
        notice_=new QLabel(tr("The Feature Origin anchors the wrap. On a sphere, distances from the Origin are preserved and widths change."),this);
        notice_->setWordWrap(true);layout->addWidget(notice_);
        connect(output_,&QComboBox::currentIndexChanged,this,[this](int index){pending_.output=static_cast<document::WrappingOutput>(index);refresh();notify();if(layout_changed)layout_changed();});
        connect(height_,&QDoubleSpinBox::valueChanged,this,[this](double value){pending_.height=value==displayed_height_?initial_height_:value;notify();});
        connect(group_,&QButtonGroup::idClicked,this,[this]{notify();});
        connect(reverse_u_,&QCheckBox::toggled,this,[this](bool on){pending_.reverse_u=on;notify();});
        connect(reverse_v_,&QCheckBox::toggled,this,[this](bool on){pending_.reverse_v=on;notify();});
        connect(target_,&QTableWidget::cellClicked,this,[this](int,int column){if(column==2)begin_entry();});refresh();
    }
    const document::WrappingParameters& parameters()const{return pending_;}
    void set_relief_mode(bool relief) {
        if(relief)pending_.output=document::WrappingOutput::Relief;
        else if(pending_.output==document::WrappingOutput::Relief)pending_.output=document::WrappingOutput::Curves;
        {const QSignalBlocker blocker(output_);output_->setCurrentIndex(pending_.output==document::WrappingOutput::Surface?1:0);}
        refresh();
    }
    bool subtract()const{return pending_.output==document::WrappingOutput::Relief&&group_->checkedId()==1;}
    void set_subtract(bool on){group_->button(on?1:0)->setChecked(true);}
    bool active_pick()const{return active_;}
    void begin_entry(){active_=true;refresh();if(pick_requested)pick_requested(true);}
    void finish_entry(){active_=false;refresh();}
    bool set_target(document::ExtrusionParameters::EndTarget value) {
        if(!active_||value.kind!=document::EndTargetKind::Face)return false;
        pending_.target=std::move(value);active_=false;inspected_=false;refresh();notify();return true;
    }
    std::vector<kernel::FaceReference> inspected_faces()const {
        if(inspected_&&pending_.target)return {pending_.target->reference};return {};
    }
    void refresh() {
        const bool relief=pending_.output==document::WrappingOutput::Relief;
        form_->setRowVisible(output_,!relief);
        details_->setRowVisible(height_,relief);details_->setRowVisible(operations_,relief);
        target_->setHorizontalHeaderLabels({tr("Č."),{},tr("Cílová plocha"),{}});
        target_->setItem(0,0,new QTableWidgetItem("1"));
        auto* indicator=ui::build_reference_row_indicator([this]{pending_.target.reset();inspected_=false;active_=true;refresh();notify();if(inspection_changed)inspection_changed();if(pick_requested)pick_requested(true);});
        indicator->findChild<QPushButton*>()->setToolTip(tr("Vymazat referenci"));ui::set_reference_row_populated(indicator,pending_.target.has_value());target_->setCellWidget(0,1,indicator);
        const auto label=pending_.target?(face_label?face_label(pending_.target->reference):QString::fromStdString(pending_.target->label)):tr("Select a cylindrical, conical or spherical face to wrap.");
        auto* cell=new ui::ReferenceCellItem(label);if(pending_.target)cell->set_reference(QString::fromStdString(pending_.target->reference.owner_id+":"+pending_.target->reference.semantic_key));
        cell->set_active_input(active_);cell->set_inspected(inspected_);target_->setItem(0,2,cell);
        target_->setCellWidget(0,3,ui::centered_cell_widget(ui::build_reference_inspection_button(pending_.target.has_value(),inspected_,[this](bool on){inspected_=on;refresh();if(inspection_changed)inspection_changed();})));
    }
protected:
    void changeEvent(QEvent* event)override {
        QWidget::changeEvent(event);if(event->type()!=QEvent::LanguageChange)return;
        output_->setItemText(0,tr("Křivky"));output_->setItemText(1,tr("Plocha"));
        static_cast<QLabel*>(form_->labelForField(output_))->setText(tr("Výsledek"));
        static_cast<QLabel*>(details_->labelForField(height_))->setText(tr("Korekce"));static_cast<QLabel*>(details_->labelForField(operations_))->setText(tr("Operace"));
        group_->button(0)->setText(tr("Přičíst"));group_->button(1)->setText(tr("Odečíst"));
        reverse_u_->setText(tr("Reverse horizontal direction"));reverse_v_->setText(tr("Reverse vertical direction"));
        notice_->setText(tr("The Feature Origin anchors the wrap. On a sphere, distances from the Origin are preserved and widths change."));refresh();
    }
private:
    void notify(){if(changed)changed();}
    document::WrappingParameters pending_;
    QFormLayout *form_{},*details_{};QComboBox* output_{};QTableWidget* target_{};
    ui::UnitDoubleSpinBox* height_{};double displayed_height_{},initial_height_{};QWidget* operations_{};QButtonGroup* group_{};
    QCheckBox *reverse_u_{},*reverse_v_{};QLabel* notice_{};bool active_{},inspected_{};
};
}
