#include <zima/ui/unit_spin_box.hpp>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace zima::ui;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void near(double actual,double expected){check(std::abs(actual-expected)<1e-10*std::max(1.,std::abs(expected)),"Unit input changed physical value");}
void enter(UnitDoubleSpinBox& field,const QString& text) {
    field.findChild<QLineEdit*>()->setText(text);
    field.interpretText();
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        for(const auto unit:{"mm","cm","m","in"})for(const auto angular:{"deg","rad"}) {
            QWidget parent;parent.setProperty("zimaDocumentUnits",QVariantMap{{"Length",unit},{"Angle",angular}});
            UnitDoubleSpinBox length(InputQuantity::Length,&parent);
            length.setLocale(QLocale::c());length.set_display_decimals(3);length.setRange(-1e6,1e6);
            const double scale=zima::document::length_unit_mm(unit);
            length.setValue(25.4);check(length.suffix()==QString(" ")+unit,"Wrong length unit suffix");
            check(length.text().contains(QLocale::c().toString(25.4/scale,'f',3)),"Wrong displayed length");
            enter(length,length.text());near(length.value(),25.4);
            enter(length,"1.001");near(length.value(),1.001*scale);
            length.setSingleStep(.1);length.stepUp();near(length.value(),1.101*scale);
            length.stepDown();near(length.value(),1.001*scale);
            for(const double original:{0.12345678901234567,1e-20,-1e-20,999999.123456789}) {
                length.setValue(original);enter(length,length.text());
                check(length.value()==original,"Unchanged rounded text lost native precision");
            }
            UnitDoubleSpinBox angle(InputQuantity::Angle,&parent);angle.setLocale(QLocale::c());angle.setRange(-360,360);angle.set_display_decimals(6);
            angle.setValue(180);enter(angle,angle.text());near(angle.value(),180);
            enter(angle,"1");near(angle.value(),QString(angular)=="rad"?180./std::numbers::pi:1.);
            UnitDoubleSpinBox count(InputQuantity::Scalar,&parent);count.setRange(0,100);enter(count,"2");near(count.value(),2);check(count.suffix().isEmpty(),"Count acquired length units");
        }
        for(const auto* language:{"cs_CZ","en_US","de_DE","fr_FR","ru_RU"}) {
            QWidget outer;outer.setProperty("zimaDocumentUnits",QVariantMap{{"Length","cm"}});
            QWidget inner(&outer);inner.setProperty("zimaDocumentUnits",QVariantMap{{"Length","in"}});
            UnitDoubleSpinBox field(InputQuantity::Length,&inner);
            field.setLocale(QLocale(language));field.setPrefix("L = ");field.setRange(-1e6,1e6);
            field.setValue(50.8);
            check(field.suffix()==" in","Unit input ignored nearest document context");
            const auto text=field.locale().toString(1.0005,'f',4);
            enter(field,field.prefix()+text+field.suffix());near(field.value(),1.0005*25.4);
            int changes=0;
            QObject::connect(&field,&QDoubleSpinBox::valueChanged,[&]{++changes;});
            enter(field,field.text());check(changes==0,"Unchanged localized input emitted a geometry change");
            field.set_display_decimals(6);check(changes==0,"Display precision changed geometry");
            near(field.value(),1.0005*25.4);
            field.set_quantity(InputQuantity::Scalar);check(changes==0,"Changing input quantity modified native storage");
        }
        std::cout<<"Canonical unit input, mm/cm/m/in, deg/rad, stepping and unchanged rounded values passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
