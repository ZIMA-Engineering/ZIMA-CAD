#include "measurement_ui_verification.hpp"
#include "assembly_workspace_window.hpp"
#include "measurement_dialog.hpp"
#include "mass_properties_dialog.hpp"
#include "drawing_window.hpp"
#include "drawing_dimension_dialog.hpp"
#include <zima/viewer/mesh_view.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/measurement_record.hpp>
#include <zima/commands/dispatcher.hpp>
#include <QAction>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QTabBar>
#include <QMenu>
#include <QTimer>
#include <iostream>

namespace zima::app {
int verify_measurement_inspector(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory){
try{
    const auto check=[](bool condition,const char* message){if(!condition)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);};
    auto part=document::PartDocument::create_default();auto box=document::PartDocument::create_box_container();box.box={10,20,30};part.history={box};
    part.physical_parameters["MASS_DENSITY"]="7850";part.physical_parameter_units["MASS_DENSITY"]="kg/m^3";
    kernel::OcctKernel kernel;const auto bodies=kernel.evaluate_history(part.kernel_operations());
    const auto path=directory/"measurement-ui.prtz";part.save(path,bodies);
    check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open measurement fixture");
    window.resize(1200,850);window.show();flush();
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
    auto* action=window.findChild<QAction*>("measureAction");
    check(view&&action&&action->isEnabled()&&!action->icon().isNull(),"Measurement toolbar action missing");
    const auto dialog=[&]()->MeasurementDialog*{
        for(auto* child:window.findChildren<QDialog*>())
            if(auto* d=dynamic_cast<MeasurementDialog*>(child);d&&d->isVisible())return d;
        return nullptr;
    };
    const auto mouse=[&](QEvent::Type type,QPointF at,Qt::MouseButton button){
        QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,
            type==QEvent::MouseButtonRelease||type==QEvent::MouseMove?Qt::NoButton:button,Qt::NoModifier);
        QApplication::sendEvent(view,&event);
    };
    const auto click=[&](QPointF at,Qt::MouseButton button){
        mouse(QEvent::MouseMove,at,Qt::NoButton);mouse(QEvent::MouseButtonPress,at,button);
        mouse(QEvent::MouseButtonRelease,at,button);flush();
    };
    const auto select=[&](viewer::CandidateKind kind,const std::string& occurrence=std::string{}){
        const auto matches=[&](const auto& c){return c.kind==kind&&(occurrence.empty()||c.instance_path==occurrence);};
        for(int y=20;y<view->height()-20;y+=5)for(int x=20;x<view->width()-20;x+=5){
            const QPointF at(x,y);const auto candidates=view->selection_candidates_at(at);
            if(std::ranges::none_of(candidates,matches))continue;
            mouse(QEvent::MouseMove,at,Qt::NoButton);
            for(std::size_t i=0;i<candidates.size();++i){
                const auto offered=view->offered_candidate();
                if(offered&&matches(*offered)){
                    mouse(QEvent::MouseButtonPress,at,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,at,Qt::LeftButton);flush();return;
                }
                mouse(QEvent::MouseButtonPress,at,Qt::RightButton);mouse(QEvent::MouseButtonRelease,at,Qt::RightButton);
            }
        }
        throw std::runtime_error("Measurement reference kind not offered by common picker");
    };
    // Closing a Drawing property window must release the shared command owner
    // before returning to Part and invoking the View inspector.
    auto drawing=drawing::DrawingDocument::create_default();
    drawing.sheets.front().views.push_back(drawing::DrawingDocument::create_view(part.document_id,path,bodies.back().mesh));
    const auto drawing_path=directory/"measurement-lifecycle.drwz";drawing.save(drawing_path);
    check(window.open_document_path(QString::fromStdString(drawing_path.string())),"Cannot open lifecycle Drawing");
    window.findChild<QAction*>("drawingDimensionAction")->trigger();flush();
    DrawingDimensionDialog* dimension_properties{};
    for(auto* child:window.findChildren<QDialog*>())
        if(auto* d=dynamic_cast<DrawingDimensionDialog*>(child);d&&d->isVisible())dimension_properties=d;
    check(dimension_properties,"Drawing dimension properties did not open");
    dimension_properties->reject();flush();
    check(window.open_document_path(QString::fromStdString(path.string())),"Cannot return to Part after Drawing properties");
    flush();
    const auto camera=view->camera_state();action->trigger();flush();
    check(dialog()&&dialog()->windowFlags().testFlag(Qt::SubWindow)&&dialog()->parentWidget()==&window,"Measurement is not an internal Properties window");
    select(viewer::CandidateKind::Vertex);
    check(dialog()->active_reference()==1&&dialog()->geometries()[0]&&dialog()->geometries()[0]->values.position,"First point did not produce coordinates and arm second field");
    click({20,20},Qt::MiddleButton);
    check(dialog()&&dialog()->active_reference()==-1&&!dialog()->inspected()[0],"Short MMB closed inspector or retained entry/highlight");
    mouse(QEvent::MouseButtonDblClick,{20,20},Qt::MiddleButton);flush();
    check(!dialog(),"Double MMB over View did not close inspector");
    check(view->camera_state()==camera,"Measurement close changed camera");
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    check(document::PartDocument::load(path).measurements.empty(),"Closing inspector implicitly saved measurement");
    action->trigger();flush();select(viewer::CandidateKind::Edge);
    check(dialog()->geometries()[0]&&dialog()->geometries()[0]->values.length,"Edge length unavailable");
    select(viewer::CandidateKind::Face);
    check(dialog()->geometries()[1]&&dialog()->geometries()[1]->values.area&&dialog()->distance(),"Second face has no area/distance");
    check(dialog()->findChild<QPushButton*>("saveMeasurement")->isEnabled(),"Valid measurement cannot be saved");
    const auto gui_value=commands::Json::parse(document::serialize_measurements({dialog()->current()})).at(0);
    const commands::Json evaluate_request={{"command","measurement.evaluate"},{"arguments",{{"references",gui_value.at("references")}}}};
    const auto from_console=window.execute_console_command(QString::fromStdString(evaluate_request.dump()));
    check(from_console.ok&&from_console.data.at("values")==gui_value.at("values")&&from_console.data.at("distance")==gui_value.at("distance"),
        "Read-only console measurement disagrees with the active GUI inspector");
    check(dialog()!=nullptr,"Measurement query closed the active inspector");

