#include "assembly_workspace_window.hpp"
#include "fem_body_dialog.hpp"
#include "fem_condition_dialog.hpp"
#include "fem_distance_dialog.hpp"
#include "fem_video_export.hpp"
#include <QSlider>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QFile>
#include <QFileDialog>
#include <zima/workspace/fem_condition_operations.hpp>
#include <zima/document/placement_orientation.hpp>
#include "../tests/profile_solid_fixture.hpp"
#include <zima/workspace/fem_operations.hpp>
#include <zima/workspace/body_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <zima/ui/operation_activity.hpp>
#include <QApplication>
#include <QAction>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QEventLoop>
#include <QTimer>
#include <QQuaternion>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QToolBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QVariantAnimation>
#include <QElapsedTimer>
#include <iostream>
#ifdef ZIMA_ENABLE_FEM_MESHER
#include "fem_linear_statics.hpp"
#include "fem_volume_mesh.hpp"
#include "fem_thermal.hpp"
#include "fem_probe.hpp"
#endif

namespace zima::app {
namespace {
class FemProgressPaintSpy final : public QProxyStyle {
public:
    FemProgressPaintSpy():QProxyStyle(QStyleFactory::create("Fusion")){}
    mutable QString painted_text;
    void drawControl(ControlElement element,const QStyleOption* option,QPainter* painter,
            const QWidget* widget=nullptr) const override {
        if(element==CE_ProgressBar)
            if(const auto* progress=qstyleoption_cast<const QStyleOptionProgressBar*>(option))
                painted_text=progress->text;
        QProxyStyle::drawControl(element,option,painter,widget);
    }
};
}
int verify_fem_body(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
#ifdef ZIMA_ENABLE_FEM_MESHER
        const auto reproduction=qEnvironmentVariable("ZIMA_VERIFY_FEM_DOCUMENT");
        if(!reproduction.isEmpty()) {
            window.resize(1600,1000);window.show();
            check(window.open_document_path(reproduction),"FEM reproduction document did not open");flush();
            auto& session=window.workspace_.open_part(window.workspace_.active_document_id())->session;
            std::string fem;
            for(const auto& body:session.document().body_history.bodies())if(body.fem){fem=body.scope.id;break;}
            check(!fem.empty(),"FEM reproduction contains no FEM Body");
            window.activate_body(fem);flush();
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_FEM_REGENERATE")) {
                window.regenerate_document_action_->trigger();flush();
                const auto& conditions=session.document().body_history.find(fem)->fem->conditions;
                check(std::ranges::all_of(conditions,[](const auto& c){return c.placement.reference_valid;}),
                    "FEM reproduction regeneration retained unresolved placements");
                const auto output=directory/"fem-regenerated-copy.prtz";
                session.document().save(output,session.calculated_boundaries());
                const auto reopened=document::PartDocument::load(output);
                check(reopened.body_history.find(fem)->fem->conditions==conditions,"Reopen changed regenerated FEM conditions");
                std::cout<<"FEM native document regeneration: all condition placements resolved; native reopen preserved definitions\n";
            }
            const auto revision=session.revision(),calculations=kernel::body_calculation_count.load();
            auto* progress=window.operation_progress_;
            unsigned mesh_ticks=0,solve_ticks=0;
            bool solving=false,capture_attempted=false,progress_captured=false;
            QTimer observer;
            QObject::connect(&observer,&QTimer::timeout,&observer,[&] {
                if(!window.operation_activity_->active()||!progress->isVisible()||!progress->format().startsWith("FEM"))return;
                ++(solving?solve_ticks:mesh_ticks);
                const auto prefix=QObject::tr("FEM: solving the system (iteration %1)…").section("%1",0,0);
                if(solving&&!capture_attempted&&progress->format().startsWith(prefix)) {
                    capture_attempted=true;
                    progress_captured=window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"fem-calculation-progress.png")));
                }
            });
            observer.start(20);
            window.generate_fem_mesh();flush();
            if(!window.active_generated_fem_mesh())throw std::runtime_error("FEM reproduction mesh generation failed: "+window.state_->text().toStdString());
            solving=true;
            window.solve_fem_statics();flush();
            observer.stop();
            check(window.active_fem_deformation(),"FEM reproduction solution failed");
            check(mesh_ticks&&solve_ticks&&progress_captured,"FEM did not present live meshing and solver progress");
            check(!window.operation_activity_->active()&&progress->value()==100,"FEM progress remained active after calculation");
            check(session.revision()==revision&&kernel::body_calculation_count.load()==calculations,
                "FEM reproduction changed its document or recalculated CAD");
            window.viewer_->set_standard_view(viewer::StandardView::Isometric);
            QEventLoop camera_settled;QTimer::singleShot(350,&camera_settled,&QEventLoop::quit);camera_settled.exec();
            auto camera=window.viewer_->camera_state();
            const auto rotation=QQuaternion::fromAxisAndAngle(0,0,1,55)*QQuaternion(camera[0],camera[1],camera[2],camera[3]);
            camera[0]=rotation.scalar();camera[1]=rotation.x();camera[2]=rotation.y();camera[3]=rotation.z();
            window.viewer_->set_camera_state(camera);window.viewer_->fit_all();flush();
            for(bool stress:{false,true}) {
                window.select_fem_result(stress?AssemblyWorkspaceWindow::FemResultQuantity::VonMises:AssemblyWorkspaceWindow::FemResultQuantity::Displacement);
                flush();window.viewer_->repaint();flush();
                check(!window.viewer_->triangle_scalar_field().empty(),"FEM result colors are missing");
                const auto screenshot=directory/(stress?"fem-stress-result.png":"fem-displacement-result.png");
                check(window.grab().save(QString::fromStdString(document::path_to_utf8(screenshot))),"FEM result capture failed");
                std::cout<<"FEM native document GUI solve: "<<window.state_->text().toStdString()
                         <<"; unchanged revision and CAD calculation count; captured "<<screenshot.string()<<std::endl;
            }
            window.show_fem_deformation_settings();flush();
            auto* presentation=window.findChild<QDialog*>("femDeformationSettingsDialog");
            presentation->findChild<QPushButton*>("femAutomaticMagnification")->click();
            presentation->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            window.findChild<QAction*>("femOriginalAction")->trigger();
            window.findChild<QAction*>("femExtremaAction")->trigger();flush();
            window.viewer_->grabFramebuffer();
            const auto uploads=window.viewer_->property("gpuMeshUploadCount"),vectors=window.viewer_->property("resultVectorUploadCount");
            std::vector<double> frame_ms;
            for(unsigned frame=0;frame<40;++frame) {
                window.fem_animation_fraction_=frame/39.;window.update_fem_deformation();
                QElapsedTimer elapsed;elapsed.start();window.viewer_->grabFramebuffer();frame_ms.push_back(elapsed.nsecsElapsed()/1e6);
            }
            std::ranges::sort(frame_ms);
            std::cout<<"FEM magnified result frames (including framebuffer readback): median="<<frame_ms[20]
                <<" ms; maximum="<<frame_ms.back()<<" ms; magnification="<<window.fem_magnification_<<std::endl;
            check(window.viewer_->property("gpuMeshUploadCount")==uploads&&window.viewer_->property("resultVectorUploadCount")==vectors&&
                session.revision()==revision&&kernel::body_calculation_count.load()==calculations,"Real FEM animation repeated geometry work");
            check(window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"fem-magnified-original.png"))),
                "FEM comparison screenshot failed");
            return 0;
        }
