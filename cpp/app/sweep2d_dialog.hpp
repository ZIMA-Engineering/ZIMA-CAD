#pragma once
#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>
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
class Sweep2DDialog final : public ui::PropertiesSubWindow {
public:
    document::HistoryContainer pending;
    std::function<void(unsigned)> edit_sketch;
    std::function<void()> changed;
    std::function<void()> select_plane;
    bool plane_input{};
    unsigned plane_index{};
    std::array<bool,2> plane_inspected{};
    Sweep2DDialog(document::HistoryContainer initial,std::function<void(document::HistoryContainer)> commit,QWidget* parent)
        : PropertiesSubWindow(tr("Vlastnosti 2D Sweepu"),parent),pending(std::move(initial)),commit_(std::move(commit)) {
        setObjectName("sweep2dDialog");setAttribute(Qt::WA_DeleteOnClose);setMinimumWidth(380);
        auto* form=new QFormLayout;
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& value){pending.name=value.toStdString();});
        auto* mode=new QComboBox(this);mode->addItems({tr("Přičíst"),tr("Odečíst")});mode->setCurrentIndex(pending.combine_mode==document::CombineMode::Subtract?1:0);form->addRow(tr("Operace"),mode);
        connect(mode,&QComboBox::currentIndexChanged,this,[this](int i){pending.combine_mode=i?document::CombineMode::Subtract:document::CombineMode::Add;notify();});
        base_=new QComboBox(this);base_->addItems({"XY","XZ","YZ",tr("Vybraná rovina")});base_->setObjectName("sweep2dBasePlane");form->addRow(tr("Rovina průřezu"),base_);
        connect(base_,&QComboBox::activated,this,[this](int i){
            if(i==3){arm(0);return;}
            pending.sweep2d.planes[0].reset();plane_input=false;plane_inspected[0]=false;
            auto s=sketcher::Sketch::from_serialized(pending.sweep2d.sketches[0]);s.plane=static_cast<sketcher::SketchPlane>(i);s.plane_reference_owner_id.clear();s.plane_offset=0;s.refresh_default_frame();set_sketch(0,s);refresh_reference();
        });
        refs_=new QTableWidget(2,2,this);refs_->setObjectName("sweep2dReferences");refs_->horizontalHeader()->hide();refs_->verticalHeader()->hide();refs_->setFixedHeight(74);refs_->setSelectionMode(QAbstractItemView::NoSelection);refs_->setEditTriggers(QAbstractItemView::NoEditTriggers);refs_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);refs_->setColumnWidth(1,32);ui::install_reference_cell_delegate(refs_);
        for(unsigned i=0;i<2;++i){cells_[i]=new ui::ReferenceCellItem;refs_->setItem(i,0,cells_[i]);eyes_[i]=ui::build_reference_inspection_button(false,false,[this,i](bool enabled){plane_inspected[i]=enabled;refresh_reference();notify();});refs_->setCellWidget(i,1,ui::centered_cell_widget(eyes_[i]));}
        connect(refs_,&QTableWidget::cellClicked,this,[this](int row,int column){if(column==0)arm(row);});form->addRow(tr("Roviny: průřez / dráha"),refs_);
        auto* reset_path=new QPushButton(tr("Výchozí rovina dráhy (XZ kontejneru)"),this);form->addRow(reset_path);
        connect(reset_path,&QPushButton::clicked,this,[this]{pending.sweep2d.planes[1].reset();plane_input=false;plane_inspected[1]=false;refresh_reference();notify();});
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
        const QSignalBlocker base_block(base_);const auto s=sketcher::Sketch::from_serialized(pending.sweep2d.sketches[0]);base_->setCurrentIndex(pending.sweep2d.planes[0]?3:static_cast<int>(s.plane));
        for(unsigned i=0;i<2;++i){const QSignalBlocker blocker(eyes_[i]);const auto& ref=pending.sweep2d.planes[i];cells_[i]->set_reference(ref?QString::fromStdString(ref->semantic_key):QString());cells_[i]->set_active_input(plane_input&&plane_index==i);cells_[i]->set_inspected(plane_inspected[i]);eyes_[i]->setEnabled(ref.has_value());eyes_[i]->setChecked(plane_inspected[i]);}refs_->viewport()->update();
    }
    void end_reference(){plane_input=false;plane_inspected={};refresh_reference();notify();}
    void set_status(const QString& text){status_->setText(text);}
    void set_sketch(unsigned stage,const sketcher::Sketch& s){pending.sweep2d.sketches.at(stage)=s.serialized();notify();}
protected:
    bool submit() override {try{document::PartDocument::reframe_sweep2d_sketches(pending);commit_(pending);return true;}catch(const std::exception& e){set_status(QString::fromUtf8(e.what()));return false;}}
private:
    std::function<void(document::HistoryContainer)> commit_;
    QComboBox* base_{};QComboBox* side_{};QDoubleSpinBox* thickness_{};QLabel* status_{};QTableWidget* refs_{};
    std::array<ui::ReferenceCellItem*,2> cells_{};std::array<QToolButton*,2> eyes_{};
    void notify(){if(changed)changed();}
    void update_thin(){const bool enabled=pending.sweep2d.result_type==document::ProfileResultType::Thin;thickness_->setEnabled(enabled);side_->setEnabled(enabled);}
    void arm(unsigned stage){plane_index=stage;plane_input=true;refresh_reference();if(select_plane)select_plane();}
};
}
