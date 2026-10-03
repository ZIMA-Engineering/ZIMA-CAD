#pragma once
#include "sketch_button_style.hpp"
#include <zima/document/part_document.hpp>
#include "sweep_placement_dialog.hpp"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <zima/ui/unit_spin_box.hpp>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QHeaderView>
#include <QToolButton>
#include <zima/ui/reference_cell.hpp>
#include <array>
#include <functional>
#include <set>
namespace zima::app {
class HelicalSweepDialog final : public SweepPlacementDialog {
public:
    HelicalSweepDialog(document::HistoryContainer initial,std::function<void(document::HistoryContainer)> commit,QWidget* parent)
      : SweepPlacementDialog(tr("Vlastnosti H-tažení"),std::move(initial),parent),commit_(std::move(commit)) {
        setObjectName("helicalSweepDialog");setAttribute(Qt::WA_DeleteOnClose);setMinimumWidth(360);
        auto* form=new QFormLayout;
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](auto v){pending.name=v.toStdString();});
        content_layout()->addLayout(form);install_placement();form=new QFormLayout;
        const std::array<QString,3> names{tr("1. Kružnice a počáteční bod…"),tr("2. Radiální vodicí skica…"),tr("3. Skica průřezu…")};
        for(unsigned i=0;i<3;++i){auto* button=new QPushButton(names[i],this);button->setObjectName(QString("helicalSketch%1").arg(i));style_sketch_button(button);form->addRow(button);connect(button,&QPushButton::clicked,this,[this,i]{if(edit_sketch)edit_sketch(i);});}
        auto* result=new QComboBox(this);result->setObjectName("helicalResultType");
        result->addItems({tr("Těleso"),tr("Thin"),tr("Plocha")});
        result->setCurrentIndex(pending.helical.result_type==document::ProfileResultType::Surface?2:pending.helical.result_type==document::ProfileResultType::Thin?1:0);
        form->addRow(tr("Typ výsledku"),result);
        auto* thickness=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);thickness->setObjectName("helicalThickness");
        thickness->setRange(.001,1000000);
        thickness->setValue(pending.helical.thickness);form->addRow(tr("Tloušťka"),thickness);
        ui::bind_numeric_value_lock(thickness,"thickness",pending.value_locks,[this]{notify();});
        auto* side=new QComboBox(this);side->setObjectName("helicalThinSide");side->addItems({tr("Dovnitř"),tr("Ven"),tr("Symetricky")});
        side->setCurrentIndex(pending.helical.thin_mode==document::ThinMode::OneSide?0:pending.helical.thin_mode==document::ThinMode::OtherSide?1:2);
        side->setToolTip(tr("Symetricky: polovina celkové tloušťky na každou stranu. U otevřené kontury stranu určuje její směr."));
        form->addRow(tr("Strana tloušťky"),side);
        const auto update_thin=[this,form,thickness,side]{const bool thin=pending.helical.result_type==document::ProfileResultType::Thin;
            form->setRowVisible(thickness,thin);form->setRowVisible(side,thin);
            if(auto* subtract=findChild<QPushButton*>("primitiveSubtractOperation")) {
                if(pending.is_surface_result()&&subtract->isChecked())findChild<QPushButton*>("primitiveAddOperation")->click();
                subtract->setEnabled(!pending.is_surface_result());
            }};
        connect(result,&QComboBox::currentIndexChanged,this,[this,update_thin](int i){
            pending.helical.result_type=i==2?document::ProfileResultType::Surface:i==1?document::ProfileResultType::Thin:document::ProfileResultType::Solid;
            update_thin();notify();});
        connect(thickness,&QDoubleSpinBox::valueChanged,this,[this](double v){pending.helical.thickness=v;notify();});
        connect(side,&QComboBox::currentIndexChanged,this,[this](int i){pending.helical.thin_mode=i==0?document::ThinMode::OneSide:i==1?document::ThinMode::OtherSide:document::ThinMode::Symmetric;notify();});
        update_thin();
        auto* base_plane=new QComboBox(this);base_plane->setObjectName("helicalBasePlane");
        base_plane->addItems({"XY","XZ","YZ"});
        base_plane->setCurrentIndex(static_cast<int>(sketcher::Sketch::from_serialized(pending.helical.sketches[0]).plane));
        form->addRow(tr("Rovina"),base_plane);
        connect(base_plane,&QComboBox::currentIndexChanged,this,[this](int index){
            auto base=sketcher::Sketch::from_serialized(pending.helical.sketches[0]);
            base.plane=static_cast<sketcher::SketchPlane>(index);
            pending.helical.sketches[0]=base.serialized();notify();
        });
        auto* base_offset=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);base_offset->setObjectName("helicalBaseOffset");
        base_offset->set_display_decimals(ui::numeric_decimal_places(this,4));base_offset->setRange(-1000000,1000000);
        base_offset->setValue(sketcher::Sketch::from_serialized(pending.helical.sketches[0]).plane_offset);
        form->addRow(tr("Odsazení základní skici"),base_offset);
        ui::bind_numeric_value_lock(base_offset,"base_offset",pending.value_locks,[this]{notify();});
        connect(base_offset,&QDoubleSpinBox::valueChanged,this,[this](double value){
            auto base=sketcher::Sketch::from_serialized(pending.helical.sketches[0]);base.plane_offset=value;
            pending.helical.sketches[0]=base.serialized();notify();
        });
        circle_=new QComboBox(this);point_=new QComboBox(this);circle_->setObjectName("helicalCircle");point_->setObjectName("helicalStartPoint");
        form->addRow(tr("Základní kružnice"),circle_);form->addRow(tr("Počáteční bod"),point_);
        connect(circle_,&QComboBox::activated,this,[this]{pending.helical.circle_id=circle_->currentData().toString().toStdString();notify();});
        connect(point_,&QComboBox::activated,this,[this]{pending.helical.start_point_id=point_->currentData().toString().toStdString();notify();});
        auto* pitch=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);pitch->setObjectName("helicalPitch");pitch->set_display_decimals(zima::ui::numeric_decimal_places(this,4));pitch->setRange(.0001,1000000);pitch->setValue(pending.helical.pitch);form->addRow(tr("Stoupání"),pitch);
        ui::bind_numeric_value_lock(pitch,"pitch",pending.value_locks,[this]{notify();});
        connect(pitch,&QDoubleSpinBox::valueChanged,this,[this](double p){pending.helical.pitch=p;notify();});
        auto* hand=new QComboBox(this);hand->setObjectName("helicalHandedness");hand->addItems({tr("Pravý"),tr("Levý")});hand->setCurrentIndex(pending.helical.left_handed?1:0);form->addRow(tr("Směr vinutí"),hand);
        connect(hand,&QComboBox::currentIndexChanged,this,[this](int i){pending.helical.left_handed=i!=0;notify();});
        status_=new QLabel(this);status_->setWordWrap(true);form->addRow(status_);content_layout()->addLayout(form);install_operation_buttons();update_thin();refresh_choices();
    }
    void set_status(const QString& text){status_->setText(text);}
    void set_sketch(unsigned stage,const sketcher::Sketch& sketch){pending.helical.sketches.at(stage)=sketch.serialized();if(stage==0)refresh_choices();notify();}
    void refresh_choices(){
        const QSignalBlocker a(circle_),b(point_);circle_->clear();point_->clear();
        circle_->addItem(tr("Vyberte kružnici"),QString());point_->addItem(tr("Vyberte bod"),QString());
        const auto s=sketcher::Sketch::from_serialized(pending.helical.sketches[0]);std::set<std::string> centers;
        for(const auto& c:s.circles){centers.insert(c.center_point_id);if(!c.construction)circle_->addItem(tr("Kružnice %1 — ⌀%2 mm").arg(circle_->count()).arg(2*c.radius),QString::fromStdString(c.id));}
        for(const auto& p:s.points)if(!centers.contains(p.id))point_->addItem(tr("Bod %1 [%2; %3]").arg(point_->count()).arg(p.x).arg(p.y),QString::fromStdString(p.id));
        auto restore=[](QComboBox* box,std::string& id){int i=box->findData(QString::fromStdString(id));if(i<=0&&id.empty()&&box->count()==2){i=1;id=box->itemData(i).toString().toStdString();}box->setCurrentIndex(std::max(0,i));};
        restore(circle_,pending.helical.circle_id);restore(point_,pending.helical.start_point_id);
    }
protected:
    bool submit() override {try{document::PartDocument::reframe_helical_sketches(pending);commit_(pending);return true;}catch(const std::exception& e){set_status(tr(e.what()));return false;}}
private:
    std::function<void(document::HistoryContainer)> commit_;QComboBox* circle_{};QComboBox* point_{};QLabel* status_{};
    void notify(){if(changed)changed();}
};
}