#endif
        // Independent geometry oracle for the animated picker: a triangle
        // translates along X by exactly 10 mm per magnification unit.
        {
            viewer::MeshView probe_view;probe_view.resize(600,400);probe_view.show();
            probe_view.set_standard_view(viewer::StandardView::Top);
            QEventLoop settled;QTimer::singleShot(350,&settled,&QEventLoop::quit);settled.exec();
            for(bool reversed:{false,true})for(auto projection:{viewer::ProjectionMode::Orthographic,viewer::ProjectionMode::Perspective}) {
                kernel::ViewerMesh triangle;triangle.vertices={{10,0,0},{13,0,0},{10,3,0}};
                triangle.triangles=reversed?std::vector<std::uint32_t>{0,2,1}:std::vector<std::uint32_t>{0,1,2};
                probe_view.set_mesh(triangle);probe_view.set_projection_mode(projection);
                probe_view.fit_points({{0,-3,0},{24,6,0}});
                probe_view.set_triangle_scalar_field({1},"Test",0,1);
                probe_view.set_result_displacements({{10,0,0},{10,0,0},{10,0,0}});
                std::optional<viewer::MeshView::ResultHit> clicked;
                probe_view.set_result_probe([&](const auto& hit){clicked=hit;});
                probe_view.grabFramebuffer();const auto uploads=probe_view.property("gpuMeshUploadCount");
                const auto vectors=probe_view.property("resultVectorUploadCount");
                for(double factor:{0.,1.,2.,.5}) {
                    probe_view.set_result_deformation(factor,true);flush();probe_view.grabFramebuffer();
                    QPointF pixel;std::optional<viewer::MeshView::ResultHit> hit;
                    for(int y=30;y<probe_view.height()-25&&!hit;y+=4)for(int x=20;x<probe_view.width()-20&&!hit;x+=4) {
                        pixel={double(x),double(y)};hit=probe_view.result_hit_at(pixel);
                    }
                    if(!hit) {
                        probe_view.grabFramebuffer().save(QString::fromStdString(document::path_to_utf8(directory/"probe-failure.png")));
                        const auto ray=probe_view.ray_at({300,200});
                        std::cerr<<"Probe failure: reversed="<<reversed<<" projection="<<int(projection)<<" factor="<<factor;
                        if(ray)std::cerr<<" ray="<<ray->first.x<<','<<ray->first.y<<','<<ray->first.z<<" direction="<<ray->second.x<<','<<ray->second.y<<','<<ray->second.z;
                        std::cerr<<std::endl;
                    }
                    check(hit.has_value(),"Animated triangle is not pickable");
                    const auto p=hit->position;
                    check(std::abs(p.z)<1e-5&&p.x>=10*factor-1e-5&&p.y>=-1e-5&&p.x-10*factor+p.y<=3+1e-5,
                        "Animated picker does not intersect the displayed triangle");
                    check(probe_view.selection_candidates_at(pixel).empty(),"Animation offered stale CAD candidates");
                    QMouseEvent click(QEvent::MouseButtonPress,pixel,probe_view.mapToGlobal(pixel.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(&probe_view,&click);
                    check(clicked&&clicked->node==hit->node,"Animated mouse click lost the picked node");
                    probe_view.set_result_deformation(factor<1?2:0,true);
                    check(!probe_view.result_hit_at(pixel),"Transparent original or old position is pickable");
                    check(probe_view.property("gpuMeshUploadCount")==uploads&&probe_view.property("resultVectorUploadCount")==vectors&&
                        probe_view.mesh().vertices==triangle.vertices,"Deformation rebuilt the mesh");
                }
                probe_view.set_mesh(triangle);
                check(!probe_view.result_hit_at({300,200})&&probe_view.result_markers().empty(),"Scene change retained result picking");
            }
            probe_view.hide();
        }
        auto part=document::PartDocument::create_default();
        const auto first=test::rectangular_feature(part,{20,10,5}),second=test::rectangular_feature(part,{10,6,3});part.history={first,second};
        document::BodyHistoryGraph graph;const auto a=graph.create_body("Stock A");graph.insert({document::PartHistoryKind::Feature,first.id});
        const auto b=graph.create_body("Stock B");graph.insert({document::PartHistoryKind::Feature,second.id});
        const auto joined=graph.create_boolean("Union",kernel::BodyCombination::Add,a,b);graph.activate({});part.set_body_history(graph);
        kernel::OcctKernel kernel;const auto path=directory/"fem-body-ui.prtz";
        part.save(path,workspace::calculate_part_with_resolved_references(kernel,part));
        window.resize(1400,950);window.show();check(window.open_document_path(QString::fromStdString(document::path_to_utf8(path))),"FEM fixture did not open");flush();
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
#include "fem_origin_ui_verification.inc"
        for(auto* command:{window.sketch_action_,window.extrusion_action_,window.shell_action_,window.construction_axis_action_})check(!command->isEnabled(),"FEM enables material editing");
        check(window.fem_mode_button_->isVisible()&&!window.sheet_metal_mode_button_->isVisible(),"FEM mode buttons are incorrect");
        const auto mesh_controls=[&]{return session.document().body_history.find(fem)->fem->mesh;};
        auto* mesh_menu=window.findChild<QMenu*>("femMeshMenu");
        auto* mesh_settings=window.findChild<QAction*>("femMeshSettingsAction");
        check(mesh_menu&&mesh_menu->menuAction()->isVisible()&&mesh_settings&&mesh_settings->isEnabled(),"FEM mesh settings menu is missing");
        const auto sidebar_action=[&](QAction* action) {
            window.tools_toolbar_->layout()->activate();flush();
            auto* button=qobject_cast<QToolButton*>(window.tools_toolbar_->widgetForAction(action));
            check(button&&button->isVisible()&&button->defaultAction()==action&&!action->menu(),
                "FEM direct command is missing its shared sidebar button");
            check(button->isCheckable()==action->isCheckable()&&button->isChecked()==action->isChecked(),
                "FEM sidebar toggle lost its checked state");
            return button;
        };
        const auto sidebar_actions=window.tools_toolbar_->actions();
        const auto current_sidebar_action=[&](const char* name)->QAction* {
            for(auto* action:window.tools_toolbar_->actions())if(action->objectName()==name)return action;
            return nullptr;
        };
        check(sidebar_actions.indexOf(current_sidebar_action("femBodyPropertiesAction"))==
            sidebar_actions.indexOf(window.selection_action_)+1,"FEM Body properties is not directly below Selection");
        for(auto* action:mesh_menu->actions())sidebar_action(action);
        check(!window.tools_toolbar_->widgetForAction(mesh_menu->menuAction()),"FEM still exposes the Mesh popup");
#ifdef ZIMA_ENABLE_FEM_MESHER
        for(auto* action:window.findChild<QMenu*>("femCalculationMenu")->actions())sidebar_action(action);
        auto* results_menu=window.findChild<QMenu*>("femResultsMenu");
        check(results_menu&&!window.tools_toolbar_->widgetForAction(results_menu->menuAction()),"FEM still exposes the Results popup");
        int previous_result=sidebar_actions.indexOf(current_sidebar_action("femShowDeformationAction"));
        for(auto* action:results_menu->actions())if(!action->isSeparator()&&action->isVisible()) {
            check(!sidebar_action(action)->isEnabled(),"FEM result command is available before calculation");
            const int index=sidebar_actions.indexOf(action);
            check(index>previous_result,"FEM results are not ordered after calculation");previous_result=index;
        }
#endif
        const auto open_settings=[&] {
            mesh_settings->trigger();flush();
            auto* settings=dynamic_cast<ui::PropertiesSubWindow*>(window.findChild<QDialog*>("femMeshSettingsDialog"));
            check(settings&&settings->windowType()==Qt::SubWindow,"FEM mesh settings dialog is missing or floating");
            return settings;
        };
        const auto original_controls=mesh_controls();
        auto settings_revision=session.revision();
        auto* generate=window.findChild<QAction*>("femGenerateMeshAction");
        auto* show_mesh=window.findChild<QAction*>("femShowMeshAction");
        const auto check_mesh=[&] {
            generate->trigger();flush();
            const auto* packet=window.active_generated_fem_mesh();
            check(packet&&!packet->triangles.empty()&&!packet->edges.empty(),"FEM did not generate a drawable volume-mesh boundary");
            check(show_mesh->isChecked()&&view->mesh().triangles==packet->triangles,"FEM did not display the generated mesh");
            check(sidebar_action(show_mesh)->isChecked(),"Displayed FEM mesh has no active sidebar state");
            check(!window.operation_activity_->active()&&window.operation_progress_->value()==100,
                "FEM mesh progress was not completed");
            const auto progress_generation=window.operation_progress_generation_;
            generate->trigger();flush();
            check(window.operation_progress_generation_==progress_generation,"Cached FEM mesh restarted progress");
            sidebar_action(show_mesh)->click();flush();
            check(!show_mesh->isChecked()&&view->mesh().triangles==snapshot->mesh.triangles,"FEM mesh toggle did not restore the source");
        };
        if(generate) {
            check(show_mesh&&!show_mesh->isEnabled(),"FEM offered a mesh before generation");
            check_mesh();
            check(session.revision()==settings_revision&&kernel::body_calculation_count.load()==count,"FEM meshing recalculated or changed CAD");
            sidebar_action(show_mesh)->click();flush();
            const auto* packet=window.active_generated_fem_mesh();
            const auto previous_mode=view->display_mode();const auto mesh_calculations=kernel::body_calculation_count.load();
            view->set_display_mode(viewer::DisplayMode::Shaded);flush();
            const auto with_edges=view->grabFramebuffer();
            view->set_analysis_mesh_edges(false);flush();
            check(view->grabFramebuffer()!=with_edges,"Show mesh does not draw FEM edges in Shaded mode");
            window.preserve_view_on_refresh_=true;window.refresh_scene();flush();
            for(int cycle=0;cycle<2;++cycle) {
                window.activate_body(b);flush();
                check(!window.active_fem_body()&&view->mesh().vertices!=packet->vertices,"Material Body activation retained the FEM mesh");
                const auto material_view=view->grabFramebuffer();view->set_analysis_mesh_edges(false);flush();
                check(view->grabFramebuffer()==material_view,"Material Body retained the FEM wire override");
                window.activate_body(fem);flush();
                check(show_mesh->isChecked()&&view->mesh().vertices==packet->vertices&&view->grabFramebuffer()==with_edges,
                    "Returning to FEM did not restore the checked, visible mesh exactly");
            }
            check(view->display_mode()==viewer::DisplayMode::Shaded&&kernel::body_calculation_count.load()==mesh_calculations,
                "FEM activation changed CAD shading preferences or recalculated geometry");
            view->set_display_mode(previous_mode);flush();
            sidebar_action(show_mesh)->click();flush();
            settings_revision=session.revision(); // Body activation is a separate UI transaction.
        }
#ifdef ZIMA_ENABLE_FEM_MESHER
        {
            auto definition=*session.document().body_history.find(fem)->fem;
            check(!snapshot->kernel_faces.empty()&&!snapshot->kernel_edges.empty(),
                "FEM mesh fixture has no native face or edge binding");
            const auto& face=snapshot->kernel_faces.front().reference;
            document::FemCondition pressure;
            pressure.id="verification-pressure";
            pressure.kind=document::FemConditionKind::Pressure;
            pressure.region=document::FemRegionKind::Surface;
            pressure.placement.reference_valid=true;
            pressure.references={{face.owner_id,face.semantic_key,face.instance_path}};
            document::FemCondition force;
            force.id="verification-origin";
            force.kind=document::FemConditionKind::Force;
            force.placement.z=2.5;
            force.placement.reference_valid=true;
            auto line_force=force;
            line_force.id="verification-edge";
            line_force.region=document::FemRegionKind::Curve;
            const auto& edge=snapshot->kernel_edges.front().reference;
            line_force.references={{edge.owner_id,edge.semantic_key,edge.instance_path}};
            auto fixed=force;
            fixed.id="verification-shared-origin";
            fixed.kind=document::FemConditionKind::Constraint;
            definition.conditions={pressure,force,line_force,fixed};
            const auto meshed=generate_fem_volume_mesh(snapshot.get(),definition);
            const auto& pressure_mesh=meshed.condition_regions.at(pressure.id);
            check(!meshed.tetrahedra.empty()&&!pressure_mesh.nodes.empty()&&!pressure_mesh.elements.empty(),
                "FEM mesh did not retain the exact face condition group");
            const auto& force_mesh=meshed.condition_regions.at(force.id);
            check(force_mesh.nodes.size()==1&&force_mesh.elements.size()==1&&
                force_mesh.elements.front().front()==force_mesh.nodes.front()&&
                meshed.surface.vertices.at(force_mesh.nodes.front())==kernel::Vec3{0,0,2.5},
                "FEM mesh did not embed the exact placed Force origin");
            check(meshed.condition_regions.at(fixed.id).nodes==force_mesh.nodes,
                "FEM conditions at the same Origin did not share the exact mesh node");
            double area=0;
            const auto& vertices=meshed.surface.vertices;
            for(const auto& triangle:pressure_mesh.elements) {
                const auto& a=vertices.at(triangle[0]);
                const auto& b=vertices.at(triangle[1]);
                const auto& c=vertices.at(triangle[2]);
                const auto normal=kernel::Vec3{
                    (b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y),
                    (b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z),
                    (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)};
                area+=std::sqrt(normal.x*normal.x+normal.y*normal.y+normal.z*normal.z)/2;
            }
            check(std::abs(area-50)<1e-7||std::abs(area-100)<1e-7||std::abs(area-200)<1e-7,
                "FEM pressure element group does not cover one complete box face");
            double length=0;
            for(const auto& segment:meshed.condition_regions.at(line_force.id).elements) {
                const auto& a=vertices.at(segment[0]);
                const auto& b=vertices.at(segment[1]);
                length+=std::hypot(b.x-a.x,b.y-a.y,b.z-a.z);
            }
            check(std::abs(length-5)<1e-7||std::abs(length-10)<1e-7||std::abs(length-20)<1e-7,
                "FEM line-force element group does not cover one complete box edge");
            const auto& packet=snapshot->mesh;
            kernel::FaceReference cap;
            for(std::size_t i=0;i<packet.triangle_references.size();++i) {
                const auto top=[&](std::size_t corner){return std::abs(packet.vertices[packet.triangles[3*i+corner]].z-2.5)<1e-7;};
                if(top(0)&&top(1)&&top(2)){cap=packet.triangle_references[i];break;}
            }
            check(cap.valid(),"FEM projected Sketch fixture lacks its top face");
            auto sketch=sketcher::Sketch::create_default();
            sketch.owner_container_id=force.id;
            sketch.plane_auto=false;sketch.plane_offset=5;
            static_cast<void>(sketch.add_rectangle(-2,-1,2,1));
            auto projected=force;
            projected.name="Projected pressure";
            projected.kind=document::FemConditionKind::Pressure;projected.pressure_MPa=2.5;
            projected.region=document::FemRegionKind::Surface;
            projected.references={{cap.owner_id,cap.semantic_key,cap.instance_path}};
            projected.projected_sketch=document::FemProjectedSketch{sketch.serialized(),{{0,0,2.5}}};
            document::validate_fem_condition(projected);
            auto sketch_definition=definition;
            sketch_definition.conditions={projected};
            const auto projected_mesh=generate_fem_volume_mesh(snapshot.get(),sketch_definition);
            double projected_area=0;
            for(const auto& triangle:projected_mesh.condition_regions.at(projected.id).elements) {
                const auto& a=projected_mesh.surface.vertices[triangle[0]];
                const auto& b=projected_mesh.surface.vertices[triangle[1]];
                const auto& c=projected_mesh.surface.vertices[triangle[2]];
                check(std::abs(a.z-2.5)<1e-7&&std::abs(b.z-2.5)<1e-7&&std::abs(c.z-2.5)<1e-7,
                    "Projected FEM patch is off the exact material face");
                projected_area+=std::abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))/2;
            }
            check(std::abs(projected_area-8)<1e-7,"FEM sketch did not split out the requested 8 mm² patch");
            auto line_sketch=sketcher::Sketch::create_default();
            line_sketch.owner_container_id=line_force.id;
            line_sketch.plane_auto=false;line_sketch.plane_offset=5;
            static_cast<void>(line_sketch.add_segment(-2,3,2,3));
            auto projected_line=line_force;
            projected_line.kind=document::FemConditionKind::Constraint;
            projected_line.references=projected.references;
            projected_line.projected_sketch=document::FemProjectedSketch{line_sketch.serialized(),{}};
            sketch_definition.conditions.push_back(projected_line);
            const auto mixed_mesh=generate_fem_volume_mesh(snapshot.get(),sketch_definition);
            double projected_length=0;
            for(const auto& segment:mixed_mesh.condition_regions.at(projected_line.id).elements) {
                const auto& a=mixed_mesh.surface.vertices[segment[0]];
                const auto& b=mixed_mesh.surface.vertices[segment[1]];
                projected_length+=std::abs(b.x-a.x);
            }
            check(std::abs(projected_length-4)<1e-7,"FEM projected line did not bind a 4 mm material mesh edge");
            for(auto order:{document::FemElementOrder::Linear,document::FemElementOrder::Quadratic})
            for(double plane:{-2.5,0.,2.5}) {
                auto direct=projected;
                direct.id="direct-surface";direct.placement={};direct.placement.reference_valid=true;
                direct.references.clear();direct.projected_sketch->surface_seeds.clear();
                direct.projected_sketch->surface_region_keys.clear();direct.projected_sketch->project_to_body=false;
                auto planar_sketch=sketcher::Sketch::from_serialized(direct.projected_sketch->serialized);
                planar_sketch.owner_container_id=direct.id;planar_sketch.plane_offset=plane;
                direct.projected_sketch->serialized=planar_sketch.serialized();
                auto direct_line=projected_line;
                direct_line.id="direct-curve";direct_line.placement=direct.placement;
                direct_line.references.clear();direct_line.projected_sketch->project_to_body=false;
                auto planar_line=sketcher::Sketch::from_serialized(direct_line.projected_sketch->serialized);
                planar_line.owner_container_id=direct_line.id;planar_line.plane_offset=plane;
                direct_line.projected_sketch->serialized=planar_line.serialized();
                auto direct_definition=definition;direct_definition.mesh.order=order;
                direct_definition.conditions={direct,direct_line};
                const auto direct_mesh=generate_fem_volume_mesh(snapshot.get(),direct_definition);
                double area=0,length=0;
                for(const auto& triangle:direct_mesh.condition_regions.at(direct.id).elements) {
                    const auto a=direct_mesh.surface.vertices[triangle[0]],b=direct_mesh.surface.vertices[triangle[1]],c=direct_mesh.surface.vertices[triangle[2]];
                    check(std::abs(a.z-plane)<1e-7&&std::abs(b.z-plane)<1e-7&&std::abs(c.z-plane)<1e-7,
                        "Direct FEM surface moved off its authored plane");
                    area+=std::abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))/2;
                }
                for(const auto& edge:direct_mesh.condition_regions.at(direct_line.id).elements) {
                    const auto a=direct_mesh.surface.vertices[edge[0]],b=direct_mesh.surface.vertices[edge[1]];
                    check(std::abs(a.z-plane)<1e-7&&std::abs(b.z-plane)<1e-7,"Direct FEM curve moved off its authored plane");
                    length+=std::hypot(b.x-a.x,b.y-a.y,b.z-a.z);
                }
                check(std::abs(area-8)<1e-7&&std::abs(length-4)<1e-7,"Direct FEM mesh lost authored area or length");
                if(plane==0.&&order==document::FemElementOrder::Linear) {
                    direct_definition.conditions.push_back(projected);
                    direct_definition.conditions.push_back(projected_line);
                    direct_definition.conditions.push_back(pressure);
                    check(generate_fem_volume_mesh(snapshot.get(),direct_definition).condition_regions.size()==5,
                        "Mixed planar/projected/body-face FEM mesh lost a region");
                }
            }
            std::cout<<"FEM planar curve/surface mesh: both boundary sides and internal plane, linear/quadratic, mixed projected/body-face groups passed\n";
            window.show_fem_condition_properties(document::FemConditionKind::Force);flush();
            auto* authored=dynamic_cast<FemConditionDialog*>(window.findChild<QDialog*>("femConditionDialog"));
            check(authored,"Projected Force Properties did not open");
            authored->findChild<QComboBox*>("femConditionRegion")->setCurrentIndex(1);
            authored->findChild<QCheckBox*>("femProjectSketch")->setChecked(true);
            authored->findChild<QDoubleSpinBox*>("femSketchOffset")->setValue(5);
            authored->findChild<QPushButton*>("femEditSketch")->click();flush();
            check(window.sweep_profile_sketch_draft_&&!window.active_sketch_id_.empty()&&window.finish_sketch_action_->isEnabled(),
                "FEM did not enter the shared Sketch editor");
            static_cast<void>(window.sweep_profile_sketch_draft_->add_segment(-2,3,2,3));
            window.finish_sketch_action_->trigger();flush();
            check(authored->isVisible()&&window.active_sketch_id_.empty(),"FEM Sketch did not return to owning Properties");
            authored->set_geometry(0,{cap.owner_id,cap.semantic_key,cap.instance_path});
            const auto authored_id=authored->pending_value().id;
            authored->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            const auto* accepted=workspace::find_fem_condition(session.document(),fem,authored_id);
            check(accepted&&accepted->projected_sketch,"FEM owned Sketch did not commit");
            session.document().save(path,session.calculated_boundaries());
            const auto reopened=document::PartDocument::load(path);
            const auto* from_disk=workspace::find_fem_condition(reopened,fem,authored_id);
            check(from_disk&&from_disk->projected_sketch==accepted->projected_sketch,
                "FEM owned Sketch did not reopen with its condition");
            window.findChild<QAction*>("undoAction")->trigger();flush();
            check(!workspace::find_fem_condition(session.document(),fem,authored_id),"FEM owned Sketch Undo failed");
            window.findChild<QAction*>("redoAction")->trigger();flush();
            check(workspace::find_fem_condition(session.document(),fem,authored_id),"FEM owned Sketch Redo failed");
            window.findChild<QAction*>("undoAction")->trigger();flush();
            settings_revision=session.revision();
            auto copy=snapshot.get();
            copy.kernel_placements.push_back({{30,0,0},{}});
            kernel::BodyResult compound;
            compound.kernel_shape=kernel::kernel_body_compound;
            compound.kernel_compound_bodies={"first","second"};
            compound.body_outputs.emplace("first",snapshot);
            compound.body_outputs.emplace("second",kernel::BodySnapshot(std::move(copy)));
            auto two_members=definition;
            two_members.conditions={pressure};
            const auto compound_mesh=generate_fem_volume_mesh(compound,two_members);
            const auto& regions=compound_mesh.condition_regions.at(pressure.id).nodes;
            check(std::ranges::any_of(regions,[&](auto node){return compound_mesh.surface.vertices[node].x<15;})&&
                std::ranges::any_of(regions,[&](auto node){return compound_mesh.surface.vertices[node].x>15;}),
                "FEM compound condition did not bind both exact material members");
            definition.conditions.front().references.front().semantic_key="missing";
            bool rejected=false;
            try{static_cast<void>(generate_fem_volume_mesh(snapshot.get(),definition));}
            catch(const std::invalid_argument&){rejected=true;}
            check(rejected,"FEM mesher accepted an unresolved face");
        }
