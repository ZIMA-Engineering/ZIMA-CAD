#include "../tests/profile_solid_fixture.hpp"
#include "assembly_workspace_window.hpp"
#include "section_properties_dialog.hpp"
#include "section_source.hpp"
#include "drawing_window.hpp"
#include <zima/drawing/view_orientation.hpp>
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
#include <QRadioButton>
#include <QToolBar>
#include <iostream>
namespace zima::app {
int verify_sections(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& base){
    try{
        using namespace zima;const auto dir=base/"sections";std::filesystem::create_directories(dir);
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);};
        QString modal_error;QTimer catcher;QObject::connect(&catcher,&QTimer::timeout,[&]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){modal_error=box->text();box->accept();}});catcher.start(30);
        {
            SectionComponentsWidget hatch(&window);
            document::SectionComponent a{0,{12.3456789,2.3456789,1.23456789,0},true};
            document::SectionComponent b{0,{-23.456789,3.456789,-.23456789,1},true};
            const document::SectionComponent missing{2,{31.23456789,1.23456789,.3456789,2},true};
            hatch.set_components({{"a","A"},{"b","B"}},{{"a",a},{"b",b},{"missing",missing}});
            check(hatch.values().at("a")==a&&hatch.values().at("b")==b,"Opening hatch fields rounded untouched settings");
            check(hatch.values().contains("missing")&&hatch.values().at("missing")==missing,"Hatch editor dropped an unavailable component's settings");
            static_cast<QComboBox*>(hatch.cellWidget(0,1))->setCurrentIndex(2);a.mode=2;
            check(hatch.values().at("a")==a&&hatch.values().at("b")==b,"Changing cut mode rounded hatch values");
            hatch.item(0,0)->setSelected(true);hatch.item(1,0)->setSelected(true);
            static_cast<QDoubleSpinBox*>(hatch.cellWidget(0,3))->setValue(4.5);a.hatch.spacing_mm=b.hatch.spacing_mm=4.5;
            check(hatch.values().at("a")==a&&hatch.values().at("b")==b,"Bulk hatch edit changed an untouched parameter");
            static_cast<QPushButton*>(hatch.cellWidget(0,6))->click();
            a.hatch.angle=std::remainder(a.hatch.angle+90,180);b.hatch.angle=std::remainder(b.hatch.angle+90,180);
            check(hatch.values().at("a")==a&&hatch.values().at("b")==b&&hatch.values().at("missing")==missing,"Hatch reverse lost exact per-component parameters");
        }
        kernel::OcctKernel kernel;auto part=document::PartDocument::create_default();part.name="Section test tube";
        auto box=test::rectangular_feature(part,{40,30,30});auto bore=test::rectangular_feature(part,{20,50,14});bore.combine_mode=document::CombineMode::Subtract;part.history={box,bore};
        document::BodyHistoryGraph body_graph;static_cast<void>(body_graph.create_body("Tube"));
        body_graph.insert({document::PartHistoryKind::Feature,box.id});body_graph.insert({document::PartHistoryKind::Feature,bore.id});body_graph.activate({});part.set_body_history(body_graph);
        const auto cache=kernel.evaluate_history(part.kernel_operations());const auto path=dir/"section.prtz";part.save(path,cache);
        window.showMaximized();check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open section fixture");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());auto* tree=window.findChild<QTreeWidget*>("documentTree");auto* create=window.findChild<QAction*>("createSectionAction");auto* save=window.findChild<QAction*>("saveDocumentAction");
        check(view&&tree&&create&&create->isEnabled(),"Missing Section action");view->set_standard_view(viewer::StandardView::Top);{QEventLoop animation;QTimer::singleShot(1000,&animation,&QEventLoop::quit);animation.exec();}flush();
        const auto dialog=[&]{return dynamic_cast<SectionPropertiesDialog*>(window.findChild<QDialog*>("sectionProperties"));};
        check(!tree->topLevelItem(0)->child(1)->isExpanded(),"Opening a Part expanded Sections");
        check(tree->headerItem()->text(0).isEmpty(),"Tree still shows the document-type heading");
        int previous_button_x=-1;
        for(const auto* id:{"fileSettingsAction","materialAction","documentParametersAction","familyTableAction","relationsAction"}) {
            auto* button=tree->header()->findChild<QToolButton*>("tree"+QString::fromLatin1(id));
            check(button&&button->isVisible()&&!button->icon().isNull()&&button->x()>previous_button_x,"Tree document toolbar order or icon is wrong");
            previous_button_x=button->x();
        }
        const auto view_actions=window.findChild<QToolBar*>("viewToolbar")->actions();
        check(!view_actions.contains(window.findChild<QAction*>("documentParametersAction"))&&!view_actions.contains(window.findChild<QAction*>("familyTableAction")),"Document actions remain duplicated over View");
        {
            QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==box.id&&(*i)->data(0,Qt::UserRole+3)=="part-container"){row=*i;break;}
            check(row!=nullptr,"Box tree row is missing");
            tree->setCurrentItem(row);flush();
            check(view->confirmed_candidate()&&view->confirmed_candidate()->owner_id==box.id,"Single tree selection did not confirm the object");
            tree->scrollToItem(row);const auto point=tree->visualItemRect(row).center();
            QMouseEvent twice(QEvent::MouseButtonDblClick,QPointF(point),QPointF(tree->viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(),&twice);flush();
            check(std::ranges::any_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==box.id;}),"Tree double-click did not expose existing dimensions");
            check(!window.findChild<QDialog*>("primitivePropertiesDialog"),"Tree double-click opened Properties");
        }
        tree->topLevelItem(0)->child(1)->setExpanded(true);
        const auto line=[&](bool bent=false){
            dialog()->findChild<QPushButton*>("editSectionSketch")->click();flush();check(!dialog()->isVisible(),"Sketch did not hide parent Section properties");
            {QEventLoop animation;QTimer::singleShot(1000,&animation,&QEventLoop::quit);animation.exec();}
            check(!view->confirmed_candidate(),"Section Sketch retained a confirmed object");
            auto* external=window.findChild<QAction*>("sketchExternalReferenceAction");
            check(external&&external->isEnabled(),"Section Sketch external references are disabled");
            external->trigger();flush();
            bool source_offered=false;
            for(int y=8;y<view->height()&&!source_offered;y+=12)for(int x=8;x<view->width();x+=12)
                if(std::ranges::any_of(view->selection_candidates_at(QPointF(x,y)),[&](const auto& c){return c.owner_id==box.id||c.owner_id==bore.id;})){source_offered=true;break;}
            check(source_offered,"New Section Sketch has no original geometry reference candidates");
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
        check(dialog()->windowTitle()==QObject::tr("Vlastnosti řezu"),"Section Properties has an inconsistent title");
        check(!window.findChild<QAction*>("cancelSectionSketchAction"),"Section Sketch retains a separate Cancel action");
        const auto* sketch_button=dialog()->findChild<QPushButton*>("editSectionSketch");
        check(sketch_button&&sketch_button->text()==QObject::tr("Skica…"),"Section Sketch button is not localized");
        check(dialog()->height()>=std::min(800,window.height()-24),"Section properties did not use the available vertical space");
        auto* scroll=dialog()->findChild<QScrollArea*>();auto* name=dialog()->findChild<QLineEdit*>("sectionName");auto* translation=dialog()->findChild<QDoubleSpinBox*>("sweepTranslation0");
        check(scroll&&scroll->widget()->isAncestorOf(name)&&scroll->widget()->isAncestorOf(translation)&&translation->mapTo(dialog(),QPoint{}).y()>name->mapTo(dialog(),QPoint{}).y()+name->height(),"Section controls are outside their scroll layout");
        QLabel* heading{};for(auto* label:dialog()->findChildren<QLabel*>())
            if(label->text()==QObject::tr("Umístění kontejneru"))heading=label;
        check(heading && heading->mapTo(dialog(),QPoint{}).y()-name->mapTo(dialog(),QPoint{}).y()-name->height()<40,
            "Section placement has excessive spacing below its name");
        const auto origin_reference=part.origin_viewer_mesh().original_references.points.front().reference;
        const auto click_tree=[&](const std::string& id){
            QTreeWidgetItem* item{};
            {for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id){item=*it;break;}}
            check(item!=nullptr,"Section reference Tree item is missing");
            for(auto* parent=item->parent();parent;parent=parent->parent())parent->setExpanded(true);
            tree->scrollToItem(item);flush();
            const QPointF pos(tree->visualItemRect(item).center()),global(tree->viewport()->mapToGlobal(pos.toPoint()));
            for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
                QMouseEvent event(type,pos,global,Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(tree->viewport(),&event);
            }flush();
        };
        auto* origins=dialog()->findChild<QPushButton*>("containerOriginSelectionButton");
        check(origins!=nullptr,"Section has no shared Origin button");origins->click();flush();
        check(origins->isChecked(),"Section Origin button did not arm the command");
        click_tree(box.id);
        check(std::ranges::any_of(view->mesh().points,[&](const auto& point){return point.reference.owner_id==box.container_origin.id;}),
            "Section Origin button did not expose the selected container origin");
        origins->click();flush();click_tree(origin_reference.owner_id);
        check(dialog()->values().placement.reference_valid&&std::ranges::count_if(dialog()->values().placement.references,[](const auto& ref){return !ref.orientation_only;})==3,
            "Clicking the whole document Origin did not fill Section placement planes");
        for(const auto& reference:dialog()->values().placement.references)
            check(reference.owner_id==origin_reference.owner_id && reference.semantic_key.starts_with("origin:plane:"),
                "Section stored an incorrect document Origin reference");
        dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();create->trigger();flush();
        dialog()->findChild<QDoubleSpinBox*>("sweepTranslation0")->setValue(7);dialog()->findChild<QComboBox*>("sectionSketchPlane")->setCurrentIndex(1);
        check(std::abs(dialog()->values().plane_origin.x-7)<1e-7&&std::abs(dialog()->values().plane_y.z+1)<1e-7,"Section placement or own XZ plane was ignored");
        dialog()->findChild<QPushButton*>("editSectionSketch")->click();flush();window.findChild<QAction*>("finishSketchAction")->trigger();flush();check(dialog()->isVisible()&&dialog()->values().sketch.segments.empty(),"Finishing an empty Section Sketch did not return to Properties");line(true);dialog()->findChild<QCheckBox*>("sectionShowCut")->setChecked(true);flush();
        window.grab().save(QString::fromStdString((dir/"section-properties.png").string()));
        dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();save->trigger();flush();check(document::PartDocument::load(path).sections.empty(),"Cancel saved section");
        create->trigger();flush();line();
        {auto sketch=dialog()->values().sketch;sketch.apply_dimension(sketch.create_segment_dimension(sketch.segments.front().id));dialog()->set_sketch(0,sketch);}
        dialog()->findChild<QCheckBox*>("sectionShowCut")->setChecked(true);
        const QPointF point(30,30),global(view->mapToGlobal(point.toPoint()));QMouseEvent short_click(QEvent::MouseButtonPress,point,global,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier),release(QEvent::MouseButtonRelease,point,global,Qt::MiddleButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&short_click);QApplication::sendEvent(view,&release);flush();check(dialog()!=nullptr,"Short MMB committed Section");
        QMouseEvent double_click(QEvent::MouseButtonDblClick,point,global,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);QApplication::sendEvent(view,&double_click);flush();check(!dialog(),"MMB double click did not save Section");save->trigger();flush();
        auto stored=document::PartDocument::load(path);check(stored.sections.size()==1&&stored.sections.front().show_cut,"Section was not persisted");const auto section_id=stored.sections.front().id,point_id=stored.sections.front().sketch.points.front().id;
        auto* root=tree->topLevelItem(0);auto* group=root->child(1);check(group->data(0,Qt::UserRole+3)=="document-sections"&&group->childCount()==2&&group->child(0)->data(0,Qt::UserRole+3)=="document-section-normal","Sections and permanent Normal are not immediately after Origin");
        check(group->isExpanded(),"Scene refresh lost the user's Sections expansion");
        check(!(group->flags()&Qt::ItemIsUserCheckable)&&!(group->child(1)->flags()&Qt::ItemIsUserCheckable)&&!group->child(1)->data(0,Qt::CheckStateRole).isValid(),"Section tree still exposes a checkbox");
        check(!group->icon(0).isNull()&&!group->child(0)->icon(0).isNull()&&!group->child(1)->icon(0).isNull(),"Missing section icons");
        check(group->icon(0).pixmap(24).toImage()!=group->child(0)->icon(0).pixmap(24).toImage()&&group->child(0)->icon(0).pixmap(24).toImage()!=group->child(1)->icon(0).pixmap(24).toImage(),"Section icons are indistinguishable");
        auto* unchanged_row=group->child(1);unchanged_row->setToolTip(0,"Presentation change");flush();check(tree->topLevelItem(0)->child(1)->child(1)==unchanged_row,"A presentation change rebuilt the tree inside itemChanged");
        const auto menu_action=[&](QTreeWidgetItem* row,const QString& label,bool protected_row=false){
            for(auto* parent=row->parent();parent;parent=parent->parent())parent->setExpanded(true);
            bool chosen=false,protected_ok=true;QTimer::singleShot(0,[&]{for(auto* menu:window.findChildren<QMenu*>())if(menu->isVisible()){
                const bool can_create=row->data(0,Qt::UserRole+3)=="document-sections";
                if(!can_create)protected_ok&=menu->actions().front()->text()==QObject::tr("Aktivní")&&
                    (!menu->actions().front()->icon().isNull())==row->font(0).bold();
                protected_ok&=std::ranges::count_if(menu->actions(),[](auto* action){return action->text()==QObject::tr("Nový řez…");})==(can_create?1:0);
                for(auto* action:menu->actions()){if(protected_row&&(action->text()==QObject::tr("Odstranit")||action->text()==QObject::tr("Vlastnosti")||action->objectName()=="renameTreeItemAction"))protected_ok=false;
                    if(action->text()==label){chosen=true;menu->setActiveAction(action);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);return;}}
                menu->close();return;
            }});tree->customContextMenuRequested(tree->visualItemRect(row).center());flush();check(protected_ok,"Section row exposes an invalid create, delete, or edit action");return chosen;
        };
        const auto rename_section=[&](bool accept){
            auto* current=tree->topLevelItem(0)->child(1)->child(1);
            check(menu_action(current,QObject::tr("Přejmenovat…")),"Section has no independent Rename action");
            auto* rename=window.findChild<QDialog*>("renameTreeItemDialog");
            check(rename&&rename->isVisible()&&!dialog(),"Rename opened Section Properties");
            rename->findChild<QLineEdit*>("renameTreeItemName")->setText("Renamed section");
            rename->findChild<QDialogButtonBox*>()->button(accept?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();flush();
        };
        const auto name_before=window.execute_console_command("section.list").data;
        rename_section(false);check(window.execute_console_command("section.list").data==name_before,"Cancel renamed Section");
        rename_section(true);save->trigger();flush();check(document::PartDocument::load(path).sections.front().name=="Renamed section","Standalone Section rename did not persist");
        window.findChild<QAction*>("undoAction")->trigger();flush();
        check(menu_action(tree->topLevelItem(0)->child(1)->child(1),QObject::tr("Upravit")),"Section has no Edit action");
        check(!dialog(),"Section Edit opened Properties");
        check(view->mesh().triangles.size()==cache.back().mesh.triangles.size(),"Section dimension editing clipped the solid");
        const auto calculations=window.property("sectionCalculationCount").toULongLong();
        window.show_parameter_dimensions(section_id);flush();
        check(window.property("sectionCalculationCount").toULongLong()==calculations,"Showing unchanged dimensions recalculated Section");
        check(!view->mesh().dimensions.empty(),"Section Edit omitted dimensions");
        const auto dimension=stored.sections.front().sketch.dimensions.front();
        viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;
        candidate.owner_id=stored.sections.front().sketch.id;candidate.semantic_key="dimension:"+dimension.id;
        window.edit_dimension_inline(candidate);flush();
        auto* field=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(field&&field->isVisible(),"Section dimension editor did not open");
        field->setText(QString::number(std::ceil(dimension.value)+2));QMetaObject::invokeMethod(field,"returnPressed");flush();
        save->trigger();flush();

        check(std::abs(document::PartDocument::load(path).sections.front().sketch.dimensions.front().value-std::ceil(dimension.value)-2)<1e-7,"Section dimension edit did not persist");
        check(view->mesh().triangles.size()==cache.back().mesh.triangles.size(),"Dimension commit restored clipping before editing ended");
        window.findChild<QAction*>("undoAction")->trigger();flush();save->trigger();flush();
        check(document::PartDocument::load(path).sections.front().sketch.dimensions.front().value==dimension.value,"Section dimension edit is not undoable");
        window.finish_parameter_dimensions();flush();
        save->trigger();flush();
        check(document::PartDocument::load(path).sections.front().show_plane==stored.sections.front().show_plane,"Section Edit changed plane visibility");
        group=tree->topLevelItem(0)->child(1);
        check(menu_action(group->child(0),QObject::tr("Aktivní"),true),"Normal cannot be activated");check(view->mesh().triangles.size()==cache.back().mesh.triangles.size(),"Normal did not restore the complete body");
        const auto normal_revision=window.execute_console_command("section.list").data.at("revision");
        group=tree->topLevelItem(0)->child(1);
        check(menu_action(group->child(0),QObject::tr("Aktivní"),true),"Normal cannot be activated twice");
        check(window.execute_console_command("section.list").data.at("revision")==normal_revision,"Unchanged GUI Section activation added history");
        const auto plane_shown=[&]{return std::ranges::any_of(view->mesh().edges,[](const auto& edge){return edge.reference.semantic_key=="section:sketch";});};
        save->trigger();flush();const bool initially_visible=document::PartDocument::load(path).sections.front().show_plane;
        check(menu_action(tree->topLevelItem(0)->child(1)->child(1),initially_visible?QObject::tr("Skrýt rovinu řezu"):QObject::tr("Zobrazit rovinu řezu")),"Section menu lacks plane visibility toggle");
        check(plane_shown()!=initially_visible&&view->mesh().triangles.size()==cache.back().mesh.triangles.size(),"Plane visibility changed the solid or failed to update the overlay");
        save->trigger();flush();check(!document::PartDocument::load(path).sections.front().show_cut,"Showing the plane activated cutting");
        const auto plane_calculations=window.property("sectionCalculationCount").toULongLong();
        check(menu_action(tree->topLevelItem(0)->child(1)->child(1),initially_visible?QObject::tr("Zobrazit rovinu řezu"):QObject::tr("Skrýt rovinu řezu")),"Section menu did not reverse its plane visibility label");
        check(window.property("sectionCalculationCount").toULongLong()==plane_calculations,"Toggling plane visibility recalculated unchanged Section");
        group=tree->topLevelItem(0)->child(1);
        window.show_tree_item_properties(group->child(1));flush();
        check(dialog()&&!dialog()->values().show_cut&&view->mesh().triangles.size()==cache.back().mesh.triangles.size(),
            "Editing an inactive Section clipped the solid");
        dialog()->findChild<QCheckBox*>("sectionShowPlane")->setChecked(true);flush();
        check(view->mesh().triangles.size()==cache.back().mesh.triangles.size()&&
            std::ranges::any_of(view->mesh().edges,[](const auto& edge){return edge.overlay&&edge.reference.semantic_key=="section:sketch";}),
            "Section-plane preview did not show its sketch over the complete solid");
        check(std::ranges::any_of(view->mesh().edges,[](const auto& edge){return edge.overlay&&edge.color=="#00C000"&&edge.reference.semantic_key=="section:sketch";}),
            "Section-plane preview omitted through-solid hatching");
        check(std::ranges::none_of(view->mesh().points,[&](const auto& point){return point.reference.owner_id==dialog()->values().sketch.id;}),
            "Section-plane preview includes Sketch point markers");
        view->set_view_direction(document::section_frame(dialog()->values()).normal);
        {QEventLoop animation;QTimer::singleShot(1000,&animation,&QEventLoop::quit);animation.exec();flush();}
        window.grab().save(QString::fromStdString((dir/"section-plane-through-solid.png").string()));
        dialog()->findChild<QCheckBox*>("sectionShowCut")->setChecked(true);flush();
        check(view->mesh().triangles.size()==cache.back().mesh.triangles.size(),"Active Section Properties clipped the solid");
        const auto plane_edges=view->mesh().edges.size();
        dialog()->findChild<QCheckBox*>("sectionShowPlane")->setChecked(false);flush();
        check(view->mesh().edges.size()<plane_edges,"Active Section ignores Show plane toggle");
        dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        group=tree->topLevelItem(0)->child(1);menu_action(group,{},true);check(menu_action(group->child(1),QObject::tr("Aktivní")),"A-A cannot be activated");
        group=tree->topLevelItem(0)->child(1);
        window.show_tree_item_properties(group->child(1));flush();check(dialog()!=nullptr,"Cannot edit saved Section");dialog()->findChild<QDoubleSpinBox*>("sweepRotation2")->setValue(25);dialog()->findChild<QCheckBox*>("sectionShowCut")->setChecked(false);dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();save->trigger();flush();stored=document::PartDocument::load(path);
        check(stored.sections.front().id==section_id&&stored.sections.front().sketch.points.front().id==point_id,"Editing changed Section/point identity");
        check(view->mesh().triangles.size()==cache.back().mesh.triangles.size(),"Disabling cut did not restore complete body");
        window.findChild<QAction*>("undoAction")->trigger();flush();save->trigger();flush();stored=document::PartDocument::load(path);check(stored.sections.front().show_cut,"Section edit is not undoable");
        const auto cap_normal=document::section_frame(stored.sections.front()).normal;
        const auto cap_view=[&](double sign){view->set_view_direction({sign*cap_normal.x,sign*cap_normal.y,sign*cap_normal.z});QEventLoop animation;QTimer::singleShot(1000,&animation,&QEventLoop::quit);animation.exec();flush();};
        const auto green_pixels=[&]{const auto image=view->grabFramebuffer();int count=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto color=image.pixelColor(x,y);count+=color.green()>110&&color.red()<70&&color.blue()<70;}return count;};
        check(std::ranges::any_of(view->mesh().edges,[](const auto& edge){return edge.color=="#00C000"&&!edge.reference.valid();}),"Active Section lost its 3D hatch helpers");
        cap_view(-1);
        for(const auto display:{viewer::DisplayMode::Shaded,viewer::DisplayMode::ShadedWithEdges}){view->set_display_mode(display);flush();check(green_pixels()>100,"3D cut surface does not render green hatching");}
        window.grab().save(QString::fromStdString((dir/"section-3d.png").string()));
        cap_view(1);check(green_pixels()<20,"3D hatching is visible through the opposite body wall");
        auto drawing=drawing::DrawingDocument::create_default();drawing.source_document_id=part.document_id;drawing.source_path=path;auto& sheet=drawing.sheets.front();
        auto parent=drawing::DrawingDocument::create_view(part.document_id,path,cache.back().mesh,drawing::ViewOrientation::Top);parent.x=150;parent.y=120;parent.name="Top";
        auto cut=drawing::DrawingDocument::create_view(part.document_id,path,cache.back().mesh,drawing::ViewOrientation::Front);cut.x=65;cut.y=120;cut.name="A–A";sheet.views={parent,cut};const auto drawing_path=dir/"section.drwz";drawing.save(drawing_path);
        check(window.open_document_path(QString::fromStdString(drawing_path.string())),"Cannot open section drawing");flush();DrawingWindow* dw=nullptr;for(auto* w:window.findChildren<QMainWindow*>())if(auto* drawing_window=dynamic_cast<DrawingWindow*>(w)){dw=drawing_window;break;}check(dw!=nullptr,"Drawing window missing");
        for(const auto& key:{sheet.id,cut.id}) {
            QTreeWidgetItem* row{};for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==key){row=*it;break;}
            check(row!=nullptr,"Drawing sheet/view has no stable Tree identity");
            bool offered=false;QTimer::singleShot(0,[&]{for(auto* menu:window.findChildren<QMenu*>())if(menu->isVisible()){
                for(auto* action:menu->actions())if(action->objectName()=="renameTreeItemAction") {offered=true;menu->setActiveAction(action);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);return;}
                menu->close();return;
            }});tree->customContextMenuRequested(tree->visualItemRect(row).center());flush();
            check(offered,"Drawing sheet/view has no Rename action");
            auto* rename=window.findChild<QDialog*>("renameTreeItemDialog");check(rename&&rename->isVisible(),"Drawing Rename did not open");
            rename->findChild<QLineEdit*>("renameTreeItemName")->setText("Renamed drawing item");rename->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            check(key==sheet.id?dw->document_for_test().find_sheet(key)->name=="Renamed drawing item":dw->document_for_test().find_view(key)->name=="Renamed drawing item","Drawing did not refresh its renamed item");
            window.findChild<QAction*>("undoAction")->trigger();flush();
        }
        dw->select_view_for_test(cut.id);window.findChild<QAction*>("editDrawingViewAction")->trigger();flush();
        auto* props=window.findChild<QDialog*>("drawingViewProperties");check(props!=nullptr,"Missing drawing properties");auto* combo=props->findChild<QComboBox*>("drawingSection");check(combo&&combo->count()==2,"Saved Section is missing from drawing choices");combo->setCurrentIndex(1);check(!props->findChild<QCheckBox*>("drawingSectionAlign")&&props->findChild<QComboBox*>("drawingViewOrientation")->isEnabled()&&props->findChild<QPushButton*>("drawingRotateRight")->isEnabled(),"Section took ownership of view orientation");flush();
        auto* hatch_rows=dynamic_cast<SectionComponentsWidget*>(props->findChild<QTableWidget*>("sectionComponents"));check(hatch_rows&&hatch_rows->rowCount()>0,"Missing component hatch row");
        check(hatch_rows->findChildren<QCheckBox*>().empty(),"Redundant custom-hatch checkbox remains");
        static_cast<QPushButton*>(hatch_rows->cellWidget(0,6))->click();flush();const auto hatch_values=hatch_rows->values();
        check(hatch_values.begin()->second.custom_hatch&&std::abs(hatch_values.begin()->second.hatch.angle+45)<1e-9,"Reverse did not automatically enable the opposite hatch slope");
        props->findChild<QCheckBox*>("drawingViewCaption")->setChecked(true);
        auto* section_label=props->findChild<QCheckBox*>("drawingSectionLabel");check(section_label&&section_label->isChecked(),"Missing independent Section label toggle");section_label->setChecked(false);section_label->setChecked(true);flush();
        window.grab().save(QString::fromStdString((dir/"section-drawing-properties.png").string()));props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();check(!window.findChild<QDialog*>("drawingViewProperties"),"Drawing Section failed to commit");
        const auto* result=dw->document_for_test().find_view(cut.id);check(result&&result->section_id==section_id&&result->section_parent_id==parent.id,"Drawing lost section source or marker link");check(result->camera.horizontal==cut.camera.horizontal&&result->camera.vertical==cut.camera.vertical&&result->camera.depth==cut.camera.depth,"Selecting a Section changed view orientation");check(std::ranges::any_of(result->projected_edges,[](const auto& e){return e.hatch;}),"Drawing has no hatch");
        auto caption=dw->view_label_center_for_test(cut.id),label=dw->view_label_center_for_test(cut.id,true);check(caption&&label&&caption->y()<label->y(),"Section label and caption do not have independent default positions");
        auto* drawing_canvas=dw->findChild<QWidget*>("drawingCanvas");const auto drag_label=[&](QPointF from,QPointF to){
            QMouseEvent press(QEvent::MouseButtonPress,from,QPointF(drawing_canvas->mapToGlobal(from.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(drawing_canvas,&press);
            QMouseEvent move(QEvent::MouseMove,to,QPointF(drawing_canvas->mapToGlobal(to.toPoint())),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(drawing_canvas,&move);
            QMouseEvent release(QEvent::MouseButtonRelease,to,QPointF(drawing_canvas->mapToGlobal(to.toPoint())),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(drawing_canvas,&release);flush();
        };
        drag_label(*label,*label+QPointF(35,20));check(dw->document_for_test().find_view(cut.id)->section_label_position.has_value()&&!dw->document_for_test().find_view(cut.id)->caption_position,"Moving A-A moved the caption or failed to store its position");
        check(dw->view_label_center_for_test(cut.id)==caption,"A-A drag moved the caption");
        dw->select_view_for_test({});flush();dw->export_pdf(dir/"section.pdf");window.grab().save(QString::fromStdString((dir/"section-drawing.png").string()));dw->document_for_test().save(drawing_path);
        const auto reopened=drawing::DrawingDocument::load(drawing_path);check(reopened.find_view(cut.id)->section_label_position==dw->document_for_test().find_view(cut.id)->section_label_position,"A-A placement did not survive reopening");
        const auto handle=dw->annotation_handle_for_test(section_id);check(handle.has_value(),"Section arrow has no manipulation point");
        // Relative quarter turns keep the cut visible without changing the Part.
        const auto has_hatch=[&]{return std::ranges::any_of(dw->document_for_test().find_view(cut.id)->projected_edges,[](const auto& e){return e.hatch&&!e.hidden;});};
        const auto source_before=document::serialize_sections({*dw->document_for_test().find_view(cut.id)->section_snapshot});
        dw->select_view_for_test(cut.id);window.findChild<QAction*>("editDrawingViewAction")->trigger();flush();props=window.findChild<QDialog*>("drawingViewProperties");
        props->findChild<QPushButton*>("drawingRotateRight")->click();props->findChild<QPushButton*>("drawingRotateRight")->click();props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(drawing::same_view_orientation(dw->document_for_test().find_view(cut.id)->camera,drawing::standard_camera(drawing::ViewOrientation::Back))&&has_hatch(),"Rotating the cut view concealed its hatch");
        window.findChild<QAction*>("editDrawingViewAction")->trigger();flush();props=window.findChild<QDialog*>("drawingViewProperties");
        check(!props->findChild<QCheckBox*>("drawingSectionReverse"),"Redundant section side override remains");
        check(!props->findChild<QCheckBox*>("drawingHatching")&&!props->findChild<QDoubleSpinBox*>("drawingHatchAngle")&&!props->findChild<QDoubleSpinBox*>("drawingHatchSpacing"),"Duplicate hatch controls remain above the component table");
        auto* hatch_table=props->findChild<QTableWidget*>("sectionComponents");check(hatch_table&&hatch_table->columnCount()==7,"Component hatch table has no offset column");
        static_cast<QDoubleSpinBox*>(hatch_table->cellWidget(0,4))->setValue(.35);
        props->findChild<QComboBox*>("drawingViewOrientation")->setCurrentIndex(0);props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();check(has_hatch()&&std::abs(dw->document_for_test().find_view(cut.id)->section_snapshot->components.begin()->second.hatch.offset_mm-.35)<1e-9,"Component hatch offset did not persist");
        {
            const auto component=dw->document_for_test().find_view(cut.id)->section_snapshot->components.begin()->first;
            const auto execute_hatch=[&](const char* command,zima::commands::Json arguments) {
                const auto request=zima::commands::Json{{"command",command},{"arguments",std::move(arguments)}}.dump();
                const auto result=window.execute_console_command(QString::fromUtf8(request.c_str()));
                check(result.ok,(std::string(command)+": "+result.code+": "+result.message).c_str());flush();return result.data;
            };
            const auto before=execute_hatch("drawing.view.hatch.get",{{"view",cut.id}});
            check(before["items"][0]["hatch"]["offset_mm"]==.35,"CLI cannot read GUI hatch edit");
            execute_hatch("drawing.view.hatch.set",{{"view",cut.id},{"components",zima::commands::Json::array({{{"component",component},{"hatch",{{"offset_mm",.375}}}}})}});
            dw->select_view_for_test(cut.id);window.findChild<QAction*>("editDrawingViewAction")->trigger();flush();props=window.findChild<QDialog*>("drawingViewProperties");
            auto* table=props->findChild<QTableWidget*>("sectionComponents");
            auto* settings=dynamic_cast<SectionComponentsWidget*>(table);
            check(settings&&settings->values().at(component).hatch.offset_mm==.375&&std::abs(static_cast<QDoubleSpinBox*>(table->cellWidget(0,4))->value()-.375)<=.0051,"GUI properties did not preserve CLI hatch precision");
            props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            check(execute_hatch("drawing.view.hatch.get",{{"view",cut.id}})["items"][0]["hatch"]["offset_mm"]==.375,"Opening and confirming properties rounded the source hatch");
            execute_hatch("drawing.view.hatch.set",{{"view",cut.id},{"components",zima::commands::Json::array({{{"component",component},{"hatch",{{"offset_mm",.35}}}}})}});
        }
        // A console mutation clears ordinary selection; restore the view for
        // the subsequent GUI-only property actions in this scenario.
        dw->select_view_for_test(cut.id);flush();
        const auto after_style=*dw->document_for_test().find_view(cut.id)->section_snapshot;
        check(document::serialize_sections(document::parse_sections(source_before)).size()>0&&after_style.sketch.serialized()==document::parse_sections(source_before).front().sketch.serialized(),"Hatch editing changed the cutting sketch");
        // Per-view hatch visibility changes no source settings or cut geometry.
        const auto section_before_visibility=document::serialize_sections({*dw->document_for_test().find_view(cut.id)->section_snapshot});
        const auto triangle_count=dw->document_for_test().find_view(cut.id)->projected_triangles.size();
        for(bool hide:{true,false}){
            window.findChild<QAction*>("editDrawingViewAction")->trigger();flush();props=window.findChild<QDialog*>("drawingViewProperties");
            auto* table=props->findChild<QTableWidget*>("sectionComponents");static_cast<QComboBox*>(table->cellWidget(0,1))->setCurrentIndex(hide?1:0);
            if(hide){static_cast<QDoubleSpinBox*>(table->cellWidget(0,4))->setValue(.8);props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
                check(document::serialize_sections({*dw->document_for_test().find_view(cut.id)->section_snapshot})==section_before_visibility,"Cancel wrote hatch settings");
                window.findChild<QAction*>("editDrawingViewAction")->trigger();flush();props=window.findChild<QDialog*>("drawingViewProperties");table=props->findChild<QTableWidget*>("sectionComponents");static_cast<QComboBox*>(table->cellWidget(0,1))->setCurrentIndex(1);
            }
            props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            check(has_hatch()!=hide&&dw->document_for_test().find_view(cut.id)->projected_triangles.size()==triangle_count,"Hatch visibility changed the cut surface");
            check(document::serialize_sections({*dw->document_for_test().find_view(cut.id)->section_snapshot})==section_before_visibility,"Drawing hatch visibility changed source settings");
        }
        const auto source_sketch=dw->document_for_test().find_view(parent.id)->section_markers.front().sketch.serialized();
        drag_label(*handle,*handle+QPointF(35,0));
        check(dw->document_for_test().find_view(parent.id)->section_marker_offsets.contains(section_id),"Section handle did not store its paper offset");
        check(dw->document_for_test().find_view(parent.id)->section_markers.front().sketch.serialized()==source_sketch,"Dragging a section marker changed its cutting sketch");
        // Per-view trace toggles are transient until OK and do not hide the
        // actual cut or its independent A-A designation.
        check(dw->document_for_test().find_view(parent.id)->section_markers.size()==1,"New section did not offer its trace on the source view");
        dw->select_view_for_test(parent.id);window.findChild<QAction*>("editDrawingViewAction")->trigger();flush();props=window.findChild<QDialog*>("drawingViewProperties");
        auto* markers=props->findChild<QTableWidget*>("drawingSectionMarkers");check(markers&&markers->rowCount()==1&&markers->item(0,0)->checkState()==Qt::Checked,"Missing trace selection table");
        markers->item(0,0)->setCheckState(Qt::Unchecked);check(dw->document_for_test().find_view(parent.id)->section_markers.size()==1,"Trace checkbox committed before OK");
        props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        window.findChild<QAction*>("editDrawingViewAction")->trigger();flush();props=window.findChild<QDialog*>("drawingViewProperties");markers=props->findChild<QTableWidget*>("drawingSectionMarkers");markers->item(0,0)->setCheckState(Qt::Unchecked);props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(dw->document_for_test().find_view(parent.id)->section_markers.empty()&&dw->view_label_center_for_test(cut.id,true).has_value(),"Hiding trace also hid A-A or the cut");
        window.findChild<QAction*>("editDrawingViewAction")->trigger();flush();props=window.findChild<QDialog*>("drawingViewProperties");props->findChild<QTableWidget*>("drawingSectionMarkers")->item(0,0)->setCheckState(Qt::Checked);props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        dw->select_view_for_test({});dw->document_for_test().save(drawing_path);dw->export_pdf(dir/"section.pdf");window.grab().save(QString::fromStdString((dir/"section-drawing.png").string()));
        // Regenerate reads the currently open, unsaved source definition.
        const bool old_reverse=dw->document_for_test().find_view(cut.id)->section_snapshot->reversed;
        check(window.open_document_path(QString::fromStdString(path.string())),"Cannot return to source Part");flush();root=tree->topLevelItem(0);group=root->child(1);window.show_tree_item_properties(group->child(1));flush();
        const auto source_style=dialog()->values().components.begin()->second;
        check(source_style.custom_hatch&&std::abs(source_style.hatch.offset_mm-.35)<1e-9,"Drawing hatch changes did not reach source Part");
        dialog()->findChild<QCheckBox*>("sectionReverse")->setChecked(!old_reverse);dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.open_document_path(QString::fromStdString(drawing_path.string())),"Cannot return to Drawing");flush();window.findChild<QAction*>("regenerateDrawingViewAction")->trigger();flush();
        check(dw->document_for_test().find_view(cut.id)->section_snapshot->reversed!=old_reverse,"Regenerate ignored unsaved source section edit");
        check(dw->document_for_test().find_view(parent.id)->section_markers.front().reversed!=old_reverse,"Regenerate ignored the unsaved section trace direction");
        check(has_hatch(),"Changing the Part display side concealed the Drawing cut");
        check(window.open_document_path(QString::fromStdString(path.string())),"Cannot return to Part for section removal");flush();root=tree->topLevelItem(0);group=root->child(1);auto* section_row=group->child(1);
        bool removed=false;QTimer::singleShot(0,[&]{for(auto* menu:window.findChildren<QMenu*>())if(menu->isVisible())for(auto* action:menu->actions())if(action->text()==QObject::tr("Odstranit")){removed=true;menu->setActiveAction(action);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);return;}});
        group->setExpanded(true);tree->customContextMenuRequested(tree->visualItemRect(section_row).center());flush();check(removed,"Cannot remove section from tree");
        check(window.open_document_path(QString::fromStdString(drawing_path.string())),"Cannot reopen linked drawing");flush();const auto prior_edges=dw->document_for_test().find_view(cut.id)->projected_edges.size();window.findChild<QAction*>("regenerateDrawingViewAction")->trigger();flush();
        check(!modal_error.isEmpty()&&dw->document_for_test().find_view(cut.id)->projected_edges.size()==prior_edges,"Missing source section silently replaced drawing");modal_error.clear();
        // Read-only source resolution and repeated occurrence rows in Assembly.
        workspace::Workspace fixture;fixture.add_part(stored,cache,path);auto assembly=assembly::AssemblyDocument::create_default();const auto aid=assembly.document_id;const auto assembly_path=dir/"section.asmz";fixture.add_assembly(assembly,assembly_path);
        const auto first=fixture.insert_open_part(aid,part.document_id,"Tube 1"),second=fixture.insert_open_part(aid,part.document_id,"Tube 2");assembly=fixture.open_assembly(aid)->session.document();assembly.find_occurrence(second)->placement.x=65;assembly.save(assembly_path);
        check(window.open_document_path(QString::fromStdString(assembly_path.string())),"Cannot open section Assembly");flush();view->set_standard_view(viewer::StandardView::Top);create->trigger();flush();line();check(dialog()!=nullptr,"Assembly Section dialog missing");auto* rows=dialog()->findChild<QTableWidget*>("sectionComponents");check(rows&&rows->rowCount()==2&&rows->item(0,0)->data(Qt::UserRole)!=rows->item(1,0)->data(Qt::UserRole),"Repeated components were merged in Section settings");
        const auto first_key=rows->item(0,0)->data(Qt::UserRole).toString().toStdString(),second_key=rows->item(1,0)->data(Qt::UserRole).toString().toStdString();
        auto* component_rows=dynamic_cast<SectionComponentsWidget*>(rows);check(component_rows!=nullptr,"Unexpected component table");
        rows->selectAll();static_cast<QPushButton*>(rows->cellWidget(0,6))->click();
        auto styles=component_rows->values();check(styles.at(first_key).custom_hatch&&styles.at(second_key).custom_hatch&&std::abs(styles.at(first_key).hatch.angle-styles.at(second_key).hatch.angle)==90,"Multirow reverse lost alternating hatch directions");
        static_cast<QDoubleSpinBox*>(rows->cellWidget(0,3))->setValue(3.5);styles=component_rows->values();check(styles.at(first_key).hatch.spacing_mm==3.5&&styles.at(second_key).hatch.spacing_mm==3.5,"Direct multiselection hatch edit was ignored");rows->clearSelection();
        static_cast<QComboBox*>(rows->cellWidget(1,1))->setCurrentIndex(2);dialog()->findChild<QCheckBox*>("sectionShowCut")->setChecked(true);flush();window.grab().save(QString::fromStdString((dir/"section-assembly.png").string()));dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();save->trigger();flush();
        check(assembly::AssemblyDocument::load(assembly_path).sections.size()==1,"Assembly lost Section");check(modal_error.isEmpty(),modal_error.toUtf8().constData());
        // The Drawing OK transaction targets the owning Assembly Section,
        // never parameters or geometry of one of its repeated child Parts.
        auto* owning=fixture.open_assembly(aid);owning->session.commit(assembly::AssemblyDocument::load(assembly_path));
        auto shared=sections_for_assembly(owning->session.document()).front();const auto original=shared.components;
        shared.components[first_key].hatch.offset_mm=.75;shared.components[first_key].custom_hatch=true;
        const auto revision=owning->session.revision(),part_revision=fixture.open_part(part.document_id)->session.revision();
        auto commit=prepare_section_component_commit(&fixture,aid,assembly_path,shared);
        check(owning->session.revision()==revision,"Preparing hatch OK modified the Assembly");commit();
        check(owning->session.revision()==revision+1&&owning->session.document().sections.front().components.at(first_key).hatch.offset_mm==.75&&owning->session.document().sections.front().components.at(second_key)==original.at(second_key),"Hatch OK did not update the exact Assembly component");
        check(fixture.open_part(part.document_id)->session.revision()==part_revision,"Assembly hatch edit changed a child Part");
        const auto console=[&](const char* name,zima::commands::Json args=zima::commands::Json::object()) {
            const auto request=zima::commands::Json{{"command",name},{"arguments",std::move(args)}}.dump();
            const auto result=window.execute_console_command(QString::fromUtf8(request.c_str()));
            const auto error=std::string(name)+": "+result.code+": "+result.message;
            check(result.ok,error.c_str());flush();return result.data;
        };
        const auto cli_section=console("section.create",{{"path_mm",zima::commands::Json::array({zima::commands::Json::array({-50,0}),zima::commands::Json::array({100,0})})},
            {"components",zima::commands::Json::array({{{"component",first_key},{"hatch",{{"angle_degrees",12.3456789},{"spacing_mm",2.3456789}}}}})}});
        const auto cli_id=cli_section.at("object").get<std::string>();
        QTreeWidgetItem* cli_row{};
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==cli_id){cli_row=*it;break;}
        check(cli_row!=nullptr,"Console-created Section is missing from the Tree");
        window.show_tree_item_properties(cli_row);flush();check(dialog()!=nullptr,"Console-created Section cannot open Properties");
        check(dialog()->values().sketch.id==cli_section.at("sketch").get<std::string>()&&
            dialog()->values().components.at(first_key).hatch.angle==12.3456789,"Properties lost CLI Section identity or hatch precision");
        dialog()->findChild<QLineEdit*>("sectionName")->setText("CLI to Properties");
        dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog(),"Properties OK rejected a console-created Section");
        check(!tree->property("commandSelectionActive").toBool(),"Section OK left reference selection active");
        const auto cli_after=console("section.get",{{"object",cli_id}});
        check(cli_after.at("name")=="CLI to Properties"&&cli_after.at("sketch")==cli_section.at("sketch")&&
            cli_after.at("revision").get<std::uint64_t>()==cli_section.at("revision").get<std::uint64_t>()+1,"Properties did not commit one shared Section edit");
        const auto cli_components=console("section.components",{{"object",cli_id}}).at("items");
        check(std::ranges::any_of(cli_components,[&](const auto& item){return item.at("component")==first_key&&item.at("hatch").at("angle_degrees")==12.3456789&&
            item.at("hatch").at("spacing_mm")==2.3456789;}),"Properties OK rounded untouched CLI hatch values");
        const auto entities=console("sketch.entities",{{"sketch",cli_section.at("sketch")}}).at("items");
        const auto point_entity=std::ranges::find_if(entities,[](const auto& item){return item.at("kind")=="point";});
        check(point_entity!=entities.end(),"Console Section has no native point");const auto edit_point=point_entity->at("id").get<std::string>();
        const auto batch=console("section.sketch.edit",{{"object",cli_id},{"operations",zima::commands::Json::array({
            {{"command","sketch.point.move"},{"arguments",{{"point",edit_point},{"position",zima::commands::Json::array({-50,3})}}}}})}});
        check(batch.at("results").size()==1&&batch.at("changed")==true,"Console Section Sketch batch failed");
        cli_row=nullptr;for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==cli_id){cli_row=*it;break;}
        check(cli_row!=nullptr,"Batched Section disappeared from Tree");window.show_tree_item_properties(cli_row);flush();
        check(dialog()&&dialog()->values().sketch.find_point(edit_point)&&dialog()->values().sketch.find_point(edit_point)->y==3,
            "Properties did not consume the console-edited Section Sketch");
        dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(!dialog()&&!tree->property("commandSelectionActive").toBool(),"Section Cancel left reference selection active");
        console("undo");
        check(console("sketch.entity.get",{{"sketch",cli_section.at("sketch")},{"entity",edit_point}}).at("entity").at("y")==0,
            "Console Undo after Properties did not restore the whole Section Sketch batch");
        QTreeWidgetItem* component_row{};
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+3)=="part-occurrence"){component_row=*it;break;}
        check(component_row!=nullptr,"Assembly component missing for source rename");
        const auto displayed_root=tree->topLevelItem(0)->text(0);
        check(menu_action(component_row,QObject::tr("Přejmenovat…")),"Component has no source Rename action");
        auto* source_rename=window.findChild<QDialog*>("renameDocumentDialog");
        check(source_rename&&source_rename->isVisible()&&source_rename->findChild<QLineEdit*>("renameDocumentName")->text()==QString::fromStdString(path.filename().string()),
            "Component Rename targets an occurrence or owning Assembly instead of its source file");
        source_rename->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        check(tree->topLevelItem(0)->text(0)==displayed_root,"Opening source Rename switched the displayed document");
        {
            auto axis_part=document::PartDocument::create_default();auto base=test::rectangular_feature(axis_part,{30,30,30});
            auto thread=document::PartDocument::create_thread_container();thread.placement.z=-15;thread.thread.bore_length=20;thread.thread.length_forward=15;
            axis_part.history={base,thread};document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Axis test"));
            graph.insert({document::PartHistoryKind::Feature,base.id});graph.insert({document::PartHistoryKind::Feature,thread.id});axis_part.set_body_history(graph);
            const auto axis_path=dir/"axis-source.prtz";axis_part.save(axis_path,kernel.evaluate_history(axis_part.kernel_operations()));
            check(window.open_document_path(QString::fromStdString(axis_path.string())),"Cannot open thread-axis fixture");flush();
            view->set_standard_view(viewer::StandardView::Top);create->trigger();flush();dialog()->findChild<QPushButton*>("editSectionSketch")->click();flush();
            {QEventLoop animation;QTimer::singleShot(1000,&animation,&QEventLoop::quit);animation.exec();}
            window.findChild<QAction*>("sketchExternalReferenceAction")->trigger();flush();
            const QPointF center(view->width()/2.,view->height()/2.);
            const auto candidates=view->selection_candidates_at(center);
            check(std::ranges::any_of(candidates,[&](const auto& c){return c.kind==viewer::CandidateKind::Axis&&c.owner_id==thread.id;}),"Section Sketcher does not offer the perpendicular thread axis");
            // Restrict the existing command's common candidate stream to its
            // axis entry, then confirm through the real mouse event path.
            const auto filter=view->candidate_filter();
            view->set_selection_contract({viewer::CandidateKind::Axis});
            view->set_candidate_filter([filter,owner=thread.id](const auto& c){return c.owner_id==owner&&c.semantic_key=="axis:primary"&&(!filter||filter(c));},false);
            QCursor::setPos(view->mapToGlobal(center.toPoint()));flush();
            check(!view->selection_candidates_at(center).empty(),"Thread axis disappeared after command filtering");
            for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
                QMouseEvent event(type,center,QPointF(view->mapToGlobal(center.toPoint())),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
            }
            const auto axis_pick_status=window.findChild<QLabel*>("workspaceState")->text();
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            const auto refs=dialog()->values().sketch.external_references;
            if(!(refs.size()==1&&refs.front().kind==sketcher::ExternalReferenceKind::AxisPoint&&refs.front().source_owner_id==thread.id))
                throw std::runtime_error("Section Sketcher did not create the picked axis point: "+axis_pick_status.toStdString());
            for (bool segment_first : {false, true}) {
                auto draft=dialog()->values().sketch;
                const auto line=draft.add_segment(-12,4,12,4);
                dialog()->set_sketch(0,draft);
                dialog()->findChild<QPushButton*>("editSectionSketch")->click();flush();
                {QEventLoop animation;QTimer::singleShot(1000,&animation,&QEventLoop::quit);animation.exec();}
                window.findChild<QAction*>("sketchCoincidentAction")->trigger();flush();
                const auto click_candidate=[&](const std::string& key) {
                    const auto filter=view->candidate_filter();
                    view->set_candidate_filter([filter,key](const auto& c){return c.semantic_key==key&&(!filter||filter(c));},false);
                    std::optional<QPointF> location;
                    for(int y=2;y<view->height()&&!location;y+=4)for(int x=2;x<view->width();x+=4)
                        if(!view->selection_candidates_at(QPointF(x,y)).empty()){location=QPointF(x,y);break;}
                    check(location.has_value(),"C command does not offer the requested segment/axis point");
                    QCursor::setPos(view->mapToGlobal(location->toPoint()));
                    for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                        QMouseEvent event(type,*location,QPointF(view->mapToGlobal(location->toPoint())),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                        QApplication::sendEvent(view,&event);flush();
                    }
                };
                const auto point_key="external_point:"+refs.front().id, line_key="segment:"+line;
                click_candidate(segment_first?line_key:point_key);
                click_candidate(segment_first?point_key:line_key);
                window.findChild<QAction*>("finishSketchAction")->trigger();flush();
                const auto result=dialog()->values().sketch;
                check(std::ranges::any_of(result.constraints,[&](const auto& c){return c.kind==sketcher::ConstraintKind::PointOnLine&&c.geometry_id==line;}),
                    "C command did not bind the segment to the external axis point");
            }
            dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            for(const auto* type:{"part","assembly"}) {
                window.findChild<QAction*>("newDocumentAction")->trigger();flush();
                auto* create=window.findChild<QDialog*>("newDocumentDialog");check(create,"New template dialog missing");
                create->findChild<QLineEdit*>("newDocumentFileName")->setText(QString::fromStdString(std::string("axis-template-")+type+document::PartDocument::create_default().document_id));
                for(auto* radio:create->findChildren<QRadioButton*>())radio->setChecked(radio->property("documentType").toString()==type);
                create->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
                check(!window.findChild<QDialog*>("newDocumentDialog"),"New template creation failed");
                check(window.findChild<QAction*>(std::string(type)=="part"?"boxAction":"insertComponentAction")->isEnabled(),"Start template lost its active Body/component context");
            }
        }
        std::cout<<"Section UI: container placement, own plane, full Sketcher, local undo/redo, nested cancel, MMB, tree, Drawing hatch/PDF, thread axis point and repeated Assembly components passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Section UI: "<<e.what()<<'\n';return 1;}
}
}
