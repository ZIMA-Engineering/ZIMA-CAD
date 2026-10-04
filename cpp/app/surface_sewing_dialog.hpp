#pragma once
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/document/surface_sewing.hpp>
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QPushButton>
#include <QSignalBlocker>
#include <algorithm>
namespace zima::app {
class SurfaceSewingDialog final:public ui::PropertiesSubWindow {
public:
    document::HistoryContainer pending;
    std::function<void()> changed;
    std::function<QString(const kernel::FaceReference&)> label;
    SurfaceSewingDialog(document::HistoryContainer initial,std::function<void(document::HistoryContainer)> commit,QWidget* parent)
        :PropertiesSubWindow(tr("Vlastnosti sešití ploch"),parent),pending(std::move(initial)),commit_(std::move(commit)) {
        setObjectName("surfaceSewingDialog");setAttribute(Qt::WA_DeleteOnClose);
        setProperty("originSelectionBound",true);
        auto* form=new QFormLayout;content_layout()->addLayout(form);
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);name->setObjectName("surfaceSewingName");form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& text){pending.name=text.toStdString();});
        auto* note=new QLabel(tr("Vyberte plochy ke spojení do jednoho pláště. Sešití nepřidává objem."),this);note->setWordWrap(true);content_layout()->addWidget(note);
        table_=new QTableWidget(0,4,this);table_->setObjectName("surfaceSewingReferences");
        table_->setHorizontalHeaderLabels({tr("Č."),{},tr("Plocha"),{}});table_->verticalHeader()->hide();
        table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);ui::install_reference_cell_delegate(table_);
        for(int column:{0,1,3}){table_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Fixed);table_->setColumnWidth(column,34);}
        table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
        inspected_.resize(pending.surface_sewing.faces.size(),true);active_=static_cast<int>(pending.surface_sewing.faces.size());rebuild();
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){if(column==2){active_=row;refresh();notify();}});
        content_layout()->addWidget(table_,1);status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);
        set_initial_size({620,340});refresh();
    }
    int active_row()const{return active_;}
    bool inspected(int row)const{return row>=0&&row<static_cast<int>(inspected_.size())&&inspected_[row];}
    void set_face(const kernel::FaceReference& reference) {
        if(active_<0||active_>static_cast<int>(pending.surface_sewing.faces.size()))return;
        for(int i=0;i<static_cast<int>(pending.surface_sewing.faces.size());++i)
            if(i!=active_&&pending.surface_sewing.faces[i]==reference)return;
        if(active_==static_cast<int>(pending.surface_sewing.faces.size())) {pending.surface_sewing.faces.push_back(reference);inspected_.push_back(true);}
        else {pending.surface_sewing.faces[active_]=reference;inspected_[active_]=true;}
        active_=static_cast<int>(pending.surface_sewing.faces.size());rebuild();refresh();notify();
    }
    void end_entry(){active_=-1;std::fill(inspected_.begin(),inspected_.end(),false);refresh();notify();}
    void refresh() {
        for(int row=0;row<table_->rowCount();++row) {
            const bool populated=row<static_cast<int>(pending.surface_sewing.faces.size());auto* item=static_cast<ui::ReferenceCellItem*>(table_->item(row,2));
            if(populated) {const auto& reference=pending.surface_sewing.faces[row];item->set_reference(QString::fromStdString(reference.owner_id+":"+reference.semantic_key));
                item->setText(label?label(reference):QString::fromStdString(reference.owner_id));}
            else {item->clear_reference();item->setText(tr("Vyberte plochu…"));}
            item->set_active_input(active_==row);item->set_inspected(inspected(row));ui::set_reference_row_populated(table_->cellWidget(row,1),populated);
            const QSignalBlocker block(eyes_[row]);eyes_[row]->setEnabled(populated);eyes_[row]->setChecked(inspected(row));
        }
        table_->viewport()->update();
    }
private:
    void rebuild() {
        const int count=static_cast<int>(pending.surface_sewing.faces.size())+1;table_->setRowCount(0);table_->setRowCount(count);eyes_.resize(count);
        for(int row=0;row<count;++row) {
            table_->setRowHeight(row,34);table_->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
            auto* indicator=ui::build_reference_row_indicator([this,row]{
                if(row>=static_cast<int>(pending.surface_sewing.faces.size()))return;
                pending.surface_sewing.faces.erase(pending.surface_sewing.faces.begin()+row);inspected_.erase(inspected_.begin()+row);
                active_=static_cast<int>(pending.surface_sewing.faces.size());rebuild();refresh();notify();
            });
            indicator->findChild<QPushButton*>()->setToolTip(tr("Odstranit plochu ze seznamu"));table_->setCellWidget(row,1,indicator);
            table_->setItem(row,2,new ui::ReferenceCellItem);
            eyes_[row]=ui::build_reference_inspection_button(false,false,[this,row](bool on){if(row<static_cast<int>(inspected_.size())){inspected_[row]=on;refresh();notify();}});
            table_->setCellWidget(row,3,ui::centered_cell_widget(eyes_[row]));
        }
        table_->setMinimumHeight(table_->horizontalHeader()->sizeHint().height()+2*table_->frameWidth()+std::min(count,4)*34);
    }
    bool submit()override {try{commit_(pending);return true;}catch(const std::exception& error){status_->setText(tr(error.what()));return false;}}
    void notify(){if(changed)changed();}
    std::function<void(document::HistoryContainer)> commit_;
    QTableWidget* table_{};QLabel* status_{};std::vector<QToolButton*> eyes_;std::vector<bool> inspected_;int active_{};
};
}