#endif
        auto* settings=open_settings();
        settings->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(session.revision()==settings_revision,"Unchanged FEM mesh settings added Undo history");
        settings=open_settings();
        settings->findChild<QDoubleSpinBox*>("femMeshMaximum")->setValue(0);
        settings->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(settings->isVisible()&&mesh_controls()==original_controls,"Invalid FEM mesh settings were accepted");
        settings->reject();flush();
        check(session.revision()==settings_revision,"Cancelling FEM mesh settings changed the document");
        settings=open_settings();
        settings->findChild<QDoubleSpinBox*>("femMeshMaximum")->setValue(2.5);
        settings->findChild<QComboBox*>("femMeshOrder")->setCurrentIndex(0);
        settings->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(mesh_controls().maximum_size_mm==2.5,"FEM mesh settings did not commit");
        window.findChild<QAction*>("undoAction")->trigger();flush();
        check(mesh_controls()==original_controls,"FEM mesh settings Undo failed");
        window.findChild<QAction*>("redoAction")->trigger();flush();
        check(mesh_controls().maximum_size_mm==2.5,"FEM mesh settings Redo failed");
        if(generate) {
            check(!window.active_generated_fem_mesh()&&!show_mesh->isEnabled(),"FEM retained a mesh after its controls changed");
            check_mesh();
        }
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
        check(saved.body_history.find(fem)->fem->mesh==mesh_controls(),"FEM save/reopen lost mesh settings");
        check(kernel::body_calculation_count.load()==count,"FEM creation/edit/save/Undo calculated CAD");
        window.activate_body({});flush();check(action->isEnabled(),"Returning from FEM did not restore Body commands");
        check(!mesh_menu->menuAction()->isVisible(),"FEM mesh menu appears outside FEM");
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
        const QDir config(QFileInfo(window.application_settings_.base_config_path).absolutePath());
        const auto localized_settings=[&](const char* language) {
            QTemporaryDir temporary(QString::fromStdString(document::path_to_utf8(directory/"localization-XXXXXX")));
            check(temporary.isValid(),"FEM localization settings directory unavailable");
            QSettings ini(temporary.filePath("config.ini"),QSettings::IniFormat);
            ini.setValue("Application/Language",QString::fromLatin1(language));
            ini.setValue("Paths/Localization",config.filePath("localization"));ini.sync();
            check(ini.status()==QSettings::NoError,"FEM localization settings could not be written");
            // Exercise the production loader: common controls live in the INI
            // catalog, while newer FEM messages live in the supplemental JSON.
            auto settings=ApplicationSettings::load(temporary.path());
            check(!settings.qt_translations.value("Zrušit").isEmpty(),"FEM common catalog unavailable");
            return settings;
        };
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            auto settings=localized_settings(language);
            apply_application_translations(application,settings);
