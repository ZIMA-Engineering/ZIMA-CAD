#pragma once
#include <zima/document/part_document.hpp>
#include "sweep_placement_dialog.hpp"
#include <zima/ui/reference_cell.hpp>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>

namespace zima::app {
class Sweep2DDialog final : public SweepPlacementDialog {
public:
    std::function<void()> select_plane;
    bool plane_input{};
    bool plane_inspected{};
    Sweep2DDialog(document::HistoryContainer initial,std::function<void(document::HistoryContainer)> commit,QWidget* parent)
        : SweepPlacementDialog(tr("Vlastnosti 2D Sweepu"),std::move(initial),parent),commit_(std::move(commit)) {
        setObjectName("sweep2dDialog");setAttribute(Qt::WA_DeleteOnClose);setMinimumWidth(380);
        auto* form=new QFormLayout;
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& value){pending.name=value.toStdString();});
        auto* mode=new QComboBox(this);mode->addItems({tr("Přičíst"),tr("Odečíst")});mode->setCurrentIndex(pending.combine_mode==document::CombineMode::Subtract?1:0);form->addRow(tr("Operace"),mode);
        connect(mode,&QComboBox::currentIndexChanged,this,[this](int i){pending.combine_mode=i?document::CombineMode::Subtract:document::CombineMode::Add;notify();});
        content_layout()->addLayout(form);install_placement();form=new QFormLayout;
        base_=new QComboBox(this);base_->addItems({"XY","XZ / FRONT","YZ"});base_->setObjectName("sweep2dBasePlane");form->addRow(tr("Rovina skici v kontejneru"),base_);
        connect(base_,&QComboBox::activated,this,[this](int i){auto s=sketcher::Sketch::from_serialized(pending.sweep2d.sketches[0]);s.plane=static_cast<sketcher::SketchPlane>(i);set_sketch(0,s);});
        refs_=new QTableWidget(1,2,this);refs_->setObjectName("sweep2dReferences");refs_->horizontalHeader()->hide();refs_->verticalHeader()->hide();refs_->setFixedHeight(38);refs_->setSelectionMode(QAbstractItemView::NoSelection);refs_->setEditTriggers(QAbstractItemView::NoEditTriggers);refs_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);refs_->setColumnWidth(1,32);ui::install_reference_cell_delegate(refs_);
        cell_=new ui::ReferenceCellItem;refs_->setItem(0,0,cell_);eye_=ui::build_reference_inspection_button(false,false,[this](bool enabled){plane_inspected=enabled;refresh_reference();notify();});refs_->setCellWidget(0,1,ui::centered_cell_widget(eye_));
        connect(refs_,&QTableWidget::cellClicked,this,[this](int,int column){if(column==0)arm();});form->addRow(tr("Volitelná rovina dráhy"),refs_);
        auto* reset_path=new QPushButton(tr("Výchozí rovina dráhy"),this);form->addRow(reset_path);
        connect(reset_path,&QPushButton::clicked,this,[this]{pending.sweep2d.path_plane.reset();plane_input=false;plane_inspected=false;refresh_reference();notify();});
        for(unsigned i=0;i<2;++i){auto* button=new QPushButton(i?tr("2. Skica dráhy…"):tr("1. Skica průřezu…"),this);button->setObjectName(QString("sweep2dSketch%1").arg(i));form->addRow(button);connect(button,&QPushButton::clicked,this,[this,i]{if(edit_sketch)edit_sketch(i);});}
        auto* result=new QComboBox(this);result->setObjectName("sweep2dResultType");result->addItems({"Solid","Thin"});result->setCurrentIndex(pending.sweep2d.result_type==document::ProfileResultType::Thin?1:0);form->addRow(tr("Průřez"),result);
        thickness_=new QDoubleSpinBox(this);thickness_->setObjectName("sweep2dThickness");thickness_->setDecimals(3);thickness_->setRange(.001,100000);thickness_->setSuffix(" mm");thickness_->setValue(pending.sweep2d.thickness);form->addRow(tr("Tloušťka"),thickness_);
        side_=new QComboBox(this);side_->setObjectName("sweep2dThinSide");side_->addItems({tr("Jedna strana"),tr("Druhá strana"),tr("Symetricky")});side_->setCurrentIndex(pending.sweep2d.thin_mode==document::ThinMode::OneSide?0:pending.sweep2d.thin_mode==document::ThinMode::OtherSide?1:2);form->addRow(tr("Strana tloušťky"),side_);
        connect(result,&QComboBox::currentIndexChanged,this,[this](int i){pending.sweep2d.result_type=i?document::ProfileResultType::Thin:document::ProfileResultType::Solid;update_thin();notify();});
        connect(thickness_,&QDoubleSpinBox::valueChanged,this,[this](double value){pending.sweep2d.thickness=value;notify();});
        connect(side_,&QComboBox::currentIndexChanged,this,[this](int i){pending.sweep2d.thin_mode=i==0?document::ThinMode::OneSide:i==1?document::ThinMode::OtherSide:document::ThinMode::Symmetric;notify();});
        status_=new QLabel(this);status_->setWordWrap(true);form->addRow(status_);content_layout()->addLayout(form);refresh_reference();update_thin();
    }
    void refresh_reference(){
        const QSignalBlocker base_block(base_),eye_block(eye_);const auto s=sketcher::Sketch::from_serialized(pending.sweep2d.sketches[0]);base_->setCurrentIndex(static_cast<int>(s.plane));
        const auto& ref=pending.sweep2d.path_plane;cell_->set_reference(ref?QString::fromStdString(ref->semantic_key):QString());cell_->set_active_input(plane_input);cell_->set_inspected(plane_inspected);eye_->setEnabled(ref.has_value());eye_->setChecked(plane_inspected);refs_->viewport()->update();
    }
    void end_reference(){plane_input=false;plane_inspected=false;refresh_reference();notify();}
    void set_status(const QString& text){status_->setText(text);}
    void set_sketch(unsigned stage,const sketcher::Sketch& s){pending.sweep2d.sketches.at(stage)=s.serialized();notify();}
protected:
    bool submit() override {try{document::PartDocument::reframe_sweep2d_sketches(pending);commit_(pending);return true;}catch(const std::exception& e){set_status(QString::fromUtf8(e.what()));return false;}}
private:
    std::function<void(document::HistoryContainer)> commit_;
    QComboBox* base_{};QComboBox* side_{};QDoubleSpinBox* thickness_{};QLabel* status_{};QTableWidget* refs_{};
    ui::ReferenceCellItem* cell_{};QToolButton* eye_{};
    void notify(){if(changed)changed();}
    void update_thin(){const bool enabled=pending.sweep2d.result_type==document::ProfileResultType::Thin;thickness_->setEnabled(enabled);side_->setEnabled(enabled);}
    void arm(){plane_input=true;refresh_reference();if(select_plane)select_plane();}
};
}
