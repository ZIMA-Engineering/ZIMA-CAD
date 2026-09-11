#include "primitive_properties_dialog.hpp"
#include "component_properties_dialog.hpp"
#include <zima/ui/numeric_value_lock.hpp>
#include <zima/ui/container_placement_section.hpp>
#include <zima/document/placement_json.hpp>
#include <QApplication>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStyleOptionSpinBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
QAction* action(QDoubleSpinBox* field){return field->findChild<QAction*>("valueLock:"+field->property("zimaValueLockKey").toString());}
void check_lock_layout(QDoubleSpinBox* field) {
    auto* button=field->findChild<QToolButton*>("numericValueLockButton");
    auto* editor=field->findChild<QLineEdit*>();
    check(button&&editor,"Numeric lock has no separate button/editor");
    QStyleOptionSpinBox option;option.initFrom(field);option.frame=true;
    option.buttonSymbols=field->buttonSymbols();
    const auto frame=field->style()->subControlRect(QStyle::CC_SpinBox,&option,QStyle::SC_SpinBoxFrame,field);
    check(frame.right()<button->geometry().left(),"Lock overlaps the numeric frame");
    check(editor->geometry().right()<button->geometry().left(),"Lock overlaps the numeric editor");
    check(field->rect().contains(button->geometry()),"Lock escapes the field allocation");
    check(editor->actions().empty(),"Lock still participates in editor action layout");
}
void click_lock(QDoubleSpinBox* field) {
    auto* button=field->findChild<QToolButton*>("numericValueLockButton");
    const auto local=QPointF(button->rect().center());
    const auto global=QPointF(button->mapToGlobal(local.toPoint()));
    QMouseEvent press(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,local,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(button,&press);QApplication::sendEvent(button,&release);
}
void try_type(QDoubleSpinBox* field) {
    field->setFocus();field->selectAll();QKeyEvent event(QEvent::KeyPress,Qt::Key_9,Qt::NoModifier,"9");QApplication::sendEvent(field,&event);
}
}
int verify_numeric_value_locks(QApplication& application,QWidget& parent) {
    using namespace zima;
    auto initial=document::PartDocument::create_box_container();initial.box.length=30;initial.placement.x=12;
    initial.value_locks={"length"};initial.placement.value_locks={"x","rotation_z"};
    int commits=0;document::HistoryContainer stored;
    auto* dialog=new app::PrimitivePropertiesDialog(initial,true,false,[&](document::HistoryContainer value){stored=std::move(value);++commits;},&parent);
    dialog->show();application.processEvents();
    QDoubleSpinBox* length=nullptr;
    for(auto* field:dialog->findChildren<QDoubleSpinBox*>())if(field->property("zimaValueLockKey")=="length")length=field;
    check(length&&length->isReadOnly()&&action(length)->isChecked(),"A saved length lock did not reopen");
    for(int i=0;i<12;++i)application.processEvents();
    for(auto* field:dialog->findChildren<QDoubleSpinBox*>())
        if(field->isVisible()&&action(field))check_lock_layout(field);
    const auto stable_size=dialog->size();
    for(int i=0;i<20;++i){
        click_lock(length);application.processEvents();
        check(length->isReadOnly()==(i%2==1),"Mouse click did not toggle the lock exactly once");
        check(length->value()==30,"Lock click changed the numeric value");
        check(dialog->size()==stable_size,"Lock toggling changed the dialog width");
        check_lock_layout(length);
    }
    const auto screenshot=std::filesystem::current_path()/"Projects/test/numeric-lock-layout.png";
    std::filesystem::create_directories(screenshot.parent_path());
    check(dialog->grab().save(QString::fromStdString(screenshot.string())),"Cannot save numeric lock layout screenshot");
    try_type(length);check(length->value()==30,"Typing changed a locked length");
    check(!dialog->set_inline_parameter_value("length",91),"Inline edit bypassed a length lock");
    check(!dialog->set_inline_parameter_value("placement:x",91),"Inline edit bypassed a placement lock");
    check(!dialog->set_inline_parameter_value("placement:rotation_z",91),"Inline edit bypassed an angular lock");
    action(length)->trigger();check(!length->isReadOnly()&&dialog->set_inline_parameter_value("length",42),"Unlock did not enable value editing");
    dialog->reject();application.processEvents();check(commits==0,"Cancel committed the changed value or lock");
    dialog=new app::PrimitivePropertiesDialog(initial,true,false,[&](document::HistoryContainer value){stored=std::move(value);++commits;},&parent);
    dialog->show();application.processEvents();
    for(auto* field:dialog->findChildren<QDoubleSpinBox*>())if(field->property("zimaValueLockKey")=="length")length=field;
    action(length)->trigger();length->setValue(42);action(length)->trigger();
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
    check(commits==1&&stored.box.length==42&&stored.value_locks.contains("length"),"OK lost the edited value or final lock");
    auto document=document::PartDocument::create_default();document.history={stored};
    auto point=document::PartDocument::create_construction(document::ConstructionKind::Point);point.value_locks={"placement:y"};document.constructions={point};
    const auto file=std::filesystem::current_path()/"Projects/test/value-lock-roundtrip.prtz";std::filesystem::create_directories(file.parent_path());document.save(file);
    const auto reopened=document::PartDocument::load(file);
    check(reopened.history.front().value_locks==stored.value_locks&&reopened.history.front().placement.value_locks==stored.placement.value_locks&&reopened.constructions.front().value_locks==point.value_locks,"Part save/load lost a value lock");
    QWidget host(&parent);auto* layout=new QVBoxLayout(&host);ui::ContainerPlacementSection section(&host,layout,false);
    document::Placement placement;placement.x=12;placement.y=3;placement.z=4;section.initialize_numeric_values(placement);section.refresh_reference_table();host.show();application.processEvents();
    kernel::ViewerReferenceGeometry geometry;geometry.vertices={{0,0,0},{0,1,0},{0,0,1},{5,0,0},{5,1,0},{5,0,1}};geometry.triangles={0,1,2,3,4,5};geometry.triangle_references={{"plane-a","face",{}},{"plane-b","face",{}}};
    document::ConstructionReference reference{"","plane-a","face",0,true};
    reference.measured_offset=document::measure_placement_reference_offset(reference,geometry,{12,3,4});
    check(reference.measured_offset&&std::abs(*reference.measured_offset-12)<1e-10,"Reference measurement has the wrong distance/sign");
    auto offset=[&]{return qobject_cast<QDoubleSpinBox*>(section.reference_table()->cellWidget(0,2));};
    check(offset()&&action(offset()),"Empty reference row has no capture lock");
    action(offset())->trigger();QString error;
    check(section.set_reference(0,reference,"Plane A",&error),"Capture reference was rejected");
    check(section.references().front().offset==12&&!section.references().front().offset_locked&&!offset()->isReadOnly(),"One-shot capture did not fill and unlock the value");
    auto resolved=section.numeric_placement();resolved.references=section.populated_references();
    check(document::resolve_placement(resolved,geometry)&&std::abs(resolved.x-12)<1e-8&&std::abs(resolved.y-3)<1e-8&&std::abs(resolved.z-4)<1e-8,"Capturing the offset moved the origin");
    action(offset())->trigger();try_type(offset());check(offset()->value()==12&&!section.set_reference_offset(0,99),"Permanent reference lock did not reject typing/inline edit");
    reference.owner_id="plane-b";reference.measured_offset=document::measure_placement_reference_offset(reference,geometry,{12,3,4});
    check(section.set_reference(0,reference,"Plane B",&error)&&section.references().front().offset==7&&section.references().front().offset_locked,"Replacing a locked reference lost the current distance or lock");
    const nlohmann::json json=section.references().front();check(json.get<document::ConstructionReference>().offset_locked,"Reference save/load lost its lock");
    section.initialize_from_references({},[](const auto&){return QString{};});section.refresh_reference_table();
    check(section.set_reference(0,{"","axis","axis",29,false},"Axis",&error)&&section.references().front().offset==0&&section.references().front().offset_locked,"Axis reference was not automatically zeroed and locked");
    geometry.axes={{{5,6,0},{0,0,1},100,{"axis","axis",{}},{}}};
    resolved=section.numeric_placement();resolved.references=section.populated_references();
    check(document::resolve_placement(resolved,geometry)&&std::abs(resolved.x-5)<1e-8&&std::abs(resolved.y-6)<1e-8&&std::abs(resolved.z-4)<1e-8,"Axis coincidence did not snap onto the line");
    section.initialize_from_references({},[](const auto&){return QString{};});section.refresh_reference_table();
    check(section.set_reference(0,{"","point","point",29,false},"Point",&error)&&section.references().front().offset==0&&section.references().front().offset_locked,"Point reference was not automatically zeroed and locked");
    geometry.points={{{5,6,7},{"point","point",{}},{}}};
    resolved=section.numeric_placement();resolved.references=section.populated_references();
    check(document::resolve_placement(resolved,geometry)&&std::abs(resolved.x-5)<1e-8&&std::abs(resolved.y-6)<1e-8&&std::abs(resolved.z-7)<1e-8,"Point coincidence did not snap onto the point");
    reference.owner_id="plane-a";reference.offset_locked=true;reference.measured_offset.reset();
    section.initialize_from_references({{},reference},[](const auto&){return QString("Plane");});section.refresh_reference_table();
    auto* surviving=qobject_cast<QDoubleSpinBox*>(section.reference_table()->cellWidget(1,2));
    check(surviving->property("zimaValueLockKey")=="placement:reference_offset:0"&&!section.set_reference_offset(0,9),"An empty row shifted the surviving reference's lock identity");
    action(surviving)->trigger();check(section.set_reference_offset(0,9)&&surviving->value()==9,"Surviving reference could not be edited after unlocking");
    assembly::PartOccurrence component;component.occurrence_id="component";component.name="Component";component.value_locks={"placement:x"};
    auto* component_dialog=new app::ComponentPropertiesDialog(component,[](auto){},&parent);
    component_dialog->set_reference_measure_callback([](const auto&,const auto& row){return std::optional<double>{row.mate_type==assembly::MateKind::PlaneAngle?37.0:15.0};});
    component_dialog->show();application.processEvents();
    auto* table=component_dialog->findChild<QTableWidget*>("componentPlacementTable");
    auto* distance=qobject_cast<QDoubleSpinBox*>(table->cellWidget(0,4));action(distance)->trigger();
    component_dialog->set_placement_reference(0,true,{assembly::MateReferenceKind::Face,{},"a","face"},"A");
    component_dialog->set_placement_reference(0,false,{assembly::MateReferenceKind::Face,{},"b","face"},"B");
    check(component_dialog->pending_value().placement_references.front().offset==15&&!component_dialog->pending_value().placement_references.front().offset_locked,"Assembly one-shot did not capture and unlock the current distance");
    distance=qobject_cast<QDoubleSpinBox*>(table->cellWidget(0,4));action(distance)->trigger();
    check(component_dialog->pending_value().placement_references.front().offset_locked,"Assembly permanent lock was not retained");
    component_dialog->reject();application.processEvents();
    std::cout<<"Numeric value locks: GUI, inline guards, one-shot capture, reference replacement, axis/point, OK/Cancel and persistence passed.\n";
    return 0;
}