#ifdef ZIMA_ENABLE_FEM_MESHER
            window.rebuild_application_toolbar();
            for(auto* action:mesh_menu->actions())sidebar_action(action);
            for(const auto& entry:std::vector<std::pair<const char*,const char*>>{
                    {"femMeshSettingsAction","Mesh settings"},{"femGenerateMeshAction","Generate mesh"},
                    {"femShowMeshAction","Show mesh"},{"femBodyPropertiesAction","FEM Body properties"}})
                check(current_sidebar_action(entry.first)&&current_sidebar_action(entry.first)->text()==settings.qt_translations.value(entry.second),
                    "FEM direct sidebar command did not translate");
            const auto* calculation=window.findChild<QMenu*>("femCalculationMenu");
            const auto* solve=window.findChild<QAction*>("femLinearStaticsAction");
            const auto* deformation=window.findChild<QAction*>("femShowDeformationAction");
            check(calculation&&solve&&deformation&&calculation->title()==settings.qt_translations.value("Calculation")&&
                mesh_menu->title()==settings.qt_translations.value("Mesh")&&
                solve->text()==settings.qt_translations.value("Linear statics")&&
                deformation->text()==settings.qt_translations.value("Show deformation"),"FEM calculation menu did not translate");
            check(window.findChild<QMenu*>("femResultsMenu")->title()==settings.qt_translations.value("Results")&&
                window.findChild<QAction*>("femDisplacementResultAction")->text()==settings.qt_translations.value("Displacement [mm]")&&
                window.findChild<QAction*>("femStressResultAction")->text()==settings.qt_translations.value("Von Mises stress [MPa]"),
                "FEM result choices did not translate");
            for(const auto& entry:std::vector<std::pair<const char*,const char*>>{
                    {"femAnimateAction","Play deformation"},{"femOriginalAction","Transparent original"},
                    {"femExtremaAction","Show minimum / maximum"},{"femProbeAction","Probe results"},
                    {"femDeformationSettingsAction","Deformation player"}})
                check(current_sidebar_action(entry.first)&&current_sidebar_action(entry.first)->text()==settings.qt_translations.value(entry.second),
                    "FEM presentation action did not translate");
            for(const auto* key:{"FEM: preparing geometry…","FEM: generating the volume mesh…",
                    "FEM: checking mesh quality…","FEM: optimizing the quadratic mesh…","FEM: preparing mesh data…",
                    "FEM: assembling the system…","FEM: applying loads and restraints…",
                    "FEM: solving the system (iteration %1)…","FEM: calculating displacement and stress…",
                    "FEM: preparing result display…","FEM inputs changed. Repeat the operation.","FEM mesh generated.",
                    "Repair unresolved FEM conditions before calculation."})
                check(!settings.qt_translations.value(key).isEmpty()&&QObject::tr(key)==settings.qt_translations.value(key),
                    "FEM progress phase did not translate");
