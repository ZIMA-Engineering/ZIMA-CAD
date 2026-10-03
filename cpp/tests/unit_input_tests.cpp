#include <zima/ui/unit_spin_box.hpp>
#include <zima/ui/container_placement_section.hpp>
#include <zima/document/placement_json.hpp>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace zima::ui;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void near(double actual,double expected){
    if(std::abs(actual-expected)>=1e-10*std::max(1.,std::abs(expected)))
        throw std::runtime_error("Unit input changed physical value: "+std::to_string(actual)+" != "+std::to_string(expected));
}
void enter(UnitDoubleSpinBox& field,const QString& text) {
    field.findChild<QLineEdit*>()->setText(text);
    field.interpretText();
}
void placement_units(QWidget& parent,double scale,double angle_scale) {
    QWidget host(&parent);host.setLocale(QLocale::c());auto* layout=new QVBoxLayout(&host);
    ContainerPlacementSection section(&host,layout,true);
    zima::document::Placement original;
    original.x=25.412345678901;original.y=-.123456789;original.z=1e-12;
    original.rotation_x=12.345678901;original.absolute_rotation_x=original.rotation_x;
    original.rotation_offset_y=-0.;original.orientation_back=true;
    original.orientation_quarter_turns=2;original.value_locks={"y"};
    section.initialize_numeric_values(original);
    int changes=0;section.set_changed_callback([&]{++changes;});
    for(auto* field:section.translation_fields()) {
        auto* unit=dynamic_cast<UnitDoubleSpinBox*>(field);
        check(unit&&unit->native_per_unit()==scale,"Placement uses the wrong length unit");
        enter(*unit,unit->text());
    }
    for(auto* field:section.rotation_fields()) {
        auto* unit=dynamic_cast<UnitDoubleSpinBox*>(field);
        check(unit&&unit->native_per_unit()==angle_scale,"Placement uses the wrong angular unit");
        enter(*unit,unit->text());
    }
    auto pending=section.numeric_placement();
    check(pending.x==original.x&&pending.y==original.y&&pending.z==original.z&&
        pending.absolute_rotation_x==original.absolute_rotation_x&&
        pending.orientation_back==original.orientation_back&&
        pending.orientation_quarter_turns==original.orientation_quarter_turns&&
        std::signbit(pending.rotation_offset_y),"Unchanged placement lost precision or side choice");
    check(changes==0,"Unchanged placement triggered preview/recalculation");
    check(section.translation_fields()[1]->isReadOnly(),"Placement lost its numeric lock");
    enter(*dynamic_cast<UnitDoubleSpinBox*>(section.translation_fields()[0]),"1.001");
    near(section.numeric_placement().x,1.001*scale);
    enter(*dynamic_cast<UnitDoubleSpinBox*>(section.rotation_fields()[0]),".125");
    near(section.numeric_placement().absolute_rotation_x,.125*angle_scale);
    zima::document::ConstructionReference reference{"","plane","face",-.123456789,true};
    reference.flip=true;
    section.initialize_from_references({reference},[](const auto&){return QString("Plane");});
    section.refresh_reference_table();
    auto* offset=dynamic_cast<UnitDoubleSpinBox*>(section.reference_table()->cellWidget(0,3));
    check(offset&&offset->native_per_unit()==scale,"Reference offset uses the wrong unit");
    enter(*offset,offset->text());
    check(section.references().front().offset==reference.offset&&section.references().front().flip,
        "Unchanged reference lost its offset or side choice");
    enter(*offset,".5");near(section.references().front().offset,.5*scale);
    zima::kernel::ViewerReferenceGeometry geometry;
    geometry.vertices={{0,0,0},{0,1,0},{0,0,1}};geometry.triangles={0,1,2};
    geometry.triangle_references={{"plane","face",{}}};
    pending=section.numeric_placement();pending.references=section.populated_references();
    const nlohmann::json encoded=pending;
    auto reopened=encoded.get<zima::document::Placement>();
    check(reopened.references.front().flip,"Persistence lost reference side");
    check(zima::document::resolve_placement(reopened,geometry),"Converted reference no longer solves");
    near(std::abs(reopened.x),.5*scale);
    reference.offset=-0.;section.initialize_from_references({reference},[](const auto&){return QString("Plane");});
    section.refresh_reference_table();offset=dynamic_cast<UnitDoubleSpinBox*>(section.reference_table()->cellWidget(0,3));
    enter(*offset,offset->text());
    check(std::signbit(section.references().front().offset)&&section.references().front().flip,
        "Zero offset lost its saved side semantics");
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
            placement_units(parent,scale,QString(angular)=="rad"?180./std::numbers::pi:1.);
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
