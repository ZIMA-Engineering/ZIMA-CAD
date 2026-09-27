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
#include <functional>
namespace zima::app {
class BoundarySurfaceDialog final:public ui::PropertiesSubWindow {
public:
    document::HistoryContainer pending;
    std::function<void()> changed;
    std::function<QString(const document::BoundaryCurveSource&)> label;
    BoundarySurfaceDialog(document::HistoryContainer initial,std::function<void(document::HistoryContainer)> commit,QWidget* parent)
        :PropertiesSubWindow(tr("Vlastnosti hraniční plochy"),parent),pending(std::move(initial)),commit_(std::move(commit)) {
        setObjectName("boundarySurfaceDialog");setAttribute(Qt::WA_DeleteOnClose);
        auto* form=new QFormLayout;content_layout()->addLayout(form);
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);name->setObjectName("boundarySurfaceName");form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& value){pending.name=value.toStdString();});
        auto* note=new QLabel(tr("Vyberte čtyři hranice po obvodu. Každá hranice je křivka Skici nebo otevřený řetězec Skici či 3D křivky."),this);note->setWordWrap(true);content_layout()->addWidget(note);
        table_=new QTableWidget(4,4,this);table_->setObjectName("boundarySurfaceReferences");
        table_->setHorizontalHeaderLabels({tr("Č."),{},tr("Hraniční křivka"),{}});
        table_->verticalHeader()->hide();table_->setSelectionMode(QAbstractItemView::NoSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);ui::install_reference_cell_delegate(table_);
        for(int column:{0,1,3}){table_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Fixed);table_->setColumnWidth(column,34);}
        table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
        for(int row=0;row<4;++row) {
            table_->setRowHeight(row,34);table_->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
            auto* indicator=ui::build_reference_row_indicator([this,row]{pending.boundary_surface.boundaries[row]={};inspected_[row]=false;active_=row;refresh();notify();});
            indicator->findChild<QPushButton*>()->setToolTip(tr("Vymazat hranici; řádek zůstane zachován"));table_->setCellWidget(row,1,indicator);
            table_->setItem(row,2,new ui::ReferenceCellItem);
            eyes_[row]=ui::build_reference_inspection_button(false,false,[this,row](bool on){inspected_[row]=on;refresh();notify();});
            table_->setCellWidget(row,3,ui::centered_cell_widget(eyes_[row]));
        }
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){if(column==2){active_=row;refresh();notify();}});
        table_->setFixedHeight(table_->horizontalHeader()->sizeHint().height()+4*34+2*table_->frameWidth());
        content_layout()->addWidget(table_);status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);
        active_=-1;for(int row=0;row<4;++row)if(pending.boundary_surface.boundaries[row].owner_id.empty()){active_=row;break;}
        set_initial_size({550,340});refresh();
    }
    int active_row()const{return active_;}
    bool inspected(int row)const{return inspected_.at(row);}
    void set_boundary(document::BoundaryCurveSource source){
        if(active_<0)return;
        for(int row=0;row<4;++row)if(row!=active_&&pending.boundary_surface.boundaries[row]==source)return;
        pending.boundary_surface.boundaries[active_]=std::move(source);inspected_[active_]=false;
        active_=-1;for(int i=0;i<4;++i)if(pending.boundary_surface.boundaries[i].owner_id.empty()){active_=i;break;}
        refresh();notify();
    }
    void end_entry(){active_=-1;inspected_.fill(false);refresh();notify();}
    void refresh(){
        for(int row=0;row<4;++row){const auto& source=pending.boundary_surface.boundaries[row];const bool populated=!source.owner_id.empty();
            auto* item=static_cast<ui::ReferenceCellItem*>(table_->item(row,2));
            if(populated){item->set_reference(QString::fromStdString(source.owner_id+":"+source.curve_id));item->setText(label?label(source):QString::fromStdString(source.owner_id));}
            else {item->clear_reference();item->setText(tr("Vyberte hranici"));}
            item->set_active_input(active_==row);item->set_inspected(inspected_[row]);
            ui::set_reference_row_populated(table_->cellWidget(row,1),populated);
            const QSignalBlocker block(eyes_[row]);eyes_[row]->setEnabled(populated);eyes_[row]->setChecked(inspected_[row]);
        }
        table_->viewport()->update();
    }
    void set_status(const QString& message){status_->setText(message);}
private:
    bool submit()override{try{commit_(pending);return true;}catch(const std::exception& e){set_status(tr(e.what()));return false;}}
    void notify(){if(changed)changed();}
    std::function<void(document::HistoryContainer)> commit_;
    QTableWidget* table_{};QLabel* status_{};std::array<QToolButton*,4> eyes_{};std::array<bool,4> inspected_{};int active_{0};
};
}
