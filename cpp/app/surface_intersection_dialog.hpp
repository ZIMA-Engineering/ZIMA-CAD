#pragma once
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/document/surface_intersection.hpp>
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QSignalBlocker>
namespace zima::app {
class SurfaceIntersectionDialog final:public ui::PropertiesSubWindow {
public:
    document::HistoryContainer pending;
    std::function<void()> changed;
    std::function<QString(const kernel::FaceReference&)> label;
    SurfaceIntersectionDialog(document::HistoryContainer initial,std::function<void(document::HistoryContainer)> commit,QWidget* parent)
        :PropertiesSubWindow(tr("Vlastnosti průsečíku ploch"),parent),pending(std::move(initial)),commit_(std::move(commit)) {
        setObjectName("surfaceIntersectionDialog");setAttribute(Qt::WA_DeleteOnClose);
        // This command consumes faces and has no placement or Origin input.
        setProperty("originSelectionBound",true);
        auto* form=new QFormLayout;content_layout()->addLayout(form);
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);name->setObjectName("surfaceIntersectionName");form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& text){pending.name=text.toStdString();});
        auto* note=new QLabel(tr("Vyberte dvě původní plochy. Výsledkem jsou samostatné 3D křivky a body."),this);note->setWordWrap(true);content_layout()->addWidget(note);
        table_=new QTableWidget(2,4,this);table_->setObjectName("surfaceIntersectionReferences");
        table_->setHorizontalHeaderLabels({tr("Č."),{},tr("Plocha"),{}});table_->verticalHeader()->hide();
        table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);ui::install_reference_cell_delegate(table_);
        for(int column:{0,1,3}){table_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Fixed);table_->setColumnWidth(column,34);}
        table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
        active_=-1;
        for(int row=0;row<2;++row) {
            table_->setRowHeight(row,34);table_->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
            // Required faces can be replaced by clicking their field. They
            // cannot be removed; the shared arrow only prompts an empty slot.
            indicators_[row]=ui::build_reference_row_indicator({});table_->setCellWidget(row,1,ui::centered_cell_widget(indicators_[row]));
            table_->setItem(row,2,new ui::ReferenceCellItem);
            inspected_[row]=pending.surface_intersection.faces[row].valid();
            if(!inspected_[row]&&active_<0)active_=row;
            eyes_[row]=ui::build_reference_inspection_button(inspected_[row],inspected_[row],[this,row](bool on){inspected_[row]=on;refresh();notify();});
            table_->setCellWidget(row,3,ui::centered_cell_widget(eyes_[row]));
        }
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){if(column==2){active_=row;refresh();notify();}});
        table_->setFixedHeight(table_->horizontalHeader()->sizeHint().height()+2*table_->frameWidth()+68);
        content_layout()->addWidget(table_);status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);
        set_initial_size({620,300});refresh();
    }
    int active_row()const{return active_;}
    bool inspected(unsigned row)const{return row<2&&inspected_[row];}
    void set_face(const kernel::FaceReference& reference) {
        if(active_<0||active_>1||!reference.valid()||pending.surface_intersection.faces[1-active_]==reference)return;
        pending.surface_intersection.faces[active_]=reference;inspected_[active_]=true;
        active_=pending.surface_intersection.faces[1-active_].valid()?-1:1-active_;refresh();notify();
    }
    void end_entry(){active_=-1;inspected_={};refresh();notify();}
    void refresh() {
        for(int row=0;row<2;++row) {
            const auto& reference=pending.surface_intersection.faces[row];const bool populated=reference.valid();
            auto* item=static_cast<ui::ReferenceCellItem*>(table_->item(row,2));
            if(populated){item->set_reference(QString::fromStdString(reference.owner_id+":"+reference.semantic_key));item->setText(label?label(reference):QString::fromStdString(reference.owner_id));}
            else {item->clear_reference();item->setText(tr("Vyberte plochu…"));}
            item->set_active_input(active_==row);item->set_inspected(inspected_[row]);indicators_[row]->setVisible(!populated);
            const QSignalBlocker block(eyes_[row]);eyes_[row]->setEnabled(populated);eyes_[row]->setChecked(inspected_[row]);
        }
        table_->viewport()->update();
    }
private:
    bool submit()override {try{commit_(pending);return true;}catch(const std::exception& error){status_->setText(tr(error.what()));return false;}}
    void notify(){if(changed)changed();}
    std::function<void(document::HistoryContainer)> commit_;
    QTableWidget* table_{};QLabel* status_{};std::array<QWidget*,2> indicators_{};
    std::array<QToolButton*,2> eyes_{};std::array<bool,2> inspected_{};int active_{-1};
};
}
