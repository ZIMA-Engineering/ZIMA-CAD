#include "console_ui_verification.hpp"
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
        input->setText("context");QApplication::sendEvent(input,&enter);flush();
        check(window.grab().save(QString::fromStdString((directory/"command-console.png").string())),"Console screenshot failed");
        toggle->trigger();flush();check(!dock->isVisible(),"Console toggle did not hide panel");
        std::cout<<"Console panel, shared operations, transactions, stale target and error paths passed\n";
        return 0;
    }catch(const std::exception& error){window.grab().save(QString::fromStdString((directory/"command-console-failure.png").string()));std::cerr<<"Console contract: "<<error.what()<<'\n';return 1;}
}
} // namespace zima::app