#endif
            for(const auto* theme:{"dark","light"}) {
                settings.theme=theme;apply_application_appearance(application,settings);flush();
#ifdef ZIMA_ENABLE_FEM_MESHER
                auto* progress=window.operation_progress_;
                auto* spy=new FemProgressPaintSpy;spy->setParent(progress);progress->setStyle(spy);
                const auto progress_label=QObject::tr("FEM: assembling the system…");
                window.begin_status_operation(progress_label);
                window.update_status_operation(progress_label,1,4);
                static_cast<void>(progress->grab());
                check(spy->painted_text.contains(progress_label)&&spy->painted_text.contains("25%")&&
                    !spy->painted_text.contains("%p%"),"Progress painting lost the phase or displayed a literal percentage placeholder");
                window.grab().save(QString::fromStdString(document::path_to_utf8(directory/(std::string("fem-progress-")+language+"-"+theme+".png"))));
                window.update_status_operation(QObject::tr("FEM: solving the system (iteration %1)…").arg(12),-1,0);
                static_cast<void>(progress->grab());
                check(spy->painted_text==QObject::tr("FEM: solving the system (iteration %1)…").arg(12),
                    "Indeterminate solver progress lost its translated iteration text");
                window.finish_status_operation(QObject::tr("FEM mesh generated."));
                check(!window.operation_activity_->active(),"Progress presentation remained active after completion");
                progress->setStyle(nullptr);delete spy;progress->hide();
#endif
                window.show_fem_body_properties(fem);flush();dialog=current();check(dialog,"Localized FEM dialog missing");
                check(dialog->windowTitle()==QObject::tr("FEM Body properties"),"FEM title did not translate");
                window.grab().save(QString::fromStdString(document::path_to_utf8(directory/(std::string("fem-body-")+language+"-"+theme+".png"))));
                dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
                window.show_fem_condition_properties(document::FemConditionKind::Force,force_id);flush();
                auto* load=condition_dialog();check(load,"Localized condition dialog missing");
                check(load->windowTitle()==QObject::tr("FEM condition properties")&&load->findChild<QComboBox*>("femConditionKind")->currentText()==QObject::tr("Force"),"Condition title/type did not translate");
                const auto retained=load->pending_value();
                QEvent translation(QEvent::LanguageChange);QApplication::sendEvent(load,&translation);flush();check(load->pending_value()==retained,"Condition retranslation changed authored values");
                check(load->buttons()->button(QDialogButtonBox::Cancel)->text()==QObject::tr("Zrušit"),"Condition Cancel lost its catalog translation");
                window.grab().save(QString::fromStdString(document::path_to_utf8(directory/(std::string("fem-condition-")+language+"-"+theme+".png"))));
                auto* region=load->findChild<QComboBox*>("femConditionRegion");
                auto* project=load->findChild<QCheckBox*>("femProjectSketch");
                check(region->count()==4&&region->itemText(3)==settings.qt_translations.value("Body surface")&&
                    project->text()==settings.qt_translations.value("Project Sketch onto body"),"FEM region choices did not translate");
                for(int choice=0;choice<4;++choice)for(bool projection:{false,true}) {
                    if((choice==0||choice==3)&&projection)continue;
                    region->setCurrentIndex(choice);project->setChecked(projection);flush();
                    const bool sketch=choice==1||choice==2;
                    auto* plane_controls=dynamic_cast<ProfilePlaneControls*>(load->findChild<QGroupBox*>("femPlaneGroup")->parentWidget());
                    check(plane_controls&&plane_controls->plane_group->title()==settings.qt_translations.value("Rovina")&&
                        plane_controls->sketch_group->title()==settings.qt_translations.value("Skica")&&
                        plane_controls->plane_label->text()==settings.qt_translations.value("Výchozí rovina")&&
                        qobject_cast<QLabel*>(plane_controls->rows->labelForField(plane_controls->offset))->text()==settings.qt_translations.value("Odsazení roviny"),
                        "Shared FEM Plane controls did not translate");
                    check(load->findChild<QPushButton*>("femEditSketch")->isVisible()==sketch&&
                        project->isVisible()==sketch&&load->findChild<QDoubleSpinBox*>("femSketchOffset")->isVisible()==sketch&&
                        load->findChild<QTableWidget*>("femConditionGeometry")->isVisible()==(choice==3||(sketch&&projection)),
                        "Localized FEM mode exposes incorrect controls");
                    const auto draft=load->pending_value();
                    QEvent retranslate(QEvent::LanguageChange);QApplication::sendEvent(load,&retranslate);flush();
                    check(load->pending_value()==draft,"Mode retranslation changed the FEM draft");
                    window.grab().save(QString::fromStdString(document::path_to_utf8(directory/(std::string("fem-region-")+
                        std::to_string(choice)+(projection?"-projected-":"-direct-")+language+"-"+theme+".png"))));
                }
                load->reject();flush();
            }
        }
        apply_application_translations(application,ApplicationSettings{});
