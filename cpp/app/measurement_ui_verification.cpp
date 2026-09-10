#include "measurement_ui_verification.hpp"
#include "assembly_workspace_window.hpp"
#include "measurement_dialog.hpp"
#include <zima/viewer/mesh_view.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/part_document.hpp>
#include <QAction>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QTabBar>
#include <iostream>

namespace zima::app {
int verify_measurement_inspector(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory){
try{
    const auto check=[](bool condition,const char* message){if(!condition)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);};
    auto part=document::PartDocument::create_default();auto box=document::PartDocument::create_box_container();box.box={10,20,30};part.history={box};
    part.physical_parameters["MASS_DENSITY"]="7850";part.physical_parameter_units["MASS_DENSITY"]="kg/m^3";
    kernel::OcctKernel kernel;const auto bodies=kernel.evaluate_history(part.kernel_operations());
    const auto path=directory/"measurement-ui.prtz";part.save(path,bodies);
    check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open measurement fixture");
    window.resize(1200,850);window.show();flush();
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
    auto* action=window.findChild<QAction*>("measureAction");
    check(view&&action&&action->isEnabled()&&!action->icon().isNull(),"Measurement toolbar action missing");
    const auto dialog=[&]()->MeasurementDialog*{
        for(auto* child:window.findChildren<QDialog*>())
            if(auto* d=dynamic_cast<MeasurementDialog*>(child);d&&d->isVisible())return d;
        return nullptr;
    };
    const auto mouse=[&](QEvent::Type type,QPointF at,Qt::MouseButton button){
        QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,
            type==QEvent::MouseButtonRelease||type==QEvent::MouseMove?Qt::NoButton:button,Qt::NoModifier);
        QApplication::sendEvent(view,&event);
    };
    const auto click=[&](QPointF at,Qt::MouseButton button){
        mouse(QEvent::MouseMove,at,Qt::NoButton);mouse(QEvent::MouseButtonPress,at,button);
        mouse(QEvent::MouseButtonRelease,at,button);flush();
    };
    const auto select=[&](viewer::CandidateKind kind){
        for(int y=20;y<view->height()-20;y+=5)for(int x=20;x<view->width()-20;x+=5){
            const QPointF at(x,y);const auto candidates=view->selection_candidates_at(at);
            if(std::ranges::none_of(candidates,[&](const auto& c){return c.kind==kind;}))continue;
            mouse(QEvent::MouseMove,at,Qt::NoButton);
            for(std::size_t i=0;i<candidates.size();++i){
                const auto offered=view->offered_candidate();
                if(offered&&offered->kind==kind){
                    mouse(QEvent::MouseButtonPress,at,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,at,Qt::LeftButton);flush();return;
                }
                mouse(QEvent::MouseButtonPress,at,Qt::RightButton);mouse(QEvent::MouseButtonRelease,at,Qt::RightButton);
            }
        }
        throw std::runtime_error("Measurement reference kind not offered by common picker");
    };
    const auto camera=view->camera_state();action->trigger();flush();
    check(dialog()&&dialog()->windowFlags().testFlag(Qt::SubWindow)&&dialog()->parentWidget()==&window,"Measurement is not an internal Properties window");
    select(viewer::CandidateKind::Vertex);
    check(dialog()->active_reference()==1&&dialog()->geometries()[0]&&dialog()->geometries()[0]->values.position,"First point did not produce coordinates and arm second field");
    click({20,20},Qt::MiddleButton);
    check(dialog()&&dialog()->active_reference()==-1&&!dialog()->inspected()[0],"Short MMB closed inspector or retained entry/highlight");
    mouse(QEvent::MouseButtonDblClick,{20,20},Qt::MiddleButton);flush();
    check(!dialog(),"Double MMB over View did not close inspector");
    check(view->camera_state()==camera,"Measurement close changed camera");
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    check(document::PartDocument::load(path).measurements.empty(),"Closing inspector implicitly saved measurement");
    action->trigger();flush();select(viewer::CandidateKind::Edge);
    check(dialog()->geometries()[0]&&dialog()->geometries()[0]->values.length,"Edge length unavailable");
    select(viewer::CandidateKind::Face);
    check(dialog()->geometries()[1]&&dialog()->geometries()[1]->values.area&&dialog()->distance(),"Second face has no area/distance");
    check(dialog()->findChild<QPushButton*>("saveMeasurement")->isEnabled(),"Valid measurement cannot be saved");
    dialog()->findChild<QPushButton*>("saveMeasurement")->click();flush();check(!dialog(),"Save did not close inspector");
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    auto stored=document::PartDocument::load(path);
    check(stored.measurements.size()==1&&stored.measurements[0].references.size()==2,"Save did not persist two references");
    auto* tree=window.findChild<QTreeWidget*>();
    QTreeWidgetItem* row{};
    for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole+3)=="document-measurement"){row=*i;break;}
    check(row,"Saved measurement not in tree");
    window.show_tree_item_properties(row);flush();
    check(dialog()&&dialog()->distance()&&dialog()->active_reference()==-1,"Saved measurement cannot reopen");
    // A broken reference remains editable through the same reference-entry control.
    const auto saved_ref=dialog()->current().references[0];
    dialog()->reject();flush();
    auto mesh=view->mesh();
    std::erase_if(mesh.edges,[&](const auto& e){return e.reference.owner_id==saved_ref.owner_id&&e.reference.semantic_key==saved_ref.semantic_key;});
    std::erase_if(mesh.original_references.edges,[&](const auto& e){return e.reference.owner_id==saved_ref.owner_id&&e.reference.semantic_key==saved_ref.semantic_key;});
    view->set_mesh(mesh,false);
    row=nullptr;for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole+3)=="document-measurement"){row=*i;break;}
    window.show_tree_item_properties(row);flush();
    check(dialog()&&!dialog()->geometries()[0]&&!dialog()->findChild<QPushButton*>("saveMeasurement")->isEnabled(),"Broken reference is silently accepted");
    dialog()->findChild<QTableWidget*>("measurementReference1")->cellClicked(0,1);select(viewer::CandidateKind::Vertex);
    check(dialog()->geometries()[0]&&dialog()->findChild<QPushButton*>("saveMeasurement")->isEnabled(),"Broken reference cannot be replaced");
    dialog()->reject();flush();
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    check(document::PartDocument::load(path).measurements[0].references[0]==saved_ref,"Cancel committed replacement");
    // The same dialog's mass/volume path is exercised by selecting a whole object.
    action->trigger();flush();select(viewer::CandidateKind::Container);
    check(dialog()->geometries()[0]&&dialog()->geometries()[0]->values.volume,"Whole object volume unavailable");
    check(std::abs(dialog()->geometries()[0]->values.volume->value-6000)<1e-8,"Object inspector has wrong volume");
    check(dialog()->geometries()[0]->values.mass&&std::abs(dialog()->geometries()[0]->values.mass->value-.0471)<1e-10,"Inspector did not use material density");
    if(qEnvironmentVariableIsSet("ZIMA_MEASUREMENT_CAPTURE"))window.grab().save(qEnvironmentVariable("ZIMA_MEASUREMENT_CAPTURE"));
    dialog()->reject();flush();
    std::cout<<"Measurement inspector picking, MMB, persistence and repair passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
}
