#pragma once
#include "sweep_placement_dialog.hpp"
#include "sketch_button_style.hpp"
#include <zima/document/sheet_transition.hpp>
#include <QPushButton>
#include <zima/document/part_document.hpp>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <functional>
namespace zima::app {
class SheetTransitionDialog final:public SweepPlacementDialog {
public:
    SheetTransitionDialog(document::HistoryContainer initial,std::function<void(document::HistoryContainer)> commit,QWidget* parent)
        : SweepPlacementDialog(tr("Přechod plechu"),std::move(initial),parent),commit_(std::move(commit)) {
        setObjectName("sheetTransitionDialog");setAttribute(Qt::WA_DeleteOnClose);set_initial_size({490,720});
        auto* form=new QFormLayout;content_layout()->addLayout(form);name_=new QLineEdit(QString::fromStdString(pending.name),this);form->addRow(tr("Název"),name_);
        connect(name_,&QLineEdit::textChanged,this,[this](const QString& name){pending.name=name.toStdString();});
        for(unsigned i=0;i<2;++i) {
            auto* button=new QPushButton(i==0?tr("SKETCH — půlkruh"):tr("SKETCH — zaoblený půlobdélník"),this);
            button->setObjectName(QString("transitionSketch%1").arg(i));style_sketch_button(button);form->addRow(button);
            connect(button,&QPushButton::clicked,this,[this,i]{if(edit_sketch)edit_sketch(i);});
        }
        install_placement();form=new QFormLayout;content_layout()->addLayout(form);
        form->addRow(new QLabel(tr("Druhý počátek — vůči počátku kontejneru"),this));
        const std::array<QString,3> axes{QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("Z")};
        for(unsigned kind=0;kind<2;++kind)for(unsigned i=0;i<3;++i) {
            auto* field=new QDoubleSpinBox(this);end_fields_[kind*3+i]=field;
            field->setObjectName(QString(kind?"transitionEndRotation%1":"transitionEndPosition%1").arg(i));
            field->setDecimals(ui::numeric_decimal_places(this,4));field->setRange(-1000000,1000000);field->setSuffix(kind?QStringLiteral(" °"):QStringLiteral(" mm"));
            auto& v=kind?pending.sheet_transition.end_rotation:pending.sheet_transition.end_position;
            field->setValue(i==0?v.x:i==1?v.y:v.z);form->addRow((kind?tr("Natočení %1"):tr("Posun %1")).arg(axes[i]),field);
            connect(field,&QDoubleSpinBox::valueChanged,this,[this,kind,i](double value){auto& v=kind?pending.sheet_transition.end_rotation:pending.sheet_transition.end_position;(i==0?v.x:i==1?v.y:v.z)=value;notify();});
        }
        auto* note=new QLabel(tr("Skici určují vnější rozměry a umístění. Tloušťka směřuje dovnitř; osy rozvinu patří na vnitřní povrch."),this);note->setWordWrap(true);form->addRow(note);
        const auto value=[&](const QString& label,const char* object,double initial,double minimum,double maximum,const QString& unit){auto* box=new QDoubleSpinBox(this);box->setObjectName(object);box->setDecimals(4);box->setRange(minimum,maximum);box->setValue(initial);box->setSuffix(unit);form->addRow(label,box);return box;};
        thickness_=value(tr("Tloušťka"),"transitionThickness",pending.sheet_transition.thickness,.001,1000," mm");
        radius_=value(tr("Vnitřní poloměr ohybu"),"transitionRadius",pending.sheet_transition.inside_radius,.001,1000," mm");
        factor_=value(tr("K-faktor"),"transitionKFactor",pending.sheet_transition.k_factor,0,1,{});
        for(std::size_t i=0;i<2;++i){counts_[i]=new QSpinBox(this);counts_[i]->setObjectName(i==0?"transitionRightFacets":"transitionLeftFacets");counts_[i]->setRange(2,128);counts_[i]->setValue(pending.sheet_transition.facets[i]);form->addRow(i==0?tr("Počet plošek pravého rohu"):tr("Počet plošek levého rohu"),counts_[i]);}
        status_=new QLabel(this);status_->setWordWrap(true);form->addRow(status_);
        for(auto* field:{thickness_,radius_,factor_})connect(field,&QDoubleSpinBox::valueChanged,this,[this]{read_parameters();notify();});
        for(auto* field:counts_)connect(field,&QSpinBox::valueChanged,this,[this]{read_parameters();notify();});
    }
    bool owns_reference_owner(const std::string& owner) const override {return owner==pending.sheet_transition.end_origin_id||SweepPlacementDialog::owns_reference_owner(owner);}
    void set_status(const QString& text) override {status_->setText(text);}
    void set_sketch(unsigned stage,const sketcher::Sketch& sketch) override {pending.sheet_transition.sketches.at(stage)=sketch.serialized();document::reframe_sheet_transition(pending);notify();}
    bool set_inline_parameter_value(std::string_view key,double value) override {
        constexpr std::array<std::string_view,6> keys{"end_x","end_y","end_z","end_rx","end_ry","end_rz"};
        for(unsigned i=0;i<6;++i)if(key==keys[i]){end_fields_[i]->setValue(value);return true;}
        if(key=="thickness"){thickness_->setValue(value);return true;}
        if(key=="inside_radius"){radius_->setValue(value);return true;}
        return SweepPlacementDialog::set_inline_parameter_value(key,value);
    }
private:
    void notify(){if(changed)changed();}
    void read_parameters(){auto& p=pending.sheet_transition;p.thickness=thickness_->value();p.inside_radius=radius_->value();p.k_factor=factor_->value();for(unsigned i=0;i<2;++i)p.facets[i]=counts_[i]->value();}
    bool submit()override {
        try {read_parameters();document::reframe_sheet_transition(pending);commit_(pending);return true;}
        catch(const std::exception& error){set_status(tr(error.what()));return false;}
    }
    std::function<void(document::HistoryContainer)> commit_;
    QLineEdit* name_{};std::array<QSpinBox*,2> counts_{};std::array<QDoubleSpinBox*,6> end_fields_{};QDoubleSpinBox *thickness_{},*radius_{},*factor_{};QLabel* status_{};
};
}
