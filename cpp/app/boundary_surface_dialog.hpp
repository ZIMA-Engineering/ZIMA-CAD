#pragma once
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/document/boundary_surface.hpp>
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QSignalBlocker>
#include <QPushButton>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <functional>
#include <limits>
namespace zima::app {
class BoundarySurfaceDialog final:public ui::PropertiesSubWindow {
public:
    document::HistoryContainer pending;
    std::function<void()> changed;
    std::function<QString(const document::BoundaryCurveSource&)> label;
    std::function<QString(const kernel::FaceReference&)> support_label;
    BoundarySurfaceDialog(document::HistoryContainer initial,std::function<void(document::HistoryContainer)> commit,QWidget* parent)
        :PropertiesSubWindow(tr("Vlastnosti zaplnění plochy"),parent),pending(std::move(initial)),commit_(std::move(commit)) {
        setObjectName("boundarySurfaceDialog");setAttribute(Qt::WA_DeleteOnClose);
        setProperty("expandBottomTable",true);
        setProperty("originSelectionBound",true);
        auto* form=new QFormLayout;content_layout()->addLayout(form);
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);name->setObjectName("boundarySurfaceName");form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& value){pending.name=value.toStdString();});
        auto* count=new QSpinBox(this);count->setObjectName("boundarySurfaceCount");count->setRange(2,std::numeric_limits<int>::max());count->setKeyboardTracking(false);count->setMaximumWidth(100);
        count->setValue(static_cast<int>(pending.boundary_surface.boundaries.size()));form->addRow(tr("Počet hranic"),count);
        auto* note=new QLabel(tr("Vyberte hranice po obvodu: křivky Skici, otevřené 3D křivky nebo původní hrany ploch."),this);note->setWordWrap(true);content_layout()->addWidget(note);
        table_=new QTableWidget(0,9,this);table_->setObjectName("boundarySurfaceReferences");
        table_->setHorizontalHeaderLabels({tr("Č."),{},tr("Hraniční křivka"),{},tr("Návaznost"),{},tr("Podpůrná plocha"),{},tr("Opačná strana")});
        table_->verticalHeader()->hide();table_->setSelectionMode(QAbstractItemView::NoSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);ui::install_reference_cell_delegate(table_);
        for(int column:{0,1,3,5,7}){table_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Fixed);table_->setColumnWidth(column,34);}
        table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(6,QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(4,QHeaderView::Fixed);table_->setColumnWidth(4,65);
        table_->horizontalHeader()->setSectionResizeMode(8,QHeaderView::ResizeToContents);
        rebuild_rows(true);
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){
            if(column==2){active_=row;support_entry_=false;refresh();notify();}
            else if(column==6&&pending.boundary_surface.boundaries[row].continuity!=kernel::SurfaceContinuity::G0){active_=row;support_entry_=true;refresh();notify();}
        });
        content_layout()->addWidget(table_,1);status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);
        connect(count,&QSpinBox::valueChanged,this,[this](int value){pending.boundary_surface.boundaries.resize(value);rebuild_rows();refresh();notify();});
        set_initial_size({820,380});refresh();
    }
    int active_row()const{return active_;}
    bool inspected(int row)const{return inspected_.at(row);}
    bool support_inspected(int row)const{return support_inspected_.at(row);}
    bool support_entry()const{return support_entry_;}
    void set_boundary(document::BoundaryCurveSource source){
        if(active_<0||support_entry_)return;
        for(int row=0;row<table_->rowCount();++row)if(row!=active_){const auto& other=pending.boundary_surface.boundaries[row];
            if(other.owner_id==source.owner_id&&other.curve_id==source.curve_id&&other.kind==source.kind)return;}
        const auto& previous=pending.boundary_surface.boundaries[active_];
        if(previous.owner_id==source.owner_id&&previous.curve_id==source.curve_id&&previous.kind==source.kind)source=previous;
        else {source.continuity=previous.continuity;support_inspected_[active_]=false;}
        pending.boundary_surface.boundaries[active_]=std::move(source);inspected_[active_]=true;
        arm_missing_boundary();
        refresh();notify();
    }
    void set_support(kernel::FaceReference support){
        if(active_<0||!support_entry_)return;
        auto& source=pending.boundary_surface.boundaries[active_];source.support=std::move(support);source.support_reversed=false;
        support_inspected_[active_]=true;arm_missing_boundary();refresh();notify();
    }
    void end_entry(){active_=-1;std::fill(inspected_.begin(),inspected_.end(),false);std::fill(support_inspected_.begin(),support_inspected_.end(),false);refresh();notify();}
    void refresh(){
        for(int row=0;row<table_->rowCount();++row){const auto& source=pending.boundary_surface.boundaries[row];const bool populated=!source.owner_id.empty();
            auto* item=static_cast<ui::ReferenceCellItem*>(table_->item(row,2));
            if(populated){item->set_reference(QString::fromStdString(source.owner_id+":"+source.curve_id));item->setText(label?label(source):QString::fromStdString(source.owner_id));}
            else {item->clear_reference();item->setText(tr("Vyberte hranici"));}
            item->set_active_input(active_==row&&!support_entry_);item->set_inspected(inspected_[row]);
            ui::set_reference_row_populated(table_->cellWidget(row,1),populated);
            const QSignalBlocker block(eyes_[row]);eyes_[row]->setEnabled(populated);eyes_[row]->setChecked(inspected_[row]);
            auto* continuity=qobject_cast<QComboBox*>(table_->cellWidget(row,4));const QSignalBlocker continuity_block(continuity);
            continuity->setCurrentIndex(static_cast<int>(source.continuity));
            const bool constrained=source.continuity!=kernel::SurfaceContinuity::G0;
            auto* support=static_cast<ui::ReferenceCellItem*>(table_->item(row,6));
            if(source.support){support->set_reference(QString::fromStdString(source.support->owner_id+":"+source.support->semantic_key));
                support->setText(support_label?support_label(*source.support):QString::fromStdString(source.support->semantic_key));}
            else{support->clear_reference();support->setText(constrained?tr("Vyberte podpůrnou plochu"):QString{});}
            support->set_active_input(active_==row&&support_entry_&&constrained);support->set_inspected(constrained&&support_inspected_[row]);
            ui::set_reference_row_populated(table_->cellWidget(row,5),source.support.has_value());table_->cellWidget(row,5)->setEnabled(constrained);
            const QSignalBlocker support_block(support_eyes_[row]);support_eyes_[row]->setEnabled(constrained&&source.support.has_value());support_eyes_[row]->setChecked(constrained&&support_inspected_[row]);
            auto* side=table_->cellWidget(row,8)->findChild<QCheckBox*>();const QSignalBlocker side_block(side);
            side->setEnabled(constrained&&source.support.has_value());side->setChecked(source.support_reversed);
        }
        table_->viewport()->update();
    }
    void set_status(const QString& message){status_->setText(message);}
