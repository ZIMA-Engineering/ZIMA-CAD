#include "drawing_window.hpp"
#include "console_ui_verification.hpp"
#include "history_tree_widget.hpp"
#include <QMenu>
#include <QFileDialog>
#include <zima/interchange/dxf.hpp>
#include <zima/document/file_path.hpp>
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
#include <QTableWidget>
#include <QSpinBox>
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
        const auto drawing_sheets=run("drawing.sheet.list").data.at("items");check(drawing_sheets.size()==2,"GUI sheet creation did not use shared model");
        const auto sheet_id=drawing_sheets[1].at("sheet").get<std::string>();
        const auto drawing_run=[&](const char* command,commands::Json args){return run(QString::fromStdString(commands::Json{{"command",command},{"arguments",std::move(args)}}.dump()));};
        drawing_run("drawing.sheet.set",{{"sheet",sheet_id},{"scale",2},{"locale","de"}});flush();
        check(window.findChild<QDoubleSpinBox*>("drawingSheetScaleNumerator")->value()==2 && window.findChild<QDoubleSpinBox*>("drawingSheetScaleDenominator")->value()==1,"Enlarged sheet scale was displayed incorrectly");
        auto* edit_sheet=window.findChild<QAction*>("editDrawingSheetAction");check(edit_sheet,"Sheet properties action missing");edit_sheet->trigger();flush();
        auto* sheet_dialog=window.findChild<QDialog*>("drawingSheetProperties");check(sheet_dialog,"Shared sheet properties dialog missing");
        auto* language=sheet_dialog->findChild<QComboBox*>("drawingSheetLanguage");check(language && language->currentText()=="de","Sheet dialog did not read CLI settings");
        language->setCurrentText("");sheet_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.findChild<QDialog*>("drawingSheetProperties") && window.execute_console_command("undo").code=="editing_in_progress","Invalid sheet properties closed or allowed a conflicting history edit");
        language->setCurrentText("fr");sheet_dialog->findChild<QDoubleSpinBox*>("drawingSheetScale")->setValue(.5);
        sheet_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QDialog*>("drawingSheetProperties") && drawing_run("drawing.sheet.get",{{"sheet",sheet_id}}).data.at("scale")==.5,"Sheet GUI did not commit shared properties");
        auto* drawing_undo=window.findChild<QAction*>("undoAction");check(drawing_undo && drawing_undo->isEnabled(),"Drawing GUI Undo remained disabled");drawing_undo->trigger();flush();
        check(drawing_run("drawing.sheet.get",{{"sheet",sheet_id}}).data.at("locale")=="de","Drawing GUI Undo did not restore properties");run("redo");
        edit_sheet->trigger();flush();sheet_dialog=window.findChild<QDialog*>("drawingSheetProperties");sheet_dialog->findChild<QComboBox*>("drawingSheetLanguage")->setCurrentText("ru");
        sheet_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        check(drawing_run("drawing.sheet.get",{{"sheet",sheet_id}}).data.at("locale")=="fr","Sheet Cancel committed pending values");
        drawing_run("drawing.sheet.delete",{{"sheet",sheet_id}});run("undo");check(run("drawing.sheet.list").data.at("items").size()==2,"Sheet deletion Undo did not restore GUI hierarchy");
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
        const auto projection_box=run("box.create 10 10 10").data.at("container").get<std::string>();
        const auto command_sketch=run("sketch.create Profile XY").data.at("sketch").get<std::string>();
        commands::Json projected_reference=commands::Json::object();
        const auto projection_sources=run(QString::fromStdString(commands::Json{{"command","reference.list"},{"arguments",{{"kind","edge"},{"owner",projection_box}}}}.dump())).data.at("items");
        for(const auto& source:projection_sources) {
            const auto source_data=run(QString::fromStdString(commands::Json{{"command","reference.get"},{"arguments",source}}.dump())).data;
            const auto& points=source_data.at("segments")[0].at("points");
            if(points.size()<2 || std::hypot(points.front()[0].get<double>()-points.back()[0].get<double>(),points.front()[1].get<double>()-points.back()[1].get<double>())<1)continue;
            projected_reference=source;break;
        }
        check(!projected_reference.empty(),"GUI projection fixture has no original edge");projected_reference["sketch"]=command_sketch;projected_reference["profile"]=true;
        run(QString::fromStdString(commands::Json{{"command","sketch.reference.create"},{"arguments",projected_reference}}.dump()));flush();
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
        commands::Json dimension_command={{"command","sketch.dimension.create"},{"arguments",{{"sketch",command_sketch},{"kind","radius"},{"geometry",{circle_id}},{"value",10},{"locked",true},{"layout",{{"text_along",3}}}}}};
        const auto dimension_id=run(QString::fromStdString(dimension_command.dump())).data.at("dimension").get<std::string>();flush();
        const auto dimension_query=QString::fromStdString("sketch.dimension.get "+command_sketch+" "+dimension_id);
        check(run(dimension_query).data.at("value")==10 && run(dimension_query).data.at("document_layout").at("text_along")==3,"Console dimension did not reach GUI document");
        run("undo");check(run(QString::fromStdString("sketch.get "+command_sketch)).data.at("counts").at("dimensions")==0,"GUI console dimension Undo failed");run("redo");
        commands::Json text_command={{"command","sketch.text.create"},{"arguments",{{"sketch",command_sketch},{"value","ZIMA"},{"position",{40,40}},{"height_mm",2},{"modeling_geometry",false}}}};
        const auto text_id=run(QString::fromStdString(text_command.dump())).data.at("text").get<std::string>();flush();
        check(run(QString::fromStdString("sketch.text.get "+command_sketch+" "+text_id)).data.at("contour_count").get<int>()>0,"Console did not create native text outlines");
        commands::Json spline_command={{"command","sketch.bspline.create"},{"arguments",{{"sketch",command_sketch},{"points",{{50,0},{53,8},{57,-3},{60,0}}},{"degree",3}}}};
        const auto spline_id=run(QString::fromStdString(spline_command.dump())).data.at("geometry").get<std::string>();
        commands::Json spline_patch={{"command","sketch.bspline.set"},{"arguments",{{"sketch",command_sketch},{"geometry",spline_id},{"points",{{50,0},{53,10},{57,2},{60,0}}}}}};
        run(QString::fromStdString(spline_patch.dump()));flush();
        check(run(QString::fromStdString("sketch.bspline.get "+command_sketch+" "+spline_id)).data.at("points")[1][1]==10,"Console spline edit did not reach document");
        run("save");const auto saved_sketch=document::PartDocument::load(directory/(stem+"-sketch.prtz"));
        check(saved_sketch.sketches.back().id==command_sketch && saved_sketch.sketches.back().circles.front().radius==10 && saved_sketch.sketches.back().dimensions.front().locked && saved_sketch.dimension_layouts.back().layout.text_along==3,"GUI console did not persist native Sketch");
        check(saved_sketch.sketches.back().texts.size()==1 && saved_sketch.sketches.back().texts.front().value=="ZIMA","Console did not persist native text");
        check(saved_sketch.sketches.back().external_references.size()==1 && !saved_sketch.sketches.back().import_blocks.empty(),"Console did not persist native projected reference");
        auto dxf_profile=sketcher::Sketch::create_default();static_cast<void>(dxf_profile.add_rectangle(0,0,12,6));
        const auto dxf_source=directory/std::filesystem::path(u8"obrys konzole.dxf");interchange::export_dxf(dxf_source,dxf_profile);
        commands::Json dxf_import={{"command","import.dxf"},{"arguments",{{"path",document::path_to_utf8(dxf_source)}}}};
        const auto dxf_result=run(QString::fromStdString(dxf_import.dump())).data;flush();
        check(dxf_result.at("imported_entities")==4 && !dxf_result.at("body_calculated").get<bool>(),"Console import did not use the DXF transaction");
        const auto before_menu_import=run("sketch.list").data.at("total").get<int>();
        auto* import_action=window.findChild<QAction*>("importDocumentAction");check(import_action,"Import menu action missing");
        bool chosen=false, timed_out=false;QTimer choose_import;choose_import.setInterval(50);
        QObject::connect(&choose_import,&QTimer::timeout,[&]{
            if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                if (!chosen) {
                    const auto absolute=std::filesystem::absolute(dxf_source);
                    dialog->setDirectory(QString::fromStdString(document::path_to_utf8(absolute.parent_path())));
                    dialog->selectFile(QString::fromStdString(document::path_to_utf8(absolute.filename())));
                    chosen=true;
                    return; // Let QFileSystemModel populate the chosen directory first.
                }
                // A visible QFileDialog with a proxy model can discard selectFile()
                // while its directory loads. Enter the path in the same field as a user.
                if (auto* filename=dialog->findChild<QLineEdit*>("fileNameEdit"))
                    filename->setText(QString::fromStdString(document::path_to_utf8(std::filesystem::absolute(dxf_source))));
                std::cout<<"Import chooser: "<<dialog->selectedFiles().join(" | ").toStdString()<<'\n';
                QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
            } else if (auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                std::cout<<"Import menu message: "<<message->text().toStdString()<<'\n';
                message->accept();
            }
        });
        QTimer import_timeout;import_timeout.setSingleShot(true);
        QObject::connect(&import_timeout,&QTimer::timeout,[&]{timed_out=true;if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();});
        choose_import.start();import_timeout.start(10000);import_action->trigger();choose_import.stop();import_timeout.stop();flush();
        const auto after_menu_import=run("sketch.list").data.at("total").get<int>();
        std::cout<<"Menu import result: selected="<<chosen<<", timeout="<<timed_out<<", sketches="<<before_menu_import<<" -> "<<after_menu_import<<'\n';
        check(chosen && !timed_out && after_menu_import==before_menu_import+1,"Menu import did not use the shared model transaction");
        const auto exported_profile=directory/(stem+"-export.dxf");
        commands::Json export_dxf={{"command","export.dxf"},{"arguments",{{"path",document::path_to_utf8(exported_profile)},{"sketch",dxf_result.at("sketch")}}}};
        check(run(QString::fromStdString(export_dxf.dump())).data.at("model_changed")==false && std::filesystem::file_size(exported_profile)>0,"Console export did not write its model snapshot");
        const auto exported_model=std::filesystem::absolute(directory/(stem+"-menu.step"));
        auto* menu_export_action=window.findChild<QAction*>("exportDocumentAction");check(menu_export_action,"Export menu action missing");
        bool export_chosen=false,export_timed_out=false;QTimer choose_export;choose_export.setInterval(50);
        QObject::connect(&choose_export,&QTimer::timeout,[&]{
            if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                if(!export_chosen) {dialog->setDirectory(QString::fromStdString(document::path_to_utf8(exported_model.parent_path())));dialog->selectNameFilter("STEP (*.step)");export_chosen=true;return;}
                if(auto* filename=dialog->findChild<QLineEdit*>("fileNameEdit"))filename->setText(QString::fromStdString(document::path_to_utf8(exported_model)));
                QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
            } else if(auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                std::cout<<"Export menu message: "<<message->text().toStdString()<<'\n';message->accept();
            }
        });
        QTimer export_timeout;export_timeout.setSingleShot(true);
        QObject::connect(&export_timeout,&QTimer::timeout,[&]{export_timed_out=true;if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();});
        choose_export.start();export_timeout.start(10000);menu_export_action->trigger();choose_export.stop();export_timeout.stop();flush();
        check(export_chosen && !export_timed_out && std::filesystem::is_regular_file(exported_model) && std::filesystem::file_size(exported_model)>0,"Menu export did not publish a STEP model");
        run("save");
        run(QString::fromStdString("new assembly "+stem+"-import-owner"));
        commands::Json assembly_import={{"command","import.step"},{"arguments",{{"path",document::path_to_utf8(exported_model)}}}};
        const auto assembly_result=run(QString::fromStdString(assembly_import.dump())).data;
        check(!assembly_result.at("parts").empty() && !assembly_result.at("occurrence").get<std::string>().empty(),"GUI console Assembly STEP import failed");
        chosen=false;timed_out=false;choose_import.start();import_timeout.start(10000);
        import_action->trigger();choose_import.stop();import_timeout.stop();flush();
        check(chosen && !timed_out,"Assembly menu import did not finish");run("save");
        const auto imported_owner=assembly::AssemblyDocument::load(directory/(stem+"-import-owner.asmz"));
        check(imported_owner.components.size()==2,"Assembly menu import did not add exactly one component");
        const auto imported_dxf_part=document::PartDocument::load(imported_owner.components.back().source_path);
        check(imported_dxf_part.sketches.back().segments.size()==4,"Assembly menu DXF did not preserve native geometry");
        run(QString::fromStdString("new part "+stem+"-metadata"));
        run("box.create 10 20 30");
        commands::Json metadata_parameters=commands::Json::array({{{"key","CLI_TEST"},{"values",{{"","before"}}}}});
        run(QString::fromStdString(commands::Json{{"command","document.parameters.set"},{"arguments",{{"parameters",metadata_parameters}}}}.dump()));
        auto* parameter_action=window.findChild<QAction*>("documentParametersAction");check(parameter_action,"Parameters action missing");
        parameter_action->trigger();flush();auto* parameter_dialog=window.findChild<QDialog*>("documentParametersDialog");
        auto* parameter_table=parameter_dialog?parameter_dialog->findChild<QTableWidget*>("documentParametersTable"):nullptr;
        check(parameter_table && parameter_table->rowCount()>=1 && parameter_table->item(0,0)->text()=="CLI_TEST","GUI did not read common parameter data");
        parameter_table->item(0,3)->setText("after GUI");parameter_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QDialog*>("documentParametersDialog") && run("document.parameters.get").data.at("parameters")[0].at("values").at("")=="after GUI","GUI parameter callback did not use shared transaction");
        parameter_action->trigger();flush();parameter_dialog=window.findChild<QDialog*>("documentParametersDialog");
        parameter_dialog->findChild<QTableWidget*>("documentParametersTable")->item(0,3)->setText("cancelled");
        parameter_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        check(run("document.parameters.get").data.at("parameters")[0].at("values").at("")=="after GUI","Parameter Cancel committed pending values");
        auto* settings_action=window.findChild<QAction*>("fileSettingsAction");check(settings_action,"File settings action missing");settings_action->trigger();flush();
        auto* settings_dialog=window.findChild<QDialog*>("fileSettingsDialog");check(settings_dialog,"File settings dialog missing");
        settings_dialog->findChild<QComboBox*>("fileUnitLength")->setCurrentText("cm");
        settings_dialog->findChild<QDoubleSpinBox*>("filePrecisionmesh_deflection")->setValue(2);
        settings_dialog->findChild<QSpinBox*>("filePrecisiondecimal_places")->setValue(6);
        settings_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        const auto metadata_settings=run("document.settings.get").data;
        check(!window.findChild<QDialog*>("fileSettingsDialog") && metadata_settings.at("units").at("Length")=="cm" && metadata_settings.at("precision").at("mesh_deflection")==2 && window.property("zimaDocumentDecimalPlaces").toInt()==6,"GUI settings callback did not share the metadata transaction");
        run("save");const auto metadata_saved=document::PartDocument::load(directory/(stem+"-metadata.prtz"));
        check(metadata_saved.user_parameters.at("CLI_TEST")=="after GUI" && metadata_saved.document_units.at("Length")=="cm","GUI metadata did not persist");
        const auto json_run=[&](const char* command,commands::Json arguments) {return run(QString::fromStdString(commands::Json{{"command",command},{"arguments",std::move(arguments)}}.dump()));};
        const auto aluminum=commands::Json::array({{{"key","MATERIAL_NAME"},{"value","Hliník"}},{{"key","MASS_DENSITY"},{"value","2700"},{"unit","kg/m^3"}}});
        json_run("document.material.set",{{"properties",aluminum}});
        auto* material_action=window.findChild<QAction*>("materialAction");check(material_action,"Material action missing");material_action->trigger();flush();
        auto* material_dialog=window.findChild<QDialog*>("materialDialog");auto* material_table=material_dialog?material_dialog->findChild<QTableWidget*>("materialTable"):nullptr;
        check(material_table,"Material dialog missing");int density_row=-1;
        for(int row=0;row<material_table->rowCount();++row)if(material_table->item(row,0) && material_table->item(row,0)->text()=="MASS_DENSITY")density_row=row;
        check(density_row>=0,"GUI did not read CLI material");material_table->item(density_row,1)->setText("-1");
        material_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.findChild<QDialog*>("materialDialog") && material_table->item(density_row,1)->text()=="-1" && QApplication::activeModalWidget()==nullptr,"Material validation lost pending data or closed the editor");
        material_table->item(density_row,1)->setText("7800");material_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QDialog*>("materialDialog") && std::abs(run("document.relations.get").data.at("model_values").at("model.mass").get<double>()-.0468)<1e-9,"Material GUI did not update cached physical mass");
        const auto library_source=std::filesystem::absolute(std::filesystem::path("config/materials/01_oceli/konstrukcni/S235JR.matz"));
        const auto load_gui_library=[&](bool confirm) {
            material_action->trigger();flush();auto* dialog=window.findChild<QDialog*>("materialDialog");
            auto* load=dialog?dialog->findChild<QPushButton*>("loadMaterialLibrary"):nullptr;check(load,"Material library button missing");
            bool chosen=false,failed=false;QTimer chooser;chooser.setInterval(50);
            QObject::connect(&chooser,&QTimer::timeout,[&]{
                if(auto* file=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                    if(!chosen){file->setDirectory(QString::fromStdString(document::path_to_utf8(library_source.parent_path())));chosen=true;return;}
                    if(auto* input=file->findChild<QLineEdit*>("fileNameEdit"))input->setText(QString::fromStdString(document::path_to_utf8(library_source)));
                    QMetaObject::invokeMethod(file,"accept",Qt::DirectConnection);
                }else if(auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){failed=true;message->accept();}
            });
            QTimer timeout;timeout.setSingleShot(true);QObject::connect(&timeout,&QTimer::timeout,[&]{failed=true;if(auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget()))modal->reject();});
            chooser.start();timeout.start(10000);load->click();chooser.stop();timeout.stop();flush();check(chosen && !failed,"GUI material library read failed");
            auto* table=dialog->findChild<QTableWidget*>("materialTable");bool found=false;
            for(int row=0;row<table->rowCount();++row)if(table->item(row,0) && table->item(row,0)->text()=="MASS_DENSITY")found=table->item(row,3)->text()=="Hustota";
            check(found,"GUI library lost the Czech description");
            dialog->findChild<QDialogButtonBox*>()->button(confirm?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();flush();
        };
        load_gui_library(false);check(std::abs(run("document.relations.get").data.at("model_values").at("model.mass").get<double>()-.0468)<1e-9,"Library Cancel committed its pending material");
        load_gui_library(true);check(std::abs(run("document.relations.get").data.at("model_values").at("model.mass").get<double>()-.0471)<1e-9,"Library OK did not use the shared material transaction");
        json_run("document.relations.set",{{"relations",commands::Json::array({{{"target","double_volume"},{"expression","model.volume * 2"}}})}});
        auto* relations_action=window.findChild<QAction*>("relationsAction");check(relations_action,"Relations action missing");relations_action->trigger();flush();
        auto* relations_dialog=window.findChild<QDialog*>("relationsDialog");auto* relations_table=relations_dialog?relations_dialog->findChild<QTableWidget*>("relationsTable"):nullptr;
        check(relations_table && relations_table->item(0,1)->text()=="model.volume * 2","GUI did not read CLI relations");
        relations_table->item(0,1)->setText("model.volume * 3");relations_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QDialog*>("relationsDialog") && run("document.relations.get").data.at("parameters").at("double_volume")=="18.000000","Relations GUI did not use shared evaluation");
        json_run("document.relations.set",{{"relations",commands::Json::array()}});relations_action->trigger();flush();relations_dialog=window.findChild<QDialog*>("relationsDialog");
        relations_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(run("document.relations.get").data.at("relations").empty(),"Opening empty relations invented a mass relation");
        const commands::Json family_table={{"columns",{"LENGTH"}},{"instances",commands::Json::array({{{"name","Varianta 10"},{"values",{{"LENGTH","10"}}}}})}};
        json_run("document.family.set",{{"table",family_table}});auto* family_action=window.findChild<QAction*>("familyTableAction");check(family_action,"Family action missing");family_action->trigger();flush();
        auto* family_dialog=window.findChild<QDialog*>("familyTableDialog");auto* family_widget=family_dialog?family_dialog->findChild<QTableWidget*>("familyTableTable"):nullptr;
        check(family_widget && family_widget->item(1,1)->text()=="10","GUI did not read native family table");family_widget->item(1,1)->setText("20");
        family_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QDialog*>("familyTableDialog") && run("document.family.get").data.at("table").at("instances")[0].at("values").at("LENGTH")=="20","Family GUI did not commit common data");
        family_action->trigger();flush();family_dialog=window.findChild<QDialog*>("familyTableDialog");family_dialog->findChild<QTableWidget*>("familyTableTable")->item(1,1)->setText("999");
        family_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        check(run("document.family.get").data.at("table").at("instances")[0].at("values").at("LENGTH")=="20","Family Cancel committed pending values");
        run("save");const auto engineering_saved=document::PartDocument::load(directory/(stem+"-metadata.prtz"));
        check(std::abs(std::stod(engineering_saved.physical_parameters.at("MASS_DENSITY"))-7.85e-6)<1e-12 && engineering_saved.relations.empty() && engineering_saved.family_table.find("20")!=std::string::npos,"GUI engineering metadata did not persist");
        run(QString::fromStdString("new assembly "+stem+"-components"));
        const auto component_owner=run("context").data.at("active_document").get<std::string>();
        const auto console_occurrence=json_run("component.insert",{{"source",engineering_saved.document_id},{"name","CLI component"}}).data;
        check(run("component.list").data.at("total")==1,"Console component insertion did not reach Assembly");
        auto* insert_menu=window.findChild<QMenu*>("insertComponentMenu");check(insert_menu,"Component menu missing");
        QMetaObject::invokeMethod(insert_menu,"aboutToShow",Qt::DirectConnection);QAction* insert_source=nullptr;
        for(auto* action:insert_menu->actions())if(action->objectName()=="insertSourceAction" && action->text().startsWith(QString::fromStdString(stem+"-metadata")))insert_source=action;
        check(insert_source && insert_source->isEnabled(),"Open Part missing from component menu");insert_source->trigger();flush();
        QDialog* component_dialog=nullptr;for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible() && dialog->findChild<QTableWidget*>("componentPlacementTable"))component_dialog=dialog;
        check(component_dialog,"GUI insertion did not open original component properties");component_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        check(run("component.list").data.at("total")==2 && json_run("component.get",{{"instance_path",console_occurrence.at("instance_path")}}).data.at("name")=="CLI component","GUI insertion did not share native command transaction");
        run("save");const auto saved_components=assembly::AssemblyDocument::load(directory/(stem+"-components.asmz"));check(saved_components.components.size()==2,"GUI component insertion did not persist");
        run(QString::fromStdString("new assembly "+stem+"-component-top"));json_run("component.insert",{{"source",component_owner}});
        check(json_run("component.list",{{"recursive",true}}).data.at("total")==3,"Console nested component query lost its hierarchy");
        const auto nested_rows=json_run("component.list",{{"recursive",true}}).data.at("items");std::string source_path;
        for(const auto& row:nested_rows)if(row.at("kind")=="part"){source_path=row.at("instance_path").get<std::string>();break;}
        check(!source_path.empty(),"Nested source path missing");
        json_run("close",{{"document",engineering_saved.document_id}});json_run("close",{{"document",component_owner}});flush();
        QTreeWidgetItem* source_item=nullptr;
        for(QTreeWidgetItemIterator it(model_tree);*it;++it)if((*it)->data(0,Qt::UserRole+1).toString().toStdString()==source_path){source_item=*it;break;}
        check(source_item,"Nested source tree item missing");model_tree->setCurrentItem(source_item);model_tree->scrollToItem(source_item);flush();
        bool chose_source=false;QTimer::singleShot(0,[&]{
            for(auto* menu:window.findChildren<QMenu*>())if(menu->isVisible())for(auto* action:menu->actions())if(action->objectName()=="openComponentSourceAction") {
                chose_source=true;menu->setActiveAction(action);QKeyEvent accept(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&accept);return;
            }
        });
        QTimer source_menu_timeout;source_menu_timeout.setSingleShot(true);
        QObject::connect(&source_menu_timeout,&QTimer::timeout,[&]{for(auto* menu:window.findChildren<QMenu*>())if(menu->isVisible())menu->close();});
        source_menu_timeout.start(10000);model_tree->customContextMenuRequested(model_tree->visualItemRect(source_item).center());source_menu_timeout.stop();flush();
        check(chose_source && run("context").data.at("active_document")==engineering_saved.document_id,"GUI source context action did not use shared source opening");
        bool middle_open=false;for(const auto& doc:run("documents").data)if(doc.at("id")==component_owner)middle_open=true;
        check(!middle_open,"Opening a source left intermediate assemblies open");
        const auto view_source_path=directory/(stem+"-metadata.prtz");std::vector<kernel::BodyResult> view_source_cache;
        const auto view_source=document::PartDocument::load(view_source_path,&view_source_cache);check(!view_source_cache.empty(),"Drawing GUI source cache missing");
        auto view_document=drawing::DrawingDocument::create_default();view_document.source_document_id=view_source.document_id;view_document.source_path=view_source_path;
        auto base_view=drawing::DrawingDocument::create_view(view_source.document_id,view_source_path,view_source_cache.back().mesh);
        auto projected_view=drawing::DrawingDocument::create_view(view_source.document_id,view_source_path,view_source_cache.back().mesh);projected_view.parent_view_id=base_view.id;projected_view.projection_direction=drawing::ProjectionDirection::Right;projected_view.x=50;
        view_document.sheets.front().views={projected_view,base_view};const auto view_file=directory/(stem+"-drawing-views.drwz");view_document.save(view_file);
        json_run("open",{{"path",document::path_to_utf8(view_file)}});flush();
        check(run("drawing.view.list").data.at("total")==2,"GUI view query lost projected hierarchy");
        auto* drawing_window=dynamic_cast<DrawingWindow*>(window.findChild<QWidget*>("drawingWorkspace"));check(drawing_window,"Drawing workspace missing");
        auto* regenerate_view=window.findChild<QAction*>("regenerateDrawingViewAction");check(regenerate_view&&regenerate_view->isEnabled(),"GUI view regeneration disabled");regenerate_view->trigger();flush();
        check(json_run("drawing.view.get",{{"view",base_view.id}}).data.at("model_annotations").get<int>()>0,"GUI regeneration omitted common model annotations");
        check(json_run("drawing.view.references",{{"view",base_view.id}}).data.at("total").get<int>()>0,"GUI view lacks persisted original references");
        drawing_window->select_view(base_view.id);flush();auto* delete_view=window.findChild<QAction*>("deleteDrawingViewAction");check(delete_view&&delete_view->isEnabled(),"GUI view deletion disabled");delete_view->trigger();flush();
        check(run("drawing.view.list").data.at("total")==0,"GUI view deletion left projected descendants");run("undo");check(run("drawing.view.list").data.at("total")==2,"GUI view deletion Undo lost the hierarchy");
        json_run("drawing.view.set",{{"view",base_view.id},{"x_mm",110},{"name","Z konzole"}});flush();
        check(json_run("drawing.view.get",{{"view",projected_view.id}}).data.at("x_mm")==60,"Console did not move the projected child");
        drawing_window->select_view(base_view.id);auto* edit_view=window.findChild<QAction*>("editDrawingViewAction");check(edit_view&&edit_view->isEnabled(),"View properties disabled");edit_view->trigger();flush();
        auto* view_properties=window.findChild<QDialog*>("drawingViewProperties");check(view_properties&&view_properties->findChild<QLineEdit*>("drawingViewName")->text()=="Z konzole","View dialog did not consume console edits");
        view_properties->findChild<QDoubleSpinBox*>("drawingViewX")->setValue(120);
        check(window.execute_console_command("undo").code=="editing_in_progress","View preview allowed conflicting Undo");
        view_properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QDialog*>("drawingViewProperties")&&json_run("drawing.view.get",{{"view",projected_view.id}}).data.at("x_mm")==70,"GUI did not share the view edit operation");
        run("undo");check(json_run("drawing.view.get",{{"view",projected_view.id}}).data.at("x_mm")==60,"GUI view edit Undo did not restore the child");
        const auto console_projection=json_run("drawing.view.create",{{"sheet",view_document.sheets.front().id},{"parent_view",base_view.id},{"projection_direction","top"},{"distance_mm",45}}).data.at("view").get<std::string>();flush();
        check(run("drawing.view.list").data.at("total")==3&&json_run("drawing.view.get",{{"view",console_projection}}).data.at("parent_view")==base_view.id,"Console-created projection was not displayed");
        const auto offered_annotations=json_run("drawing.annotation.list",{{"view",base_view.id},{"mode","show"}}).data.at("items");
        check(!offered_annotations.empty(),"Console did not offer model annotations");
        const auto annotation_ref=offered_annotations[0].at("reference");
        const commands::Json annotation_request={{"views",commands::Json::array({commands::Json{{"view",base_view.id},{"selected",commands::Json::array({annotation_ref})}}})}};
        json_run("drawing.annotation.show_erase",annotation_request);flush();
        const auto visible_annotations=[&]{return json_run("drawing.annotation.list",{{"view",base_view.id},{"mode","erase"}}).data.at("items");};
        check(visible_annotations().size()==1&&visible_annotations()[0].at("reference")==annotation_ref,"Console Show/Erase changed the wrong annotation");
        run("undo");flush();check(visible_annotations().empty(),"Console Show/Erase Undo failed");
        drawing_window->select_view(base_view.id);auto* show_erase=window.findChild<QAction*>("drawingShowEraseAction");check(show_erase&&show_erase->isEnabled(),"GUI Show/Erase disabled");
        show_erase->trigger();flush();auto* annotation_dialog=window.findChild<QDialog*>("drawingShowEraseDialog");check(annotation_dialog,"Show/Erase properties missing");
        const commands::Json blocked_annotation={{"command","drawing.annotation.show_erase"},{"arguments",annotation_request}};
        check(window.execute_console_command(QString::fromStdString(blocked_annotation.dump())).code=="editing_in_progress","Console modified annotations during GUI preview");
        annotation_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        check(window.grab().save(QString::fromStdString((directory/"command-drawing-views.png").string())),"Drawing view screenshot failed");
        json_run("close",{{"discard",true}});
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
