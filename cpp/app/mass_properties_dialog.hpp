#pragma once
#include <zima/document/body_properties.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/kernel/dimension_layout.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>
namespace zima::app {
class MassPropertiesDialog final : public ui::PropertiesSubWindow {
public:
    using Save=std::function<void(document::BodyProperties)>;
    using Preview=std::function<void(const document::BodyProperties&)>;
    MassPropertiesDialog(document::BodyProperties row,const std::map<std::string,std::string>& units,
        QString scope,Save save,Preview preview,QWidget* parent)
        :PropertiesSubWindow(tr("Vlastnosti tělesa"),parent),row_(std::move(row)),save_(std::move(save)),preview_(std::move(preview)) {
        setObjectName("massPropertiesDialog");setAttribute(Qt::WA_DeleteOnClose);set_initial_size({460,430});
        length_=document::length_unit_mm(units.at("Length"));mass_=document::mass_unit_kg(units.at("Mass"));
        length_unit_=QString::fromStdString(units.at("Length"));mass_unit_=QString::fromStdString(units.at("Mass"));
        auto* scroll=new QScrollArea(this);scroll->setObjectName("bodyPropertiesScroll");scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
        auto* body=new QWidget(scroll);auto* content=new QVBoxLayout(body);content->setContentsMargins(0,0,6,0);
        scroll->setWidget(body);content_layout()->addWidget(scroll);
        auto* form=new QFormLayout;content->addLayout(form);
        name_=new QLineEdit(QString::fromStdString(row_.name));name_->setObjectName("bodyPropertiesName");form->addRow(tr("Název"),name_);
        auto* location=new QLabel(scope);location->setWordWrap(true);form->addRow(tr("Měřená geometrie"),location);
        result_=new QLabel;result_->setObjectName("bodyPropertiesResults");result_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        result_->setWordWrap(true);content->addWidget(result_);
        auto* axes=new QFormLayout;content->addLayout(axes);
        for(int i=0;i<3;++i) {
            auto* angle=new QDoubleSpinBox;angle->setObjectName(QString("bodyPropertiesRotation%1").arg(i));
            angle->setRange(-360000,360000);angle->setDecimals(6);angle->setSuffix(QStringLiteral("°"));
            angle->setValue(i==0?row_.rotation_degrees.x:i==1?row_.rotation_degrees.y:row_.rotation_degrees.z);
            axes->addRow(i==0?tr("Natočení os kolem X"):i==1?tr("Natočení os kolem Y"):tr("Natočení os kolem Z"),angle);
            connect(angle,&QDoubleSpinBox::valueChanged,this,[this,i](double value){
                (i==0?row_.rotation_degrees.x:i==1?row_.rotation_degrees.y:row_.rotation_degrees.z)=value;refresh();preview_(current());});
        }
        auto* show=new QCheckBox(tr("Zobrazit Origin v těžišti"));show->setObjectName("bodyPropertiesVisible");show->setChecked(row_.visible);
        content->addWidget(show);connect(show,&QCheckBox::toggled,this,[this](bool on){row_.visible=on;preview_(current());});
        auto* hint=new QLabel(tr("Poloha Origin se vypočítá z geometrie. Regenerovat aktualizuje hodnoty v tomto místě historie."));
        hint->setWordWrap(true);content->addWidget(hint);content->addStretch();refresh();
    }
    document::BodyProperties current()const{auto row=row_;row.name=name_->text().trimmed().toStdString();return row;}
protected:
    bool submit()override {save_(current());return true;}
private:
    QString number(double x)const {return QString::fromStdString(kernel::dimension_number(x,ui::numeric_decimal_places(this)));}
    void refresh() {
        if(!row_.error.empty()){result_->setText(tr(row_.error.c_str()));return;}
        if(!row_.integrals){result_->clear();return;}
        QStringList lines;
        lines<<tr("Objem: %1").arg(number(row_.volume/std::pow(length_,3))+" "+length_unit_+QStringLiteral("³"));
        lines<<tr("Povrch: %1").arg(number(row_.area/(length_*length_))+" "+length_unit_+QStringLiteral("²"));
        const auto c=row_.integrals->centroid;
        lines<<tr("Těžiště — X: %1; Y: %2; Z: %3").arg(number(c.x/length_)+" "+length_unit_,number(c.y/length_)+" "+length_unit_,number(c.z/length_)+" "+length_unit_);
        if(row_.density_kg_mm3) {
            lines<<tr("Hmotnost: %1").arg(number(row_.volume * *row_.density_kg_mm3/mass_)+" "+mass_unit_);
            auto t=kernel::inertia_rotate(row_.integrals->inertia,kernel::inertia_frame(row_.rotation_degrees),true);
            const double factor=*row_.density_kg_mm3/(mass_*length_*length_);
            lines<<tr("Hmotnostní momenty v osách Origin (%1):").arg(mass_unit_+QStringLiteral("·")+length_unit_+QStringLiteral("²"));
            lines<<QString("Ixx: %1   Iyy: %2   Izz: %3").arg(number(t[0]*factor),number(t[4]*factor),number(t[8]*factor));
            lines<<QString("Ixy: %1   Ixz: %2   Iyz: %3").arg(number(t[1]*factor),number(t[2]*factor),number(t[5]*factor));
        }else lines<<tr("Hmotnost: není zadaná hustota materiálu.");
        result_->setText(lines.join('\n'));
    }
    document::BodyProperties row_;Save save_;Preview preview_;QLineEdit* name_{};QLabel* result_{};
    double length_{1},mass_{1};QString length_unit_,mass_unit_;
};
}