    auto* tree=window.findChild<QTreeWidget*>();tree->collapseAll();
    dialog()->findChild<QPushButton*>("saveMeasurement")->click();flush();check(!dialog(),"Save did not close inspector");
    check(tree->currentItem()&&tree->currentItem()->data(0,Qt::UserRole+3)=="document-measurement","Save did not reveal/select its Tree record");
    const auto check_before_cursor=[&] {
        auto* record=tree->currentItem();auto* parent=record->parent();check(parent!=nullptr,"Measurement has no owning tree branch");
        check(!parent->data(0,Qt::UserRole+3).toString().endsWith("insert-here"),"Measurement became a child of Insert Here");
        for(int i=0;i<parent->childCount();++i)if(parent->child(i)->data(0,Qt::UserRole+3).toString().endsWith("insert-here"))
            check(parent->indexOfChild(record)<i,"Saved measurement follows Insert Here");
    };
    check_before_cursor();
    for(auto* parent=tree->currentItem()->parent();parent;parent=parent->parent())check(parent->isExpanded(),"Saved measurement is hidden in a collapsed parent");
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    auto stored=document::PartDocument::load(path);
    check(stored.measurements.size()==1&&stored.measurements[0].references.size()==2,"Save did not persist two references");
    const auto saved_list=window.execute_console_command("measurement.list");
    const commands::Json get_request={{"command","measurement.get"},{"arguments",{{"object",stored.measurements[0].id}}}};
    const auto saved_get=window.execute_console_command(QString::fromStdString(get_request.dump()));
    check(saved_list.ok&&saved_list.data.at("total")==1&&saved_get.ok&&saved_get.data.at("values")==gui_value.at("values"),
        "Console cannot read a measurement saved by GUI");