#ifdef ZIMA_ENABLE_FEM_MESHER
        {
            constexpr double span=120,width=12,depth=10,load=100,young=210000,poisson=.3;
            auto beam=document::PartDocument::create_default();
            auto feature=test::rectangular_feature(beam,{width,depth,span});
            beam.history={feature};
            beam.physical_parameters={{"YOUNG_MODULUS","210"},{"POISSON_RATIO","0.3"}};
            beam.physical_parameter_units={{"YOUNG_MODULUS","GPa"},{"POISSON_RATIO","1"}};
            document::BodyHistoryGraph beam_graph;
            const auto stock=beam_graph.create_body("Cantilever");
            beam_graph.insert({document::PartHistoryKind::Feature,feature.id});
            beam.set_body_history(beam_graph);
            const auto calculated=workspace::calculate_part_with_resolved_references(kernel,beam);
            const auto solid=calculated.back().body_outputs.at(stock);
            const auto cap_at=[&](double z) {
                const auto& surface=solid->mesh;
                for(std::size_t triangle=0;triangle<surface.triangle_references.size();++triangle){
                    const auto on_cap=[&](unsigned corner){
                        return std::abs(surface.vertices[surface.triangles[3*triangle+corner]].z-z)<1e-7;
                    };
                    if(on_cap(0)&&on_cap(1)&&on_cap(2))return surface.triangle_references[triangle];
                }
                throw std::runtime_error("Cantilever cap is missing.");
            };
            document::FemCondition support;
            support.id="cantilever-support";support.name="Clamp";
            support.region=document::FemRegionKind::Surface;support.placement.reference_valid=true;
            auto face=cap_at(-span/2);
            support.references={{face.owner_id,face.semantic_key,face.instance_path}};
            auto support_sketch=sketcher::Sketch::create_default();
            support_sketch.owner_container_id=support.id;support_sketch.plane_auto=false;
            support_sketch.plane_offset=-span/2-5;
            static_cast<void>(support_sketch.add_rectangle(-5.8,-4.8,5.8,4.8));
            support.projected_sketch=document::FemProjectedSketch{support_sketch.serialized(),{{0,0,-span/2}}};
            document::FemCondition tip;
            tip.id="cantilever-force";tip.name="Tip force";
            tip.kind=document::FemConditionKind::Force;tip.region=document::FemRegionKind::Surface;
            tip.force_N=load;tip.placement.reference_valid=true;tip.placement.rotation_y=90;
            face=cap_at(span/2);
            tip.references={{face.owner_id,face.semantic_key,face.instance_path}};
            document::FemDefinition analysis;
            analysis.source_id=stock;analysis.conditions={support,tip};
            analysis.mesh.maximum_size_mm=4;analysis.mesh.minimum_size_mm=.15;
            const auto volume=generate_fem_volume_mesh(solid.get(),analysis);
            auto unrestrained=analysis;unrestrained.conditions.erase(unrestrained.conditions.begin());
            bool rejected=false;
            try {static_cast<void>(solve_fem_linear_statics(volume,unrestrained,{young,poisson}));}
            catch(const std::runtime_error& error){rejected=std::string_view(error.what()).find("rigid-body")!=std::string_view::npos;}
            check(rejected,"The static solver accepted a floating cantilever");
            const auto solution=solve_fem_linear_statics(volume,analysis,{young,poisson});
            kernel::Vec3 reaction{};
            for(const auto r:solution.reaction){reaction.x+=r.x;reaction.y+=r.y;reaction.z+=r.z;}
            check(std::abs(reaction.x+load)<1e-4&&std::abs(reaction.y)<1e-4&&std::abs(reaction.z)<1e-4,
                "The clamp reaction does not balance the applied force");
            const auto& end_nodes=volume.condition_regions.at(tip.id).nodes;
            double tip_dx{};
            for(const auto node:end_nodes)tip_dx+=solution.displacement[node].x/end_nodes.size();
            const double inertia=depth*width*width*width/12;
            const double theory=load*span*span*span/(3*young*inertia);
            std::cout<<"Cantilever tip "<<tip_dx<<" mm, beam theory "<<theory
                <<" mm, clamp reaction "<<reaction.x<<" N\n";
            check(std::abs(tip_dx-theory)/theory<.06,"The cantilever FEM displacement disagrees with beam theory");
            document::BodyHistory fem_body;
            fem_body.scope.id="cantilever-fem";fem_body.name="Cantilever FEM";fem_body.fem=analysis;
            const auto beam_fem=beam_graph.create_fem(fem_body);
            beam_graph.activate(beam_fem);
            beam.set_body_history(beam_graph);
            const auto beam_path=directory/"fem-cantilever.prtz";
            beam.save(beam_path,workspace::calculate_part_with_resolved_references(kernel,beam));
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(beam_path))),"Cantilever did not reopen");
            window.activate_body(beam_fem);flush();
            auto* solve=window.findChild<QAction*>("femLinearStaticsAction");
            auto* deformation=window.findChild<QAction*>("femShowDeformationAction");
            check(solve&&deformation&&!deformation->isEnabled(),"The FEM result was available before solving");
            const auto* beam_session=&window.workspace_.open_part(beam.document_id)->session;
            const auto beam_revision=beam_session->revision();
            generate->trigger();flush();solve->trigger();flush();
            const auto* deformed=window.active_fem_deformation();
            check(deformed&&deformation->isChecked()&&deformed->vertices==window.viewer_->mesh().vertices,
                "The static command did not display the deformed cantilever");
            check(beam_session->revision()==beam_revision,"Generating a static result modified the document");
            check(!window.operation_activity_->active()&&window.operation_progress_->value()==100,
                "FEM solver progress was not completed");
            const auto progress_generation=window.operation_progress_generation_;
            solve->trigger();flush();
            check(window.operation_progress_generation_==progress_generation,"Cached FEM solution restarted calculation progress");
            auto* stress_action=window.findChild<QAction*>("femStressResultAction");
            auto* displacement_action=window.findChild<QAction*>("femDisplacementResultAction");
            for(auto* action:results_menu->actions())if(!action->isSeparator()&&action->isVisible())
                check(sidebar_action(action)->isEnabled(),"Calculated FEM result command is not available in the sidebar");
            const auto colors=view->triangle_scalar_field();
            check(colors.size()==volume.surface.triangles.size()/3,"Displacement colors are missing");
            const auto displacement_image=view->grabFramebuffer();
            const auto verify_legend=[&](const QImage& image,const QString& label) {
                const int panel_width=std::max(200,QFontMetrics(view->font()).horizontalAdvance(label)+24);
                const double scale=double(image.width())/view->width();
                const int x=int((view->width()-panel_width-18+24)*scale);
                const auto high=image.pixelColor(x,int(97*scale)),low=image.pixelColor(x,int(267*scale));
                check(high.red()>180&&high.green()<110&&low.blue()>150&&low.red()<70,
                    "Result legend does not match the blue-to-red mesh palette");
            };
            verify_legend(displacement_image,QObject::tr("Displacement [mm]"));
            unsigned colored_pixels=0;
            for(int y=90;y<displacement_image.height();++y)for(int x=0;x<displacement_image.width()-400;++x) {
                const auto c=displacement_image.pixelColor(x,y);
                if(std::max({c.red(),c.green(),c.blue()})-std::min({c.red(),c.green(),c.blue()})>100)++colored_pixels;
            }
            check(colored_pixels>100,"Result colors did not reach the rendered mesh");
            const auto upload=view->property("gpuMeshUploadCount");const auto camera=view->camera_state();
            const auto mesh_vertices=view->mesh().vertices;
            for(unsigned repeat=0;repeat<3;++repeat) {
                sidebar_action(stress_action)->click();flush();
                check(stress_action->isChecked()&&!displacement_action->isChecked(),"Result choice is not exclusive");
                check(view->triangle_scalar_field()!=colors,"Stress did not replace displacement colors");
                const auto stress_image=view->grabFramebuffer();
                verify_legend(stress_image,QObject::tr("Von Mises stress [MPa]"));
                check(stress_image!=displacement_image,"Result switching did not change the rendered field");
                sidebar_action(displacement_action)->click();flush();
                check(view->triangle_scalar_field()==colors,"Displacement field was not restored exactly");
                check(view->mesh().vertices==mesh_vertices&&view->camera_state()==camera&&view->property("gpuMeshUploadCount")==upload,
                    "Result switching rebuilt geometry or moved the camera");
                check(beam_session->revision()==beam_revision,"Result switching changed the document");
            }
            // Result presentation owns no model transaction or per-frame upload.
            auto* animate=window.findChild<QAction*>("femAnimateAction");
            auto* original=window.findChild<QAction*>("femOriginalAction");
            auto* extrema=window.findChild<QAction*>("femExtremaAction");
            auto* probe=window.findChild<QAction*>("femProbeAction");
            auto* settings_action=window.findChild<QAction*>("femDeformationSettingsAction");
            for(const auto* language:{"cs","en","de","fr","ru"}) {
                auto localized=localized_settings(language);
                apply_application_translations(application,localized);window.update_fem_result_presentation();
                for(const auto* theme:{"dark","light"}) {
                    localized.theme=theme;apply_application_appearance(application,localized);
                    window.rebuild_application_toolbar();flush();
                    auto* display=window.findChild<QAction*>("femShowDeformationAction");
                    auto* display_button=sidebar_action(display);
                    check(display->isChecked(),"FEM result display is not marked active");
                    for(auto* active_button:{display_button,sidebar_action(displacement_action)}) {
                        check(active_button->isChecked(),"Current FEM result quantity has no active sidebar state");
                        const auto image=active_button->grab().toImage();unsigned azure=0;
                        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
                            const auto color=image.pixelColor(x,y);
                            if(color.red()<40&&color.green()>150&&color.blue()>180)++azure;
                        }
                        check(azure>unsigned(image.width()*image.height()/3),"Active FEM sidebar button is not azure in this theme");
                    }
                    sidebar_action(settings_action)->click();flush();
                    auto* form=window.findChild<QDialog*>("femDeformationSettingsDialog");
                    check(form,"Localized deformation settings are missing");
                    const auto* buttons=form->findChild<QDialogButtonBox*>();
                    check(buttons&&buttons->button(QDialogButtonBox::Cancel)->text()==localized.qt_translations.value("Zrušit"),
                        "Deformation settings Cancel did not use the common catalog");
                    check(form->findChild<QDoubleSpinBox*>("femMagnification")->isVisible()&&
                        form->findChild<QDoubleSpinBox*>("femAnimationDuration")->isVisible()&&
                        form->findChild<QPushButton*>("femAutomaticMagnification")->isVisible(),"Player settings are initially hidden");
                    const auto player_geometry=form->geometry();
                    check(form->windowTitle()==localized.qt_translations.value("Deformation player"),"FEM player title did not translate");
                    auto* player_button=form->findChild<QToolButton*>("femPlayerPlayButton");
                    check(player_button->defaultAction()->text()==localized.qt_translations.value("Play deformation"),"Player Play did not translate");
                    player_button->click();flush();
                    check(player_button->isChecked()&&player_button->defaultAction()->text()==localized.qt_translations.value("Pause deformation"),
                        "Player Pause did not translate or become active");
                    const auto playing_image=player_button->grab().toImage();unsigned playing_azure=0;
                    for(int y=0;y<playing_image.height();++y)for(int x=0;x<playing_image.width();++x) {
                        const auto color=playing_image.pixelColor(x,y);
                        if(color.red()<40&&color.green()>150&&color.blue()>180)++playing_azure;
                    }
                    check(playing_azure>unsigned(playing_image.width()*playing_image.height()/3),"Playing FEM button is not azure in this theme");
                    player_button->click();flush();
                    check(form->findChild<QPushButton*>("femSaveVideo")->text()==localized.qt_translations.value("Save video"),"Save video did not translate");
                    for(double seconds:{.3,30.,3.}) {
                        form->findChild<QDoubleSpinBox*>("femAnimationDuration")->setValue(seconds);
                        form->findChild<QDoubleSpinBox*>("femMagnification")->setValue(seconds*100);
                        form->findChild<QSlider*>("femPlayerTimeline")->setValue(500);flush();
                        check(form->geometry()==player_geometry,"Player resized or moved after changing its settings");
                    }
                    for(const auto* key:{"Deformation magnification","Cycle duration",
                            "Animation interpolates the static result. Values and colors always show the calculated load case."})
                        check(std::ranges::any_of(form->findChildren<QLabel*>(),[&](const auto* label){return label->text()==localized.qt_translations.value(key);}),
                            "Deformation settings contain untranslated text");
                    window.grab().save(QString::fromStdString(document::path_to_utf8(directory/(std::string("fem-deformation-")+language+"-"+theme+".png"))));
                    form->reject();flush();
                }
            }
            apply_application_translations(application,ApplicationSettings{});
            window.update_fem_result_presentation();window.rebuild_application_toolbar();
            flush();const auto player_camera=view->camera_state();
            settings_action->trigger();flush();
            auto* settings=window.findChild<QDialog*>("femDeformationSettingsDialog");
            check(settings&&settings->parentWidget()==&window&&(settings->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,
                "Deformation settings do not use the shared properties window");
            const auto player_calculations=kernel::body_calculation_count.load();
            const auto player_geometry=settings->geometry();
            auto* timeline=settings->findChild<QSlider*>("femPlayerTimeline");
            auto* play=settings->findChild<QToolButton*>("femPlayerPlayButton");
            const QRect view_area(view->mapTo(&window,QPoint{}),view->size());
            check(timeline&&play&&view_area.contains(settings->geometry()),"FEM player is not inside the bottom right of the View");
            timeline->setValue(0);flush();check(view->result_deformation_factor()==0,"Player start is not undeformed");
            timeline->setValue(500);flush();check(view->result_deformation_factor()==1,"Player midpoint is not the full static deformation");
            const auto play_icon=play->icon().pixmap(24,24).toImage();play->click();flush();
            check(play->isChecked()&&play->icon().pixmap(24,24).toImage()!=play_icon,"Player did not switch from Play to Pause");
            play->click();flush();const auto paused_factor=view->result_deformation_factor();
            check(!play->isChecked()&&play->icon().pixmap(24,24).toImage()==play_icon,"Player pause did not restore Play");
            QEventLoop held_frame;QTimer::singleShot(80,&held_frame,&QEventLoop::quit);held_frame.exec();
            check(view->result_deformation_factor()==paused_factor,"Paused player advanced its frame");
            timeline->setValue(1000);flush();check(view->result_deformation_factor()==0,"Player end did not return to the undeformed state");
            timeline->setValue(500);flush();
            check(beam_session->revision()==beam_revision&&kernel::body_calculation_count.load()==player_calculations,
                "Player navigation changed the document or calculated CAD");
            const auto export_camera=view->camera_state();
            const auto video_path=QString::fromStdString(document::path_to_utf8(directory/std::filesystem::u8path("deformation-žluťoučký.mp4")));
            bool video_name_offered=false;
            QTimer::singleShot(100,settings,[&] {
                if(auto* chooser=window.findChild<QFileDialog*>()) {
                    video_name_offered=QFileInfo(chooser->selectedFiles().value(0)).fileName()=="fem-cantilever.mp4";
                    // A visible QFileDialog proxy can discard selectFile() while loading.
                    chooser->findChild<QLineEdit*>("fileNameEdit")->setText(video_path);
                    QMetaObject::invokeMethod(chooser,"accept",Qt::DirectConnection);
                }
            });
            settings->findChild<QPushButton*>("femSaveVideo")->click();flush();
            check(video_name_offered,"Video export did not offer the source filename");
            if(QFileInfo(video_path).size()<=1000)
                std::cerr<<settings->findChild<QLabel*>("femVideoStatus")->text().toStdString()<<'\n';
            check(QFileInfo(video_path).size()>1000,"Video export did not write MP4");
            check(settings->geometry()==player_geometry,"Video export status resized or moved the player");
            check(view->result_deformation_factor()==1,"Video export did not restore the displayed deformation");
            check(view->camera_state()==export_camera,"Video export moved the camera");
            check(beam_session->revision()==beam_revision,"Video export changed the native document");
            QMediaPlayer decoded;QVideoSink sink;QEventLoop playback;QTimer deadline;deadline.setSingleShot(true);
            decoded.setVideoSink(&sink);int decoded_frames=0;QImage first_frame,peak_frame;bool decode_failed=false;
            QObject::connect(&sink,&QVideoSink::videoFrameChanged,&playback,[&](const QVideoFrame& frame) {
                if(!frame.isValid())return;++decoded_frames;
                if(first_frame.isNull())first_frame=frame.toImage();
                if(frame.startTime()>=1400000&&frame.startTime()<=1600000)peak_frame=frame.toImage();
            });
            QObject::connect(&decoded,&QMediaPlayer::mediaStatusChanged,&playback,[&](auto status) {if(status==QMediaPlayer::EndOfMedia)playback.quit();});
            QObject::connect(&decoded,&QMediaPlayer::errorOccurred,&playback,[&](auto,const QString&){decode_failed=true;playback.quit();});
            QObject::connect(&deadline,&QTimer::timeout,&playback,&QEventLoop::quit);
            deadline.start(15000);decoded.setSource(QUrl::fromLocalFile(video_path));decoded.play();playback.exec();decoded.stop();
            check(!decode_failed&&decoded_frames>=85&&std::abs(decoded.duration()-3000)<100&&!first_frame.isNull(),
                "Exported MP4 cannot be played with its expected frame count and duration");
            check(!peak_frame.isNull()&&peak_frame!=first_frame,"Exported MP4 did not animate the deformation");
            const auto protected_video=QString::fromStdString(document::path_to_utf8(directory/"existing-video.mp4"));
            {QFile existing(protected_video);check(existing.open(QIODevice::WriteOnly),"Cannot prepare protected export fixture");existing.write("keep-existing-video");}
            bool export_failed=false;
            try {write_fem_video(protected_video,.3,[](double)->QImage {throw std::runtime_error("deliberate frame failure");});}
            catch(const std::exception&){export_failed=true;}
            {QFile existing(protected_video);check(existing.open(QIODevice::ReadOnly)&&export_failed&&existing.readAll()=="keep-existing-video",
                "Failed video export replaced an existing file");}
            settings->findChild<QDoubleSpinBox*>("femMagnification")->setValue(20);
            settings->reject();flush();
            check(window.fem_magnification_==1&&view->camera_state()==player_camera,"Cancel changed deformation settings");
            settings_action->trigger();flush();settings=window.findChild<QDialog*>("femDeformationSettingsDialog");
            settings->findChild<QPushButton*>("femAutomaticMagnification")->click();
            const double magnification=settings->findChild<QDoubleSpinBox*>("femMagnification")->value();
            check(magnification>1,"Automatic magnification did not reveal bending");
            settings->findChild<QDoubleSpinBox*>("femAnimationDuration")->setValue(1.5);
            mouse(QEvent::MouseButtonPress,Qt::MiddleButton);mouse(QEvent::MouseButtonRelease,Qt::NoButton);
            check(window.properties_dialog_==settings,"Short middle click committed deformation settings");
            mouse(QEvent::MouseButtonDblClick,Qt::MiddleButton);flush();
            check(window.fem_magnification_==magnification&&window.fem_animation_speed_==2&&
                view->result_deformation_factor()==magnification,"Deformation settings did not commit presentation");
            const auto enlarged=view->grabFramebuffer();
            sidebar_action(original)->click();flush();const auto ghost=view->grabFramebuffer();
            check(ghost!=enlarged,"Transparent original did not affect rendering");
            sidebar_action(extrema)->click();flush();
            check(view->result_markers().size()==2,"Minimum and maximum markers are missing");
            sidebar_action(probe)->click();flush();
            QPointF probe_pixel;std::optional<viewer::MeshView::ResultHit> hit;
            for(int y=50;y<view->height()-60&&!hit;y+=12)for(int x=40;x<view->width()-220&&!hit;x+=12) {
                probe_pixel={double(x),double(y)};hit=view->result_hit_at(probe_pixel);
            }
            check(hit.has_value(),"Magnified mesh cannot be probed");
            QMouseEvent probe_click(QEvent::MouseButtonPress,probe_pixel,view->mapToGlobal(probe_pixel.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(view,&probe_click);flush();
            check(view->result_markers().size()==3&&window.state_->text().startsWith(QObject::tr("Mesh node:")),
                "Actual result click did not report the nearest mesh node");
            const auto sampled=view->result_markers().back();
            check(sampled.position==deformed->vertices.at(hit->node),"Probe marker has the wrong node identity");
            stress_action->trigger();flush();
            check(view->result_markers().size()==3&&view->result_markers().back().label.startsWith(QObject::tr("Element average:")),
                "Stress probe did not retain owning-element semantics");
            displacement_action->trigger();flush();
            view->grabFramebuffer();const auto vector_upload=view->property("resultVectorUploadCount");
            const auto field_upload=view->property("scalarFieldUploadCount");
            const auto animation_calculations=kernel::body_calculation_count.load();
            sidebar_action(animate)->click();flush();
            check(window.fem_animation_running(),"Animation did not start");
            window.fem_animation_->setCurrentTime(375);flush();const auto animated=view->grabFramebuffer();
            check(std::abs(view->result_deformation_factor()-magnification*.5)<magnification*.1,
                "Animation factor does not interpolate the static result");
            sidebar_action(animate)->click();flush();const auto paused=view->result_deformation_factor();
            QEventLoop pause_wait;QTimer::singleShot(120,&pause_wait,&QEventLoop::quit);pause_wait.exec();
            check(!window.fem_animation_running()&&view->result_deformation_factor()==paused,"Animation pause did not hold the frame");
            check(animated!=ghost&&view->property("gpuMeshUploadCount")==upload&&
                view->property("resultVectorUploadCount")==vector_upload&&view->property("scalarFieldUploadCount")==field_upload&&
                view->mesh().vertices==mesh_vertices&&view->triangle_scalar_field()==colors&&
                beam_session->revision()==beam_revision&&kernel::body_calculation_count.load()==animation_calculations,
                "Animation changed results, uploaded geometry, solved CAD or edited the document");
            window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"fem-animation-comparison.png")));
            animate->trigger();flush();deformation->trigger();flush();
            check(!window.fem_animation_running()&&view->result_markers().empty(),"Hiding results retained animation or markers");
            window.fem_magnification_=1;window.fem_animation_fraction_=1;
            window.fem_show_original_=window.fem_show_extrema_=window.fem_probe_enabled_=false;
            window.fem_probe_selection_.reset();window.fem_animation_->stop();
            check(!deformation->isChecked()&&window.viewer_->mesh().vertices==window.active_generated_fem_mesh()->vertices,
                "Turning off deformation did not restore the undeformed mesh");
            check(view->triangle_scalar_field().empty(),"Turning off results left a stale color field");
            auto& pressure_session=window.workspace_.open_part(beam.document_id)->session;
            const auto pressure_calculations=kernel::body_calculation_count.load();
            for(int mode=0;mode<3;++mode){
                auto pressure=tip;pressure.kind=document::FemConditionKind::Pressure;
                pressure.name="Pressure";pressure.pressure_MPa=2.5;pressure.placement.rotation_y=0;
                if(mode){
                    auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=pressure.id;
                    sketch.plane_auto=false;sketch.plane_offset=span/2+(mode==2?5:0);
                    static_cast<void>(sketch.add_rectangle(-3,-2,3,2));
                    sketch.refresh_default_frame();
                    pressure.projected_sketch=document::FemProjectedSketch{sketch.serialized(),{},{},mode==2};
                    if(mode==1)pressure.references.clear();
                    else pressure.projected_sketch->surface_seeds={{0,0,span/2}};
                }
                auto edit=workspace::prepare_fem_edit(pressure_session.document(),beam_fem);
                check(workspace::commit_fem_condition(window.workspace_,edit,pressure),"Pressure update failed");
                window.refresh_scene();window.rebuild_application_toolbar();flush();
                generate->trigger();flush();
                const auto pressure_revision=pressure_session.revision();
                solve->trigger();flush();
                check(window.active_fem_deformation()&&deformation->isChecked(),"Pressure command did not produce deformation");
                const auto first=window.active_fem_deformation()->vertices;
                check(pressure_session.revision()==pressure_revision,"Pressure solve changed the document");
                window.show_fem_condition_properties(document::FemConditionKind::Pressure,pressure.id);flush();
                auto* dialog=dynamic_cast<FemConditionDialog*>(window.findChild<QDialog*>("femConditionDialog"));
                check(dialog,"Pressure properties did not open");
                dialog->findChild<QDoubleSpinBox*>("femPressureValue")->setValue(5);
                dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
                check(pressure_session.revision()==pressure_revision&&window.active_fem_deformation(),
                    "Pressure Cancel lost the unchanged result");
                window.show_fem_condition_properties(document::FemConditionKind::Pressure,pressure.id);flush();
                dialog=dynamic_cast<FemConditionDialog*>(window.findChild<QDialog*>("femConditionDialog"));
                dialog->findChild<QDoubleSpinBox*>("femPressureValue")->setValue(5);
                dialog->findChild<QCheckBox*>("femPressureReverse")->setChecked(true);
                dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                check(!window.active_fem_deformation()&&window.active_generated_fem_mesh(),
                    "Pressure value/direction edit did not invalidate only the result");
                solve->trigger();flush();
                const auto* second=window.active_fem_deformation();check(second,"Edited pressure did not solve");
                const auto* original=window.active_generated_fem_mesh();
                for(std::size_t i=0;i<first.size();++i){
                    const auto a=first[i],b=second->vertices[i],p=original->vertices[i];
                    check(std::hypot(b.x-p.x+2*(a.x-p.x),b.y-p.y+2*(a.y-p.y),b.z-p.z+2*(a.z-p.z))<1e-8,
                        "GUI pressure edit did not reverse and double displacement");
                }
                check(pressure_session.undo(),"Pressure Undo failed");window.refresh_scene();window.rebuild_application_toolbar();flush();
                check(!window.active_fem_deformation(),"Pressure Undo retained an obsolete result");
                solve->trigger();flush();
                check(window.active_fem_deformation()&&window.active_fem_deformation()->vertices==first,
                    "Pressure Undo did not reproduce the original result");
                check(pressure_session.redo(),"Pressure Redo failed");window.refresh_scene();window.rebuild_application_toolbar();flush();
                solve->trigger();flush();check(window.active_fem_deformation(),"Pressure Redo did not solve");
                const auto saved=directory/("fem-pressure-"+std::to_string(mode)+".prtz");
                pressure_session.document().save(saved,pressure_session.calculated_boundaries());
                const auto reopened=document::PartDocument::load(saved);
                check(reopened.body_history==pressure_session.document().body_history,"Pressure reopen changed its region or direction");
                const auto& restored=*reopened.body_history.find(beam_fem)->fem;
                const auto restored_mesh=generate_fem_volume_mesh(solid.get(),restored);
                const auto restored_result=solve_fem_linear_statics(restored_mesh,restored,{young,poisson});
                check(fem_deformed_mesh(restored_mesh,restored_result).vertices==window.active_fem_deformation()->vertices,
                    "Saved pressure did not reproduce the GUI result");
            }
            check(kernel::body_calculation_count.load()==pressure_calculations,"Pressure edits or solves recalculated CAD");
            const auto valid_result=window.active_fem_deformation()->vertices;
            auto incomplete_material=workspace::material_data(window.workspace_,beam.document_id);
            incomplete_material.properties.erase("POISSON_RATIO");
            incomplete_material.units.erase("POISSON_RATIO");
            incomplete_material.descriptions.erase("POISSON_RATIO");
            check(workspace::set_material_data(window.workspace_,beam.document_id,incomplete_material),"Incomplete material fixture did not change");
            window.refresh_scene();window.rebuild_application_toolbar();flush();
            const auto failed_revision=pressure_session.revision();
            window.solve_fem_statics();flush();
            check(!window.active_fem_deformation()&&!window.operation_activity_->active()&&
                window.operation_progress_->maximum()==100&&window.operation_progress_->value()==0,
                "Failed FEM worker left running or successful progress");
            check(window.state_->text()==QObject::tr("Set a valid Young's modulus and Poisson ratio in the Part material.")&&
                pressure_session.revision()==failed_revision,"FEM worker failure lost its diagnostic or modified the document");
            check(pressure_session.undo(),"Material Undo after FEM failure failed");
            window.refresh_scene();window.rebuild_application_toolbar();flush();
            window.solve_fem_statics();flush();
            check(window.active_fem_deformation()&&window.active_fem_deformation()->vertices==valid_result&&
                !window.operation_activity_->active(),"FEM result did not recover after failed-worker input repair");
#include "fem_thermal_ui_verification.inc"
            std::cout<<"FEM pressure GUI: body face, planar/projected Sketch, solve/display, Cancel, magnitude/reversal, Undo/Redo and native reopen passed\n";
            std::cout<<"FEM progress GUI: translated phases, actual percentage painting, busy iteration text, cached commands and failed-worker cleanup passed\n";
        }
#endif
        std::cout<<"FEM GUI: shared Body/Boolean source, entry, inspection, middle confirmation, Cancel, unchanged OK, activation, readonly CAD, Undo/Redo, save/reopen, five languages and two themes passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
} // namespace zima::app
