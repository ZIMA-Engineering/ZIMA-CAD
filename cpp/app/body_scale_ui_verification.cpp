#include "assembly_workspace_window.hpp"
#include "body_scale_dialog.hpp"
#include "../tests/profile_solid_fixture.hpp"
#include <zima/workspace/model_calculation.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QApplication>
#include <QAction>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QTabBar>
#include <iostream>

namespace zima::app {
int verify_body_scale(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    try {
        const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        auto part=document::PartDocument::create_default();auto feature=test::rectangular_feature(part,{10,10,10});part.history={feature};
        document::BodyHistoryGraph graph;const auto source=graph.create_body("Scale source");
        graph.insert({document::PartHistoryKind::Feature,feature.id});graph.activate({});part.set_body_history(graph);
        kernel::OcctKernel kernel;const auto path=directory/"body-scale-ui.prtz";
        part.save(path,workspace::calculate_part_with_resolved_references(kernel,part));
        window.resize(1200,900);window.show();check(window.open_document_path(QString::fromStdString(path.string())),"Scale fixture did not open");flush();
        check(window.execute_console_command("body.activate").ok,"Part context could not be activated");flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");auto* action=window.findChild<QAction*>("bodyScaleAction");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
        check(tree&&view,"Scale workspace controls are missing");
        check(action&&action->isEnabled(),"Scale command is unavailable");
        check(!action->icon().isNull(),"Scale command has no icon");
        const auto check_cursor=[&](const char* kind) {
            bool found=false;
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+3).toString()==kind) {
                check((*it)->foreground(0).color()==QColor("#00D1FF"),"Insert Here is not azure");found=true;
            }
            check(found,"Insert Here cursor is missing");
        };
        check_cursor("part-body-insert-here");
        auto* tabs=window.findChild<QTabBar*>("documentTabs");
        auto* slot=tabs->tabButton(tabs->currentIndex(),QTabBar::RightSide);
        auto* close=slot->findChild<QPushButton*>("documentTabCloseButton");
        check(close&&slot->width()-close->geometry().right()-1==2,"Tab close button is not at the right inset");
        const auto tab=tabs->tabRect(tabs->currentIndex());
        const QRect button(close->mapTo(tabs,QPoint{}),close->size());
        const int tab_top=button.top()-tab.top(),bottom=tab.bottom()-button.bottom(),right=tab.right()-button.right();
        check(std::abs(tab_top-bottom)<=1&&right==tab_top,"Tab close button outer gaps are not balanced");
        tabs->grab().save(QString::fromStdString((directory/"document-tab-spacing.png").string()));
        const auto row=[&](const std::string& id)->QTreeWidgetItem*{
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&
                (*it)->data(0,Qt::UserRole+3).toString()=="part-body")return *it;return nullptr;};
        tree->setCurrentItem(row(source));action->trigger();flush();
        auto* dialog=dynamic_cast<BodyScaleDialog*>(window.findChild<QDialog*>("bodyScaleDialog"));
        check(dialog&&dialog->pending.scale->source_id==source,"Scale did not prefill selected Body");
        check(dialog->parentWidget()==&window&&(dialog->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"Scale properties are not internal");
        check(!dialog->buttons()->button(QDialogButtonBox::Apply),"Scale exposes Apply");
        const auto scaled=dialog->pending.scope.id;
        dialog->request_source();flush();check(dialog->active_input(),"Scale source input did not arm");
        QMetaObject::invokeMethod(tree,"itemClicked",Qt::DirectConnection,Q_ARG(QTreeWidgetItem*,row(source)),Q_ARG(int,0));flush();
        check(dialog->pending.scale->source_id==source&&!dialog->active_input(),"Tree did not confirm the source Body");
        auto* refs=dialog->findChild<QTableWidget*>("bodyScaleSource");
        auto* field=dynamic_cast<ui::ReferenceCellItem*>(refs->item(0,2));
        auto* eye=refs->cellWidget(0,3)->findChild<QToolButton*>();eye->click();flush();
        check(field->is_inspected()&&!field->is_active_input(),"Source inspection armed replacement");
        const auto top=refs->mapTo(dialog,QPoint{}).y();dialog->resize(dialog->width()+60,dialog->height()+120);flush();
        check(refs->mapTo(dialog,QPoint{}).y()==top,"Resizing moved the source row vertically");
        dialog->findChild<QDoubleSpinBox*>("bodyScaleFactor")->setValue(2);flush();
        window.grab().save(QString::fromStdString((directory/"body-scale-properties.png").string()));
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QDialog*>("bodyScaleDialog")&&row(scaled)&&row(scaled)->childCount()==0,"Scale did not commit as a history-free Body");
        const auto suppress=window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.suppress"},{"arguments",{{"body",source},{"suppressed",true}}}}.dump()));
        check(suppress.ok,"Source suppression failed");flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        std::vector<kernel::BodyResult> cache;auto saved=document::PartDocument::load(path,&cache);
        check(std::abs(cache.back().volume-8000)<1e-7&&saved.body_history.find(source)->suppressed,"Scale result includes suppressed source");
        tree->setCurrentItem(row(scaled));window.show_tree_item_properties(row(scaled));flush();
        dialog=dynamic_cast<BodyScaleDialog*>(window.findChild<QDialog*>("bodyScaleDialog"));
        check(dialog&&dialog->pending.scale->factor==2,"Scale edit did not reopen its parameters");
        const auto input_edges=view->mesh().original_references.edges.size();
        dialog->findChild<QTableWidget*>("bodyScaleSource")->cellWidget(0,3)->findChild<QToolButton*>()->click();flush();
        check(dialog->inspected()&&view->mesh().original_references.edges.size()>input_edges,"Suppressed source inspection has no geometry");
        dialog->findChild<QDoubleSpinBox*>("bodyScaleFactor")->setValue(3);dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        const auto cancelled=document::PartDocument::load(path,&cache);
        check(cancelled.body_history==saved.body_history&&std::abs(cache.back().volume-8000)<1e-7,"Cancel changed Scale or its source");
        const auto command=[&](const char* name,commands::Json args=commands::Json::object()) {
            const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",name},{"arguments",std::move(args)}}.dump()));
            if(!result.ok)throw std::runtime_error(result.message);flush();return result.data;
        };
        command("new",{{"type","assembly"},{"name","Scale assembly"}});
        check_cursor("assembly-insert-here");
        const auto occurrence=command("component.insert",{{"source",part.document_id}}).at("occurrence").get<std::string>();
        command("component.insert",{{"source",part.document_id}});
        const auto prefix=assembly::InstancePath{}.child(occurrence).encoded();
        command("component.activate",{{"instance_path",prefix}});command("body.activate");
        tree->setCurrentItem(row(scaled));window.show_tree_item_properties(row(scaled));flush();
        dialog=dynamic_cast<BodyScaleDialog*>(window.findChild<QDialog*>("bodyScaleDialog"));check(dialog,"Nested Scale properties did not open");
        dialog->request_source();flush();bool offered=false;
        for(int y=4;y<view->height();y+=16)for(int x=4;x<view->width();x+=16)
            for(const auto& candidate:view->selection_candidates_at(QPointF(x,y))) {
                check(candidate.instance_path==prefix&&candidate.owner_id==source,"Scale offered another occurrence or a leaf feature as source");offered=true;
            }
        check(offered,"Nested Scale has no source candidate");
        const QPointF point(view->width()/2.,view->height()/2.);
        QMouseEvent press(QEvent::MouseButtonPress,point,point,point,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,point,point,point,Qt::MiddleButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);flush();
        check(window.findChild<QDialog*>("bodyScaleDialog")&&!dialog->active_input()&&dialog->pending.scale->source_id==source,
            "Short middle click committed Scale or deleted its source");
        QMouseEvent confirm(QEvent::MouseButtonDblClick,point,point,point,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(view,&confirm);flush();
        check(!window.findChild<QDialog*>("bodyScaleDialog"),"Middle double click over View did not confirm Scale");
        std::cout<<"Scale GUI source entry, inspection, resizing, commit, suppression, reopen and Cancel passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
} // namespace zima::app
