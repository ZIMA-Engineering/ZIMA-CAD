#pragma once
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <zima/ui/numeric_value_lock.hpp>
#include <zima/document/surface_thicken.hpp>
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QFormLayout>
#include <QComboBox>
#include <QToolButton>
#include <QPushButton>
#include <QSignalBlocker>
namespace zima::app {
class SurfaceThickenDialog final:public ui::PropertiesSubWindow {
public:
    document::HistoryContainer pending;
    std::function<void()> changed;
    std::function<QString(const kernel::FaceReference&)> label;
    SurfaceThickenDialog(document::HistoryContainer initial,std::function<void(document::HistoryContainer)> commit,QWidget* parent)
        :PropertiesSubWindow(tr("Vlastnosti zesílení plochy"),parent),pending(std::move(initial)),commit_(std::move(commit)) {
        setObjectName("surfaceThickenDialog");setAttribute(Qt::WA_DeleteOnClose);setProperty("originSelectionBound",true);
        auto* form=new QFormLayout;content_layout()->addLayout(form);
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);name->setObjectName("surfaceThickenName");form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& text){pending.name=text.toStdString();});
        thickness_=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);thickness_->setObjectName("surfaceThickenThickness");
        thickness_->setRange(0.,1.e9);thickness_->setValue(pending.surface_thicken.thickness);form->addRow(tr("Tloušťka"),thickness_);
        connect(thickness_,&QDoubleSpinBox::valueChanged,this,[this](double value){pending.surface_thicken.thickness=value;});
        ui::bind_numeric_value_lock(thickness_,"thickness",pending.value_locks,[this]{notify();});
        auto* side=new QComboBox(this);side->setObjectName("surfaceThickenSide");
        side->addItems({tr("První strana"),tr("Druhá strana"),tr("Symetricky")});side->setCurrentIndex(static_cast<int>(pending.surface_thicken.side));form->addRow(tr("Směr"),side);
        connect(side,&QComboBox::currentIndexChanged,this,[this](int value){pending.surface_thicken.side=static_cast<kernel::SurfaceThicknessSide>(value);});
        auto* note=new QLabel(tr("Tloušťka se měří kolmo k ploše. Symetricky znamená polovinu tloušťky na každou stranu."),this);
        note->setWordWrap(true);content_layout()->addWidget(note);
        table_=new QTableWidget(1,4,this);table_->setObjectName("surfaceThickenReferences");table_->setHorizontalHeaderLabels({{},{},tr("Plocha"),{}});
        table_->verticalHeader()->hide();table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);ui::install_reference_cell_delegate(table_);
        for(int col:{0,1,3}){table_->horizontalHeader()->setSectionResizeMode(col,QHeaderView::Fixed);table_->setColumnWidth(col,34);}table_->setColumnWidth(0,28);
        table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);table_->setRowHeight(0,34);
        auto* number=new QTableWidgetItem("1");number->setTextAlignment(Qt::AlignCenter);table_->setItem(0,0,number);table_->setItem(0,2,new ui::ReferenceCellItem);
        // A required source is replaced by clicking its field, never deleted.
        table_->setCellWidget(0,1,ui::build_reference_row_indicator({}));
        eye_=ui::build_reference_inspection_button(false,false,[this](bool on){inspected_=on;refresh();notify();});table_->setCellWidget(0,3,ui::centered_cell_widget(eye_));
        connect(table_,&QTableWidget::cellClicked,this,[this](int,int col){if(col==2){active_=true;refresh();notify();}});
        table_->setFixedHeight(table_->horizontalHeader()->sizeHint().height()+2*table_->frameWidth()+34);content_layout()->addWidget(table_);
        status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);
        inspected_=pending.surface_thicken.face.valid();
        // Use the shared natural-height layout rather than an oversized form.
        set_initial_size({620,0});refresh();
    }
    int active_row()const{return active_?0:-1;}
    bool inspected()const{return inspected_&&pending.surface_thicken.face.valid();}
    void set_face(const kernel::FaceReference& face){if(!active_)return;pending.surface_thicken.face=face;active_=false;inspected_=true;refresh();notify();}
    void end_entry(){active_=false;inspected_=false;refresh();notify();}
    void refresh(){
        const auto& face=pending.surface_thicken.face;const bool populated=face.valid();auto* item=static_cast<ui::ReferenceCellItem*>(table_->item(0,2));
        if(populated){item->set_reference(QString::fromStdString(face.owner_id+":"+face.semantic_key));item->setText(label?label(face):QString::fromStdString(face.owner_id));}
        else {item->clear_reference();item->setText(tr("Vyberte plochu…"));}
        item->set_active_input(active_);item->set_inspected(inspected());
        const QSignalBlocker block(eye_);eye_->setEnabled(populated);eye_->setChecked(inspected());table_->viewport()->update();
    }
private:
    bool submit()override{try{thickness_->interpretText();commit_(pending);return true;}catch(const std::exception& error){status_->setText(tr(error.what()));return false;}}
    void notify(){if(changed)changed();}
    std::function<void(document::HistoryContainer)> commit_;
    QTableWidget* table_{};QToolButton* eye_{};ui::UnitDoubleSpinBox* thickness_{};QLabel* status_{};bool active_{true},inspected_{};
};
}
