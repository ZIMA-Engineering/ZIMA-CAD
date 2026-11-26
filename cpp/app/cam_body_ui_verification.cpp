#include "assembly_workspace_window.hpp"
#include "cam_body_dialog.hpp"
#include "cam_machine_dialog.hpp"
#include "cam_tools_dialog.hpp"
#include "cam_operations_dialog.hpp"
#include "cam_nc_dialog.hpp"
#include "cam_calculation_dialog.hpp"
#include "cam_clamping_dialog.hpp"
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
#include <QElapsedTimer>
#include <QThread>
#include <QFileDialog>
#include <QFile>
#include <QTimer>
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
        const auto choose_file=[&](QPushButton* trigger,const std::filesystem::path& file){
            bool chosen=false,timed_out=false;QTimer choose;choose.setInterval(80);
            QObject::connect(&choose,&QTimer::timeout,[&]{if(auto* d=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){
                if(!chosen){d->setDirectory(QString::fromStdString(document::path_to_utf8(file.parent_path())));d->selectFile(QString::fromStdString(document::path_to_utf8(file.filename())));chosen=true;return;}
                if(auto* name=d->findChild<QLineEdit*>("fileNameEdit"))name->setText(QString::fromStdString(document::path_to_utf8(file)));
                QMetaObject::invokeMethod(d,"accept",Qt::DirectConnection);
            }});
            QTimer timeout;timeout.setSingleShot(true);QObject::connect(&timeout,&QTimer::timeout,[&]{timed_out=true;if(auto* d=qobject_cast<QDialog*>(QApplication::activeModalWidget()))d->reject();});
            choose.start();timeout.start(10000);trigger->click();choose.stop();timeout.stop();flush();check(chosen&&!timed_out,"CAM library/export file chooser failed");
        };
        const auto library=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/lib/CAM";
        auto doc=document::PartDocument::create_default();auto stock_feature=test::rectangular_feature(doc,{20,10,6});
        auto target_feature=test::rectangular_feature(doc,{20,10,4});auto fixture_feature=test::rectangular_feature(doc,{30,14,2});doc.history={stock_feature,target_feature,fixture_feature};
        document::BodyHistoryGraph graph;const auto stock=graph.create_body("Authored stock");graph.insert({document::PartHistoryKind::Feature,stock_feature.id});
        const auto target=graph.create_body("Authored target");graph.insert({document::PartHistoryKind::Feature,target_feature.id});
        const auto fixture=graph.create_body("Authored fixture");graph.insert({document::PartHistoryKind::Feature,fixture_feature.id});
        auto fixture_body=*graph.find(fixture);fixture_body.scope.placement.z=-2;fixture_body.visible=false;graph.update_body(fixture_body);
        auto machining_sketch=sketcher::Sketch::create_default();static_cast<void>(machining_sketch.add_circle(0,0,3));
        auto machining_owner=document::PartDocument::create_sketch_container();machining_sketch.owner_container_id=machining_owner.id;
        doc.sketches.push_back(machining_sketch);doc.history.push_back(machining_owner);static_cast<void>(graph.create_body("Machining profiles"));graph.insert({document::PartHistoryKind::Feature,machining_owner.id});
        graph.activate({});doc.set_body_history(graph);
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
        choose_file(machine->findChild<QPushButton*>("camImportMachine"),library/"Machines/Milling/Three_axis_mill.prtz");
        check(machine->pending_value().maximum_spindle_rpm==12000&&machine->pending_value().postprocessor_id=="generic"&&session.revision()==revision,"Machine library import lost capabilities or committed the setup");
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
        // Child editors belong to the parent draft. Cancelling the setup after
        // child OK must discard those edits without a document transaction.
        const auto editor_revision=session.revision();window.show_cam_body_properties(id);flush();dialog=current();
        check(!window.tabs_->isEnabled(),"CAM draft allows its scene to move to another document");
        dialog->findChild<QPushButton*>("camEditTools")->click();flush();
        auto* tools=dynamic_cast<CamToolsDialog*>(window.properties_dialog_);check(tools,"CAM tool editor is not connected");
        tools->findChild<QPushButton*>("camAddTool")->click();flush();
        choose_file(tools->findChild<QPushButton*>("camImportTool"),library/"Tools/Milling/End_mill_10.toolz");
        check(tools->pending_value().size()==2&&tools->pending_value()[1].feed_mm_min==2663&&tools->pending_value()[1].cutting.front().z_mm==-22,"Tool library import lost explicit dimensions or cutting conditions");
        tools->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(session.revision()==editor_revision&&dialog->pending_value().cam->tools.size()==2,"Child tool OK committed the document");
        dialog->reject();flush();check(session.revision()==editor_revision&&session.document().body_history.find(id)->cam->tools.empty(),"Parent Cancel published child tools");
        check(window.tabs_->isEnabled(),"CAM Cancel did not release document tabs");
        window.show_cam_body_properties(id);flush();dialog=current();
        dialog->findChild<QPushButton*>("camEditClampings")->click();flush();auto* clamping=dynamic_cast<CamClampingDialog*>(window.properties_dialog_);
        check(clamping,"Clamping editor is not connected");clamping->findChild<QPushButton*>("camAddClamping")->click();flush();
        const auto sketch_id=test::rectangular_sketch_id(stock_feature);auto* sketch_row=row(sketch_id,"part-sketch");check(sketch_row,"Clamping Sketch is missing in Tree");
        tree->expandAll();tree->scrollToItem(sketch_row);flush();click(tree->viewport(),tree->visualItemRect(sketch_row).center());
        check(!clamping->active_input()&&clamping->pending_value()[0].contact_sketch_id==sketch_id,"Clamping Tree reference did not select contact Sketch");
        clamping->arm(1);flush();click(tree->viewport(),tree->visualItemRect(sketch_row).center());
        check(clamping->pending_value()[0].upper_sketch_id==sketch_id,"Clamping upper reference was not independent");
        auto* clamp_table=clamping->findChild<QTableWidget*>("camClampingReferences");
        check(!clamp_table->cellWidget(0,1)&&!clamp_table->cellWidget(1,1),"Required clamping references expose removal");
        const auto clamp_scene=view->base_mesh_revision();clamp_table->cellWidget(0,5)->findChild<QToolButton*>()->click();flush();
        check(!clamping->active_input()&&!view->transient_edges().empty()&&view->base_mesh_revision()==clamp_scene,"Clamping inspection armed entry or rebuilt base scene");
        const auto before_arrow=view->transient_edges()[view->transient_edges().size()-3].points;
        clamping->findChild<QComboBox*>("camClampingSide0")->setCurrentIndex(1);flush();
        const auto after_arrow=view->transient_edges()[view->transient_edges().size()-3].points;
        check(before_arrow[0]==after_arrow[0]&&before_arrow[1]!=after_arrow[1],"Clamping side did not reverse inspected normal");
        for(const auto& edge:view->transient_edges())check(edge.color=="#00D1FF"&&edge.overlay,"Clamping inspection violates azure wire contract");
        clamping->arm(0);view->fit_all();flush();std::optional<QPointF> clamp_hit;
        for(int y=5;y<view->height()&&!clamp_hit;y+=6)for(int x=5;x<view->width()&&!clamp_hit;x+=6)
            for(const auto& c:view->selection_candidates_at(QPointF(x,y)))if(c.owner_id==sketch_id){clamp_hit=QPointF(x,y);break;}
        check(clamp_hit.has_value(),"Clamping Sketch is not available for View picking");
        QMouseEvent clamp_move(QEvent::MouseMove,*clamp_hit,QPointF(view->mapToGlobal(clamp_hit->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&clamp_move);flush();
        for(std::size_t i=0;i<view->selection_candidates_at(*clamp_hit).size();++i){if(view->offered_candidate()&&view->offered_candidate()->owner_id==sketch_id)break;
            for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent e(type,*clamp_hit,QPointF(view->mapToGlobal(clamp_hit->toPoint())),Qt::RightButton,type==QEvent::MouseButtonPress?Qt::RightButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();}}
        click(view,clamp_hit->toPoint());flush();
        check(!clamping->active_input()&&clamping->pending_value()[0].contact_sketch_id==sketch_id,"Clamping View click did not confirm the Sketch");
        clamping->end_input();clamping->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(session.revision()==editor_revision&&dialog->pending_value().cam->clampings.size()==1,"Clamping child OK committed the setup");
        dialog->findChild<QPushButton*>("camEditTools")->click();flush();tools=dynamic_cast<CamToolsDialog*>(window.properties_dialog_);
        tools->findChild<QPushButton*>("camAddTool")->click();flush();tools->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        dialog->findChild<QPushButton*>("camEditOperations")->click();flush();auto* operations=dynamic_cast<CamOperationsDialog*>(window.properties_dialog_);check(operations,"CAM operation editor is not connected");
        operations->findChild<QPushButton*>("camAddFacing")->click();flush();operations->findChild<QDoubleSpinBox*>("camFacingStepOver")->setValue(4);
        operations->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(session.revision()==editor_revision,"Child operation OK committed the document");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(session.revision()==editor_revision+1,"CAM draft did not commit exactly once");
        const auto calculation_revision=session.revision();window.calculate_cam_body();flush();QElapsedTimer wait;wait.start();
        while(dynamic_cast<CamCalculationDialog*>(window.properties_dialog_)&&wait.elapsed()<60000){flush();QThread::msleep(5);}
        auto* nc=dynamic_cast<CamNcDialog*>(window.properties_dialog_);check(nc,"CAM calculation did not open its generic NC inspector");
        check(!window.tabs_->isEnabled(),"NC inspector allows an overlay in another document");
        // The rectangular fixtures are symmetric around Z=0: stock [-3,3],
        // target [-2,2]. Facing removes the upper 20*10*1 layer only; the lower
        // allowance intentionally remains for a second, turned-over setup.
        const auto calculated=session.document().body_history.find(id)->cam->calculated;
        if(calculated)std::cout<<"GUI CAM removed="<<calculated->removed_mm3<<" mm3; collisions="<<calculated->collisions.size()<<'\n';
        check(calculated&&calculated->collisions.empty()&&std::abs(calculated->removed_mm3-200)<1e-3,"CAM GUI calculated incorrect remaining stock or collisions");
        check(calculated->contacts.size()==1&&std::abs(calculated->contacts[0].remaining_mm2-200)<1e-6,"GUI clamping contact report differs from exact geometry");
        check(session.revision()==calculation_revision+1,"CAM calculation did not publish exactly one transaction");
        const auto inspection_revision=session.revision();const auto nc_count=kernel::body_calculation_count.load();
        const auto exported=std::filesystem::absolute(directory/"verified-program.nc.txt");
        choose_file(nc->findChild<QPushButton*>("camNcExport"),exported);QFile nc_file(QString::fromStdString(document::path_to_utf8(exported)));
        check(nc_file.open(QIODevice::ReadOnly)&&nc_file.readAll().toStdString()==nc->program_text(),"Generic NC export differs from the calculated program");nc_file.close();
        nc->findChild<QPushButton*>("camNcNext")->click();nc->findChild<QSlider*>("camNcFraction")->setValue(700);nc->findChild<QCheckBox*>("camNcRemaining")->setChecked(true);flush();
        check(session.revision()==inspection_revision&&kernel::body_calculation_count.load()==nc_count,"NC stepping recalculated geometry or modified the document");
        check(window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"cam-nc-program.png"))),"CAM NC capture failed");nc->reject();flush();
        window.calculate_cam_body();flush();check(dynamic_cast<CamNcDialog*>(window.properties_dialog_)&&session.revision()==inspection_revision,"Current CAM recalculated or made an Undo transaction");window.properties_dialog_->reject();flush();
        const auto cancellation_input=workspace::prepare_cam_calculation(window.workspace_,doc.document_id,id);bool cancellation_published=false;
        auto* cancelling=new CamCalculationDialog(cancellation_input,[&](auto){cancellation_published=true;},&window);
        window.properties_dialog_=cancelling;QObject::connect(cancelling,&QDialog::finished,&window,[&]{window.properties_dialog_=nullptr;});cancelling->show();cancelling->reject();wait.restart();
        while(window.properties_dialog_&&wait.elapsed()<60000){flush();QThread::msleep(5);}
        check(!window.properties_dialog_&&!cancellation_published&&session.revision()==inspection_revision,"Cancelled CAM calculation published or did not close");
        check(kernel::body_calculation_count.load()==count,"CAM calculation regenerated source CAD geometry");
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
                auto definition=*session.document().body_history.find(id)->cam;
                auto* translated_tools=new CamToolsDialog(definition,[](auto){},&window);translated_tools->show();flush();
                check(translated_tools->findChild<QPushButton*>("camAddTool")->text()==settings.qt_translations.value("Add tool"),"Tool editor text did not follow language");translated_tools->reject();flush();
                auto* translated_operations=new CamOperationsDialog(definition,[](auto){},&window);translated_operations->show();flush();
                check(translated_operations->findChild<QPushButton*>("camAddFacing")->text()==settings.qt_translations.value("Add facing"),"Operation editor text did not follow language");
                check(translated_operations->findChild<QPushButton*>("camAddPocket")->text()==settings.qt_translations.value("Add pocket"),"Pocket action did not follow language");
                translated_operations->findChild<QPushButton*>("camAddContour")->click();flush();
                check(translated_operations->findChild<QComboBox*>("camContourSide")->itemText(1)==settings.qt_translations.value("Outside"),"Contour side did not follow language");
                check(translated_operations->findChild<QCheckBox*>("camProfileClockwise")->text()==settings.qt_translations.value("Clockwise tool path"),"Profile direction did not follow language");translated_operations->reject();flush();
                auto* translated_clamping=new CamClampingDialog(definition,{{sketch_id,"Authored contact"}},[](auto){},&window);translated_clamping->show();flush();
                check(translated_clamping->findChild<QComboBox*>("camClampingSide0")->itemText(0)==settings.qt_translations.value("Positive normal"),"Clamping side text did not follow language");
                check(translated_clamping->pending_value()==definition.clampings,"Language changed authored clamping data");translated_clamping->reject();flush();
                auto reported=std::make_shared<document::CamCalculated>(*calculated);
                const auto compiled=cam::compile(reported->program,reported->machine,reported->tools,reported->machine_work_origin);const auto& motion=compiled.motions.front();
                reported->warnings.push_back({cam::ClampingWarningKind::ContactLoss,motion.operation_id,motion.block_id,motion.tool_id,definition.clampings.front().id,0,10,190});
                auto* translated_nc=new CamNcDialog(reported,[](auto,bool){},&window);translated_nc->show();flush();
                check(translated_nc->findChild<QPushButton*>("camNcNext")->text()==settings.qt_translations.value("Next motion"),"NC inspector text did not follow language");
                auto* reports=translated_nc->findChild<QListWidget*>("camNcCollisions");reports->setCurrentRow(0);flush();
                check(translated_nc->findChild<QListWidget*>("camNcLines")->currentItem()->data(Qt::UserRole).toString().toStdString()==motion.block_id,"Clamping warning selected the wrong NC block");
                translated_nc->reject();flush();
            }
            apply_application_translations(application,window.application_settings_);
        }
        // Exercise native profile selection through actual Tree and View mouse
        // events. Child edits remain private until the setup's own OK.
        const auto profile_revision=session.revision();window.show_cam_body_properties(id);flush();dialog=current();
        dialog->findChild<QPushButton*>("camEditTools")->click();flush();tools=dynamic_cast<CamToolsDialog*>(window.properties_dialog_);
        for(const auto* field:{"camCuttingRadius0","camCuttingRadius1","camShankRadius0","camShankRadius1"})tools->findChild<QDoubleSpinBox*>(field)->setValue(1);
        tools->buttons()->button(QDialogButtonBox::Ok)->click();flush();dialog->findChild<QPushButton*>("camEditOperations")->click();flush();
        operations=dynamic_cast<CamOperationsDialog*>(window.properties_dialog_);check(operations,"Profile operation editor did not open");
        click(operations->findChild<QPushButton*>("camAddPocket"),operations->findChild<QPushButton*>("camAddPocket")->rect().center());
        auto* profile_row=row(machining_sketch.id,"part-sketch");check(profile_row,"Machining profile missing from Tree");tree->expandAll();tree->scrollToItem(profile_row);flush();
        click(tree->viewport(),tree->visualItemRect(profile_row).center());
        check(!operations->active_input()&&std::get<cam::ProfileMilling>(operations->pending_value().back()).sketch_id==machining_sketch.id,"Profile Tree click failed");
        auto* profile_refs=operations->findChild<QTableWidget*>("camProfileReference");const auto profile_scene=view->base_mesh_revision();
        profile_refs->cellWidget(0,3)->findChild<QToolButton*>()->click();flush();
        check(!operations->active_input()&&!view->transient_edges().empty()&&view->base_mesh_revision()==profile_scene,"Profile inspection changed the base scene or armed selection");
        for(const auto& edge:view->transient_edges())check(edge.color=="#00D1FF"&&edge.overlay,"Profile inspection did not use exact azure wire");
        operations->arm_profile();flush();view->fit_all();flush();std::optional<QPointF> profile_hit;
        for(int y=5;y<view->height()&&!profile_hit;y+=4)for(int x=5;x<view->width()&&!profile_hit;x+=4)
            for(const auto& c:view->selection_candidates_at(QPointF(x,y)))if(c.owner_id==machining_sketch.id){profile_hit=QPointF(x,y);break;}
        check(profile_hit.has_value(),"Machining Sketch unavailable in View");
        QMouseEvent profile_move(QEvent::MouseMove,*profile_hit,QPointF(view->mapToGlobal(profile_hit->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&profile_move);flush();
        for(std::size_t i=0;i<view->selection_candidates_at(*profile_hit).size();++i){if(view->offered_candidate()&&view->offered_candidate()->owner_id==machining_sketch.id)break;
            for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent e(type,*profile_hit,QPointF(view->mapToGlobal(profile_hit->toPoint())),Qt::RightButton,type==QEvent::MouseButtonPress?Qt::RightButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();}}
        click(view,profile_hit->toPoint());check(!operations->active_input(),"Profile View replacement failed");
        operations->findChild<QDoubleSpinBox*>("camProfileTop")->setValue(3);operations->findChild<QDoubleSpinBox*>("camProfileBottom")->setValue(2);
        const auto profile_draft=operations->pending_value();
        for(const auto* theme:{"light","dark","light"}){
            auto settings=window.application_settings_;settings.theme=theme;apply_application_appearance(application,settings);flush();
            check(operations->pending_value()==profile_draft&&session.revision()==profile_revision&&kernel::body_calculation_count.load()==count,"Profile theme change altered machining inputs");
            check(!operations->findChild<QCheckBox*>("camProfileClockwise")->visibleRegion().isEmpty(),"Profile editor did not grow to show its machining fields");
            check(operations->grab().save(QString::fromStdString(document::path_to_utf8(directory/(std::string("cam-profile-editor-")+theme+".png")))),"Profile editor capture failed");
        }
        apply_application_appearance(application,window.application_settings_);flush();
        operations->findChild<QTableWidget*>("camOperations")->cellWidget(0,2)->findChild<QAbstractButton*>()->click();flush();
        check(operations->pending_value().size()==1&&std::holds_alternative<cam::ProfileMilling>(operations->pending_value().front()),"Profile draft did not retain its operation");
        operations->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(session.revision()==profile_revision,"Profile child OK committed setup");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(session.revision()==profile_revision+1,"Profile setup did not commit once");
        window.calculate_cam_body();flush();wait.restart();while(dynamic_cast<CamCalculationDialog*>(window.properties_dialog_)&&wait.elapsed()<60000){flush();QThread::msleep(5);}
        nc=dynamic_cast<CamNcDialog*>(window.properties_dialog_);check(nc,"Profile calculation did not open NC inspector");
        const auto profile_result=session.document().body_history.find(id)->cam->calculated;
        check(profile_result&&profile_result->collisions.empty()&&std::abs(profile_result->removed_mm3-9*std::numbers::pi)<1e-6,"GUI profile pocket removed wrong volume");
        const auto profile_inspection=session.revision();nc->findChild<QPushButton*>("camNcNext")->click();nc->findChild<QSlider*>("camNcFraction")->setValue(700);flush();
        check(session.revision()==profile_inspection&&kernel::body_calculation_count.load()==count,"Profile NC inspection changed design geometry");
        check(window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"cam-profile-pocket.png"))),"Profile pocket capture failed");nc->reject();flush();
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