    const auto execute=[&](const char* command,commands::Json args=commands::Json::object()) {
        const commands::Json request={{"command",command},{"arguments",std::move(args)}};
        const auto result=window.execute_console_command(QString::fromStdString(request.dump()));
        if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.code+": "+result.message);return result;
    };
    const auto measurement_id=stored.measurements[0].id;
    check(execute("measurement.set",{{"object",measurement_id},{"name","CLI renamed"}}).data.at("changed")==true,"Console did not edit GUI measurement");
    QTreeWidgetItem* row{};
    for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole+3)=="document-measurement"){row=*i;break;}
    check(row,"Saved measurement not in tree");
    window.show_tree_item_properties(row);flush();
    check(dialog()&&dialog()->distance()&&dialog()->active_reference()==-1,"Saved measurement cannot reopen");
    check(dialog()->findChild<QLineEdit*>("measurementName")->text()=="CLI renamed","Properties did not display the CLI rename");
    dialog()->findChild<QLineEdit*>("measurementName")->setText("GUI renamed");
    dialog()->findChild<QPushButton*>("saveMeasurement")->click();flush();check(!dialog(),"Shared GUI measurement commit did not close");
    const auto renamed=execute("measurement.get",{{"object",measurement_id}}).data;
    check(renamed.at("name")=="GUI renamed"&&renamed.at("references")==gui_value.at("references"),"GUI mutation changed original identities");
    const auto reopen=[&] {
        row=nullptr;for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole+3)=="document-measurement"){row=*i;break;}
        check(row,"Measurement disappeared from tree");window.show_tree_item_properties(row);flush();check(dialog()!=nullptr,"Cannot reopen measurement properties");
    };
    reopen();dialog()->findChild<QPushButton*>("saveMeasurement")->click();flush();
    check(!dialog()&&execute("measurement.get",{{"object",measurement_id}}).data.at("revision")==renamed.at("revision"),"Unchanged GUI Save added history");
    execute("measurement.delete",{{"object",measurement_id}});execute("undo");reopen();
    // A broken reference remains editable through the same reference-entry control.
    const auto saved_ref=dialog()->current().references[0];
    dialog()->reject();flush();
    auto mesh=view->mesh();
    std::erase_if(mesh.edges,[&](const auto& e){return e.reference.owner_id==saved_ref.owner_id&&e.reference.semantic_key==saved_ref.semantic_key;});
    std::erase_if(mesh.original_references.edges,[&](const auto& e){return e.reference.owner_id==saved_ref.owner_id&&e.reference.semantic_key==saved_ref.semantic_key;});
    view->set_mesh(mesh,false);
    row=nullptr;for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole+3)=="document-measurement"){row=*i;break;}
    window.show_tree_item_properties(row);flush();
    check(dialog()&&!dialog()->geometries()[0]&&!dialog()->findChild<QPushButton*>("saveMeasurement")->isEnabled(),"Broken reference is silently accepted");
    dialog()->findChild<QTableWidget*>("measurementReference1")->cellClicked(0,1);select(viewer::CandidateKind::Vertex);
    check(dialog()->geometries()[0]&&dialog()->findChild<QPushButton*>("saveMeasurement")->isEnabled(),"Broken reference cannot be replaced");
    dialog()->reject();flush();
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    check(document::PartDocument::load(path).measurements[0].references[0]==saved_ref,"Cancel committed replacement");
    // The same dialog's mass/volume path is exercised by selecting a whole object.
    action->trigger();flush();select(viewer::CandidateKind::Container);
    check(dialog()->geometries()[0]&&dialog()->geometries()[0]->values.volume,"Whole object volume unavailable");
    check(std::abs(dialog()->geometries()[0]->values.volume->value-6000)<1e-8,"Object inspector has wrong volume");
    check(dialog()->geometries()[0]->values.mass&&std::abs(dialog()->geometries()[0]->values.mass->value-.0471)<1e-10,"Inspector did not use material density");
    if(qEnvironmentVariableIsSet("ZIMA_MEASUREMENT_CAPTURE"))window.grab().save(qEnvironmentVariable("ZIMA_MEASUREMENT_CAPTURE"));
    dialog()->reject();flush();
    // Body measurement shares the inspector's explicit Save contract.
    auto* mass_action=window.findChild<QAction*>("massPropertiesAction");
    check(mass_action&&mass_action->isEnabled()&&!mass_action->icon().isNull(),"Body properties action missing");
    const auto mass_dialog=[&]()->MassPropertiesDialog* {
        for(auto* child:window.findChildren<QDialog*>())if(auto* d=dynamic_cast<MassPropertiesDialog*>(child);d&&d->isVisible())return d;
        return nullptr;
    };
    mass_action->trigger();flush();check(mass_dialog(),"Body properties did not open");
    check(mass_action->text()==QObject::tr("Měření tělesa")&&mass_dialog()->windowTitle()==QObject::tr("Měření tělesa"),"Body measurement is confused with Body placement properties");
    check(mass_dialog()->parentWidget()==&window&&(mass_dialog()->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"Body properties uses a native window");
    check(mass_dialog()->buttons()->standardButtons()==(QDialogButtonBox::Ok|QDialogButtonBox::Cancel),"Body properties has extra commit actions");
    mass_dialog()->resize(460,260);flush();
    const auto* mass_ok=mass_dialog()->buttons()->button(QDialogButtonBox::Ok);
    const auto ok_rect=QRect(mass_ok->mapTo(mass_dialog(),QPoint()),mass_ok->size());
    check(mass_ok->isVisible()&&mass_ok->isEnabled()&&mass_dialog()->rect().contains(ok_rect),"Body properties OK was clipped by results on a small window");
    auto* mass_save=mass_dialog()->findChild<QPushButton*>("saveBodyProperties");
    check(mass_save&&mass_save->isEnabled()&&mass_dialog()->rect().contains(QRect(mass_save->mapTo(mass_dialog(),QPoint()),mass_save->size())),"Explicit Save is missing or clipped");
    mass_dialog()->resize(460,430);flush();
    check(mass_dialog()->current().integrals&&std::abs(mass_dialog()->current().volume-6000)<1e-8,"Body properties shows wrong geometry");
    const auto draft_id=mass_dialog()->current().id;
    check(execute("body_properties.list").data.at("total")==0,"Opening body measurement inserted a record");
    for(QTreeWidgetItemIterator it(tree);*it;++it)check((*it)->data(0,Qt::UserRole).toString().toStdString()!=draft_id,"Preview appeared in Tree before Save");
    check(view->confirmed_candidate()&&view->confirmed_candidate()->owner_id==draft_id+":origin","Initial centroid preview is not azure");
    mass_dialog()->reject();flush();check(execute("body_properties.list").data.at("total")==0,"Cancel created a mass feature");
    mass_action->trigger();flush();
    click(QPointF(25,25),Qt::MiddleButton);check(mass_dialog(),"Short MMB committed mass properties");
    mouse(QEvent::MouseButtonDblClick,QPointF(25,25),Qt::MiddleButton);mouse(QEvent::MouseButtonRelease,QPointF(25,25),Qt::MiddleButton);flush();
    check(!mass_dialog(),"Double MMB over View did not confirm mass properties");
    check(execute("body_properties.list").data.at("total")==0,"Double MMB implicitly saved a body measurement");
    mass_action->trigger();flush();mass_dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    check(execute("body_properties.list").data.at("total")==0,"OK implicitly saved a body measurement");
    mass_action->trigger();flush();mass_dialog()->findChild<QPushButton*>("saveBodyProperties")->click();flush();
    const auto mass_id=execute("body_properties.list").data.at("items").at(0).at("object").get<std::string>();
    check(tree->currentItem()&&tree->currentItem()->data(0,Qt::UserRole+3)=="body-properties","Mass feature not selected in Tree");
    check_before_cursor();
    check(std::ranges::any_of(view->mesh().points,[&](const auto& p){return p.reference.owner_id==mass_id+":origin";}),"COG Origin missing from View");
    auto* centroid_item=tree->currentItem()->child(0);check(centroid_item&&centroid_item->text(0)==QObject::tr("Těžiště"),"Centroid Tree label is ambiguous");
    tree->setCurrentItem(centroid_item);flush();
    check(view->confirmed_candidate()&&view->confirmed_candidate()->owner_id==mass_id+":origin","Selecting centroid in Tree does not highlight its own frame");
    click({25,25},Qt::LeftButton);check(!view->confirmed_candidate()&&tree->selectedItems().empty(),"Empty View click retained centroid selection");
    const auto mass_reopen=[&] {
        QTreeWidgetItem* item{};for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==mass_id){item=*i;break;}
        check(item,"Mass row missing");window.show_tree_item_properties(item);flush();check(mass_dialog(),"Mass edit window missing");
    };
    mass_reopen();mass_dialog()->findChild<QDoubleSpinBox*>("bodyPropertiesRotation2")->setValue(90);
    const auto axis=std::ranges::find_if(view->mesh().axes,[&](const auto& a){return a.reference.owner_id==mass_id+":origin"&&a.reference.semantic_key=="origin:axis:x";});
    check(axis!=view->mesh().axes.end()&&std::abs(axis->direction.y-1)<1e-8,"Origin rotation preview did not update");
    if(qEnvironmentVariableIsSet("ZIMA_MEASUREMENT_CAPTURE"))window.grab().save(qEnvironmentVariable("ZIMA_MEASUREMENT_CAPTURE")+".mass.png");
    mass_dialog()->reject();flush();check(execute("body_properties.get",{{"object",mass_id}}).data["rotation_degrees"][2]==0,"Cancel changed Origin axes");
    execute("box.create",{{"length_mm","100"},{"width_mm","200"},{"height_mm","300"}});flush();
    const auto full_vertices=view->mesh().vertices;
    mass_reopen();double extent{};for(const auto& p:view->mesh().vertices)extent=std::max(extent,std::abs(p.z));
    check(mass_dialog()->current().integrals&&std::abs(mass_dialog()->current().volume-6000)<1e-8,"Downstream edit broke mass history anchor");
    check(extent<31,"Mass properties displayed downstream geometry");
    mass_dialog()->reject();flush();check(view->mesh().vertices==full_vertices,"Cancel did not restore full history display");
    execute("undo");flush();
    // Active Body rows and the insertion marker deliberately carry the same
    // Body ID. Information records must resolve the real owning Body row.
    const auto before_analysis_revision=execute("measurement.get",{{"object",measurement_id}}).data.at("revision");
    const auto active_body=execute("body.create",{{"name","Analysis owner"}}).data.at("body").get<std::string>();
    const auto analysis_box=execute("box.create",{{"length_mm","10"},{"width_mm","8"},{"height_mm","6"}}).data.at("container").get<std::string>();
    const auto in_body=execute("body_properties.create").data.at("object").get<std::string>();
    const auto first_mass=execute("body_properties.get",{{"object",in_body}}).data;
    // The first Body adopts the existing 10 x 20 x 30 solid; the small box
    // lies wholly inside it, so the measured volume remains 6000 mm3.
    check(std::abs(first_mass.at("volume_mm3").get<double>()-6000)<1e-8,"Active Body measurement ignored its existing solid");
    const auto body_measurement=execute("measurement.create",{{"references",commands::Json::array({{{"kind","object"},{"owner",analysis_box},{"key",""},{"instance_path",""}}})}}).data.at("object").get<std::string>();
    const auto analysis_row=[&](const std::string& id){
        QTreeWidgetItem* found{};for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id){found=*it;break;}
        check(found&&found->parent(),"Information feature disappeared from Tree");
        check(found->parent()->data(0,Qt::UserRole+3)=="part-body"&&found->parent()->data(0,Qt::UserRole).toString().toStdString()==active_body,"Information feature is not a direct child of its Body");
        tree->setCurrentItem(found);check_before_cursor();return found;
    };
    analysis_row(in_body);analysis_row(body_measurement);
    const auto later_box=execute("box.create",{{"length_mm","100"},{"width_mm","80"},{"height_mm","60"}}).data.at("container").get<std::string>();
    for(const auto& id:{in_body,body_measurement}) {
        auto* row=analysis_row(id);auto* parent=row->parent();int later=-1;
        for(int i=0;i<parent->childCount();++i)if(parent->child(i)->data(0,Qt::UserRole).toString().toStdString()==later_box)later=i;
        check(later>parent->indexOfChild(row),"Later feature moved an earlier measurement below itself");
    }
    const auto later_mass=execute("body_properties.get",{{"object",in_body}}).data;
    check(std::abs(later_mass.at("volume_mm3").get<double>()-6000)<1e-8,"Later feature changed historical volume");
    for(bool expected:{false,true}) {
        auto* item=analysis_row(in_body);tree->scrollToItem(item);flush();bool invoked=false;
        QTimer::singleShot(0,&window,[&]{
            auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if(!menu)return;
            for(auto* action:menu->actions())if(action->objectName()=="bodyPropertiesVisibilityAction") {
                invoked=true;menu->setActiveAction(action);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);return;
            }
            menu->close();
        });
        QMetaObject::invokeMethod(tree,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,tree->visualItemRect(item).center()));flush();
        check(invoked,"Body properties visibility context action missing");
        check(execute("body_properties.get",{{"object",in_body}}).data.at("visible")==expected,"Context menu did not save Origin visibility");
        check(std::ranges::any_of(view->mesh().points,[&](const auto& p){return p.reference.owner_id==in_body+":origin";})==expected,"Origin picker/display ignored visibility");
    }
    // Restore the original source before the existing Assembly inspector test.
    for(int i=0;i<10&&execute("measurement.get",{{"object",measurement_id}}).data.at("revision")!=before_analysis_revision;++i)execute("undo");
    check(execute("measurement.get",{{"object",measurement_id}}).data.at("revision")==before_analysis_revision,"Analysis history could not be undone");flush();
    auto second_part=document::PartDocument::create_default();
    auto second_box=document::PartDocument::create_box_container();second_box.box={20,20,30};second_part.history={second_box};
    second_part.physical_parameters["MASS_DENSITY"]="2700";second_part.physical_parameter_units["MASS_DENSITY"]="kg/m^3";
    const auto second_bodies=kernel.evaluate_history(second_part.kernel_operations());
    const auto second_path=directory/"measurement-second.prtz";second_part.save(second_path,second_bodies);
    auto assembly=assembly::AssemblyDocument::create_default();
    auto first=assembly::AssemblyDocument::create_part_occurrence("Steel",part.document_id,path,bodies.back());
    first.grounded=true;first.density_kg_mm3=7.85e-6;first.mass_volume_mm3=6000;
    auto second=assembly::AssemblyDocument::create_part_occurrence("Aluminium",second_part.document_id,second_path,second_bodies.back());
    second.grounded=true;second.placement.x=40;second.density_kg_mm3=2.7e-6;second.mass_volume_mm3=12000;
    assembly.components={first,second};
    const auto assembly_path=directory/"measurement-ui.asmz";assembly.save(assembly_path);
    check(window.open_document_path(QString::fromStdString(assembly_path.string())),"Cannot open two-body Assembly");
    flush();action->trigger();flush();
    select(viewer::CandidateKind::Occurrence,assembly::InstancePath{{first.occurrence_id}}.encoded());
    select(viewer::CandidateKind::Occurrence,assembly::InstancePath{{second.occurrence_id}}.encoded());
    check(dialog()&&dialog()->geometries()[0]&&dialog()->geometries()[1]&&dialog()->distance(),"Two-body results unavailable");
    const auto& info=dialog()->geometries();
    check(info[0]->values.volume&&info[1]->values.volume&&std::abs(info[0]->values.volume->value-6000)<1e-8&&
          std::abs(info[1]->values.volume->value-12000)<1e-8,"Second body reused first body volume");
    check(info[0]->values.mass&&info[1]->values.mass&&std::abs(info[0]->values.mass->value-.0471)<1e-10&&
          std::abs(info[1]->values.mass->value-.0324)<1e-10,"Second body reused first body material");
    for(int i=1;i<=2;++i){
        const auto text=dialog()->findChild<QLabel*>(QString("measurementInfo%1").arg(i))->text();
        check(text.contains(QObject::tr("Objem: %1").section("%1",0,0))&&text.contains(QObject::tr("Hmotnost: %1").section("%1",0,0))&&text.contains(','),"Entity summary omitted volume, mass or decimal comma");
    }
    check(std::abs(dialog()->distance()->distance.value-25)<1e-8,"Two-body gap differs from analytic 25mm");
    if(qEnvironmentVariableIsSet("ZIMA_MEASUREMENT_CAPTURE"))window.grab().save(qEnvironmentVariable("ZIMA_MEASUREMENT_CAPTURE")+".assembly.png");
    tree->collapseAll();dialog()->findChild<QPushButton*>("saveMeasurement")->click();flush();
    check(tree->currentItem()&&tree->currentItem()->data(0,Qt::UserRole+3)=="document-measurement","Assembly Save did not reveal record");
    check_before_cursor();
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    check(assembly::AssemblyDocument::load(assembly_path).measurements.size()==1,"Assembly lost saved measurement");
    execute("component.activate",{{"instance_path",assembly::InstancePath{{first.occurrence_id}}.encoded()}});flush();
    action->trigger();flush();check(dialog()!=nullptr,"Read-only measurement inspector unavailable during activation");
    select(viewer::CandidateKind::Vertex);
    check(dialog()->geometries()[0]&&!dialog()->findChild<QPushButton*>("saveMeasurement")->isEnabled(),
        "Inactive displayed Assembly allowed a measurement write through the active Part");
    dialog()->reject();flush();execute("component.deactivate");
    if(qEnvironmentVariableIsSet("ZIMA_BODY_MEASUREMENT_SOURCE")) {
        const auto source=std::filesystem::path(qEnvironmentVariable("ZIMA_BODY_MEASUREMENT_SOURCE").toStdWString());
        std::vector<kernel::BodyResult> cached;const auto source_doc=document::PartDocument::load(source,&cached);
        const auto probe_path=directory/"centroid-source.prtz";source_doc.save(probe_path,cached);
        check(window.open_document_path(QString::fromStdString(probe_path.string())),"Cannot open centroid source copy");flush();
        const auto before=execute("body_properties.list").data.at("total").get<int>();
        const auto body_before=execute("body.list").data;
        view->set_reference_visibility(viewer::ReferenceVisibility::Origins,false);
        mass_action->trigger();flush();check(mass_dialog(),"Source body measurement did not open");
        const auto draft=mass_dialog()->current();check(draft.integrals&&draft.error.empty(),"Source centroid unavailable");
        const auto c=draft.integrals->centroid;
        check(std::abs(c.x)>1&&std::abs(c.y)>1&&std::abs(c.z)>1,"Source centroid must distinguish the Body origin");
        const auto point=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.owner_id==draft.id+":origin";});
        check(point!=view->mesh().points.end()&&point->position==c&&point->label==QObject::tr("Těžiště").toStdString(),"Source preview reused Body origin or label");
        check(execute("body_properties.list").data.at("total")==before,"Source preview was persisted before Save");
        click({25,25},Qt::LeftButton);
        check(view->confirmed_candidate()&&view->confirmed_candidate()->owner_id==draft.id+":origin","Preview lost azure color on empty View click");
        if(qEnvironmentVariableIsSet("ZIMA_MEASUREMENT_CAPTURE"))window.grab().save(qEnvironmentVariable("ZIMA_MEASUREMENT_CAPTURE")+".source.png");
        mass_dialog()->findChild<QPushButton*>("saveBodyProperties")->click();flush();
        check(execute("body_properties.list").data.at("total")==before+1,"Save did not insert exactly one source measurement");
        const auto body_after=execute("body.list").data;
        check(body_before.at("items")[0].at("placement")==body_after.at("items")[0].at("placement")&&body_before.at("items")[0].at("origin")==body_after.at("items")[0].at("origin"),"Measurement changed Body placement or identity");
        auto* saved_origin=tree->currentItem()->child(0);tree->setCurrentItem(saved_origin);flush();
        check(view->confirmed_candidate()&&view->confirmed_candidate()->owner_id==draft.id+":origin","Source centroid Tree selection is wrong");
        if(qEnvironmentVariableIsSet("ZIMA_MEASUREMENT_CAPTURE"))window.grab().save(qEnvironmentVariable("ZIMA_MEASUREMENT_CAPTURE")+".source-tree.png");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(probe_path).body_properties.size()==static_cast<std::size_t>(before+1),"Explicitly saved source measurement did not persist");
        check(document::PartDocument::load(source).body_properties==source_doc.body_properties,"User source was modified");
    }
    std::cout<<"Measurement inspector picking, explicit Save, centroid preview, MMB, persistence and repair passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
}
