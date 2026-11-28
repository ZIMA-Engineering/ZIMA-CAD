#include "assembly_workspace_window.hpp"
#include "console_ui_verification.hpp"
#include "application_settings.hpp"
#include <zima/workspace/profile_operations.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QMenu>
#include <QMouseEvent>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <iostream>

namespace zima::app {
int verify_assembly_modes_ui(QApplication& application,AssemblyWorkspaceWindow&,
        const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto original=ApplicationSettings::load(QDir::currentPath());
    const auto restore=qScopeGuard([&]{apply_application_translations(application,original);apply_application_appearance(application,original);});
    try {
        kernel::OcctKernel kernel;workspace::Workspace live;
        auto part=document::PartDocument::create_default();const auto body=part.body_history.create_body("Specimen");
        live.add_part(part);live.activate(part.document_id);
        auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;
        static_cast<void>(sketch.add_rectangle(0,0,20,15));
        auto feature=document::PartDocument::create_feature_container(sketch.id);feature.feature.sides[0].length=10;
        workspace::commit_profile(live,kernel,part.document_id,feature,workspace::ProfileEditMode::Create,sketch);
        const auto* source=live.open_part(part.document_id);
        const auto part_path=directory/"mode-specimen.prtz";
        source->session.document().save(part_path,source->session.calculated_boundaries());
        auto nested=assembly::AssemblyDocument::create_default();
        const auto component=assembly::AssemblyDocument::create_part_occurrence("Specimen",part.document_id,
            part_path.filename(),source->session.calculated_boundaries().back());
        nested.components={component};const auto nested_path=directory/"mode-nested.asmz";nested.save(nested_path);
        auto top=assembly::AssemblyDocument::create_default();
        const auto outer=assembly::AssemblyDocument::create_assembly_occurrence("Subassembly",nested.document_id,nested_path.filename(),nested);
        top.components={outer};const auto top_path=directory/"mode-top.asmz";top.save(top_path);
        const auto outer_path=assembly::InstancePath{{outer.occurrence_id}}.encoded();
        const auto inner_path=assembly::InstancePath{{outer.occurrence_id,component.occurrence_id}}.encoded();
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QTemporaryDir settings_dir;check(settings_dir.isValid(),"Assembly mode settings directory failed");
            QSettings ini(settings_dir.filePath("config.ini"),QSettings::IniFormat);
            ini.setValue("Application/Language",QString::fromLatin1(language));
            ini.setValue("Paths/Localization",QDir::current().absoluteFilePath("config/localization"));ini.sync();
            auto settings=ApplicationSettings::load(settings_dir.path());apply_application_translations(application,settings);
            AssemblyWorkspaceWindow window(QString::fromStdWString(directory.wstring()),settings_dir.path());
            window.resize(1400,950);window.show();
            check(window.open_document_path(QString::fromStdWString(top_path.wstring())),"Assembly mode fixture failed to open");flush();
            auto* tree=window.tree_;auto* toolbar=window.tools_toolbar_;
            auto* assembly_action=window.findChild<QAction*>("applicationModeAction1");
            auto* mechanisms_action=window.findChild<QAction*>("applicationModeAction10");
            const auto row=[&](const char* role,const std::string& path)->QTreeWidgetItem* {
                for(QTreeWidgetItemIterator it(tree);*it;++it)
                    if((*it)->data(0,Qt::UserRole+3)==role&&(*it)->data(0,Qt::UserRole+1).toString().toStdString()==path)return *it;
                return nullptr;
            };
            const auto activate_group=[&](const char* role,const std::string& path) {
                auto* item=row(role,path);check(item,"Assembly group missing");
                for(auto* p=item->parent();p;p=p->parent())p->setExpanded(true);
                tree->scrollToItem(item);flush();const auto point=tree->visualItemRect(item).center();
                for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease,QEvent::MouseButtonDblClick,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,QPointF(point),QPointF(tree->viewport()->mapToGlobal(point)),Qt::LeftButton,
                        type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(tree->viewport(),&event);flush();
                }
            };
            auto* root=tree->topLevelItem(0);
            check(root&&root->childCount()>=3&&root->child(0)->data(0,Qt::UserRole+3)=="document-sections"&&
                root->child(1)->data(0,Qt::UserRole+3)=="assembly-mechanisms-group"&&
                root->child(2)->data(0,Qt::UserRole+3)=="assembly-model-group","Assembly group ordering is incorrect");
            check(row("assembly-mechanisms-group",{})->childCount()==0,"Empty Mechanisms group owns model objects");
            check(row("assembly-occurrence",outer_path)->parent()==row("assembly-model-group",{}),"Assembly occurrence lost grouping");
            check(row("assembly-insert-here",{})&&row("assembly-insert-here",{})->parent()==row("assembly-model-group",{}),"Assembly history cursor lost its owner");
            check(window.tree_item_reorder_enabled(row("assembly-occurrence",outer_path)),"Grouping disabled immediate occurrence reorder");
            check(assembly_action->isChecked()&&mechanisms_action->isEnabled()&&window.assembly_mode_button_->isVisible()&&
                window.mechanisms_mode_button_->isVisible()&&!window.mech_mode_button_->isVisible(),"Assembly modes have incorrect availability");
            check(mechanisms_action->text()==settings.qt_translations.value("Mechanisms")&&
                row("assembly-mechanisms-group",{})->text(0)==mechanisms_action->text(),"Mechanisms action/group are not localized");
            const auto baseline=window.execute_console_command("documents").data;
            const auto calls=kernel::body_calculation_count.load();const auto* triangles=window.viewer_->mesh().triangles.data();
            window.mechanisms_mode_button_->click();flush();
            check(mechanisms_action->isChecked()&&!assembly_action->isChecked(),"Mechanisms button did not activate its application");
            check(std::ranges::any_of(toolbar->actions(),[](auto* a){return a->objectName()=="mechanismsPlaceholderAction"&&!a->isEnabled();})&&
                !toolbar->actions().contains(window.findChild<QAction*>("insertComponentAction"))&&window.command_insert_menu_->actions().empty(),"Mechanisms placeholder exposes modeling commands");
            check(window.viewer_->mesh().triangles.data()==triangles,"Empty mode switch republished geometry");
            activate_group("assembly-model-group",{});check(assembly_action->isChecked(),"Assembly Tree activation failed");
            activate_group("assembly-mechanisms-group",{});check(mechanisms_action->isChecked(),"Mechanisms Tree activation failed");
            check(window.execute_console_command("documents").data==baseline&&kernel::body_calculation_count.load()==calls,"Mode switching modified the document or calculated material");
            for(const auto* theme:{"light","dark","light"}) {
                settings.theme=QString::fromLatin1(theme);apply_application_appearance(application,settings);flush();
                check(!window.mechanisms_mode_button_->icon().pixmap(24,24).isNull(),"Mechanisms icon lost theme contrast");
                check(window.assembly_mode_button_->palette().color(QPalette::ButtonText)==application.palette().color(QPalette::ButtonText),"Inactive Assembly button retained the previous theme text color");
                check(window.grab().save(QString::fromStdWString((directory/(std::string("assembly-modes-")+language+"-"+theme+".png")).wstring())),"Assembly mode capture failed");
            }
            // Nested editing remains owned by the immediate Assembly. The
            // top-level context and independent per-document mode stay intact.
            check(window.activate_occurrence_for_test(outer_path),"Nested Assembly activation failed");flush();
            check(window.workspace_.active_document_id()==nested.document_id&&window.workspace_.displayed_document_id()==top.document_id&&assembly_action->isChecked(),"Nested Assembly changed display/owner/mode");
            check(row("part-occurrence",inner_path)->parent()==row("assembly-model-group",outer_path)&&
                window.tree_item_reorder_enabled(row("part-occurrence",inner_path)),"Nested grouping changed immediate history ownership");
            window.mechanisms_mode_button_->click();flush();
            check(window.activate_occurrence_for_test(inner_path),"Nested Part activation failed");flush();
            check(window.workspace_.active_document_id()==part.document_id&&window.workspace_.displayed_document_id()==top.document_id&&
                !mechanisms_action->isEnabled()&&!window.mechanisms_mode_button_->isVisible()&&window.modeling_mode_button_->isVisible(),"Part inherited Assembly Mechanisms mode");
            activate_group("assembly-model-group",{});flush();
            check(window.workspace_.active_document_id()==top.document_id&&assembly_action->isChecked(),"Top Assembly group did not restore ownership");
            check(window.activate_occurrence_for_test(outer_path),"Nested Assembly reactivation failed");flush();
            check(mechanisms_action->isChecked(),"Nested Assembly did not retain its own mode");
            window.deactivate_active_occurrence_for_test();flush();
            check(assembly_action->isChecked()&&kernel::body_calculation_count.load()==calls,"Context activation recalculated material or lost top mode");
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto saved=assembly::AssemblyDocument::load(top_path);
            check(saved.components.size()==1&&saved.components.front().occurrence_id==outer.occurrence_id&&saved.complete_history_order()==top.complete_history_order(),"Presentation groups changed native Assembly history");
        }
        std::cout<<"Assembly modes: five languages, Light/Dark/Light, ordered groups, empty application, mouse activation, nested ownership, history cursor/reorder, native save and zero calculation passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"Assembly modes: "<<error.what()<<'\n';return 1;}
}
}