private:
    void arm_missing_boundary(){
        active_=-1;support_entry_=false;
        for(std::size_t i=0;i<pending.boundary_surface.boundaries.size();++i) {
            const auto& source=pending.boundary_surface.boundaries[i];
            if(source.owner_id.empty()){active_=static_cast<int>(i);break;}
            if(source.continuity!=kernel::SurfaceContinuity::G0&&!source.support){active_=static_cast<int>(i);support_entry_=true;break;}
        }
    }
    void rebuild_rows(bool initialize=false){
        const int count=static_cast<int>(pending.boundary_surface.boundaries.size());
        inspected_.resize(count,false);support_inspected_.resize(count,false);eyes_.resize(count);support_eyes_.resize(count);table_->setRowCount(0);table_->setRowCount(count);
        for(int row=0;row<count;++row) {
            if(initialize)inspected_[row]=!pending.boundary_surface.boundaries[row].owner_id.empty();
            if(initialize)support_inspected_[row]=pending.boundary_surface.boundaries[row].support.has_value();
            table_->setRowHeight(row,34);table_->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
            auto* indicator=ui::build_reference_row_indicator([this,row]{pending.boundary_surface.boundaries[row]={};inspected_[row]=false;support_inspected_[row]=false;active_=row;support_entry_=false;refresh();notify();});
            indicator->findChild<QPushButton*>()->setToolTip(tr("Vymazat hranici; řádek zůstane zachován"));table_->setCellWidget(row,1,indicator);
            table_->setItem(row,2,new ui::ReferenceCellItem);
            eyes_[row]=ui::build_reference_inspection_button(false,false,[this,row](bool on){inspected_[row]=on;refresh();notify();});
            table_->setCellWidget(row,3,ui::centered_cell_widget(eyes_[row]));
            auto* continuity=new QComboBox(this);continuity->setObjectName(QString("boundaryContinuity%1").arg(row));continuity->addItems({"G0","G1","G2"});
            continuity->setToolTip(tr("G0: poloha; G1: tečnost; G2: křivost. G1/G2 vyžaduje hranu podpůrné plochy."));
            connect(continuity,&QComboBox::currentIndexChanged,this,[this,row](int index){
                auto& source=pending.boundary_surface.boundaries[row];source.continuity=static_cast<kernel::SurfaceContinuity>(index);
                if(index&&!source.support){active_=row;support_entry_=true;}
                else if(active_==row&&support_entry_)active_=-1;
                refresh();notify();
            });table_->setCellWidget(row,4,continuity);
            auto* support_clear=ui::build_reference_row_indicator([this,row]{auto& source=pending.boundary_surface.boundaries[row];source.support.reset();source.support_reversed=false;support_inspected_[row]=false;active_=row;support_entry_=true;refresh();notify();});
            support_clear->findChild<QPushButton*>()->setToolTip(tr("Vymazat podpůrnou plochu; hranice zůstane zachována"));table_->setCellWidget(row,5,support_clear);
            table_->setItem(row,6,new ui::ReferenceCellItem);
            support_eyes_[row]=ui::build_reference_inspection_button(false,false,[this,row](bool on){support_inspected_[row]=on;refresh();notify();});
            table_->setCellWidget(row,7,ui::centered_cell_widget(support_eyes_[row]));
            auto* side=new QCheckBox(this);side->setToolTip(tr("Použít opačnou stranu podpůrné plochy"));
            connect(side,&QCheckBox::toggled,this,[this,row](bool on){pending.boundary_surface.boundaries[row].support_reversed=on;notify();});
            table_->setCellWidget(row,8,ui::centered_cell_widget(side));
        }
        const int header=table_->horizontalHeader()->sizeHint().height()+2*table_->frameWidth();
        table_->setMinimumHeight(header+std::min(count,4)*34);table_->setMaximumHeight(QWIDGETSIZE_MAX);
        arm_missing_boundary();
    }
    bool submit()override{try{commit_(pending);return true;}catch(const std::exception& e){set_status(tr(e.what()));return false;}}
    void notify(){if(changed)changed();}
    std::function<void(document::HistoryContainer)> commit_;
    QTableWidget* table_{};QLabel* status_{};std::vector<QToolButton*> eyes_,support_eyes_;std::vector<bool> inspected_,support_inspected_;int active_{0};bool support_entry_{};
};
}
