#include "assembly_workspace_window.hpp"
#include "section_properties_dialog.hpp"
#include "drawing_window.hpp"
#include <zima/viewer/mesh_view.hpp>
#include <QApplication>
#include <QAction>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QTreeWidget>
#include <QTimer>
#include <QEventLoop>
#include <QMessageBox>
#include <QMenu>
#include <QKeyEvent>
#include <iostream>
namespace zima::app {
int verify_sections(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& base){
    try{
        using namespace zima;const auto dir=base/"sections";std::filesystem::create_directories(dir);
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);};
        QString modal_error;QTimer catcher;QObject::connect(&catcher,&QTimer::timeout,[&]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){modal_error=box->text();box->accept();}});catcher.start(30);
        kernel::OcctKernel kernel;auto part=document::PartDocument::create_default();part.name="Section test tube";
        auto box=document::PartDocument::create_box_container();box.box={40,30,30};auto bore=document::PartDocument::create_box_container();bore.box={20,50,14};bore.combine_mode=document::CombineMode::Subtract;part.history={box,bore};
        const auto cache=kernel.evaluate_history(part.kernel_operations());const auto path=dir/"section.prtz";part.save(path,cache);
        window.showMaximized();check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open section fixture");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());auto* tree=window.findChild<QTreeWidget*>("documentTree");auto* create=window.findChild<QAction*>("createSectionAction");auto* save=window.findChild<QAction*>("saveDocumentAction");
        check(view&&tree&&create&&create->isEnabled(),"Missing Section action");view->set_standard_view(viewer::StandardView::Top);{QEventLoop animation;QTimer::singleShot(500,&animation,&QEventLoop::quit);animation.exec();}flush();
        const auto dialog=[&]{return dynamic_cast<SectionPropertiesDialog*>(window.findChild<QDialog*>("sectionProperties"));};
        const auto line=[&](bool bent=false){
            dialog()->findChild<QPushButton*>("editSectionSketch")->click();flush();check(!dialog()->isVisible(),"Sketch did not hide parent Section properties");
            {QEventLoop animation;QTimer::singleShot(500,&animation,&QEventLoop::quit);animation.exec();}
            auto* polyline=window.findChild<QAction*>("sketchPolylineAction");check(polyline&&polyline->isEnabled(),"Section has no ordinary Sketcher tools");polyline->trigger();flush();
            std::vector<QPointF> points{QPointF(view->width()*.25,view->height()*.5),QPointF(view->width()*(bent?.5:.75),view->height()*.5)};if(bent)points.push_back(QPointF(view->width()*.5,view->height()*.25));
            for(const QPointF point:points){
                QMouseEvent move(QEvent::MouseMove,point,QPointF(view->mapToGlobal(point.toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&move);
                QMouseEvent press(QEvent::MouseButtonPress,point,QPointF(view->mapToGlobal(point.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QMouseEvent release(QEvent::MouseButtonRelease,point,QPointF(view->mapToGlobal(point.toPoint())),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);flush();
            }
            auto* undo=window.findChild<QAction*>("undoAction"),*redo=window.findChild<QAction*>("redoAction");check(undo&&undo->isEnabled(),"Section sketch has no local Undo");undo->trigger();flush();check(redo->isEnabled(),"Section sketch Undo changed the model history");redo->trigger();flush();
            if(!bent){
                const auto sketch_id=dialog()->values().sketch.id;const auto positions=[&]{std::map<std::string,std::array<double,3>> result;for(const auto& p:view->mesh().points)if(p.reference.owner_id==sketch_id&&p.reference.semantic_key.starts_with("point:"))result[p.reference.semantic_key]={p.position.x,p.position.y,p.position.z};return result;};
                const auto before=positions();check(before.size()>=2,"Section sketch points are missing from View");
                // Locate the persisted endpoint in the current camera. Inference
                // may snap it away from the originally clicked screen pixel.
                const auto endpoint=std::ranges::max_element(before,[](const auto& a,const auto& b){return a.second[0]<b.second[0];});const auto& world=endpoint->second;const auto frame=dialog()->values();
                const double tx=(world[0]-frame.plane_origin.x)*frame.plane_x.x+(world[1]-frame.plane_origin.y)*frame.plane_x.y+(world[2]-frame.plane_origin.z)*frame.plane_x.z;
                const double ty=(world[0]-frame.plane_origin.x)*frame.plane_y.x+(world[1]-frame.plane_origin.y)*frame.plane_y.y+(world[2]-frame.plane_origin.z)*frame.plane_y.z;
                const QPointF middle(view->width()/2.,view->height()/2.);const auto local=[&](QPointF p){const auto ray=view->ray_at(p);return frame.sketch.intersect_ray(ray->first,ray->second).value();};
                const auto c=local(middle),x=local(middle+QPointF(100,0)),y=local(middle+QPointF(0,100));const double a=x[0]-c[0],b=y[0]-c[0],d=x[1]-c[1],e=y[1]-c[1],det=a*e-b*d;
                const QPointF start=middle+QPointF(100*((tx-c[0])*e-(ty-c[1])*b)/det,100*((ty-c[1])*a-(tx-c[0])*d)/det),end=start+QPointF(30,0);
                check(std::ranges::any_of(view->selection_candidates_at(start),[&](const auto& candidate){return candidate.semantic_key==endpoint->first;}),"Drag endpoint is not offered by the common picker");
                const auto mouse=[&](QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
                mouse(QEvent::MouseMove,start,Qt::NoButton,Qt::NoButton);mouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
                check(positions()!=before,"Dragging a point in Section sketch did not change its draft");undo->trigger();flush();check(positions()==before,"Point drag Undo escaped the Section draft");
            }
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();check(dialog()->isVisible()&&dialog()->values().sketch.segments.size()==(bent?2:1),"Finishing Section sketch lost the line");
        };
        create->trigger();flush();check(dialog()&&dialog()->parentWidget()==&window&&(dialog()->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"Section is not shared internal dialog");
        auto* scroll=dialog()->findChild<QScrollArea*>();auto* name=dialog()->findChild<QLineEdit*>("sectionName");auto* translation=dialog()->findChild<QDoubleSpinBox*>("sweepTranslation0");
        check(scroll&&scroll->widget()->isAncestorOf(name)&&scroll->widget()->isAncestorOf(translation)&&translation->mapTo(dialog(),QPoint{}).y()>name->mapTo(dialog(),QPoint{}).y()+name->height(),"Section controls are outside their scroll layout");
        const auto origin_reference=part.origin_viewer_mesh().original_references.points.front().reference;
        check(dialog()->set_reference(0,{{},origin_reference.owner_id,origin_reference.semantic_key},"Origin"),"Section rejected the shared placement reference");dialog()->changed();check(dialog()->values().placement.reference_valid&&dialog()->values().placement.references.size()==1,"Section did not resolve its ordinary placement reference");
        dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();create->trigger();flush();
        dialog()->findChild<QDoubleSpinBox*>("sweepTranslation0")->setValue(7);dialog()->findChild<QComboBox*>("sectionSketchPlane")->setCurrentIndex(1);
        check(std::abs(dialog()->values().plane_origin.x-7)<1e-7&&std::abs(dialog()->values().plane_y.z+1)<1e-7,"Section placement or own XZ plane was ignored");
        dialog()->findChild<QPushButton*>("editSectionSketch")->click();flush();window.findChild<QAction*>("cancelSectionSketchAction")->trigger();flush();check(dialog()->isVisible()&&dialog()->values().sketch.segments.empty(),"Cancel sketch did not restore parent draft");line(true);dialog()->findChild<QCheckBox*>("sectionShowCut")->setChecked(true);flush();
        window.grab().save(QString::fromStdString((dir/"section-properties.png").string()));
        dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();save->trigger();flush();check(document::PartDocument::load(path).sections.empty(),"Cancel saved section");
        create->trigger();flush();line();dialog()->findChild<QCheckBox*>("sectionShowCut")->setChecked(true);
        const QPointF point(30,30),global(view->mapToGlobal(point.toPoint()));QMouseEvent short_click(QEvent::MouseButtonPress,point,global,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier),release(QEvent::MouseButtonRelease,point,global,Qt::MiddleButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&short_click);QApplication::sendEvent(view,&release);flush();check(dialog()!=nullptr,"Short MMB committed Section");
        QMouseEvent double_click(QEvent::MouseButtonDblClick,point,global,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);QApplication::sendEvent(view,&double_click);flush();check(!dialog(),"MMB double click did not save Section");save->trigger();flush();
        auto stored=document::PartDocument::load(path);check(stored.sections.size()==1&&stored.sections.front().show_cut,"Section was not persisted");const auto section_id=stored.sections.front().id,point_id=stored.sections.front().sketch.points.front().id;
        auto* root=tree->topLevelItem(0);auto* group=root->child(1);check(group->data(0,Qt::UserRole+3)=="document-sections"&&group->childCount()==2&&group->child(0)->data(0,Qt::UserRole+3)=="document-section-normal","Sections and permanent Normal are not immediately after Origin");
        const auto menu_action=[&](QTreeWidgetItem* row,const QString& label,bool protected_row=false){
            bool chosen=false,protected_ok=true;QTimer::singleShot(0,[&]{for(auto* menu:window.findChildren<QMenu*>())if(menu->isVisible()){
                for(auto* action:menu->actions()){if(protected_row&&(action->text()==QObject::tr("Odstranit")||action->text()==QObject::tr("Vlastnosti / přejmenovat…")))protected_ok=false;
                    if(action->text()==label){chosen=true;menu->setActiveAction(action);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);return;}}
                menu->close();return;
            }});tree->customContextMenuRequested(tree->visualItemRect(row).center());flush();check(protected_ok,"Permanent Section row exposes deletion/editing");return chosen;
        };
        check(menu_action(group->child(0),QObject::tr("Aktivní"),true),"Normal cannot be activated");check(view->mesh().triangles.size()==cache.back().mesh.triangles.size(),"Normal did not restore the complete body");
        group=tree->topLevelItem(0)->child(1);menu_action(group,{},true);check(menu_action(group->child(1),QObject::tr("Aktivní")),"A-A cannot be activated");
        group=tree->topLevelItem(0)->child(1);
        window.show_tree_item_properties(group->child(1));flush();check(dialog()!=nullptr,"Cannot edit saved Section");dialog()->findChild<QDoubleSpinBox*>("sweepRotation2")->setValue(25);dialog()->findChild<QCheckBox*>("sectionShowCut")->setChecked(false);dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();save->trigger();flush();stored=document::PartDocument::load(path);
        check(stored.sections.front().id==section_id&&stored.sections.front().sketch.points.front().id==point_id,"Editing changed Section/point identity");
        check(view->mesh().triangles.size()==cache.back().mesh.triangles.size(),"Disabling cut did not restore complete body");
        window.findChild<QAction*>("undoAction")->trigger();flush();save->trigger();flush();stored=document::PartDocument::load(path);check(stored.sections.front().show_cut,"Section edit is not undoable");
        view->set_standard_view(viewer::StandardView::Isometric);{QEventLoop animation;QTimer::singleShot(500,&animation,&QEventLoop::quit);animation.exec();}flush();window.grab().save(QString::fromStdString((dir/"section-3d.png").string()));
        auto drawing=drawing::DrawingDocument::create_default();drawing.source_document_id=part.document_id;drawing.source_path=path;auto& sheet=drawing.sheets.front();
        auto parent=drawing::DrawingDocument::create_view(part.document_id,path,cache.back().mesh,drawing::ViewOrientation::Top);parent.x=150;parent.y=120;parent.name="Top";
        auto cut=drawing::DrawingDocument::create_view(part.document_id,path,cache.back().mesh);cut.x=65;cut.y=120;cut.name="A–A";sheet.views={parent,cut};const auto drawing_path=dir/"section.drwz";drawing.save(drawing_path);
        check(window.open_document_path(QString::fromStdString(drawing_path.string())),"Cannot open section drawing");flush();DrawingWindow* dw=nullptr;for(auto* w:window.findChildren<QMainWindow*>())if(auto* drawing_window=dynamic_cast<DrawingWindow*>(w)){dw=drawing_window;break;}check(dw!=nullptr,"Drawing window missing");dw->select_view_for_test(cut.id);window.findChild<QAction*>("editDrawingViewAction")->trigger();flush();
        auto* props=window.findChild<QDialog*>("drawingViewProperties");check(props!=nullptr,"Missing drawing properties");auto* combo=props->findChild<QComboBox*>("drawingSection");check(combo&&combo->count()==2,"Saved Section is missing from drawing choices");combo->setCurrentIndex(1);props->findChild<QDoubleSpinBox*>("drawingHatchSpacing")->setValue(2);flush();
        window.grab().save(QString::fromStdString((dir/"section-drawing-properties.png").string()));props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();check(!window.findChild<QDialog*>("drawingViewProperties"),"Drawing Section failed to commit");
        const auto* result=dw->document_for_test().find_view(cut.id);check(result&&result->section_id==section_id&&result->section_parent_id==parent.id,"Drawing lost section source or marker link");check(std::ranges::any_of(result->projected_edges,[](const auto& e){return e.hatch;}),"Drawing has no hatch");
        dw->select_view_for_test({});flush();dw->export_pdf(dir/"section.pdf");window.grab().save(QString::fromStdString((dir/"section-drawing.png").string()));dw->document_for_test().save(drawing_path);
        // Regenerate reads the currently open, unsaved source definition.
        const bool old_reverse=result->section_snapshot->reversed;
        check(window.open_document_path(QString::fromStdString(path.string())),"Cannot return to source Part");flush();root=tree->topLevelItem(0);group=root->child(1);window.show_tree_item_properties(group->child(1));flush();
        dialog()->findChild<QCheckBox*>("sectionReverse")->setChecked(!old_reverse);dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.open_document_path(QString::fromStdString(drawing_path.string())),"Cannot return to Drawing");flush();window.findChild<QAction*>("regenerateDrawingViewAction")->trigger();flush();
        check(dw->document_for_test().find_view(cut.id)->section_snapshot->reversed!=old_reverse,"Regenerate ignored unsaved source section edit");
        check(window.open_document_path(QString::fromStdString(path.string())),"Cannot return to Part for section removal");flush();root=tree->topLevelItem(0);group=root->child(1);auto* section_row=group->child(1);
        bool removed=false;QTimer::singleShot(0,[&]{for(auto* menu:window.findChildren<QMenu*>())if(menu->isVisible())for(auto* action:menu->actions())if(action->text()==QObject::tr("Odstranit")){removed=true;menu->setActiveAction(action);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);return;}});
        tree->customContextMenuRequested(tree->visualItemRect(section_row).center());flush();check(removed,"Cannot remove section from tree");
        check(window.open_document_path(QString::fromStdString(drawing_path.string())),"Cannot reopen linked drawing");flush();const auto prior_edges=dw->document_for_test().find_view(cut.id)->projected_edges.size();window.findChild<QAction*>("regenerateDrawingViewAction")->trigger();flush();
        check(!modal_error.isEmpty()&&dw->document_for_test().find_view(cut.id)->projected_edges.size()==prior_edges,"Missing source section silently replaced drawing");modal_error.clear();
        // Read-only source resolution and repeated occurrence rows in Assembly.
        workspace::Workspace fixture;fixture.add_part(stored,cache,path);auto assembly=assembly::AssemblyDocument::create_default();const auto aid=assembly.document_id;const auto assembly_path=dir/"section.asmz";fixture.add_assembly(assembly,assembly_path);
        const auto first=fixture.insert_open_part(aid,part.document_id,"Tube 1"),second=fixture.insert_open_part(aid,part.document_id,"Tube 2");assembly=fixture.open_assembly(aid)->session.document();assembly.find_occurrence(second)->placement.x=65;assembly.save(assembly_path);
        check(window.open_document_path(QString::fromStdString(assembly_path.string())),"Cannot open section Assembly");flush();view->set_standard_view(viewer::StandardView::Top);create->trigger();flush();line();check(dialog()!=nullptr,"Assembly Section dialog missing");auto* rows=dialog()->findChild<QTableWidget*>("sectionComponents");check(rows&&rows->rowCount()==2&&rows->item(0,0)->data(Qt::UserRole)!=rows->item(1,0)->data(Qt::UserRole),"Repeated components were merged in Section settings");
        static_cast<QComboBox*>(rows->cellWidget(1,1))->setCurrentIndex(2);dialog()->findChild<QCheckBox*>("sectionShowCut")->setChecked(true);flush();window.grab().save(QString::fromStdString((dir/"section-assembly.png").string()));dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();save->trigger();flush();
        check(assembly::AssemblyDocument::load(assembly_path).sections.size()==1,"Assembly lost Section");check(modal_error.isEmpty(),modal_error.toUtf8().constData());
        std::cout<<"Section UI: container placement, own plane, full Sketcher, local undo/redo, nested cancel, MMB, tree, Drawing hatch/PDF and repeated Assembly components passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Section UI: "<<e.what()<<'\n';return 1;}
}
}
