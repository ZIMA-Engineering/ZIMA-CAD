#include "assembly_workspace_window.hpp"
#include "fem_body_dialog.hpp"
#include "fem_condition_dialog.hpp"
#include <zima/workspace/fem_condition_operations.hpp>
#include <zima/document/placement_orientation.hpp>
#include "../tests/profile_solid_fixture.hpp"
#include <zima/workspace/fem_operations.hpp>
#include <zima/workspace/body_operations.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QApplication>
#include <QAction>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

namespace zima::app {
int verify_fem_body(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        auto part=document::PartDocument::create_default();
        const auto first=test::rectangular_feature(part,{20,10,5}),second=test::rectangular_feature(part,{10,6,3});part.history={first,second};
        document::BodyHistoryGraph graph;const auto a=graph.create_body("Stock A");graph.insert({document::PartHistoryKind::Feature,first.id});
        const auto b=graph.create_body("Stock B");graph.insert({document::PartHistoryKind::Feature,second.id});
        const auto joined=graph.create_boolean("Union",kernel::BodyCombination::Add,a,b);graph.activate({});part.set_body_history(graph);
        kernel::OcctKernel kernel;const auto path=directory/"fem-body-ui.prtz";
        part.save(path,workspace::calculate_part_with_resolved_references(kernel,part));
        window.resize(1400,950);window.show();check(window.open_document_path(QString::fromStdString(path.string())),"FEM fixture did not open");flush();
        window.activate_body({});flush();
        auto* tree=window.tree_;auto* view=window.viewer_;auto* action=window.findChild<QAction*>("createFemAction");
        check(action&&action->isEnabled(),"Create FEM is unavailable at Body level");
        auto& session=window.workspace_.open_part(part.document_id)->session;
        const auto row=[&](const std::string& id)->QTreeWidgetItem*{
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&
                ((*it)->data(0,Qt::UserRole+3).toString()=="part-body"||(*it)->data(0,Qt::UserRole+3).toString()=="part-body-boolean"))return *it;return nullptr;};
        const auto current=[&]{return dynamic_cast<FemBodyDialog*>(window.findChild<QDialog*>("femBodyDialog"));};
        const auto count=kernel::body_calculation_count.load(),revision=session.revision();
        const auto snapshot=session.calculated_boundaries().back().body_outputs.at(a);
        tree->setCurrentItem(row(joined));action->trigger();flush();auto* dialog=current();
        check(dialog&&dialog->pending.name==QObject::tr("FEM Body").toStdString()+" 1","New FEM Body has no numbered default name");
        check(dialog&&dialog->pending.fem->source_id==joined,"FEM did not prefill Boolean source");
        check(dialog->parentWidget()==&window&&(dialog->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"FEM properties are not internal");
        check(!dialog->buttons()->button(QDialogButtonBox::Apply),"FEM exposes Apply");
        dialog->arm();flush();
        QMetaObject::invokeMethod(tree,"itemClicked",Qt::DirectConnection,Q_ARG(QTreeWidgetItem*,row(a)),Q_ARG(int,0));flush();
        check(dialog->pending.fem->source_id==a&&!dialog->active_input(),"FEM Tree could not select a consumed Body");
        auto* refs=dialog->findChild<QTableWidget*>("femBodySource");
        auto* field=dynamic_cast<ui::ReferenceCellItem*>(refs->item(0,2));
        refs->cellWidget(0,3)->findChild<QToolButton*>()->click();flush();
        check(field->is_inspected()&&!field->is_active_input()&&!view->transient_edges().empty(),"FEM eye armed replacement or omitted wire");
        const auto top=refs->mapTo(dialog,QPoint{}).y();dialog->resize(dialog->width()+80,dialog->height()+130);flush();
        check(refs->mapTo(dialog,QPoint{}).y()==top&&refs->rowHeight(0)==32,"FEM resize moved or stretched reference row");
        dialog->arm();flush();view->fit_all();flush();
        bool offered=false;std::optional<QPointF> view_hit;
        for(int y=5;y<view->height();y+=18)for(int x=5;x<view->width();x+=18)for(const auto& c:view->selection_candidates_at(QPointF(x,y))) {
            check(c.kind==viewer::CandidateKind::Container&&(c.owner_id==a||c.owner_id==b||c.owner_id==joined),"FEM offered a leaf or foreign source");offered=true;
            if(c.owner_id==a&&!view_hit)view_hit=QPointF(x,y);
        }
        check(offered,"FEM View source entry offers no geometry");
        check(view_hit.has_value(),"Consumed FEM source is absent from View picking");
        QMouseEvent move(QEvent::MouseMove,*view_hit,QPointF(view->mapToGlobal(view_hit->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&move);flush();
        const auto click_at=[&](Qt::MouseButton button) {
            for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,*view_hit,QPointF(view->mapToGlobal(view_hit->toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&event);flush();
            }
        };
        for(std::size_t i=0;i<view->selection_candidates_at(*view_hit).size();++i) {
            if(view->offered_candidate()&&view->offered_candidate()->owner_id==a)break;click_at(Qt::RightButton);
        }
        click_at(Qt::LeftButton);check(dialog->pending.fem->source_id==a&&!dialog->active_input(),"FEM View click did not retain its exact source");dialog->arm();flush();
        const QPointF point(view->width()/2.,view->height()/2.);
        const auto mouse=[&](QEvent::Type type,Qt::MouseButtons buttons){QMouseEvent event(type,point,point,point,Qt::MiddleButton,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
        mouse(QEvent::MouseButtonPress,Qt::MiddleButton);mouse(QEvent::MouseButtonRelease,Qt::NoButton);
        check(current()&&!dialog->active_input()&&dialog->pending.fem->source_id==a,"Short middle click committed FEM or deleted source");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(session.revision()==revision&&kernel::body_calculation_count.load()==count,"FEM opening/entry/Cancel changed CAD or document");
        tree->setCurrentItem(row(a));action->trigger();flush();dialog=current();const auto fem=dialog->pending.scope.id;
        mouse(QEvent::MouseButtonDblClick,Qt::MiddleButton);
        check(!current()&&row(fem)&&row(fem)->childCount()==2&&window.active_fem_body(),"FEM creation did not activate its history-free Body");
        check(workspace::read_fem_source(session,fem).shares_with(snapshot),"FEM source packet was duplicated");
        check(view->mesh().triangles==snapshot->mesh.triangles&&view->mesh().vertices==snapshot->mesh.vertices,"Active FEM contains another or duplicate Body");
        for(auto* command:{window.sketch_action_,window.extrusion_action_,window.shell_action_,window.construction_axis_action_})check(!command->isEnabled(),"FEM enables material editing");
        check(window.fem_mode_button_->isVisible()&&!window.sheet_metal_mode_button_->isVisible(),"FEM mode buttons are incorrect");
        window.show_tree_item_properties(row(fem));flush();dialog=current();check(dialog,"FEM properties did not reopen");
        check(view->mesh().axes.empty()&&std::ranges::none_of(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==fem+":origin";}),
            "FEM properties added a modeling Origin to the analysis input");
        const auto unchanged=session.revision();dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(session.revision()==unchanged,"Unchanged FEM OK added Undo history");
        window.show_tree_item_properties(row(fem));flush();dialog=current();dialog->set_source(joined,QStringLiteral("Union"));
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(session.document().body_history.find(fem)->fem->source_id==joined,"FEM replacement did not commit");
        window.findChild<QAction*>("undoAction")->trigger();flush();check(session.document().body_history.find(fem)->fem->source_id==a,"FEM source Undo failed");
        window.findChild<QAction*>("redoAction")->trigger();flush();check(session.document().body_history.find(fem)->fem->source_id==joined,"FEM source Redo failed");
        session.document().save(path,session.calculated_boundaries());std::vector<kernel::BodyResult> cached;
        const auto saved=document::PartDocument::load(path,&cached);
        check(saved.body_history.find(fem)->fem->source_id==joined&&std::abs(cached.back().volume-1000.)<1e-7,"FEM save/reopen lost source or duplicated volume");
        check(kernel::body_calculation_count.load()==count,"FEM creation/edit/save/Undo calculated CAD");
        window.activate_body({});flush();check(action->isEnabled(),"Returning from FEM did not restore Body commands");
        action->trigger();flush();dialog=current();
        check(dialog&&dialog->pending.name==QObject::tr("FEM Body").toStdString()+" 2","Second FEM Body repeats its default name");
        dialog->reject();flush();
        const auto suppressed_count=kernel::body_calculation_count.load();
        static_cast<void>(workspace::set_part_body_suppressed(window.workspace_,kernel,part.document_id,fem,true));
        static_cast<void>(workspace::set_part_body_suppressed(window.workspace_,kernel,part.document_id,fem,false));
        check(kernel::body_calculation_count.load()==suppressed_count,"Suppressing analysis recalculated CAD");
        window.activate_body(a);flush();check(window.sketch_action_->isEnabled()&&!window.fem_mode_button_->isVisible(),"Material Body commands did not recover");
        // Exercise the real first condition commands before localization.
#include "fem_condition_ui_verification.inc"
        // All localized strings use the same catalog and internal dialog.
        const auto root=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/localization";
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QFile file(QString::fromStdString((root/(std::string(language)+".qt.json")).generic_string()));check(file.open(QIODevice::ReadOnly),"FEM catalog unavailable");
            ApplicationSettings settings;settings.language=language;const auto messages=QJsonDocument::fromJson(file.readAll()).object();
            for(auto it=messages.begin();it!=messages.end();++it)settings.qt_translations[it.key()]=it.value().toString();
            apply_application_translations(application,settings);
            for(const auto* theme:{"dark","light"}) {
                settings.theme=theme;apply_application_appearance(application,settings);flush();
                window.show_fem_body_properties(fem);flush();dialog=current();check(dialog,"Localized FEM dialog missing");
                check(dialog->windowTitle()==QObject::tr("FEM Body properties"),"FEM title did not translate");
                window.grab().save(QString::fromStdString((directory/(std::string("fem-body-")+language+"-"+theme+".png")).string()));
                dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
                window.show_fem_condition_properties(document::FemConditionKind::Force,force_id);flush();
                auto* load=condition_dialog();check(load,"Localized condition dialog missing");
                check(load->windowTitle()==QObject::tr("FEM condition properties")&&load->findChild<QComboBox*>("femConditionKind")->currentText()==QObject::tr("Force"),"Condition title/type did not translate");
                const auto retained=load->pending_value();
                QEvent translation(QEvent::LanguageChange);QApplication::sendEvent(load,&translation);flush();check(load->pending_value()==retained,"Condition retranslation changed authored values");
                check(load->buttons()->button(QDialogButtonBox::Cancel)->text()==QObject::tr("Zrušit"),"Condition Cancel lost its catalog translation");
                window.grab().save(QString::fromStdString((directory/(std::string("fem-condition-")+language+"-"+theme+".png")).string()));
                load->reject();flush();
            }
        }
        apply_application_translations(application,ApplicationSettings{});
        std::cout<<"FEM GUI: shared Body/Boolean source, entry, inspection, middle confirmation, Cancel, unchanged OK, activation, readonly CAD, Undo/Redo, save/reopen, five languages and two themes passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
} // namespace zima::app
