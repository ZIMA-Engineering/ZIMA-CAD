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
    Sweep2DDialog(document::HistoryContainer initial,std::function<void(document::HistoryContainer)> commit,QWidget* parent)
        : SweepPlacementDialog(tr("Vlastnosti 2D Sweepu"),std::move(initial),parent),commit_(std::move(commit)) {
        setObjectName("sweep2dDialog");setAttribute(Qt::WA_DeleteOnClose);setMinimumWidth(380);
        auto* form=new QFormLayout;
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& value){pending.name=value.toStdString();});
        content_layout()->addLayout(form);install_placement();form=new QFormLayout;
        auto* planes=new QLabel(tr("1. reference umístění: rovina průřezu\n2. reference umístění: rovina dráhy"),this);planes->setWordWrap(true);form->addRow(planes);
        for(unsigned i=0;i<2;++i){auto* button=new QPushButton(i?tr("2. Skica dráhy…"):tr("1. Skica průřezu…"),this);button->setObjectName(QString("sweep2dSketch%1").arg(i));form->addRow(button);connect(button,&QPushButton::clicked,this,[this,i]{if(edit_sketch)edit_sketch(i);});}
        auto* result=new QComboBox(this);result->setObjectName("sweep2dResultType");result->addItems({"Solid","Thin"});result->setCurrentIndex(pending.sweep2d.result_type==document::ProfileResultType::Thin?1:0);form->addRow(tr("Průřez"),result);
        thickness_=new QDoubleSpinBox(this);thickness_->setObjectName("sweep2dThickness");thickness_->setDecimals(3);thickness_->setRange(.001,100000);thickness_->setSuffix(" mm");thickness_->setValue(pending.sweep2d.thickness);form->addRow(tr("Tloušťka"),thickness_);
        side_=new QComboBox(this);side_->setObjectName("sweep2dThinSide");side_->addItems({tr("Jedna strana"),tr("Druhá strana"),tr("Symetricky")});side_->setCurrentIndex(pending.sweep2d.thin_mode==document::ThinMode::OneSide?0:pending.sweep2d.thin_mode==document::ThinMode::OtherSide?1:2);form->addRow(tr("Strana tloušťky"),side_);
        connect(result,&QComboBox::currentIndexChanged,this,[this](int i){pending.sweep2d.result_type=i?document::ProfileResultType::Thin:document::ProfileResultType::Solid;update_thin();notify();});
        connect(thickness_,&QDoubleSpinBox::valueChanged,this,[this](double value){pending.sweep2d.thickness=value;notify();});
        connect(side_,&QComboBox::currentIndexChanged,this,[this](int i){pending.sweep2d.thin_mode=i==0?document::ThinMode::OneSide:i==1?document::ThinMode::OtherSide:document::ThinMode::Symmetric;notify();});
        status_=new QLabel(this);status_->setWordWrap(true);form->addRow(status_);content_layout()->addLayout(form);install_operation_buttons();update_thin();
    }
    void set_status(const QString& text){status_->setText(text);}
    void set_sketch(unsigned stage,const sketcher::Sketch& s){pending.sweep2d.sketches.at(stage)=s.serialized();notify();}
protected:
    bool submit() override {try{document::PartDocument::reframe_sweep2d_sketches(pending);commit_(pending);return true;}catch(const std::exception& e){set_status(QString::fromUtf8(e.what()));return false;}}
private:
    std::function<void(document::HistoryContainer)> commit_;
    QComboBox* side_{};QDoubleSpinBox* thickness_{};QLabel* status_{};
    void notify(){if(changed)changed();}
    void update_thin(){const bool enabled=pending.sweep2d.result_type==document::ProfileResultType::Thin;thickness_->setEnabled(enabled);side_->setEnabled(enabled);}
};
}
