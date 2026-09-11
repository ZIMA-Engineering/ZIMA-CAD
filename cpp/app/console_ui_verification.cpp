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
        const auto before=run("documents").data;run("tree");run("context");check(run("documents").data==before,"Read commands changed document state");
        run("save");
        const auto path=directory/(stem+".prtz");
        auto loaded=document::PartDocument::load(path);check(loaded.history.size()==1,"Console did not save the GUI feature");
        run("undo");run("save");loaded=document::PartDocument::load(path);check(loaded.history.empty(),"Console Undo did not use GUI history");
        run("redo");run("regenerate");run("save");loaded=document::PartDocument::load(path);check(loaded.history.size()==1,"Console Redo/regenerate lost feature");
        const auto missing=(directory/(stem+"-missing.prtz")).generic_string();
        commands::Json open={{"command","open"},{"arguments",{{"path",missing}}}};
        check(!window.execute_console_command(QString::fromStdString(open.dump())).ok,"Missing file reported success");
        check(QApplication::activeModalWidget()==nullptr,"Command error opened a modal message box");
        run(QString::fromStdString("new part "+stem+"-io-error"));
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
