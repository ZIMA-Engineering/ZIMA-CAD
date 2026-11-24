#include "assembly_workspace_window.hpp"
#include "cam_body_dialog.hpp"
#include "cam_machine_dialog.hpp"
#include "global_settings_dialog.hpp"
#include "../tests/profile_solid_fixture.hpp"
#include <zima/workspace/cam_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/document/placement_orientation.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <zima/command_host/host.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QScopedValueRollback>
#include <QMouseEvent>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <iostream>

namespace zima::app {
int verify_cam_body(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    unsigned checks{};
    try {
        const auto check=[&](bool ok,const char* message){++checks;if(qEnvironmentVariableIsSet("ZIMA_VERIFY_CAM_TRACE"))std::cerr<<"CAM check "<<checks<<": "<<message<<" = "<<ok<<std::endl;if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto click=[&](QWidget* widget,QPoint point){for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
            QMouseEvent event(type,QPointF(point),QPointF(widget->mapToGlobal(point)),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(widget,&event);flush();}};
        auto doc=document::PartDocument::create_default();auto stock_feature=test::rectangular_feature(doc,{20,10,6});
        auto target_feature=test::rectangular_feature(doc,{20,10,4});auto fixture_feature=test::rectangular_feature(doc,{30,14,2});doc.history={stock_feature,target_feature,fixture_feature};
        document::BodyHistoryGraph graph;const auto stock=graph.create_body("Authored stock");graph.insert({document::PartHistoryKind::Feature,stock_feature.id});
        const auto target=graph.create_body("Authored target");graph.insert({document::PartHistoryKind::Feature,target_feature.id});
        const auto fixture=graph.create_body("Authored fixture");graph.insert({document::PartHistoryKind::Feature,fixture_feature.id});
        auto fixture_body=*graph.find(fixture);fixture_body.scope.placement.z=-2;fixture_body.visible=false;graph.update_body(fixture_body);graph.activate({});doc.set_body_history(graph);
        doc.appearance.bodies[stock]={"#B9C2CC",.55,0};doc.appearance.bodies[target]={"#D8483E",.55,0};doc.appearance.bodies[fixture]={"#667685",.55,0};
        kernel::OcctKernel kernel;const auto path=directory/"cam-setup-ui.prtz";doc.save(path,workspace::calculate_part_with_resolved_references(kernel,doc));
        window.resize(1600,1100);window.show();check(window.open_document_path(QString::fromStdString(document::path_to_utf8(path))),"CAM fixture did not open");flush();window.activate_body({});flush();
        auto& session=window.workspace_.open_part(doc.document_id)->session;auto* tree=window.tree_;auto* view=window.viewer_;
        const auto row=[&](const std::string& id,const QString& kind="part-body")->QTreeWidgetItem*{for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&(kind.isEmpty()||(*it)->data(0,Qt::UserRole+3).toString()==kind))return *it;return nullptr;};
        const auto current=[&]{return dynamic_cast<CamBodyDialog*>(window.properties_dialog_);};
        const auto choose=[&](int index,const std::string& id){auto* d=current();auto* refs=d->findChild<QTableWidget*>("camBodySources");click(refs->viewport(),refs->visualItemRect(refs->item(index,3)).center());
            check(d->active_input(),"Source field did not arm");auto* item=row(id);check(item!=nullptr,"Source Tree row missing");tree->scrollToItem(item);flush();click(tree->viewport(),tree->visualItemRect(item).center());
            check(!d->active_input(),"Tree reference did not finish input");};
        auto* create=window.findChild<QAction*>("createCamAction");check(create&&create->isEnabled(),"Create CAM is unavailable at Body level");
        const auto count=kernel::body_calculation_count.load(),revision=session.revision();
        const auto material=document::serialize_body_result(session.calculated_boundaries().back().body_outputs.at(stock));
        create->trigger();flush();auto* dialog=current();check(dialog&&dialog->active_input(),"CAM creation did not arm stock selection");
        check(dialog->parentWidget()==&window&&(dialog->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"CAM properties are not internal");
        check(!dialog->buttons()->button(QDialogButtonBox::Apply),"CAM exposes Apply");
        choose(0,stock);choose(1,target);dialog->arm(2);view->fit_all();flush();bool hidden_offered=false;
        for(int y=5;y<view->height();y+=32)for(int x=5;x<view->width();x+=32)
            for(const auto& candidate:view->selection_candidates_at(QPointF(x,y)))hidden_offered|=candidate.owner_id==fixture;
        check(!hidden_offered,"Hidden unselected fixture was offered by View picking");choose(2,fixture);
        dialog->arm(3);check(!dialog->set_source(fixture)&&dialog->pending_value().cam->fixture_ids==std::vector{fixture},"Duplicate fixture accepted");dialog->end_input();
        check(view->mesh().triangles.size()==session.calculated_boundaries().back().body_outputs.at(stock)->mesh.triangles.size()+session.calculated_boundaries().back().body_outputs.at(target)->mesh.triangles.size()+session.calculated_boundaries().back().body_outputs.at(fixture)->mesh.triangles.size(),"Selected hidden fixture was not displayed");
        auto* refs=dialog->findChild<QTableWidget*>("camBodySources");check(!refs->cellWidget(0,1)&&!refs->cellWidget(1,1),"Required sources expose removal");
        choose(0,target);refs->cellWidget(0,4)->findChild<QToolButton*>()->click();flush();
        check(refs->cellWidget(0,4)->findChild<QToolButton*>()->isChecked()&&!refs->cellWidget(1,4)->findChild<QToolButton*>()->isChecked(),"Inspection leaked into another role using the same Body");
        choose(0,stock);check(dialog->inspected_sources().empty()&&view->transient_edges().empty(),"Source replacement retained old inspection");
        click(refs->viewport(),refs->visualItemRect(refs->item(2,3)).center());
        const auto scene_revision=view->base_mesh_revision();
        auto* eye=refs->cellWidget(2,4)->findChild<QToolButton*>();check(eye,"Fixture inspection eye missing");eye->click();flush();
        check(view->base_mesh_revision()==scene_revision,"Inspection republished the base scene");
        check(dialog->active_input()&&!view->transient_edges().empty(),"Inspection changed entry mode or omitted exact wire");
        for(const auto& edge:view->transient_edges())check(edge.overlay&&edge.color=="#00D1FF","Inspection is not azure through-occlusion wire");
        for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent e(type,QPointF(20,20),QPointF(view->mapToGlobal(QPoint{20,20})),Qt::MiddleButton,type==QEvent::MouseButtonPress?Qt::MiddleButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();}
        check(!dialog->active_input()&&dialog->inspected_sources().empty()&&view->transient_edges().empty()&&dialog->pending_value().cam->fixture_ids==std::vector{fixture},"Short middle click failed to end entry/inspection while retaining references");
        refs->cellWidget(2,4)->findChild<QToolButton*>()->click();flush();
        dialog->end_input();const auto top=refs->mapTo(dialog,QPoint{}).y();dialog->resize(dialog->width()+70,dialog->height()+80);flush();
        check(refs->mapTo(dialog,QPoint{}).y()==top&&refs->rowHeight(0)==32,"Resize stretched or moved reference rows");
        auto* remove=refs->cellWidget(2,1)->findChild<QPushButton*>();check(remove,"Fixture remove button missing");remove->click();flush();
        check(dialog->pending_value().cam->fixture_ids.empty()&&dialog->inspected_sources().empty()&&dialog->active_input(),"Fixture removal did not delete its row/inspection and rearm");choose(2,fixture);
        // Actual View picking, including coincident stock/design faces.
        dialog->arm(0);view->fit_all();flush();std::optional<QPointF> hit;
        for(int y=5;y<view->height()&&!hit;y+=16)for(int x=5;x<view->width()&&!hit;x+=16)
            for(const auto& c:view->selection_candidates_at(QPointF(x,y))){check(c.kind==viewer::CandidateKind::Container,"CAM offered a leaf source");if(c.owner_id==stock){hit=QPointF(x,y);break;}}
        check(hit.has_value(),"CAM stock absent from View picking");
        QMouseEvent move(QEvent::MouseMove,*hit,QPointF(view->mapToGlobal(hit->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&move);flush();
        for(std::size_t i=0;i<view->selection_candidates_at(*hit).size();++i){if(view->offered_candidate()&&view->offered_candidate()->owner_id==stock)break;
            for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent e(type,*hit,QPointF(view->mapToGlobal(hit->toPoint())),Qt::RightButton,type==QEvent::MouseButtonPress?Qt::RightButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();}}
        click(view,hit->toPoint());check(dialog->pending_value().cam->stock_id==stock&&!dialog->active_input(),"View source selection failed");
        auto* origin=dialog->findChild<QPushButton*>("containerDefaultOriginButton");check(origin&&origin->isEnabled(),"CAM has no default Origin entry");origin->click();flush();
        check(document::placement_references_use_whole_origin(dialog->pending_value().cam->work_frame.references),"CAM whole-Origin placement failed");
        dialog->findChild<QPushButton*>("camEditMachine")->click();flush();auto* machine=dynamic_cast<CamMachineDialog*>(window.properties_dialog_);check(machine&&!dialog->isVisible(),"Machine did not open within CAM draft");
        machine->findChild<QDoubleSpinBox*>("camMaximumFeed")->setValue(2345.125);machine->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(current()==dialog&&dialog->pending_value().cam->machine.maximum_feed_mm_min==2345.125&&session.revision()==revision,"Machine edit committed the parent or lost changes");
        dialog->findChild<QPushButton*>("camEditMachine")->click();flush();machine=dynamic_cast<CamMachineDialog*>(window.properties_dialog_);
        machine->findChild<QDoubleSpinBox*>("camMaximumFeed")->setValue(999);machine->reject();flush();check(current()->pending_value().cam->machine.maximum_feed_mm_min==2345.125,"Machine Cancel leaked into setup");
        QMouseEvent short_click(QEvent::MouseButtonPress,QPointF(20,20),QPointF(view->mapToGlobal(QPoint{20,20})),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);QApplication::sendEvent(view,&short_click);flush();check(current()==dialog&&session.revision()==revision,"Short middle click committed CAM");
        const auto id=dialog->pending_value().scope.id;dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!current()&&session.document().body_history.find(id),"CAM creation failed");
        const auto saved=*session.document().body_history.find(id);check(saved.cam->stock_id==stock&&saved.cam->target_id==target&&saved.cam->fixture_ids==std::vector{fixture},"CAM references not persisted");
        check(saved.cam->machine.maximum_feed_mm_min==2345.125&&document::placement_references_use_whole_origin(saved.cam->work_frame.references),"CAM machine/frame not persisted");
        auto precise=saved;precise.cam->work_frame.references.clear();precise.cam->work_frame.x=.123456789123;
        precise.cam->work_frame.rotation_x=precise.cam->work_frame.absolute_rotation_x=17.123456789;
        precise.cam->machine_work_origin.x=42.123456789;precise.cam->machine.maximum_feed_mm_min=2345.123456789;
        auto* precision_dialog=new CamBodyDialog(precise,[](auto){throw std::runtime_error("Precision probe committed");},&window);
        check(document::cam_intent_key(*precision_dialog->pending_value().cam)==document::cam_intent_key(*precise.cam),"CAM opening rounded authored frame/machine precision");delete precision_dialog;
        window.activate_body(id);flush();check(window.active_cam_body()&&!window.sketch_action_->isEnabled()&&!window.fillet_action_->isEnabled(),"CAM exposed material editing");
        std::map<std::string,kernel::SurfaceStyle> styles;window.apply_cam_surface_styles({}, {}, {}, styles);
        const auto alpha=[&](const std::string& body,int value){const auto& faces=session.calculated_boundaries().back().body_outputs.at(body)->mesh.triangle_references;
            check(!faces.empty(),"No CAM display faces");for(const auto& face:faces){const auto key=std::string("\x1f")+face.owner_id+"\x1f"+face.semantic_key;
                check(styles.contains(key)&&QColor(QString::fromStdString(styles.at(key).color)).alpha()==value,"Incorrect CAM role transparency");}};
        alpha(stock,64);alpha(target,255);alpha(fixture,255);
        check(session.document().appearance.bodies.at(stock).color=="#B9C2CC","CAM transparency changed authored appearance");
        window.activate_body(stock);flush();styles.clear();window.apply_cam_surface_styles({}, {}, {}, styles);check(styles.empty(),"CAM transparency escaped into Modeling");window.activate_body(id);flush();
        const auto baseline=session.revision();window.show_cam_body_properties(id);flush();dialog=current();check(dialog,"Existing CAM did not open");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(session.revision()==baseline,"Unchanged CAM OK created Undo");
        window.show_cam_body_properties(id);flush();dialog=current();
        auto* placement=dialog->findChild<QTableWidget*>("camPlacementReferences");placement->cellWidget(0,4)->findChild<QToolButton*>()->click();flush();check(!dialog->highlighted_reference_entries().empty(),"CAM frame inspection failed");
        click(placement->viewport(),placement->visualItemRect(placement->item(0,1)).center());
        auto* source_origin=row(stock+":origin",{});check(source_origin,"Source Origin missing during CAM edit");QTreeWidgetItem* plane=nullptr;
        for(int n=0;n<source_origin->childCount();++n)if(source_origin->child(n)->data(0,Qt::UserRole+5)=="origin:plane:xz")plane=source_origin->child(n);
        check(plane,"Source plane missing");source_origin->setExpanded(true);tree->scrollToItem(plane);flush();click(tree->viewport(),tree->visualItemRect(plane).center());
        check(dialog->pending_value().cam->work_frame.references.front().owner_id==stock+":origin","CAM individual frame replacement failed");
        choose(0,target);dialog->findChild<QPushButton*>("camEditMachine")->click();flush();machine=dynamic_cast<CamMachineDialog*>(window.properties_dialog_);
        machine->findChild<QLineEdit*>("camMachineName")->setText("Discarded machine");machine->buttons()->button(QDialogButtonBox::Ok)->click();flush();current()->reject();flush();
        check(session.revision()==baseline&&document::cam_intent_key(*session.document().body_history.find(id)->cam)==document::cam_intent_key(*saved.cam),"Parent Cancel leaked source/frame/machine changes");
        window.show_cam_body_properties(id);flush();dialog=current();dialog->findChild<QDoubleSpinBox*>("camMachineOriginX")->setValue(42.125);
        QMouseEvent confirm(QEvent::MouseButtonDblClick,QPointF(20,20),QPointF(view->mapToGlobal(QPoint{20,20})),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);QApplication::sendEvent(view,&confirm);flush();
        check(!current()&&session.document().body_history.find(id)->cam->machine_work_origin.x==42.125,"CAM middle double-click did not confirm");
        window.undo_action_->trigger();flush();check(session.document().body_history.find(id)->cam->machine_work_origin.x==0,"CAM Undo failed");
        window.redo_action_->trigger();flush();check(session.document().body_history.find(id)->cam->machine_work_origin.x==42.125,"CAM Redo failed");
        session.document().save(path,session.calculated_boundaries());std::vector<kernel::BodyResult> cached;const auto reopened=document::PartDocument::load(path,&cached);
        check(document::cam_intent_key(*reopened.body_history.find(id)->cam)==document::cam_intent_key(*session.document().body_history.find(id)->cam),"Native reopen changed CAM definition");
        check(kernel::body_calculation_count.load()==count&&document::serialize_body_result(session.calculated_boundaries().back().body_outputs.at(stock))==material,"CAM setup interaction recalculated or changed CAD geometry");
        window.show_cam_body_properties(id);flush();dialog=current();
        QTemporaryDir temporary;check(temporary.isValid(),"Missing settings directory");const auto localization=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/localization";
        for(const auto* language:{"cs","en","de","fr","ru"}){
            QSettings config(temporary.filePath("config.ini"),QSettings::IniFormat);config.setValue("Application/Language",language);config.setValue("Paths/Localization",QString::fromStdString(localization.generic_string()));config.sync();
            auto settings=ApplicationSettings::load(temporary.path());apply_application_translations(application,settings);flush();
            for(const auto* theme:{"light","dark"}){settings.theme=theme;apply_application_appearance(application,settings);flush();
                check(dialog->windowTitle()==settings.qt_translations.value("CAM setup properties"),"CAM title did not follow language");
                check(dialog->findChild<QTableWidget*>("camBodySources")->item(0,2)->text()==settings.qt_translations.value("Stock"),"CAM roles did not follow language");
                check(dialog->pending_value().name==saved.name,"Translation changed authored setup name");
            }
        }
        apply_application_translations(application,window.application_settings_);apply_application_appearance(application,window.application_settings_);flush();
        check(window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"cam-setup.png"))),"CAM capture failed");dialog->reject();flush();view->fit_all();flush();
        check(view->grabFramebuffer().save(QString::fromStdString(document::path_to_utf8(directory/"cam-stock-transparent.png"))),"CAM stock capture failed");
        {
            const auto locale_directory=QString::fromStdString(document::path_to_utf8(
                std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/localization"));
            QTemporaryDir config;
            for(const auto* language:{"cs","en","de","fr","ru"}) {
                QSettings ini(config.filePath("config.ini"),QSettings::IniFormat);
                ini.setValue("Application/Language",language);ini.setValue("Paths/Localization",locale_directory);ini.sync();
                auto settings=ApplicationSettings::load(config.path());apply_application_translations(application,settings);
                GlobalSettingsDialog global(settings,&window);global.show();flush();
                for(const auto* key:{"CamTools","CamMachines","CamFixtures"}) {
                    auto* field=global.findChild<QLineEdit*>(QString("globalPath")+key);
                    check(field&&field->text()==settings.configured_paths.value(key),"CAM library path field missing or changed");
                }
                bool translated=false;
                for(auto* label:global.findChildren<QLabel*>())if(label->text()==settings.qt_translations.value("CAM tool library"))translated=true;
                check(translated,"CAM library field was not translated");
                global.findChild<QLineEdit*>("globalPathCamTools")->setText("custom-tools");
                global.buttons()->button(QDialogButtonBox::Ok)->click();flush();
                check(global.settings().configured_paths.value("CamTools")=="custom-tools","CAM library path edit was lost");
            }
            apply_application_translations(application,window.application_settings_);
        }
        // Use the actual GUI document factory and normal ownership/commands.
        // A .toolz file changes library selection, not the Part editing model.
        for(const auto* unit:{"mm","in"}) {
            window.working_directory_=directory;window.application_settings_.units["Length"]=unit;
            const auto error=window.create_document("tool",QString("tool-template-")+unit);check(error.isEmpty(),"GUI tool template creation failed");flush();
            const auto tool_id=window.workspace_.active_document_id();
            auto& tool_session=window.workspace_.open_part(tool_id)->session;
            const auto& tool=tool_session.document();
            const auto cutting=tool.user_parameters.at("cam_cutting_body"),shank=tool.user_parameters.at("cam_shank_body");
            check(tool.document_units.at("Length")==unit&&tool.body_history.bodies().size()==2,"GUI tool template lost units/Bodies");
            check(tool.body_history.active_body_id()==cutting&&window.sketch_action_->isEnabled(),"GUI tool did not activate an editable cutting Body");
            window.activate_body({});flush();
            const auto root_revision=tool_session.revision();
            const auto root_create=window.command_host_->execute({{"command","sketch.create"},{"arguments",{{"name","Rejected root Sketch"},{"plane","XY"}}}});
            check(!root_create.ok&&tool_session.revision()==root_revision,"Tool allowed root-level Sketch creation");
            check(std::ranges::none_of(window.findChildren<QToolButton*>(),[&](auto* button){return button->defaultAction()==window.sketch_action_;}),"Tool root panel exposes the Sketch command");
            window.activate_body(shank);flush();check(window.sketch_action_->isEnabled(),"Tool shank did not expose ordinary Modeling commands");
            const auto result=window.command_host_->execute({{"command","sketch.create"},{"arguments",{{"name","Tool test profile"},{"plane","XY"}}}});
            check(result.ok,"Ordinary Sketch creation failed in tool shank");
            if(window.command_host_->change())window.apply_console_change(*window.command_host_->change());flush();
            check(tool_session.document().sketches.size()==1,"Tool Sketch was not retained");
            check(tool_session.document().body_owner_for_object(tool_session.document().sketches.front().owner_container_id)->scope.id==shank,"Tool Sketch escaped its owning Body");
            {
                const QScopedValueRollback capture_error(window.console_executing_,true);
                window.console_operation_error_.clear();window.save_active_document();
                if(!window.console_operation_error_.isEmpty())throw std::runtime_error(window.console_operation_error_.toStdString());
            }
            flush();
            check(std::filesystem::exists(directory/(std::string("tool-template-")+unit+".toolz")),"GUI tool save failed");
            const auto reopened=document::PartDocument::load(directory/(std::string("tool-template-")+unit+".toolz"));
            check(reopened.sketches.size()==1&&reopened.body_history.active_body_id()==shank,"Tool save/reopen lost Sketch or active Body");
            check(!std::filesystem::exists(directory/(std::string("tool-template-")+unit+".prtz")),"Tool save silently changed the extension");
        }
        {
            const auto tool_path=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/lib/CAM/Tools/Milling/End_mill_10.toolz";
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(tool_path))),"Factory tool did not open in the GUI");flush();
            window.viewer_->fit_all();flush();
            check(window.viewer_->grabFramebuffer().save(QString::fromStdString(document::path_to_utf8(directory/"cam-library-tool.png"))),"Tool framebuffer capture failed");
        }
        std::cout<<"CAM setup and tool template UI: "<<checks<<" checks passed; zero CAD calculations during setup editing\n";return 0;
    }catch(const std::exception& e){std::cerr<<"CAM setup UI after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
} // namespace zima::app
