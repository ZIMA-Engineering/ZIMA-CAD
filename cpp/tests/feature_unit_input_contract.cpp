#include "primitive_properties_dialog.hpp"
#include "construction_properties_dialog.hpp"
#include "sweep2d_dialog.hpp"
#include "helical_sweep_dialog.hpp"
#include <zima/document/physical_properties.hpp>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <numbers>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void near(double actual,double expected) {
    check(std::abs(actual-expected)<1e-10*std::max(1.,std::abs(expected)),"Feature input changed canonical units");
}
void enter(QDoubleSpinBox* field,const QString& text) {
    check(field,"Feature numeric field is missing");
    field->findChild<QLineEdit*>()->setText(text);field->interpretText();
}
}

int verify_feature_unit_input(QApplication& app,QWidget& parent) {
    using namespace zima;
    parent.resize(1600,1100);
    parent.setProperty("zimaDocumentDecimalPlaces",3);
    for(const auto unit:{"mm","cm","m","in"})for(const auto angular:{"deg","rad"}) {
        parent.setProperty("zimaDocumentUnits",QVariantMap{{"Length",unit},{"Angle",angular}});
        const double scale=document::length_unit_mm(unit);
        const double angle_scale=QString(angular)=="rad"?180./std::numbers::pi:1.;
        auto initial=document::PartDocument::create_feature_container("unit-profile");
        initial.feature.sides[0].length=25.412345678901;
        initial.feature.sides[0].draft_angle_degrees=2.123456789;
        initial.feature.sides[0].angle_degrees=90.123456789;
        initial.feature.thin_thickness=.123456789;
        initial.feature.profile_plane_offset=-.123456789;
        int commits=0;
        document::HistoryContainer stored;
        const auto create=[&] {
            auto* dialog=new app::PrimitivePropertiesDialog(initial,true,false,
                [&](document::HistoryContainer value){stored=std::move(value);++commits;},&parent);
            dialog->setLocale(QLocale::c());dialog->show();app.processEvents();return dialog;
        };
        auto* dialog=create();
        auto* field=dialog->findChild<QDoubleSpinBox*>("featureSideValue0");
        check(field&&field->suffix()==QString(" ")+unit,"Feature displays the wrong document length unit");
        check(field->cleanText()==QLocale::c().toString(initial.feature.sides[0].length/scale,'f',3),"Feature displays an unconverted length");
        if(QString(unit)=="in"&&QString(angular)=="rad") {
            const auto path=std::filesystem::current_path()/"Projects/test/unit-input/feature-in-rad.png";
            std::filesystem::create_directories(path.parent_path());
            check(dialog->grab().save(QString::fromStdString(path.string())),"Cannot capture inch Feature dialog");
        }
        enter(field,field->text());
        check(dialog->pending_value().feature==initial.feature,"Unchanged Feature fields quantized the definition");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();app.processEvents();
        check(commits==0,"Unchanged Feature OK requested a calculation/transaction");

        dialog=create();field=dialog->findChild<QDoubleSpinBox*>("featureSideValue0");
        enter(field,"1.001");near(dialog->pending_value().feature.sides[0].length,1.001*scale);
        auto* mode=dialog->findChild<QComboBox*>("featureSideMode0");
        mode->setCurrentIndex(2);
        check(field->suffix()==(QString(angular)=="rad"?QString(" rad"):QString::fromUtf8(" °")),"Rotation retained length units");
        near(field->value(),initial.feature.sides[0].angle_degrees);
        enter(field,"1.25");near(dialog->pending_value().feature.sides[0].angle_degrees,1.25*angle_scale);
        mode->setCurrentIndex(1);near(field->value(),1.001*scale);
        mode->setCurrentIndex(2);near(field->value(),1.25*angle_scale);
        dialog->reject();app.processEvents();check(commits==0,"Cancel committed converted values");

        dialog=create();field=dialog->findChild<QDoubleSpinBox*>("featureSideValue0");
        enter(field,"1.001");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();app.processEvents();
        check(commits==1,"Changed Feature did not commit exactly once");
        near(stored.feature.sides[0].length,1.001*scale);
        check(stored.feature.thin_thickness==initial.feature.thin_thickness&&
            stored.feature.profile_plane_offset==initial.feature.profile_plane_offset&&
            stored.feature.sides[0].draft_angle_degrees==initial.feature.sides[0].draft_angle_degrees,
            "Editing one length changed another native value");

        auto sweep2d=document::PartDocument::create_sweep2d_container();
        sweep2d.sweep2d.result_type=document::ProfileResultType::Thin;
        sweep2d.sweep2d.thickness=.123456789;
        auto* flat=new app::Sweep2DDialog(sweep2d,[&](auto){++commits;},&parent);
        flat->setLocale(QLocale::c());flat->show();app.processEvents();
        auto* wall=flat->findChild<QDoubleSpinBox*>("sweep2dThickness");
        check(wall&&wall->suffix()==QString(" ")+unit,"2D Sweep thickness ignores document units");
        enter(wall,wall->text());check(flat->pending.sweep2d.thickness==sweep2d.sweep2d.thickness,"Opening 2D Sweep quantized thickness");
        enter(wall,".125");near(flat->pending.sweep2d.thickness,.125*scale);
        flat->reject();app.processEvents();check(commits==1,"2D Sweep Cancel committed input");

        auto helical=document::PartDocument::create_helical_sweep_container();
        helical.helical.pitch=12.345678901;
        auto* helix=new app::HelicalSweepDialog(helical,[&](auto){++commits;},&parent);
        helix->setLocale(QLocale::c());helix->show();app.processEvents();
        auto* pitch=helix->findChild<QDoubleSpinBox*>("helicalPitch");
        check(pitch&&pitch->suffix()==QString(" ")+unit,"H Sweep pitch ignores document units");
        enter(pitch,pitch->text());check(helix->pending.helical.pitch==helical.helical.pitch,"Opening H Sweep quantized pitch");
        enter(pitch,".25");near(helix->pending.helical.pitch,.25*scale);
        auto* offset=helix->findChild<QDoubleSpinBox*>("helicalBaseOffset");
        enter(offset,"-.125");near(sketcher::Sketch::from_serialized(helix->pending.helical.sketches[0]).plane_offset,-.125*scale);
        auto* approximation=helix->findChild<QDoubleSpinBox*>("sweepPrecisionTolerance");
        check(approximation&&approximation->suffix()==" mm","Document units relabelled kernel precision");
        helix->reject();app.processEvents();check(commits==1,"H Sweep Cancel committed input");

        auto sweep3d=document::PartDocument::create_sweep3d_container();
        sweep3d.sweep3d.result_type=document::ProfileResultType::Thin;
        sweep3d.sweep3d.thickness=.123456789;
        auto* spatial=new app::ConstructionPropertiesDialog(sweep3d,true,true,[&](document::HistoryContainer){++commits;},&parent);
        spatial->setLocale(QLocale::c());spatial->show();app.processEvents();
        wall=spatial->findChild<QDoubleSpinBox*>("sweep3DThickness");
        check(wall&&wall->suffix()==QString(" ")+unit,"3D Sweep thickness ignores document units");
        enter(wall,wall->text());check(spatial->pending_sweep_value().sweep3d.thickness==sweep3d.sweep3d.thickness,"Opening 3D Sweep quantized thickness");
        enter(wall,".125");near(spatial->pending_sweep_value().sweep3d.thickness,.125*scale);
        spatial->reject();app.processEvents();check(commits==1,"3D Sweep Cancel committed input");
    }
    parent.setProperty("zimaDocumentUnits",QVariant{});
    parent.setProperty("zimaDocumentDecimalPlaces",QVariant{});
    std::cout<<"Feature and Sweep mm/cm/m/in and deg/rad input, side-mode switching, exact no-op and Cancel passed\n";
    return 0;
}
