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
#include <QKeyEvent>
#include <QEventLoop>
#include <QTimer>
#include <QMenu>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#ifdef ZIMA_ENABLE_FEM_MESHER
#include "fem_linear_statics.hpp"
#include "fem_volume_mesh.hpp"
#endif

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
        for(auto* command:{window.sketch_action_,window.extrusion_action_,window.shell_action_,window.construction_axis_action_})check(!command->isEnabled(),"FEM enables material editing");
        check(window.fem_mode_button_->isVisible()&&!window.sheet_metal_mode_button_->isVisible(),"FEM mode buttons are incorrect");
        const auto mesh_controls=[&]{return session.document().body_history.find(fem)->fem->mesh;};
        auto* mesh_menu=window.findChild<QMenu*>("femMeshMenu");
        auto* mesh_settings=window.findChild<QAction*>("femMeshSettingsAction");
        check(mesh_menu&&mesh_menu->menuAction()->isVisible()&&mesh_settings&&mesh_settings->isEnabled(),"FEM mesh settings menu is missing");
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
            show_mesh->trigger();flush();
            check(!show_mesh->isChecked()&&view->mesh().triangles==snapshot->mesh.triangles,"FEM mesh toggle did not restore the source");
        };
        if(generate) {
            check(show_mesh&&!show_mesh->isEnabled(),"FEM offered a mesh before generation");
            check_mesh();
            check(session.revision()==settings_revision&&kernel::body_calculation_count.load()==count,"FEM meshing recalculated or changed CAD");
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
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QFile file(config.filePath(QStringLiteral("localization/%1.qt.json").arg(QString::fromLatin1(language))));check(file.open(QIODevice::ReadOnly),"FEM catalog unavailable");
            ApplicationSettings settings;settings.language=language;const auto messages=QJsonDocument::fromJson(file.readAll()).object();
            for(auto it=messages.begin();it!=messages.end();++it)settings.qt_translations[it.key()]=it.value().toString();
            apply_application_translations(application,settings);
#ifdef ZIMA_ENABLE_FEM_MESHER
            window.rebuild_application_toolbar();
            const auto* calculation=window.findChild<QMenu*>("femCalculationMenu");
            const auto* solve=window.findChild<QAction*>("femLinearStaticsAction");
            const auto* deformation=window.findChild<QAction*>("femShowDeformationAction");
            check(calculation&&solve&&deformation&&calculation->title()==settings.qt_translations.value("Calculation")&&
                solve->text()==settings.qt_translations.value("Linear statics")&&
                deformation->text()==settings.qt_translations.value("Show deformation"),"FEM calculation menu did not translate");
#endif
            for(const auto* theme:{"dark","light"}) {
                settings.theme=theme;apply_application_appearance(application,settings);flush();
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
            deformation->trigger();flush();
            check(!deformation->isChecked()&&window.viewer_->mesh().vertices==window.active_generated_fem_mesh()->vertices,
                "Turning off deformation did not restore the undeformed mesh");
        }
#endif
        std::cout<<"FEM GUI: shared Body/Boolean source, entry, inspection, middle confirmation, Cancel, unchanged OK, activation, readonly CAD, Undo/Redo, save/reopen, five languages and two themes passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
} // namespace zima::app
