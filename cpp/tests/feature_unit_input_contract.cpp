#include "primitive_properties_dialog.hpp"
#include "construction_properties_dialog.hpp"
#include "sweep2d_dialog.hpp"
#include "helical_sweep_dialog.hpp"
#include "numeric_expression_edit.hpp"
#include "sketch_dimension_properties_dialog.hpp"
#include "sketch_properties_dialog.hpp"
#include "sketch_bspline_properties_dialog.hpp"
#include "sketch_text_properties_dialog.hpp"
#include "sketch_offset_dialog.hpp"
#include <zima/document/physical_properties.hpp>
#include <zima/ui/unit_spin_box.hpp>
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
void sketch_inputs(QApplication& application,QWidget& parent,double scale,double angle_scale) {
    using namespace zima;
    for(const auto quantity:{ui::InputQuantity::Length,ui::InputQuantity::Angle}) {
        const double factor=quantity==ui::InputQuantity::Length?scale:angle_scale;
        app::UnitExpressionDoubleSpinBox field(quantity,&parent);
        field.setLocale(QLocale::c());field.setRange(-1e9,1e9);field.setValue(.123456789123);
        check(field.expression_value()==field.value(),"Unchanged dimension expression lost exact value");
        field.findChild<QLineEdit*>()->setText("1/2 + 0.125");near(field.expression_value(),.625*factor);
        field.interpretText();near(field.value(),.625*factor);
        field.findChild<QLineEdit*>()->setText("-0");
        QFocusEvent out(QEvent::FocusOut);QApplication::sendEvent(&field,&out);
        check(field.expression_value()==0&&std::signbit(field.expression_value()),"Expression focus-out lost authored negative zero");
        field.findChild<QLineEdit*>()->setText("1 / 0");const auto invalid_text=field.findChild<QLineEdit*>()->text();QApplication::sendEvent(&field,&out);
        bool rejected=false;try{static_cast<void>(field.expression_value());}catch(const std::exception&){rejected=true;}
        check(rejected,"Invalid expression was accepted");check(field.findChild<QLineEdit*>()->text()==invalid_text,"Invalid expression was silently repaired");
        field.findChild<QLineEdit*>()->setText(quantity==ui::InputQuantity::Length?"0,254inch":".5rad");
        near(field.expression_value(),quantity==ui::InputQuantity::Length?6.4516:.5*180./std::numbers::pi);
        field.findChild<QLineEdit*>()->setText(quantity==ui::InputQuantity::Length?"25,4mm":"90deg");
        near(field.expression_value(),quantity==ui::InputQuantity::Length?25.4:90.);
        field.findChild<QLineEdit*>()->setText(quantity==ui::InputQuantity::Length?"1rad":"1mm");
        rejected=false;try{static_cast<void>(field.expression_value());}catch(const std::exception&){rejected=true;}
        check(rejected,"Incompatible dimension unit was accepted");
    }
    for(const auto kind:{sketcher::DimensionKind::Distance,sketcher::DimensionKind::AngleBetween}) {
        sketcher::SketchDimension initial{"units",kind,"first","second",12.3456789};
        std::optional<sketcher::SketchDimension> stored;
        auto* dialog=new app::SketchDimensionPropertiesDialog(initial,true,[&](auto d){stored=std::move(d);},&parent);
        dialog->setLocale(QLocale::c());
        auto* field=dialog->findChild<QDoubleSpinBox*>("sketchDimensionValue");
        auto* unit=dynamic_cast<app::UnitExpressionDoubleSpinBox*>(field);
        const double factor=kind==sketcher::DimensionKind::Distance?scale:angle_scale;
        check(unit&&unit->native_per_unit()==factor,"Sketch dimension uses incorrect units");
        unit->findChild<QLineEdit*>()->setText(kind==sketcher::DimensionKind::Distance?"(1/2 + .125)in":"90deg");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
        check(stored.has_value(),"Converted Sketch dimension was not committed");
        near(sketcher::dimension_display_value(*stored),kind==sketcher::DimensionKind::Distance?.625*25.4:90.);
    }
    sketcher::Sketch sketch;sketch.plane_offset=.123456789;
    auto* properties=new app::SketchPropertiesDialog(sketch,{},true,{},[](auto,auto,bool){},&parent);
    properties->setLocale(QLocale::c());
    auto* offset=properties->findChild<QDoubleSpinBox*>("sketchPlaneOffset");
    enter(offset,offset->text());check(properties->pending_value().first.plane_offset==sketch.plane_offset,"Sketch plane no-op lost precision");
    enter(offset,".125");near(properties->pending_value().first.plane_offset,.125*scale);
    properties->reject();application.processEvents();
    const std::vector<std::array<double,2>> points{{.123456789,0},{10,20},{30,40},{50,60}};
    std::vector<std::array<double,2>> saved_points;
    auto* spline=new app::SketchBSplinePropertiesDialog(3,false,points,[&](auto,bool,const auto& p){saved_points=p;},&parent);
    spline->setLocale(QLocale::c());
    auto* x=spline->findChild<QDoubleSpinBox*>("splineX1");enter(x,".125");
    spline->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
    check(saved_points.size()==points.size()&&saved_points[0]==points[0],"Spline edit changed untouched precise points");
    near(saved_points[1][0],.125*scale);
    for(const bool paper:{false,true}) {
        sketcher::SketchText text;text.value="A";text.height=2.5123456789;
        std::optional<sketcher::SketchText> saved;
        auto* dialog=new app::SketchTextPropertiesDialog(text,std::array<double,2>{0,0},{},[&](auto t){saved=std::move(t);},&parent,false,paper);
        dialog->setLocale(QLocale::c());
        auto* height=dialog->findChild<QDoubleSpinBox*>("sketchTextHeight");
        auto* angle=dialog->findChild<QDoubleSpinBox*>("sketchTextAngle");
        enter(height,".125");enter(angle,".5");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
        check(saved.has_value(),"Text was not committed");near(saved->height,.125*(paper?1:scale));near(saved->angle_degrees,.5*(paper?1:angle_scale));
    }
    sketcher::SketchOffset definition;definition.distance=.123456789;definition.source_id="source";
    auto* offset_dialog=new app::SketchOffsetDialog(definition,[](auto,bool){},&parent);offset_dialog->setLocale(QLocale::c());
    auto* distance=offset_dialog->findChild<QDoubleSpinBox*>("sketchOffsetDistance");
    enter(distance,distance->text());check(offset_dialog->values().distance==definition.distance,"Unchanged Sketch offset lost precision");
    enter(distance,".125");near(offset_dialog->values().distance,.125*scale);
    offset_dialog->reject();application.processEvents();
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
        sketch_inputs(app,parent,scale,angle_scale);
        const auto check_primitive=[&](document::HistoryContainer initial,const char* length_name,
                                     const char* angle_name,const char* scalar_name=nullptr) {
            int calls=0;
            auto* dialog=new app::PrimitivePropertiesDialog(initial,true,false,[&](auto){++calls;},&parent);
            dialog->setLocale(QLocale::c());dialog->show();app.processEvents();
            const auto inspect=[&](const char* name,double factor) {
                if(!name)return;
                auto* field=dialog->findChild<QDoubleSpinBox*>(name);
                auto* unit_field=dynamic_cast<ui::UnitDoubleSpinBox*>(field);
                check(unit_field&&unit_field->native_per_unit()==factor,"Primitive quantity uses the wrong units");
                const double original=field->value();
                enter(field,field->text());check(field->value()==original,"Unchanged primitive input lost precision");
                const double authored=std::clamp(1.25,field->minimum()/factor,field->maximum()/factor);
                enter(field,QLocale::c().toString(authored,'g',17));near(field->value(),authored*factor);
            };
            inspect(length_name,scale);inspect(angle_name,angle_scale);inspect(scalar_name,1.);
            dialog->reject();app.processEvents();check(calls==0,"Primitive unit edit escaped Cancel");
        };
        check_primitive(document::PartDocument::create_hole_container(),"holeDiameter","holeDrillPointAngle");
        check_primitive(document::PartDocument::create_thread_container(),"threadBoreLength","threadChamferAngle","threadRunoutPitchFactor");
        check_primitive(document::PartDocument::create_drill_point_container(),nullptr,"drillPointIncludedAngle");
        check_primitive(document::PartDocument::create_twisted_sheet_container(),"twistedSheetWidth","twistedSheetAngle");
        check_primitive(document::PartDocument::create_shell_container(),"shellThickness",nullptr);
        const std::vector<kernel::EdgeReference> edges{{"source","profile:edge",{}}};
        check_primitive(document::PartDocument::create_chamfer_container(edges),"edgeTreatmentPrimary","edgeTreatmentAngle");
        check_primitive(document::PartDocument::create_fillet_container(edges),"edgeTreatmentPrimary",nullptr);
        check_primitive(document::PartDocument::create_extrusion_container("profile"),"extrusionHeight",nullptr);
        check_primitive(document::PartDocument::create_revolution_container("profile"),nullptr,"revolutionAngle");
        auto construction=document::PartDocument::create_construction(document::ConstructionKind::Axis);
        construction.display_size=25.4123456789;
        auto* axis=new app::ConstructionPropertiesDialog(construction,true,[](auto){},&parent);
        axis->setLocale(QLocale::c());
        auto* extent=axis->findChild<QDoubleSpinBox*>("constructionDisplaySize");
        check(extent&&extent->suffix()==QString(" ")+unit,"Construction extent ignores document units");
        enter(extent,extent->text());check(axis->pending_value().display_size==construction.display_size,"Unchanged axis length lost precision");
        enter(extent,".125");near(axis->pending_value().display_size,.125*scale);
        axis->reject();app.processEvents();
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
