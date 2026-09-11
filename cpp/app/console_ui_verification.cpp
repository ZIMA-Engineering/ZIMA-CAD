#include "console_ui_verification.hpp"
#include "history_tree_widget.hpp"
#include <QMenu>
#include <QTimer>
#include <QMessageBox>
#include "assembly_workspace_window.hpp"
#include <zima/document/part_document.hpp>
#include <QApplication>
#include <QAction>
#include <QDockWidget>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QKeyEvent>
#include <QCursor>
#include <QElapsedTimer>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <cmath>
#include <zima/viewer/mesh_view.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace zima::app {
int verify_command_console(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    const auto check=[](bool condition,const char* message){if(!condition)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const QString& command){auto result=window.execute_console_command(command);if(!result.ok)throw std::runtime_error(command.toStdString()+": "+result.code+": "+result.message);return result;};
    try {
        window.showMaximized();flush();
        auto* dock=window.findChild<QDockWidget*>("commandConsoleDock");
        auto* toggle=window.findChild<QAction*>("showCommandConsoleAction");
        check(dock && toggle && !dock->isVisible(),"Console must start hidden");
        toggle->trigger();flush();
        check(dock->isVisible() && !dock->isFloating(),"Console is not an internal panel");
        auto* input=window.findChild<QLineEdit*>("commandConsoleInput");
        auto* output=window.findChild<QPlainTextEdit*>("commandConsoleOutput");
        check(input && output,"Console input/output missing");
        window.resizeDocks({dock},{1},Qt::Vertical);flush();
        check(output->height()<90 && input->isVisible(),"Console cannot shrink below its former output minimum");
        check(window.grab().save(QString::fromStdString((directory/"command-console-compact.png").string())),"Compact screenshot failed");
        window.resizeDocks({dock},{260},Qt::Vertical);flush();
        input->setText("help");QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(input,&enter);flush();
        check(output->toPlainText().contains("regenerate"),"Enter did not dispatch help");
        QKeyEvent up(QEvent::KeyPress,Qt::Key_Up,Qt::NoModifier);QApplication::sendEvent(input,&up);check(input->text()=="help","Command history failed");
        input->clear();
        check(window.execute_console_command("save").code=="no_document","Empty save is not rejected");
        const auto stem="console-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        run(QString::fromStdString("new part "+stem));flush();
        const auto context=run("context").data;const auto id=context.at("active_document").get<std::string>();
        check(!id.empty(),"New document has no identity");
        check(window.execute_console_command(R"({"command":"save","arguments":{"document":"wrong-id"}})").code=="document_changed","Stale target was accepted");
        auto* box=window.findChild<QAction*>("boxAction");check(box && box->isEnabled(),"Box action missing");box->trigger();flush();
        check(window.execute_console_command("regenerate").code=="editing_in_progress","Command overwrote pending edit");
        run("context");run("tree");
        QDialog* properties=nullptr;
        for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible() && dialog->findChild<QDialogButtonBox*>())properties=dialog;
        check(properties,"Box properties did not open");
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        const auto before=run("documents").data;
        auto* model_tree=window.findChild<QTreeWidget*>("documentTree");
        check(model_tree,"Model tree widget missing");
        auto* decoration=new QTreeWidgetItem(model_tree,QStringList{"UI-only-test-decoration"});
        const auto model_snapshot=run("tree").data;
        check(model_snapshot.at("projection")=="model" && model_snapshot.dump().find("UI-only-test-decoration")==std::string::npos,"Command enumerates widget decorations instead of model data");
        delete decoration;
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
        check(view,"Model View missing");
        struct CursorRestore { QPoint position=QCursor::pos(); ~CursorRestore(){QCursor::setPos(position);} } restore_cursor;
        const auto move_pointer=[&](QWidget* target) {
            window.raise();window.activateWindow();flush();
            const auto destination=target->mapToGlobal(QPoint(target->width()/3,target->height()/3));
            QCursor::setPos(destination);
            QElapsedTimer timer;timer.start();
            do { flush(); if(QCursor::pos()==destination && QApplication::widgetAt(destination)==target)break; QThread::msleep(10); } while(timer.elapsed()<1000);
            flush();
        };
        move_pointer(view);
        // Desktop automation may leave another native window above this one.
        // Context must follow the real topmost widget, not merely View bounds.
        const bool inside=QApplication::widgetAt(QCursor::pos())==view;
        const auto pointing=run("context").data;
        check(pointing.at("pointer").at("inside_view")==inside,"Context ignored actual desktop hit target");
        check(pointing.at("captured_at_unix_ms").is_number_integer() && pointing.at("camera").is_array(),"Context timestamp/camera missing");
        check(view->ray_at(QPoint(view->width()/3,view->height()/3)).has_value(),"View camera ray missing");
        check(pointing.at("pointer").at("ray").is_null()!=inside,"Context ray visibility mismatch");
        const auto offered=inside && view->last_pointer_position()==view->mapFromGlobal(QCursor::pos())?view->hovered_candidate():std::nullopt;
        check(offered?pointing.at("hover").at("owner_id")==offered->owner_id:pointing.at("hover").is_null(),"Context recomputed a different hover candidate");
        std::cout<<"Pointer context verified with View exposed="<<inside<<'\n';
        move_pointer(input);
        const auto typing=run("context").data;
        check(typing.at("pointer").at("inside_view")==false && typing.at("hover").is_null(),"Stale View hover leaked into console context");
        check(run("documents").data==before,"Read commands changed document state");
        run("save");
        const auto path=directory/(stem+".prtz");
        auto loaded=document::PartDocument::load(path);check(loaded.history.size()==1,"Console did not save the GUI feature");
        check(std::any_of(model_snapshot.at("items").begin(),model_snapshot.at("items").end(),[&](const auto& item){return item.at("id")==loaded.history.front().id && item.at("type")=="history-container";}),"Model query omitted GUI-created feature");
        run("undo");run("save");loaded=document::PartDocument::load(path);check(loaded.history.empty(),"Console Undo did not use GUI history");
        run("redo");run("regenerate");run("save");loaded=document::PartDocument::load(path);check(loaded.history.size()==1,"Console Redo/regenerate lost feature");
        const auto feature_id=loaded.history.front().id;
        const auto parameters=[&]{return run(QString::fromStdString("box.get "+feature_id)).data;};
        check(parameters().at("length_mm")==loaded.history.front().box.length,"Console cannot read GUI Box");
        run(QString::fromStdString("box.set "+feature_id+" 150"));flush();
        const auto edit_box=[&]() -> QDialog* {
            QTreeWidgetItem* item=nullptr;
            for(QTreeWidgetItemIterator it(model_tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature_id &&
                   (*it)->data(0,Qt::UserRole+3).toString()=="part-container") {item=*it;break;}
            check(item,"Updated Box not present in GUI tree");window.show_tree_item_properties(item);flush();
            for(auto* dialog:window.findChildren<QDialog*>())
                if(dialog->isVisible() && dialog->findChild<QDoubleSpinBox*>("boxLength"))return dialog;
            throw std::runtime_error("Edited Box properties did not open");
        };
        auto* edit=edit_box();auto* length=edit->findChild<QDoubleSpinBox*>("boxLength");
        check(length->value()==150,"GUI did not read command-edited dimensions");length->setValue(125);
        check(window.execute_console_command(QString::fromStdString("box.set "+feature_id+" 140")).code=="editing_in_progress","Box patch ignored pending GUI edit");
        edit->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        check(parameters().at("length_mm")==150,"Cancel committed pending Box dimensions");
        edit=edit_box();edit->findChild<QDoubleSpinBox*>("boxLength")->setValue(125);
        edit->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(parameters().at("length_mm")==125,"GUI edit did not use shared Box transaction");
        run("undo");check(parameters().at("length_mm")==150,"Undo did not restore the command change");
        run("redo");check(parameters().at("length_mm")==125,"Redo lost GUI edit");run("save");
        commands::Json precise_patch={{"command","box.set"},{"arguments",{{"container",feature_id},
            {"width_mm","80.123456789"},{"height_mm","50.987654321"}}}};
        run(QString::fromStdString(precise_patch.dump()));
        window.toggle_parameter_value_lock(feature_id,"parameter:width");flush();
        edit=edit_box();auto* width=edit->findChild<QDoubleSpinBox*>("boxWidth");
        check(width && width->isReadOnly(),"Persisted width lock did not reach GUI");
        edit->findChild<QDoubleSpinBox*>("boxLength")->setValue(130);
        edit->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(parameters().at("width_mm")==80.123456789 && parameters().at("height_mm")==50.987654321,
            "GUI rounded an untouched locked or unlocked command dimension");
        const auto precise_revision=parameters().at("revision");
        edit=edit_box();edit->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(parameters().at("revision")==precise_revision,"Unchanged precise Box created an Undo step");
        edit=edit_box();width=edit->findChild<QDoubleSpinBox*>("boxWidth");
        auto* width_lock=width->findChild<QAction*>("valueLock:width");check(width_lock,"Width lock action missing");
        width_lock->trigger();check(!width->isReadOnly(),"Width did not unlock");width->setValue(81.5);width_lock->trigger();
        edit->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(parameters().at("width_mm")==81.5 && window.parameter_value_locked(feature_id,"parameter:width").value_or(false),
            "Unlock, edit and relock in one GUI OK failed");
        precise_patch["arguments"].erase("height_mm");precise_patch["arguments"]["width_mm"]="82";
        check(window.execute_console_command(QString::fromStdString(precise_patch.dump())).code=="value_locked",
            "Command bypassed GUI-restored lock");
        run("undo");check(parameters().at("width_mm")==80.123456789,"Undo lost precise locked width");
        run("redo");run("save");
        struct PrimitiveCase {const char* command;const char* field;const char* parameter;};
        for(const auto& sample:std::vector<PrimitiveCase>{
            {"cylinder.create 3 6","cylinderHeight","height_mm"},
            {"sphere.create 3","sphereRadius","radius_mm"},
            {"cone.create 4 1 6","coneHeight","height_mm"},
            {"pyramid.create 10 8 6","pyramidHeight","height_mm"},
            {"wedge.create 10 8 6 2","wedgeHeight","height_mm"}}) {
            const auto command=std::string(sample.command);const auto kind=command.substr(0,command.find('.'));
            run(QString::fromStdString("new part "+stem+"-"+kind));
            const auto result=run(QString::fromStdString(command)).data;flush();
            const auto primitive_id=result.at("container").get<std::string>();
            QTreeWidgetItem* item=nullptr;
            for(QTreeWidgetItemIterator it(model_tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==primitive_id &&
                   (*it)->data(0,Qt::UserRole+3).toString()=="part-container") {item=*it;break;}
            check(item,"CLI-created primitive missing from GUI tree");window.show_tree_item_properties(item);flush();
            QDialog* properties=nullptr;
            for(auto* dialog:window.findChildren<QDialog*>()) if(dialog->isVisible() && dialog->findChild<QDoubleSpinBox*>(sample.field))properties=dialog;
            check(properties,"Primitive properties not shared with CLI");
            properties->findChild<QDoubleSpinBox*>(sample.field)->setValue(9);
            properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            check(run(QString::fromStdString(kind+".get "+primitive_id)).data.at(sample.parameter)==9,"GUI primitive edit did not reach command model");
            run("undo");check(run(QString::fromStdString(kind+".get "+primitive_id)).data.at(sample.parameter)==result.at(sample.parameter),"Primitive Undo lost original command value");
            run("redo");run("save");
        }
        commands::Json restore_box={{"command","open"},{"arguments",{{"path",path.generic_string()}}}};
        run(QString::fromStdString(restore_box.dump()));flush();
        const auto missing=(directory/(stem+"-missing.prtz")).generic_string();
        commands::Json open={{"command","open"},{"arguments",{{"path",missing}}}};
        check(!window.execute_console_command(QString::fromStdString(open.dump())).ok,"Missing file reported success");
        check(QApplication::activeModalWidget()==nullptr,"Command error opened a modal message box");
        run(QString::fromStdString("new part "+stem+"-io-error"));
        const auto other_id=run("context").data.at("active_document");
        run(QString::fromStdString("tree "+id));
        check(run("context").data.at("active_document")==other_id,"Reading inactive model switched GUI context");
        const auto blocked=directory/(stem+"-io-error.prtz");std::filesystem::create_directory(blocked);
        const auto failed_save=window.execute_console_command("save");std::filesystem::remove(blocked);
        check(!failed_save.ok && QApplication::activeModalWidget()==nullptr,"I/O error reported success or blocked on a dialog");
        open["arguments"]["path"]=path.generic_string();run(QString::fromStdString(open.dump()));flush();
        commands::Json activate={{"command","activate"},{"arguments",{{"document",other_id}}}};
        run(QString::fromStdString(activate.dump()));flush();
        check(run("context").data.at("active_document")==other_id,"Command activation did not switch GUI");
        check(window.execute_console_command("close").code=="unsaved_changes" && QApplication::activeModalWidget()==nullptr,"Unsafe close was accepted or opened a dialog");
        run(R"({"command":"close","arguments":{"discard":true}})");flush();
        activate["arguments"]["document"]=id;run(QString::fromStdString(activate.dump()));flush();
        const auto copy_path=directory/(stem+"-copy.prtz");
        commands::Json copy_command={{"command","save_as"},{"arguments",{{"path",copy_path.generic_string()}}}};
        run(QString::fromStdString(copy_command.dump()));flush();
        check(document::PartDocument::load(copy_path).document_id!=id && run("context").data.at("active_document")==id,"GUI console copy altered original identity/context");
        run(QString::fromStdString("new drawing "+stem+"-drawing"));flush();
        const auto drawing_id=run("context").data.at("active_document");
        run("save");flush();
        check(run("documents").data.back().at("dirty")==false,"Displaying saved Drawing made it dirty");
        auto* add_sheet=window.findChild<QAction*>("addDrawingSheetAction");check(add_sheet,"Drawing sheet action missing");
        add_sheet->trigger();flush();
        check(run("documents").data.back().at("dirty")==true,"GUI sheet edit was not tracked by command host");
        check(window.execute_console_command("close").code=="unsaved_changes","GUI drawing edit was discarded");
        run("save");run("close");flush();
        run(QString::fromStdString(activate.dump()));flush();
        run(QString::fromStdString("new part "+stem+"-bodies"));
        const auto base_body=run("box.create 10 10 10").data.at("body").get<std::string>();
        const auto tool_body=run("body.create Tool").data.at("body").get<std::string>();
        const auto tool_feature=run("box.create 4 4 4").data.at("container").get<std::string>();flush();
        view->confirm_container(tool_feature);check(view->confirmed_candidate().has_value(),"Body activation fixture has no confirmed selection");
        const auto selection_before_reference=run("context").data.at("selection");
        const auto offered_faces=run(QString::fromStdString("reference.list "+tool_feature+" face")).data;
        check(offered_faces.at("total")==6,"GUI console lost original reference faces");
        commands::Json read_reference={{"command","reference.get"},{"arguments",offered_faces.at("items")[0]}};
        const auto read_face=run(QString::fromStdString(read_reference.dump())).data;flush();
        check(read_face.at("surface").at("kind")=="plane" && run("context").data.at("selection")==selection_before_reference &&
            view->confirmed_candidate().has_value(),"Reference query altered GUI confirmation or lost original surface");
        run(QString::fromStdString("body.activate "+base_body));flush();
        check(!view->confirmed_candidate() && run("context").data.at("selection").is_null(),"Body command activation kept a stale confirmed selection");
        run(QString::fromStdString("body.activate "+tool_body));flush();
        const auto edit_object=[&](const std::string& object_id,const char* kind) {
            QTreeWidgetItem* row=nullptr;
            for(QTreeWidgetItemIterator it(model_tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==object_id && (*it)->data(0,Qt::UserRole+3).toString()==kind){row=*it;break;}
            check(row,"Body command result missing from GUI tree");window.show_tree_item_properties(row);flush();
        };
        const auto visible_properties=[&](const char* field) {
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible() && dialog->findChild<QWidget*>(field))return dialog;
            return static_cast<QDialog*>(nullptr);
        };
        edit_object(tool_body,"part-body");properties=visible_properties("bodyName");check(properties,"Body Properties missing");
        properties->findChild<QLineEdit*>("bodyName")->setText("Pending tool");
        check(window.execute_console_command("body.create forbidden").code=="editing_in_progress","Body command overwrote pending Properties");
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        check(run(QString::fromStdString("body.get "+tool_body)).data.at("name")=="Tool","Body Cancel committed pending name");
        edit_object(tool_body,"part-body");properties=visible_properties("bodyName");properties->findChild<QLineEdit*>("bodyName")->setText("GUI tool");
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(run(QString::fromStdString("body.get "+tool_body)).data.at("name")=="GUI tool","GUI Body properties did not reach command model");
        run("undo");check(run(QString::fromStdString("body.get "+tool_body)).data.at("name")=="Tool","Body edit lost shared Undo");run("redo");
        const auto boolean=run(QString::fromStdString("body.boolean.create subtract "+base_body+" "+tool_body)).data.at("boolean").get<std::string>();flush();
        edit_object(boolean,"part-body-boolean");properties=visible_properties("bodyBooleanOperation");check(properties,"Body Boolean Properties missing");
        auto* boolean_mode=properties->findChild<QComboBox*>("bodyBooleanOperation");
        boolean_mode->setCurrentIndex(boolean_mode->findData(static_cast<int>(kernel::BodyCombination::Intersect)));
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(run(QString::fromStdString("body.boolean.get "+boolean)).data.at("operation")=="intersect","GUI Boolean properties did not reach command model");
        run("save");std::vector<kernel::BodyResult> body_cache;
        const auto body_document=document::PartDocument::load(directory/(stem+"-bodies.prtz"),&body_cache);
        check(body_document.body_history.find_boolean(boolean) && !body_cache.empty() && std::abs(body_cache.back().volume-64)<1e-6,"GUI Boolean edit saved the wrong volume");
        run(QString::fromStdString("new part "+stem+"-history"));
        const auto history_first=run("box.create 10 10 10").data.at("container").get<std::string>();
        const auto history_second=run("box.create 4 4 4").data.at("container").get<std::string>();flush();
        auto* history_tree=dynamic_cast<HistoryTreeWidget*>(model_tree);check(history_tree,"History widget missing");
        const auto history_row=[&](const std::string& object) {
            for(QTreeWidgetItemIterator it(model_tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==object && (*it)->data(0,Qt::UserRole+3).toString()=="part-container")return *it;
            return static_cast<QTreeWidgetItem*>(nullptr);
        };
        auto* moving=history_row(history_second);check(moving && history_tree->reorder_enabled(moving),"History drag not enabled");
        const auto history_revision=run("history.list").data.at("revision");
        check(history_tree->reorder_requested(moving,QString::fromStdString(history_first),false),"GUI drag dependency check failed");
        check(run("history.list").data.at("revision")==history_revision,"GUI drag preview committed history");
        check(history_tree->reorder_requested(moving,QString::fromStdString(history_first),true),"GUI history move failed");flush();
        check(run("history.list").data.at("items")[0].at("object")==history_second,"GUI history move diverged from command model");
        history_tree->history_cursor_moved(0);flush();check(run("history.list").data.at("cursor")==0,"GUI history cursor diverged");
        history_tree->history_cursor_moved(2);flush();
        const auto history_menu=[&](const std::string& object,const char* action_name,bool confirm) {
            auto* row=history_row(object);check(row,"History menu target missing");
            for(auto* parent=row->parent();parent;parent=parent->parent())model_tree->expandItem(parent);
            model_tree->scrollToItem(row);model_tree->clearSelection();model_tree->setCurrentItem(row);row->setSelected(true);
            bool invoked=false;
            QTimer::singleShot(0,&window,[&] {
                auto* menu=window.findChild<QMenu*>("partHistoryMenu");if(!menu)return;
                auto* action=menu->findChild<QAction*>(action_name);if(!action){menu->close();return;}
                if(confirm)QTimer::singleShot(0,&window,[&]{
                    if(auto* question=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))question->button(QMessageBox::Yes)->click();
                });
                invoked=true;menu->setActiveAction(action);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);
            });
            model_tree->customContextMenuRequested(model_tree->visualItemRect(row).center());flush();check(invoked,"History context action missing");
        };
        history_menu(history_first,"suppressHistoryObject",false);
        check(run("history.list").data.at("items")[1].at("suppressed")==true,"GUI suppression diverged from commands");
        run("save");std::vector<kernel::BodyResult> history_cache;
        static_cast<void>(document::PartDocument::load(directory/(stem+"-history.prtz"),&history_cache));
        check(!history_cache.empty() && std::abs(history_cache.back().volume-64)<1e-6,"GUI suppression calculated wrong volume");
        run("undo");history_menu(history_second,"deleteHistoryObject",true);
        check(run("history.list").data.at("items").size()==1 && run("history.list").data.at("items")[0].at("object")==history_first,"GUI deletion diverged from commands");
        run("undo");check(run("history.list").data.at("items").size()==2,"GUI history deletion lost shared Undo");
        run(QString::fromStdString("new part "+stem+"-sketch"));
        auto* sketch_action=window.findChild<QAction*>("sketchAction");check(sketch_action && sketch_action->isEnabled(),"Sketch GUI action unavailable");
        sketch_action->trigger();flush();properties=visible_properties("sketchName");check(properties,"Sketch Properties missing");
        properties->findChild<QLineEdit*>("sketchName")->setText("GUI profile");
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(run("sketch.list").data.at("items")[0].at("name")=="GUI profile","GUI Sketch creation did not reach shared insertion");
        const auto command_sketch=run("sketch.create Profile XY").data.at("sketch").get<std::string>();
        commands::Json circle_command={{"command","sketch.circle.create"},{"arguments",{{"sketch",command_sketch},{"center",{0,0}},{"radius_mm",8}}}};
        run(QString::fromStdString(circle_command.dump()));flush();
        check(run(QString::fromStdString("sketch.get "+command_sketch)).data.at("counts").at("circles")==1,"Console Sketch geometry did not reach GUI document");
        run("undo");check(run(QString::fromStdString("sketch.get "+command_sketch)).data.at("counts").at("circles")==0,"GUI console Sketch Undo failed");run("redo");
        const auto sketch_geometry=run(QString::fromStdString("sketch.entities "+command_sketch)).data.at("items");std::string circle_id;
        for(const auto& item:sketch_geometry)if(item.at("kind")=="circle")circle_id=item.at("id").get<std::string>();
        commands::Json offset_command={{"command","sketch.offset.create"},{"arguments",{{"sketch",command_sketch},{"source",circle_id},{"distance_mm",1}}}};
        const auto offset_id=run(QString::fromStdString(offset_command.dump())).data.at("geometry").get<std::string>();flush();
        check(run(QString::fromStdString("sketch.offset.get "+command_sketch+" "+offset_id)).data.at("source")==circle_id,"GUI console offset lost native source");
        commands::Json relation_line_command={{"command","sketch.segment.create"},{"arguments",{{"sketch",command_sketch},{"first",{30,40}},{"second",{40,42}}}}};
        const auto relation_line=run(QString::fromStdString(relation_line_command.dump())).data.at("geometry").get<std::string>();
        commands::Json relation_command={{"command","sketch.constraint.create"},{"arguments",{{"sketch",command_sketch},{"kind","horizontal"},{"geometry",{relation_line}}}}};
        run(QString::fromStdString(relation_command.dump()));flush();
        check(run(QString::fromStdString("sketch.solve_status "+command_sketch)).data.at("maximum_residual").get<double>()<1e-6,"GUI console relation did not solve");
        run("save");const auto saved_sketch=document::PartDocument::load(directory/(stem+"-sketch.prtz"));
        check(saved_sketch.sketches.back().id==command_sketch && saved_sketch.sketches.back().circles.front().radius==8,"GUI console did not persist native Sketch");
        run(QString::fromStdString(activate.dump()));flush();
        input->setText("context");QApplication::sendEvent(input,&enter);flush();
        check(window.grab().save(QString::fromStdString((directory/"command-console.png").string())),"Console screenshot failed");
        toggle->trigger();flush();check(!dock->isVisible(),"Console toggle did not hide panel");
        const auto remaining=run("documents").data;
        for(const auto& row:remaining) {
            commands::Json close={{"command","close"},{"arguments",{{"document",row.at("id")},{"discard",true}}}};
            run(QString::fromStdString(close.dump()));flush();
        }
        check(run("documents").data.empty() && run("context").data.at("active_document")=="" && QApplication::activeModalWidget()==nullptr,"Closing last document did not clear the GUI workspace");
        std::cout<<"Console panel, shared operations, transactions, stale target and error paths passed\n";
        return 0;
    }catch(const std::exception& error){window.grab().save(QString::fromStdString((directory/"command-console-failure.png").string()));std::cerr<<"Console contract: "<<error.what()<<'\n';return 1;}
}
} // namespace zima::app
