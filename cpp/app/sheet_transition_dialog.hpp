#pragma once
#include "sweep_placement_dialog.hpp"
#include "sketch_button_style.hpp"
#include <zima/ui/unit_spin_box.hpp>
#include <zima/document/sheet_transition.hpp>
#include <transition_sketches.hpp>
#include <QPushButton>
#include <QTabWidget>
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
        : SweepPlacementDialog(tr("Vlastnosti přechodu plechu"),std::move(initial),parent),commit_(std::move(commit)) {
        setObjectName("sheetTransitionDialog");setAttribute(Qt::WA_DeleteOnClose);
        auto* form=new QFormLayout;content_layout()->addLayout(form);name_=new QLineEdit(QString::fromStdString(pending.name),this);form->addRow(tr("Název"),name_);
        connect(name_,&QLineEdit::textChanged,this,[this](const QString& name){pending.name=name.toStdString();});
        install_placement();
        auto* tabs=new QTabWidget(this);tabs->setObjectName("transitionParameterTabs");content_layout()->addWidget(tabs);
        auto* geometry_page=new QWidget(tabs);auto* geometry_layout=new QVBoxLayout(geometry_page);
        form=new QFormLayout;auto* geometry_form=form;geometry_layout->addLayout(form);geometry_layout->addStretch();tabs->addTab(geometry_page,tr("Geometrie"));
        form->addRow(new QLabel(tr("Druhý počátek — vůči počátku kontejneru"),this));
        const std::array<QString,3> axes{QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("Z")};
        for(unsigned kind=0;kind<2;++kind)for(unsigned i=0;i<3;++i) {
            auto* field=new ui::UnitDoubleSpinBox(kind?ui::InputQuantity::Angle:ui::InputQuantity::Length,this);end_fields_[kind*3+i]=field;
            field->setObjectName(QString(kind?"transitionEndRotation%1":"transitionEndPosition%1").arg(i));
            field->set_display_decimals(ui::numeric_decimal_places(this,4));field->setRange(-1000000,1000000);
            auto& v=kind?pending.sheet_transition.end_rotation:pending.sheet_transition.end_position;
            field->setValue(i==0?v.x:i==1?v.y:v.z);form->addRow((kind?tr("Natočení %1"):tr("Posun %1")).arg(axes[i]),field);
            connect(field,&QDoubleSpinBox::valueChanged,this,[this,kind,i](double value){auto& v=kind?pending.sheet_transition.end_rotation:pending.sheet_transition.end_position;(i==0?v.x:i==1?v.y:v.z)=value;notify();});
        }
        auto* note=new QLabel(tr("Skici určují vnější rozměry a umístění. Tloušťka směřuje dovnitř; osy rozvinu patří na vnitřní povrch."),this);note->setWordWrap(true);form->addRow(note);
        const auto value=[&](const QString& label,const char* object,double initial,double minimum,double maximum,ui::InputQuantity quantity=ui::InputQuantity::Length){auto* box=new ui::UnitDoubleSpinBox(quantity,this);box->setObjectName(object);box->set_display_decimals(4);box->setRange(minimum,maximum);box->setValue(initial);form->addRow(label,box);return box;};
        thickness_=value(tr("Tloušťka"),"transitionThickness",pending.sheet_transition.thickness,.001,1000);
        radius_=value(tr("Vnitřní poloměr ohybu"),"transitionRadius",pending.sheet_transition.inside_radius,.001,1000);
        factor_=value(tr("K-faktor"),"transitionKFactor",pending.sheet_transition.k_factor,0,1,ui::InputQuantity::Scalar);
        const bool rectangular=document::rectangular_sheet_transition(pending);
        if(rectangular) {
            auto* sides=new QComboBox(this);sides->setObjectName("transitionSides");
            sides->addItem(tr("2 sousední strany (L)"),2);sides->addItem(tr("3 strany (U)"),3);
            const auto input=research::transition::read_rectangular_sketches(sketcher::Sketch::from_serialized(pending.sheet_transition.sketches[1]),sketcher::Sketch::from_serialized(pending.sheet_transition.sketches[0]));
            sides->setCurrentIndex(input.model.sides==2?0:1);form->addRow(tr("Počet stran"),sides);
            connect(sides,&QComboBox::currentIndexChanged,this,[this,sides]{
                try {document::set_rectangular_transition_sides(pending,sides->currentData().toUInt());notify();}
                catch(const std::exception& error){set_status(tr(error.what()));}
            });
        }else for(std::size_t i=0;i<2;++i){counts_[i]=new QSpinBox(this);counts_[i]->setObjectName(i==0?"transitionRightFacets":"transitionLeftFacets");counts_[i]->setRange(2,128);counts_[i]->setValue(pending.sheet_transition.facets[i]);form->addRow(i==0?tr("Počet plošek pravého rohu"):tr("Počet plošek levého rohu"),counts_[i]);}
        auto* marking_page=new QWidget(tabs);auto* marking_layout=new QVBoxLayout(marking_page);
        form=new QFormLayout;marking_layout->addLayout(form);marking_layout->addStretch();tabs->addTab(marking_page,tr("Výroba"));
        auto& marking=pending.sheet_transition;
        notches_=new QCheckBox(tr("Zářezy na koncích ohybů"),this);notches_->setObjectName("transitionEndNotches");notches_->setChecked(marking.end_notches);form->addRow(notches_);
        notch_depth_=value(tr("Hloubka zářezu"),"transitionNotchDepth",marking.end_notch_depth,.001,1000);
        short_axes_=new QCheckBox(tr("Osy ohybů pouze na koncích"),this);short_axes_->setObjectName("transitionShortAxes");short_axes_->setChecked(marking.short_bend_axes);form->addRow(short_axes_);
        axis_length_=value(tr("Délka konce osy"),"transitionAxisEndLength",marking.bend_axis_end_length,.001,1000000);
        if(!rectangular) {
            reliefs_=new QCheckBox(tr("Odlehčit oba rohy na obdélníkovém konci"),this);reliefs_->setObjectName("transitionReliefs");reliefs_->setChecked(marking.rectangle_reliefs);form->addRow(reliefs_);
            relief_depth_=value(tr("Hloubka odlehčení"),"transitionReliefDepth",marking.rectangle_relief_depth,.001,1000);
            auto* help=new QLabel(tr("Odlehčení souvisle zkrátí oba rohy včetně plošek mezi ohyby. Hloubka se měří kolmo k obdélníkovému konci."),this);help->setWordWrap(true);form->addRow(help);
        }
        const auto marking_changed=[this]{read_parameters();update_marking_controls();notify();};
        for(auto* box:{notches_,short_axes_,reliefs_})if(box)connect(box,&QCheckBox::toggled,this,marking_changed);
        for(auto* field:{notch_depth_,axis_length_,relief_depth_})if(field)connect(field,&QDoubleSpinBox::valueChanged,this,marking_changed);
        update_marking_controls();
        form=geometry_form;
        for(unsigned i:{1u,0u}) {
            auto* button=new QPushButton(rectangular?(i==0?tr("Skica druhého obdélníku"):tr("Skica prvního obdélníku")):(i==0?tr("Skica půlkruhu"):tr("Skica zaobleného půlobdélníku")),this);
            button->setObjectName(QString("transitionSketch%1").arg(i));style_sketch_button(button);form->addRow(button);
            connect(button,&QPushButton::clicked,this,[this,i]{if(edit_sketch)edit_sketch(i);});
        }
        status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);
        for(auto* field:{thickness_,radius_,factor_})connect(field,&QDoubleSpinBox::valueChanged,this,[this]{read_parameters();notify();});
        for(auto* field:counts_)if(field)connect(field,&QSpinBox::valueChanged,this,[this]{read_parameters();notify();});
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
    void update_marking_controls() {
        notch_depth_->setEnabled(notches_->isChecked());axis_length_->setEnabled(short_axes_->isChecked());
        if(reliefs_)relief_depth_->setEnabled(reliefs_->isChecked());
    }
    void notify(){if(changed)changed();}
    void read_parameters(){auto& p=pending.sheet_transition;p.thickness=thickness_->value();p.inside_radius=radius_->value();p.k_factor=factor_->value();
        p.end_notches=notches_->isChecked();p.end_notch_depth=notch_depth_->value();
        p.short_bend_axes=short_axes_->isChecked();p.bend_axis_end_length=axis_length_->value();
        if(reliefs_){p.rectangle_reliefs=reliefs_->isChecked();p.rectangle_relief_depth=relief_depth_->value();}
        for(unsigned i=0;i<2;++i)if(counts_[i])p.facets[i]=counts_[i]->value();}
    bool submit()override {
        try {read_parameters();document::reframe_sheet_transition(pending);
            auto& p=pending.sheet_transition;p.relieved_bends.clear();
            if(p.rectangle_reliefs){const auto keys=document::sheet_transition_bend_keys(pending);p.relieved_bends.insert(keys.begin(),keys.end());}
            commit_(pending);return true;}
        catch(const std::exception& error){set_status(tr(error.what()));return false;}
    }
    std::function<void(document::HistoryContainer)> commit_;
    QCheckBox *notches_{},*short_axes_{},*reliefs_{};
    QDoubleSpinBox *notch_depth_{},*axis_length_{},*relief_depth_{};
    QLineEdit* name_{};std::array<QSpinBox*,2> counts_{};std::array<QDoubleSpinBox*,6> end_fields_{};QDoubleSpinBox *thickness_{},*radius_{},*factor_{};QLabel* status_{};
};
}
