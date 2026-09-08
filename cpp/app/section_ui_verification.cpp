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
        const auto line=[&]{for(const QPointF point:{QPointF(view->width()*.25,view->height()*.5),QPointF(view->width()*.75,view->height()*.5)}){
            QMouseEvent press(QEvent::MouseButtonPress,point,QPointF(view->mapToGlobal(point.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QMouseEvent release(QEvent::MouseButtonRelease,point,QPointF(view->mapToGlobal(point.toPoint())),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);flush();}};
        create->trigger();flush();check(dialog()&&dialog()->parentWidget()==&window&&(dialog()->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"Section is not shared internal dialog");line();dialog()->findChild<QCheckBox*>("sectionShowCut")->setChecked(true);flush();
        window.grab().save(QString::fromStdString((dir/"section-properties.png").string()));
        dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();save->trigger();flush();check(document::PartDocument::load(path).sections.empty(),"Cancel saved section");
        create->trigger();flush();line();dialog()->findChild<QCheckBox*>("sectionShowCut")->setChecked(true);
        const QPointF point(30,30),global(view->mapToGlobal(point.toPoint()));QMouseEvent short_click(QEvent::MouseButtonPress,point,global,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier),release(QEvent::MouseButtonRelease,point,global,Qt::MiddleButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&short_click);QApplication::sendEvent(view,&release);flush();check(dialog()!=nullptr,"Short MMB committed Section");
        QMouseEvent double_click(QEvent::MouseButtonDblClick,point,global,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);QApplication::sendEvent(view,&double_click);flush();check(!dialog(),"MMB double click did not save Section");save->trigger();flush();
        auto stored=document::PartDocument::load(path);check(stored.sections.size()==1&&stored.sections.front().show_cut,"Section was not persisted");const auto section_id=stored.sections.front().id,point_id=stored.sections.front().sketch.points.front().id;
        auto* root=tree->topLevelItem(0);auto* group=root->child(root->childCount()-1);check(group->data(0,Qt::UserRole+3)=="document-sections"&&group->childCount()==1,"Sections are not last in tree");
        window.show_tree_item_properties(group->child(0));flush();check(dialog()!=nullptr,"Cannot edit saved Section");dialog()->findChild<QDoubleSpinBox*>("sectionAngle")->setValue(25);dialog()->findChild<QCheckBox*>("sectionShowCut")->setChecked(false);dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();save->trigger();flush();stored=document::PartDocument::load(path);
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
        check(window.open_document_path(QString::fromStdString(path.string())),"Cannot return to source Part");flush();root=tree->topLevelItem(0);group=root->child(root->childCount()-1);window.show_tree_item_properties(group->child(0));flush();
        dialog()->findChild<QCheckBox*>("sectionReverse")->setChecked(!old_reverse);dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.open_document_path(QString::fromStdString(drawing_path.string())),"Cannot return to Drawing");flush();window.findChild<QAction*>("regenerateDrawingViewAction")->trigger();flush();
        check(dw->document_for_test().find_view(cut.id)->section_snapshot->reversed!=old_reverse,"Regenerate ignored unsaved source section edit");
        check(window.open_document_path(QString::fromStdString(path.string())),"Cannot return to Part for section removal");flush();root=tree->topLevelItem(0);group=root->child(root->childCount()-1);auto* section_row=group->child(0);
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
        std::cout<<"Section UI: create, line picking, cancel, MMB, edit, undo, tree, Drawing hatch/PDF and repeated Assembly components passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Section UI: "<<e.what()<<'\n';return 1;}
}
}
