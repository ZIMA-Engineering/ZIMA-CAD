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
#include "derived_copy_dialog.hpp"
#include "body_scale_dialog.hpp"
#include "component_properties_dialog.hpp"
#include "sheet_transition_dialog.hpp"
#include "sheet_from_body_dialog.hpp"
#include "shaft_thread_dialog.hpp"
#include "mass_properties_dialog.hpp"
#include "document_tools_dialogs.hpp"
#include <QTableWidget>
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
void family_inputs(QApplication& application,QWidget& parent,double scale,double angle_scale) {
    using namespace zima;
    const auto unit=ui::document_unit(&parent,"Length","mm").toStdString();
    const auto angle=ui::document_unit(&parent,"Angle","deg").toStdString();
    const std::vector<workspace::FamilyReference> references{
        {{"dimension","length","parameter:length"},"d1","Length","10.5",unit,scale},
        {{"dimension","angle","parameter:angle"},"d2","Angle","90",angle,angle_scale},
        {{"feature","solid",{}},"Solid","Solid","yes"}};
    document::FamilyTable model;model.columns={"d1","d2","Solid"};
    for(const auto& reference:references)model.bindings[reference.name]=reference.binding;
    model.instances={{"Variant",{{"d1","0.12345678901234566"},{"d2","90.123456789012337"},{"Solid","no"}},"row"},
        {"Inherited",{},"inherit"},
        {"Exact",{{"d1","2.54"}},"exact"}};
    app::DocumentToolData data;data.family_table=document::serialize_family_table(model);
    app::ApplicationSettings settings;int commits=0;document::FamilyTable saved;
    const auto make=[&] {
        auto* dialog=new app::FamilyTableDialog("Base",data,[&](auto value){saved=document::parse_family_table(value.family_table);++commits;},settings,&parent);
        dialog->set_references(references);dialog->show();application.processEvents();return dialog;
    };
    auto* dialog=make();auto* table=dialog->findChild<QTableWidget*>("familyTableTable");
    check(table->horizontalHeaderItem(4)->text()==QString("d1 [%1]").arg(QString::fromStdString(unit))&&
        table->horizontalHeaderItem(6)->text()==QString("d2 [%1]").arg(QString::fromStdString(angle)),"Family headers omit document units");
    const QString exact_display=unit=="in"?"0.1":unit=="cm"?"0.254":unit=="m"?"0.00254":"2.54";
    check(table->item(3,4)->text()==exact_display,"Exact Family conversion acquired binary decimal tails");
    near(table->item(0,4)->text().toDouble(),10.5/scale);
    near(table->item(1,4)->text().toDouble(),.12345678901234566/scale);
    near(table->item(0,6)->text().toDouble(),90./angle_scale);
    near(table->item(1,6)->text().toDouble(),90.123456789012337/angle_scale);
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
    check(commits==1&&saved==model,
        "Unchanged Family confirmation quantized values, changed identity or lost inheritance");
    dialog=make();table=dialog->findChild<QTableWidget*>("familyTableTable");
    table->item(1,4)->setText("(1/2+0,125)inch");table->item(1,6)->setText("1,5707963267948966rad");
    dialog->end_entry();dialog->set_references(references);
    check(table->item(1,4)->text()=="(1/2+0,125)inch","Refreshing Family inspection overwrote pending input");
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
    check(commits==2&&saved.instances.front().values.at("Solid")=="no","Family edit changed presence");
    near(std::stod(saved.instances.front().values.at("d1")),.625*25.4);
    near(std::stod(saved.instances.front().values.at("d2")),90.);
    dialog=make();table=dialog->findChild<QTableWidget*>("familyTableTable");
    table->item(1,4)->setText(".125");table->item(1,6)->setText(".25");
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
    check(commits==3,"Family implicit-unit input did not confirm");
    near(std::stod(saved.instances.front().values.at("d1")),.125*scale);
    near(std::stod(saved.instances.front().values.at("d2")),.25*angle_scale);
    dialog=make();table=dialog->findChild<QTableWidget*>("familyTableTable");
    table->item(1,4)->setText("-0inch");table->item(1,6)->setText("-0deg");
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
    check(commits==4&&std::signbit(std::stod(saved.instances.front().values.at("d1")))&&
        std::signbit(std::stod(saved.instances.front().values.at("d2"))),"Family input lost signed zero");
    dialog=make();table=dialog->findChild<QTableWidget*>("familyTableTable");
    table->item(1,4)->setText("20mm");dialog->reject();application.processEvents();
    check(commits==4,"Family Cancel committed pending units");
}
void placement_and_pattern_inputs(QApplication& application,QWidget& parent,double scale,double angle_scale) {
    using namespace zima;
    const auto input=[&](QWidget* owner,const char* name,double factor) {
        auto* field=dynamic_cast<ui::UnitDoubleSpinBox*>(owner->findChild<QDoubleSpinBox*>(name));
        check(field&&field->native_per_unit()==factor,"Placement/Pattern input has incorrect units");
        const double original=field->value();enter(field,field->text());
        check(field->value()==original,"Unchanged Placement/Pattern input lost precision");
        enter(field,".125");near(field->value(),.125*factor);
    };
    for(const bool rectangular:{false,true}) {
        auto definition=document::create_sheet_transition(rectangular);
        definition.sheet_transition.thickness=.123456789;
        auto* transition=new app::SheetTransitionDialog(definition,[](auto){},&parent);
        transition->setLocale(QLocale::c());
        input(transition,"transitionThickness",scale);near(transition->pending.sheet_transition.thickness,.125*scale);
        input(transition,"transitionRadius",scale);input(transition,"transitionEndPosition0",scale);
        input(transition,"transitionEndRotation0",angle_scale);input(transition,"transitionKFactor",1.);
        near(transition->pending.sheet_transition.end_position.x,.125*scale);
        near(transition->pending.sheet_transition.end_rotation.x,.125*angle_scale);
        near(transition->pending.sheet_transition.k_factor,.125);
        input(transition,"transitionNotchDepth",scale);input(transition,"transitionAxisEndLength",scale);
        if(!rectangular)input(transition,"transitionReliefDepth",scale);
        check(transition->pending.sheet_transition.sketches==definition.sheet_transition.sketches,"Unit fields changed transition Sketches");
        transition->reject();application.processEvents();
    }
    double saved_thickness{};
    auto* sheet=new app::SheetFromBodyDialog(.123456789,[&](auto,double thickness){saved_thickness=thickness;},&parent);
    sheet->setLocale(QLocale::c());sheet->set_reference({"source","face",{}},.123456789);
    input(sheet,"sheetSourceThickness",scale);sheet->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();near(saved_thickness,.125*scale);
    auto thread_definition=document::PartDocument::create_shaft_thread_container();
    thread_definition.shaft_thread.length=25.4123456789;
    auto* thread=new app::ShaftThreadDialog(thread_definition,[](auto){},&parent);thread->setLocale(QLocale::c());
    const auto original_thread=thread->pending().shaft_thread;
    input(thread,"shaftThreadLength",scale);input(thread,"shaftThreadRunoutFactor",1.);
    near(thread->pending().shaft_thread.length,.125*scale);near(thread->pending().shaft_thread.runout_pitch_factor,.125);
    check(thread->pending().shaft_thread.standard==original_thread.standard&&thread->pending().shaft_thread.designation==original_thread.designation&&thread->pending().shaft_thread.pitch==original_thread.pitch&&thread->pending().shaft_thread.root_diameter==original_thread.root_diameter,"Unit entry changed the thread catalog selection");
    thread->reject();application.processEvents();
    document::BodyProperties properties;properties.name="Unit measurement";properties.rotation_degrees.x=.123456789;
    const std::map<std::string,std::string> units{{"Length",ui::document_unit(&parent,"Length","mm").toStdString()},{"Angle",ui::document_unit(&parent,"Angle","deg").toStdString()},{"Mass","kg"}};
    auto* measurement=new app::MassPropertiesDialog(properties,units,{},[](auto){},[](auto){},&parent);measurement->setLocale(QLocale::c());
    input(measurement,"bodyPropertiesRotation0",angle_scale);near(measurement->current().rotation_degrees.x,.125*angle_scale);
    measurement->reject();application.processEvents();
    for(const bool circular:{false,true}) {
        document::DerivedCopyParameters parameters;parameters.pattern.emplace();parameters.pattern->circular=circular;
        parameters.pattern->angle_degrees=12.3456789;parameters.pattern->full_circle=false;
        parameters.pattern->linear[0].local_axis=0;parameters.pattern->linear[0].spacing=25.4123456789;
        int commits=0;
        auto* pattern=new app::DerivedCopyDialog(document::PartDocument::create_twisted_sheet_container(),parameters,[&](auto,auto){++commits;},&parent);
        pattern->setLocale(QLocale::c());
        input(pattern,"patternAngle",angle_scale);near(pattern->derived_copy.pattern->angle_degrees,.125*angle_scale);
        input(pattern,"patternSpacing0",scale);near(pattern->derived_copy.pattern->linear[0].spacing,.125*scale);
        check(pattern->derived_copy.pattern->count==parameters.pattern->count&&pattern->derived_copy.pattern->linear[0].count==parameters.pattern->linear[0].count,"Unit conversion altered Pattern counts");
        pattern->reject();application.processEvents();check(commits==0,"Pattern Cancel committed pending units");
    }
    document::BodyHistory body;body.name="Unit scale";body.scale=document::BodyScale{};
    body.scale->center={.123456789,2.345678912,3.456789123};body.scale->factor=1.25;
    auto* scaling=new app::BodyScaleDialog(body,[](auto){},&parent);scaling->setLocale(QLocale::c());
    input(scaling,"bodyScaleCenter0",scale);near(scaling->pending.scale->center.x,.125*scale);
    check(scaling->pending.scale->center.y==body.scale->center.y&&scaling->pending.scale->factor==1.25,"Scale center input altered another quantity");
    scaling->reject();application.processEvents();
    auto component=assembly::AssemblyDocument::create_part_occurrence("Unit component","source-part","source.prtz",{});
    component.placement.x=25.4123456789;component.placement.rotation_x=12.3456789;
    int commits=0;std::optional<assembly::PartOccurrence> stored;
    auto* dialog=new app::ComponentPropertiesDialog(component,[&](auto value){++commits;stored=std::move(value);},&parent);
    dialog->setLocale(QLocale::c());
    input(dialog,"componentTranslation",scale);input(dialog,"componentRotation",angle_scale);
    near(dialog->pending_value().placement.x,.125*scale);near(dialog->pending_value().placement.rotation_x,.125*angle_scale);
    assembly::ComponentPlacementReference row;
    row.component_reference={assembly::MateReferenceKind::Face,assembly::InstancePath{}.child(component.occurrence_id),"source","face"};
    row.target_reference={assembly::MateReferenceKind::Face,{},"target","face"};
    row.offset=.123456789;row.flip=true;
    for(const bool angular:{false,true}) {
        row.mate_type=angular?assembly::MateKind::PlaneAngle:assembly::MateKind::PlaneCoincident;
        dialog->set_placement_references({row});
        auto* table=dialog->findChild<QTableWidget*>("componentPlacementTable");
        auto* field=dynamic_cast<ui::UnitDoubleSpinBox*>(table->cellWidget(0,4));
        const double factor=angular?angle_scale:scale;
        check(field&&field->native_per_unit()==factor,"Mate offset has incorrect units");
        enter(field,field->text());check(dialog->placement_references()[0].offset==row.offset,"Mate no-op lost precision");
        enter(field,".125");near(dialog->placement_references()[0].offset,.125*factor);
        auto* limit_button=table->cellWidget(0,6)->findChild<QToolButton*>("mateLimitsButton0");check(limit_button&&limit_button->isEnabled(),"Mate limits button unavailable");limit_button->click();application.processEvents();
        ui::PropertiesSubWindow* limits=nullptr;for(auto* window:parent.findChildren<QDialog*>("mateLimitsDialog"))if(window->isVisible())limits=dynamic_cast<ui::PropertiesSubWindow*>(window);check(limits,"Mate limits dialog missing");
        limits->setLocale(QLocale::c());
        auto* current=dynamic_cast<ui::UnitDoubleSpinBox*>(limits->findChild<QDoubleSpinBox*>("mateCurrentValue"));
        check(current&&current->native_per_unit()==factor,"Mate limits use different units");near(current->value(),.125*factor);
        limits->findChild<QCheckBox*>("mateLowerEnabled")->setChecked(true);
        limits->findChild<QCheckBox*>("mateUpperEnabled")->setChecked(true);
        enter(limits->findChild<QDoubleSpinBox*>("mateLowerLimit"),".1");
        enter(limits->findChild<QDoubleSpinBox*>("mateUpperLimit"),".2");
        limits->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
        near(dialog->placement_references()[0].lower_limit.value(),.1*factor);
        near(dialog->placement_references()[0].upper_limit.value(),.2*factor);
        check(dialog->placement_references()[0].flip&&dialog->placement_references()[0].component_reference==row.component_reference,"Numeric mate input changed reference or side");
    }
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
    check(commits==1&&stored&&stored->source_document_id==component.source_document_id,"Component units changed ownership or commit count");
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
        family_inputs(app,parent,scale,angle_scale);
        placement_and_pattern_inputs(app,parent,scale,angle_scale);
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
