#include "derived_copy_dialog.hpp"
#include "component_properties_dialog.hpp"
#include "shaft_thread_dialog.hpp"
#include "body_properties_dialog.hpp"
#include "sketch_properties_dialog.hpp"
#include "history_tree_widget.hpp"
#include "helical_sweep_dialog.hpp"
#include "sweep2d_dialog.hpp"
#include "assembly_workspace_window.hpp"
#include "construction_reference_candidate_policy.hpp"
#include "construction_properties_dialog.hpp"
#include "primitive_properties_dialog.hpp"
#include "drawing_window.hpp"
#include "resource_icon.hpp"
#include "tree_reference_state.hpp"

#include <zima/ui/reference_cell.hpp>
#include <zima/viewer/mesh_view.hpp>

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QFileDialog>
#include <QTemporaryDir>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPalette>
#include <QQuaternion>
#include <QPointer>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QRadioButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QSurfaceFormat>
#include <QStyleFactory>
#include <QTabBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUuid>
#include <QWidget>

#include <iostream>
#include <filesystem>
#include <set>
#include <tuple>

namespace {

bool verify(bool condition, const char* message) {
    if (!condition) std::cerr << "Startup contract failed: " << message << '\n';
    return condition;
}

std::size_t part_insertion_marker_count(QTreeWidget* tree) {
    std::size_t count = 0;
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        const auto role = (*it)->data(0, Qt::UserRole + 3).toString();
        if (role == "part-insert-here" || role == "part-body-insert-here") ++count;
    }
    return count;
}

bool contains_rendered_geometry(const QImage& image) {
    if (image.isNull() || image.width() < 2 || image.height() < 2) return false;
    const QColor background = image.pixelColor(0, 0);
    std::size_t sampled{};
    std::size_t changed{};
    for (int y = 0; y < image.height(); y += 4) {
        for (int x = 0; x < image.width(); x += 4) {
            const QColor pixel = image.pixelColor(x, y);
            const int distance = std::abs(pixel.red() - background.red()) +
                std::abs(pixel.green() - background.green()) +
                std::abs(pixel.blue() - background.blue());
            ++sampled;
            if (distance > 24) ++changed;
        }
    }
    return sampled != 0 && changed * 20 > sampled;
}

bool contains_orange_hover(const QImage& image) {
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.red() >= 225 && pixel.green() >= 80 &&
                pixel.green() <= 175 && pixel.blue() <= 55) return true;
        }
    }
    return false;
}

bool contains_cyan_selection(const QImage& image) {
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.red() <= 80 && pixel.green() >= 175 &&
                pixel.blue() >= 190) return true;
        }
    }
    return false;
}

int verify_history_tree_drag(QApplication& application,const std::filesystem::path& test_directory) {
    // Drag real Tree rows, persist the order, and verify one-step Undo.
    {
        auto document=zima::document::PartDocument::create_default();
        auto a=zima::document::PartDocument::create_box_container();
        auto b=zima::document::PartDocument::create_box_container();
        auto c=zima::document::PartDocument::create_box_container();
        b.placement.x=30;c.placement.x=60;
        document.history={a,b,c};
        zima::kernel::OcctKernel kernel;
        const auto bodies=kernel.evaluate_history(document.kernel_operations());
        const auto part_path=test_directory / "history-drag.prtz";
        document.save(part_path,bodies);
        zima::app::AssemblyWorkspaceWindow drag_window(QString::fromStdString(test_directory.string()));
        drag_window.resize(1000,800);drag_window.show();
        const auto drag=[&](const std::string& moved,const std::string& last) {
            auto* tree=drag_window.findChild<QTreeWidget*>("documentTree");
            tree->collapseAll();tree->topLevelItem(0)->setExpanded(true);
            application.processEvents();
            QTreeWidgetItem* source=nullptr;QTreeWidgetItem* target=nullptr;
            for (QTreeWidgetItemIterator i(tree);*i;++i) {
                const auto kind=(*i)->data(0,Qt::UserRole+3).toString();
                if (kind!="part-container" && kind!="part-occurrence" && kind!="assembly-occurrence") continue;
                const auto id=(*i)->data(0,Qt::UserRole).toString().toStdString();
                if (id==moved) source=*i;
                if (id==last) target=*i;
            }
            if (!source || !target) return false;
            tree->scrollToItem(target);application.processEvents();
            const auto start=tree->visualItemRect(source).center();
            const auto end=tree->visualItemRect(target).bottomLeft()+QPoint(30,0);
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(start),QPointF(tree->viewport()->mapToGlobal(start)),
                Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(),&press);
            QMouseEvent move(QEvent::MouseMove,QPointF(end),QPointF(tree->viewport()->mapToGlobal(end)),
                Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(),&move);
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(end),QPointF(tree->viewport()->mapToGlobal(end)),
                Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(),&release);
            application.processEvents();return true;
        };
        const auto save=[&] {
            auto* action=drag_window.findChild<QAction*>("saveDocumentAction");
            QTimer modal_guard;
            QObject::connect(&modal_guard,&QTimer::timeout,&drag_window,[] {
                if (auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    std::cerr << "History save failure: " << message->text().toStdString() << '\n';
                    message->reject();
                }
            });
            modal_guard.start(100);
            if (action) action->trigger();
            application.processEvents();
        };
        if (!verify(drag_window.open_document_path(QString::fromStdString(part_path.string())) && drag(a.id,c.id),
                "Part history drag fixture failed")) return 1;
        save();
        auto reordered=zima::document::PartDocument::load(part_path);

        if (!verify(reordered.history.size()==3 && reordered.history.back().id==a.id &&
                reordered.history_order.back().id==a.id,"Part Tree drag did not persist calculation order")) return 1;
        auto* undo=drag_window.findChild<QAction*>("undoAction");
        if (!verify(undo!=nullptr,"History drag Undo is missing")) return 1;
        undo->trigger();save();
        reordered=zima::document::PartDocument::load(part_path);
        if (!verify(reordered.history.front().id==a.id,"History drag was not one Undo transaction")) return 1;

        auto assembly=zima::assembly::AssemblyDocument::create_default();
        auto first=zima::assembly::AssemblyDocument::create_part_occurrence("First",document.document_id,part_path,bodies.back());
        auto second=zima::assembly::AssemblyDocument::create_part_occurrence("Second",document.document_id,part_path,bodies.back());
        auto third=zima::assembly::AssemblyDocument::create_part_occurrence("Third",document.document_id,part_path,bodies.back());
        assembly.components={first,second,third};
        zima::assembly::ComponentDependency dependency;
        dependency.dependency_id="drag-dependency";
        dependency.prerequisite_occurrence_id=first.occurrence_id;
        dependency.dependent_occurrence_id=third.occurrence_id;
        dependency.kind=zima::assembly::ComponentDependencyKind::ExternalSketchReference;
        assembly.dependencies.push_back(dependency);
        const auto assembly_path=test_directory / "history-drag.asmz";
        assembly.save(assembly_path);
        if (!verify(drag_window.open_document_path(QString::fromStdString(assembly_path.string())) &&
                drag(first.occurrence_id,third.occurrence_id),"Assembly drag fixture failed")) return 1;
        save();
        auto changed=zima::assembly::AssemblyDocument::load(assembly_path);
        if (!verify(changed.components.front().occurrence_id==first.occurrence_id,
                "Assembly drag crossed an external Sketch dependency")) return 1;
        if (!verify(drag(second.occurrence_id,third.occurrence_id),"Independent Assembly drag failed")) return 1;
        save();changed=zima::assembly::AssemblyDocument::load(assembly_path);
        if (!verify(changed.components.back().occurrence_id==second.occurrence_id,
                "Independent Assembly component order was not persisted")) return 1;
        drag_window.close();application.processEvents();
    }

    return 0;
}

int verify_derived_copy_commands(QApplication& application,zima::app::AssemblyWorkspaceWindow& window,
        const std::filesystem::path& directory) {
    using namespace zima;
    auto part=document::PartDocument::create_default();auto box=document::PartDocument::create_box_container();box.box={8,6,4};box.placement.x=4;
    part.history={box};document::BodyHistoryGraph graph;const auto source=graph.create_body("Zdroj");
    graph.insert({document::PartHistoryKind::Feature,box.id});graph.activate({});part.set_body_history(graph);
    kernel::OcctKernel kernel;const auto calculated=kernel.evaluate_history(part.kernel_operations());
    const auto path=directory/"mirror-ui.prtz";part.save(path,calculated);
    window.resize(1200,960);window.show();
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    if(!verify(window.open_document_path(QString::fromStdString(path.string())),"Mirror Part fixture did not open"))return 1;
    flush();auto* tree=window.findChild<QTreeWidget*>("documentTree");
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
    auto* action=window.findChild<QAction*>("mirrorAction");auto* save=window.findChild<QAction*>("saveDocumentAction");
    const auto row=[&](const std::string& id,const char* kind)->QTreeWidgetItem* {
        for(QTreeWidgetItemIterator it(tree);*it;++it)
            if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&(*it)->data(0,Qt::UserRole+3).toString()==kind)return *it;
        return nullptr;
    };
    if(!verify(action&&action->isEnabled()&&!action->icon().isNull()&&view,"Mirror action/icon missing"))return 1;
    tree->setCurrentItem(row(source,"part-body"));flush();action->trigger();flush();
    auto* dialog=dynamic_cast<app::DerivedCopyDialog*>(window.findChild<QDialog*>("mirrorDialog"));
    if(!verify(dialog&&dialog->derived_copy.source_id==source,"Mirror did not prefill selected body"))return 1;
    if(!verify(part_insertion_marker_count(tree)==0,"Mirror creation retained the body-level insertion marker"))return 1;
    if(!verify(dialog->parentWidget()==&window&&(dialog->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"Mirror is not an internal properties window"))return 1;
    if(!verify(!dialog->buttons()->button(QDialogButtonBox::Apply),"Mirror exposes Apply"))return 1;
    const auto id=dialog->pending.id;
    dialog->findChild<QDoubleSpinBox*>("sweepTranslation0")->setValue(-2);
    dialog->findChild<QPushButton*>("mirrorPlane_yz")->click();flush();
    window.grab().save(QString::fromStdString((directory/"mirror-ui.png").string()));
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    if(auto* failed=window.findChild<QDialog*>("mirrorDialog")){for(auto* label:failed->findChildren<QLabel*>())std::cerr<<label->text().toStdString()<<"\n";return 1;}
    save->trigger();flush();auto stored=document::PartDocument::load(path);
    if(!verify(stored.body_history.find(id)&&stored.body_history.find(id)->derived_copy->source_id==source,"Body Mirror was not persisted"))return 1;
    auto bodies=kernel.evaluate_history(stored.kernel_operations());
    if(!verify(std::abs(bodies.back().volume-384)<1e-7&&bodies.back().body_inputs.contains(id),"Mirror is not a selectable full body"))return 1;
    tree->setCurrentItem(row(id,"part-body"));flush();
    if(!verify(view->confirmed_candidate()&&view->confirmed_candidate()->owner_id==id,"Tree did not confirm the Mirror body"))return 1;
    window.show_tree_item_properties(row(id,"part-body"));flush();
    dialog=dynamic_cast<app::DerivedCopyDialog*>(window.findChild<QDialog*>("mirrorDialog"));
    if(!verify(dialog&&dialog->pending.id==id,"Mirror edit did not reopen the same dialog"))return 1;
    for(const auto& ref:view->mesh().triangle_references)
        if(!verify(ref.owner_id!=id,"Mirror edit displays final output instead of rollback input"))return 1;
    dialog->findChild<QDoubleSpinBox*>("sweepTranslation0")->setValue(90);
    dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();save->trigger();flush();
    stored=document::PartDocument::load(path);
    if(!verify(stored.body_history.find(id)->scope.placement.x==-2,"Cancel committed Mirror placement"))return 1;
    window.show_tree_item_properties(row(id,"mirror-source"));flush();
    bool source_dialog=false;for(auto* open:window.findChildren<QDialog*>())if(dynamic_cast<app::PrimitivePropertiesDialog*>(open)&&open->isVisible())source_dialog=true;
    if(!verify(source_dialog,"Mirror source link did not open source properties"))return 1;
    for(auto* open:window.findChildren<QDialog*>())if(open->isVisible())open->reject();flush();
    // Both independent bodies remain valid Boolean operands.
    auto boolean_graph=stored.body_history;static_cast<void>(boolean_graph.create_boolean("Součet",kernel::BodyCombination::Add,source,id));
    stored.set_body_history(boolean_graph);bodies=kernel.evaluate_history(stored.kernel_operations());
    if(!verify(std::abs(bodies.back().volume-384)<1e-7,"Mirror body cannot participate in Boolean"))return 1;
    auto* pattern_action=window.findChild<QAction*>("patternAction");
    if(!verify(pattern_action&&pattern_action->isEnabled()&&!pattern_action->icon().isNull(),"Pattern command/icon missing"))return 1;
    tree->setCurrentItem(row(source,"part-body"));flush();pattern_action->trigger();flush();
    dialog=dynamic_cast<app::DerivedCopyDialog*>(window.findChild<QDialog*>("patternDialog"));
    if(!verify(dialog&&dialog->derived_copy.source_id==source&&dialog->derived_copy.pattern,"Pattern did not prefill its source"))return 1;
    if(!verify(part_insertion_marker_count(tree)==0,"Pattern creation retained the body-level insertion marker"))return 1;
    const auto pattern_id=dialog->pending.id;
    auto* mode=dialog->findChild<QComboBox*>("patternMode");auto* refs=dialog->findChild<QTableWidget*>("mirrorReferences");
    if(!verify(mode&&refs->isRowHidden(0),"Linear Pattern displays circular axis input"))return 1;
    const auto retained_axis=dialog->derived_copy.reference;
    dialog->findChild<QDoubleSpinBox*>("sweepRotation2")->setValue(90);
    dialog->findChild<QDoubleSpinBox*>("patternSpacing")->setValue(20);
    mode->setCurrentIndex(1);flush();
    if(!verify(!refs->isRowHidden(0)&&dialog->findChild<QPushButton*>("mirrorPlane_z")->isVisible(),"Circular Pattern cannot select its axis"))return 1;
    mode->setCurrentIndex(0);flush();
    if(!verify(dialog->derived_copy.reference==retained_axis,"Mode switch discarded the circular axis"))return 1;
    window.grab().save(QString::fromStdString((directory/"pattern-ui.png").string()));
    const QPointF middle(30,30);const auto global=QPointF(view->mapToGlobal(middle.toPoint()));
    QMouseEvent short_press(QEvent::MouseButtonPress,middle,global,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
    QMouseEvent short_release(QEvent::MouseButtonRelease,middle,global,Qt::MiddleButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(view,&short_press);QApplication::sendEvent(view,&short_release);flush();
    if(!verify(dialog->isVisible(),"Short middle click committed Pattern"))return 1;
    QMouseEvent confirm(QEvent::MouseButtonDblClick,middle,global,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
    QApplication::sendEvent(view,&confirm);flush();
    if(auto* failed=window.findChild<QDialog*>("patternDialog")){for(auto* label:failed->findChildren<QLabel*>())std::cerr<<label->text().toStdString()<<"\n";return 1;}
    save->trigger();flush();stored=document::PartDocument::load(path);
    const auto* pattern_body=stored.body_history.find(pattern_id);
    if(!verify(pattern_body&&pattern_body->derived_copy->pattern->count==4&&std::abs(pattern_body->derived_copy->pattern->direction.y-1)<1e-7,"Pattern did not persist local direction/count"))return 1;
    bodies=kernel.evaluate_history(stored.kernel_operations());
    if(!verify(std::abs(bodies.back().body_outputs.at(pattern_id).volume-576)<1e-7,"Pattern does not produce a full body"))return 1;
    window.show_tree_item_properties(row(pattern_id,"part-body"));flush();
    dialog=dynamic_cast<app::DerivedCopyDialog*>(window.findChild<QDialog*>("patternDialog"));
    if(!verify(dialog!=nullptr,"Pattern editing uses a different dialog"))return 1;
    dialog->findChild<QSpinBox*>("patternCount")->setValue(7);dialog->reject();flush();save->trigger();flush();
    if(!verify(document::PartDocument::load(path).body_history.find(pattern_id)->derived_copy->pattern->count==4,"Cancel changed Pattern count"))return 1;
    // Command-first workflow arms the shared Tree/View source field.
    tree->clearSelection();view->clear_selection();pattern_action->trigger();flush();
    dialog=dynamic_cast<app::DerivedCopyDialog*>(window.findChild<QDialog*>("patternDialog"));
    if(!verify(dialog&&dialog->derived_copy.source_id.empty(),"Command-first Pattern reused stale selection"))return 1;
    dialog->request_input(1);tree->setCurrentItem(row(source,"part-body"));flush();
    if(!verify(dialog->derived_copy.source_id==source,"Command-first source selection failed"))return 1;
    dialog->reject();flush();
    auto assembly=assembly::AssemblyDocument::create_default();
    auto occurrence=assembly::AssemblyDocument::create_part_occurrence("Zdroj",part.document_id,path,calculated.back());
    occurrence.placement.x=20;assembly.components={occurrence};const auto assembly_path=directory/"mirror-ui.asmz";assembly.save(assembly_path);
    if(!verify(window.open_document_path(QString::fromStdString(assembly_path.string())),"Mirror Assembly fixture did not open"))return 1;
    flush();tree->setCurrentItem(row(occurrence.occurrence_id,"part-occurrence"));flush();action->trigger();flush();
    dialog=dynamic_cast<app::DerivedCopyDialog*>(window.findChild<QDialog*>("mirrorDialog"));
    if(!verify(dialog&&dialog->derived_copy.source_id==occurrence.occurrence_id,"Mirror did not prefill selected component"))return 1;
    const auto assembly_mirror=dialog->pending.id;dialog->findChild<QPushButton*>("mirrorPlane_yz")->click();flush();
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();save->trigger();flush();
    const auto saved_assembly=assembly::AssemblyDocument::load(assembly_path);
    if(!verify(saved_assembly.find_occurrence(assembly_mirror)&&saved_assembly.find_occurrence(assembly_mirror)->derived_copy,"Assembly Mirror not persisted"))return 1;
    const auto before=saved_assembly.find_occurrence(assembly_mirror)->calculated_source.mesh.vertices.front();
    if(!verify(std::abs(before.x+calculated.back().mesh.vertices.front().x+20)<1e-7,"Assembly Mirror used the wrong source placement"))return 1;
    window.show_tree_item_properties(row(assembly_mirror,"part-occurrence"));flush();
    dialog=dynamic_cast<app::DerivedCopyDialog*>(window.findChild<QDialog*>("mirrorDialog"));
    if(!verify(dialog&&dialog->pending.id==assembly_mirror,"Assembly Mirror container properties missing"))return 1;
    dialog->reject();flush();
    tree->setCurrentItem(row(occurrence.occurrence_id,"part-occurrence"));flush();pattern_action->trigger();flush();
    dialog=dynamic_cast<app::DerivedCopyDialog*>(window.findChild<QDialog*>("patternDialog"));
    if(!verify(dialog&&dialog->derived_copy.source_id==occurrence.occurrence_id,"Assembly Pattern source prefill failed"))return 1;
    const auto group_id=dialog->pending.id;dialog->findChild<QSpinBox*>("patternCount")->setValue(3);
    dialog->findChild<QDoubleSpinBox*>("patternSpacing")->setValue(40);dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();save->trigger();flush();
    const auto patterned_assembly=assembly::AssemblyDocument::load(assembly_path);
    const auto* group=patterned_assembly.find_occurrence(group_id);
    if(!verify(group&&group->derived_copy->pattern&&group->nested_snapshot.size()==2,"Assembly Pattern occurrence group missing"))return 1;
    const auto copy_path=assembly::InstancePath{}.child(group_id).child("copy-1").encoded();QTreeWidgetItem* copy_row{};
    for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+1).toString().toStdString()==copy_path){copy_row=*it;break;}
    if(!verify(copy_row!=nullptr,"Pattern copy is absent from the Assembly tree"))return 1;
    window.show_tree_item_properties(copy_row);flush();
    bool linked_properties=false;for(auto* open:window.findChildren<QDialog*>())if(open->isVisible())linked_properties=true;
    if(!verify(linked_properties,"Pattern copy properties did not resolve its source"))return 1;
    for(auto* open:window.findChildren<QDialog*>())if(open->isVisible())open->reject();flush();
    std::cout<<"Mirror and Pattern UI contracts passed\n";return 0;
}

int verify_sweep2d_command(QApplication& application,zima::app::AssemblyWorkspaceWindow& window,
        const std::filesystem::path& directory) {
    auto doc=zima::document::PartDocument::create_default();const auto path=directory/"sweep2d-ui.prtz";doc.save(path);
    window.show();if(!verify(window.open_document_path(QString::fromStdString(path.string())),"Cannot open sweep2d fixture"))return 1;
    application.processEvents();auto* action=window.findChild<QAction*>("sweep2dAction");
    if(!verify(action&&action->isEnabled(),"2D Sweep command missing"))return 1;
    action->trigger();application.processEvents();
    auto* dialog=dynamic_cast<zima::app::Sweep2DDialog*>(window.findChild<QDialog*>("sweep2dDialog"));
    if(!verify(dialog!=nullptr,"2D Sweep properties did not open"))return 1;
    const auto feature_id=dialog->pending.id;
    auto* add_operation=dialog->findChild<QPushButton*>("primitiveAddOperation");
    auto* subtract_operation=dialog->findChild<QPushButton*>("primitiveSubtractOperation");
    if(!verify(add_operation&&subtract_operation&&add_operation->isChecked(),"Sweep operation buttons missing"))return 1;
    subtract_operation->click();
    if(!verify(dialog->pending.combine_mode==zima::document::CombineMode::Subtract&&subtract_operation->isChecked()&&!add_operation->isChecked(),"Sweep Subtract button did not select operation"))return 1;
    add_operation->click();
    if(!verify(dialog->pending.combine_mode==zima::document::CombineMode::Add&&add_operation->isChecked()&&!subtract_operation->isChecked(),"Sweep Add button did not select operation"))return 1;

    auto* placement=dialog->findChild<QTableWidget*>("sweepPlacementReferences");auto* orientation=dialog->findChild<QTableWidget*>("sweepPlacementOrientation");
    auto* placement_view=dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
    if(!verify(placement&&placement->rowCount()>=1&&placement->rowCount()<=3&&orientation&&orientation->rowCount()==2&&placement_view,"Sweep has no standard placement controls"))return 1;
    const auto camera=placement_view->camera_state();
    dialog->findChild<QDoubleSpinBox*>("sweepTranslation0")->setValue(12);
    dialog->findChild<QDoubleSpinBox*>("sweepTranslation1")->setValue(-3);
    dialog->findChild<QDoubleSpinBox*>("sweepTranslation2")->setValue(8);
    dialog->findChild<QDoubleSpinBox*>("sweepRotation1")->setValue(25);application.processEvents();
    if(!verify(dialog->pending.placement.x==12&&dialog->pending.placement.y==-3&&dialog->pending.placement.z==8&&std::abs(dialog->pending.placement.rotation_y-25)<1e-6&&placement_view->camera_state()==camera,"Sweep placement fields did not update or moved camera"))return 1;
    auto guide=zima::sketcher::Sketch::from_serialized(dialog->pending.sweep2d.path_sketch);
    static_cast<void>(guide.add_segment(0,0,0,10));dialog->set_sketch(0,guide);
    auto section=zima::sketcher::Sketch::from_serialized(dialog->pending.sweep2d.sketch_data(1));
    static_cast<void>(section.add_circle(0,0,2));dialog->set_sketch(1,section);
    auto* finish=window.findChild<QAction*>("finishSketchAction");
    auto* profiles=dialog->findChild<QTableWidget*>("sweep2dProfiles");
    if(!verify(profiles&&profiles->rowCount()==2,"Path endpoints did not offer profile sketches"))return 1;
    dialog->findChild<QPushButton*>("sweep2dStationSketch1")->click();application.processEvents();
    if(!verify(finish->isEnabled()&&dialog->pending.sweep2d.profiles.size()==2,"End profile button did not create its owned Sketch"))return 1;
    finish->trigger();application.processEvents();
    auto last_section=zima::sketcher::Sketch::from_serialized(dialog->pending.sweep2d.sketch_data(2));
    static_cast<void>(last_section.add_circle(0,0,3));dialog->set_sketch(2,last_section);

    for(unsigned stage=0;stage<3;++stage){
        dialog->findChild<QPushButton*>(stage==0?"sweep2dSketch0":stage==1?"sweep2dStationSketch0":"sweep2dStationSketch1")->click();application.processEvents();
        if(!verify(!dialog->isVisible()&&finish&&finish->isEnabled(),"2D Sweep owned Sketch did not enter Sketcher"))return 1;
        const auto frame=zima::sketcher::Sketch::from_serialized(dialog->pending.sweep2d.sketch_data(stage));
        QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
        const auto camera_frame=placement_view->camera_state();
        const auto direction=QQuaternion(camera_frame[0],camera_frame[1],camera_frame[2],camera_frame[3]).inverted().rotatedVector(QVector3D(0,0,1));
        const auto n=frame.resolved_normal;
        const double cosine=std::abs(direction.x()*n.x+direction.y()*n.y+direction.z()*n.z)/std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
        if(!verify(cosine>1-1e-6,"Sweep Sketch entry did not align camera to its resolved plane"))return 1;
        finish->trigger();application.processEvents();if(!verify(dialog->isVisible(),"Sketch did not return to pending 2D Sweep"))return 1;
    }
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
    if(!verify(!dialog->isVisible(),"2D Sweep OK did not commit"))return 1;
    auto* save=window.findChild<QAction*>("saveDocumentAction");save->trigger();application.processEvents();
    auto stored=zima::document::PartDocument::load(path);
    if(!verify(stored.history.back().placement.x==12&&stored.history.back().placement.y==-3&&stored.history.back().placement.z==8&&std::abs(stored.history.back().placement.rotation_y-25)<1e-6,"Sweep placement not persisted"))return 1;
    if(!verify(stored.history.size()==1&&stored.history.back().id==feature_id,"2D Sweep history not saved"))return 1;
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* item=nullptr;int sketches=0;
    for(QTreeWidgetItemIterator i(tree);*i;++i){if((*i)->data(0,Qt::UserRole+3).toString()=="part-sweep2d-sketch")++sketches;
        if((*i)->data(0,Qt::UserRole).toString().toStdString()==feature_id&&(*i)->data(0,Qt::UserRole+3).toString()=="part-container")item=*i;}
    if(!verify(item&&sketches==3,"Owned 2D Sweep Sketches absent from Tree"))return 1;
    for(unsigned stage=0;stage<3;++stage) {
        QTreeWidgetItem* sketch_item{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)
            if((*i)->data(0,Qt::UserRole+3).toString()=="part-sweep2d-sketch" &&
                (*i)->data(0,Qt::UserRole+6).toUInt()==stage){sketch_item=*i;break;}
        window.show_tree_item_properties(sketch_item);application.processEvents();
        if(!verify(finish->isEnabled(),"Embedded Tree Sketch did not activate"))return 1;
        finish->trigger();application.processEvents();
        auto* reopened=dynamic_cast<zima::app::Sweep2DDialog*>(window.findChild<QDialog*>("sweep2dDialog"));
        if(!verify(reopened && reopened->isVisible(),"Embedded Tree Sketch did not return to properties"))return 1;
        reopened->buttons()->button(QDialogButtonBox::Cancel)->click();application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    item=nullptr;
    for(QTreeWidgetItemIterator i(tree);*i;++i)
        if((*i)->data(0,Qt::UserRole).toString().toStdString()==feature_id &&
            (*i)->data(0,Qt::UserRole+3).toString()=="part-container"){item=*i;break;}
    window.show_tree_item_properties(item);application.processEvents();
    dialog=dynamic_cast<zima::app::Sweep2DDialog*>(window.findChild<QDialog*>("sweep2dDialog"));
    if(!verify(dialog!=nullptr,"2D Sweep edit did not reopen same dialog"))return 1;
    dialog->findChild<QDoubleSpinBox*>("sweepTranslation0")->setValue(99);
    dialog->findChild<QDoubleSpinBox*>("sweep2dThickness")->setValue(7);
    dialog->buttons()->button(QDialogButtonBox::Cancel)->click();application.processEvents();save->trigger();application.processEvents();
    stored=zima::document::PartDocument::load(path);
    if(!verify(stored.history.back().placement.x==12&&stored.history.back().sweep2d.thickness==1,"Cancel committed pending sweep2d pitch"))return 1;
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    action->trigger();application.processEvents();
    dialog=dynamic_cast<zima::app::Sweep2DDialog*>(window.findChild<QDialog*>("sweep2dDialog"));
    auto* view=dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
    if(!verify(dialog&&view,"Cannot test sweep2d reference selection"))return 1;
    view->set_standard_view(zima::viewer::StandardView::Isometric);view->fit_all();dialog->request_path_plane();application.processEvents();
    QEventLoop isometric_animation;QTimer::singleShot(950,&isometric_animation,&QEventLoop::quit);isometric_animation.exec();
    std::optional<QPointF> hit;
    std::size_t cap_index{};
    for(int y=4;y<view->height()&&!hit;y+=2)for(int x=4;x<view->width();x+=2){
        auto candidates=view->selection_candidates_at(QPointF(x,y));
        for(std::size_t i=0;i<candidates.size();++i){const auto& candidate=candidates[i];
            if(candidate.owner_id==feature_id&&candidate.kind==zima::viewer::CandidateKind::Face&&candidate.semantic_key.starts_with("sweep:cap:")){hit=QPointF(x,y);cap_index=i;break;}}
        if(hit)break;
    }
    if(!verify(hit.has_value(),"Start/end plane not offered to another 2D Sweep"))return 1;
    view->clear_selection();
    QMouseEvent move(QEvent::MouseMove,*hit,*hit,*hit,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&move);
    for(std::size_t i=0;i<cap_index;++i){
        QMouseEvent cycle_press(QEvent::MouseButtonPress,*hit,*hit,*hit,Qt::RightButton,Qt::RightButton,Qt::NoModifier);
        QMouseEvent cycle_release(QEvent::MouseButtonRelease,*hit,*hit,*hit,Qt::RightButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&cycle_press);QApplication::sendEvent(view,&cycle_release);
    }
    QMouseEvent press(QEvent::MouseButtonPress,*hit,*hit,*hit,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,*hit,*hit,*hit,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);application.processEvents();
    if(!verify(dialog->pending.sweep2d.path_plane&&dialog->pending.sweep2d.path_plane->owner_id==feature_id&&
        dialog->pending.sweep2d.path_plane->semantic_key.starts_with("sweep:cap:")&&dialog->pending.placement.references.empty(),
        "Path plane pick did not confirm the offered cap independently of placement"))return 1;
    dialog->buttons()->button(QDialogButtonBox::Cancel)->click();application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    action->trigger();application.processEvents();
    dialog=dynamic_cast<zima::app::Sweep2DDialog*>(window.findChild<QDialog*>("sweep2dDialog"));
    if(!verify(dialog!=nullptr,"Cannot test two-plane sweep placement"))return 1;
    const auto origin_geometry=doc.origin_viewer_mesh().original_references;
    for(unsigned i=0;i<2;++i){
        const auto key=i?"origin:plane:yz":"origin:plane:xz";
        auto ref=std::ranges::find_if(origin_geometry.triangle_references,[&](const auto& r){return r.semantic_key==key;});
        if(!verify(ref!=origin_geometry.triangle_references.end(),"Fixture origin plane missing"))return 1;
        zima::document::ConstructionReference reference{ref->instance_path,ref->owner_id,ref->semantic_key};reference.supports_offset=true;
        if(!verify(dialog->set_reference(i,reference,QString::fromUtf8(key)),"Sweep placement plane assignment failed"))return 1;
    }
    dialog->changed();
    auto path_frame=zima::sketcher::Sketch::from_serialized(dialog->pending.sweep2d.path_sketch);
    if(!verify(dialog->pending.sweep2d.path_plane&&dialog->pending.sweep2d.path_plane->semantic_key=="origin:plane:xz"&&
        std::abs(path_frame.resolved_normal.y)>1-1e-6,"First placement plane did not seed the path plane"))return 1;
    const auto saved_placement=dialog->pending.placement;
    QTreeWidgetItem* yz_plane{};
    for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole+3).toString()=="origin-reference"&&
        (*i)->data(0,Qt::UserRole+5).toString()=="origin:plane:yz"&&
        ((*i)->data(0,Qt::UserRole+6).isValid()?(*i)->data(0,Qt::UserRole+6):(*i)->data(0,Qt::UserRole)).toString().toStdString()==
            dialog->pending.sweep2d.path_plane->owner_id){yz_plane=*i;break;}
    if(!verify(yz_plane!=nullptr,"Document YZ plane missing from Tree"))return 1;
    dialog->request_path_plane();tree->setCurrentItem(yz_plane);application.processEvents();
    if(!verify(!dialog->path_active()&&dialog->pending.sweep2d.path_plane->semantic_key=="origin:plane:yz",
        "Tree did not assign the independent path plane"))return 1;
    path_frame=zima::sketcher::Sketch::from_serialized(dialog->pending.sweep2d.path_sketch);
    if(!verify(std::abs(path_frame.resolved_normal.x)>1-1e-6&&dialog->pending.placement==saved_placement,
        "Independent path plane changed container placement"))return 1;
    if(!verify(!dialog->findChild<QComboBox*>("sweep2dBasePlane")&&!dialog->findChild<QTableWidget*>("sweep2dReferences"),"Redundant sweep plane controls remain"))return 1;
    dialog->findChild<QPushButton*>("sweep2dSketch0")->click();application.processEvents();
    QEventLoop sketch_animation;QTimer::singleShot(950,&sketch_animation,&QEventLoop::quit);sketch_animation.exec();
    auto* polyline=window.findChild<QAction*>("sketchPolylineAction");
    if(!verify(polyline&&polyline->isEnabled(),"Polyline tool unavailable in owned Sketch"))return 1;
    polyline->trigger();application.processEvents();
    const auto mouse=[&](QPointF p,Qt::MouseButton button){
        QMouseEvent move(QEvent::MouseMove,p,p,p,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&move);
        QMouseEvent press(QEvent::MouseButtonPress,p,p,p,button,button,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,p,p,p,button,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);application.processEvents();
    };
    std::optional<QPointF> origin_hit;
    for(int y=4;y<view->height()&&!origin_hit;y+=2)for(int x=4;x<view->width();x+=2){
        const auto offered=view->selection_candidates_at(QPointF(x,y));
        if(!offered.empty()&&offered.front().semantic_key=="external_point:sketch_origin"){origin_hit=QPointF(x,y);break;}
    }
    if(!verify(origin_hit.has_value(),"Path Sketch origin not offered"))return 1;
    const QPointF chain_start=*origin_hit;
    mouse(chain_start,Qt::LeftButton);
    mouse(chain_start+QPointF(0,-60),Qt::LeftButton);
    mouse(chain_start+QPointF(40,-100),Qt::RightButton);
    mouse(chain_start+QPointF(40,-100),Qt::LeftButton);
    mouse(chain_start+QPointF(100,-100.5),Qt::RightButton);
    mouse(chain_start+QPointF(100,-100.5),Qt::LeftButton);
    window.findChild<QAction*>("sketchDimensionAction")->trigger();application.processEvents();
    const QPointF first_segment_hit=chain_start+QPointF(0,-30);
    QMouseEvent dimension_move(QEvent::MouseMove,first_segment_hit,first_segment_hit,first_segment_hit,Qt::NoButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(view,&dimension_move);
    const auto segment_candidates=view->selection_candidates_at(first_segment_hit);
    const auto segment_candidate=std::ranges::find_if(segment_candidates,[](const auto& candidate){
        return candidate.kind==zima::viewer::CandidateKind::SketchSegment;
    });
    if(!verify(segment_candidate!=segment_candidates.end(),"First polyline segment not offered after switching to dimension"))return 1;
    for(auto it=segment_candidates.begin();it!=segment_candidate;++it){
        QMouseEvent press(QEvent::MouseButtonPress,first_segment_hit,first_segment_hit,first_segment_hit,Qt::RightButton,Qt::RightButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,first_segment_hit,first_segment_hit,first_segment_hit,Qt::RightButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);
    }
    mouse(first_segment_hit,Qt::LeftButton);
    mouse(QPointF(45,45),Qt::LeftButton);
    finish->trigger();application.processEvents();
    auto chain=zima::sketcher::Sketch::from_serialized(dialog->pending.sweep2d.path_sketch);
    if(!verify(chain.dimensions.size()==1,"Direct switch from polyline failed to commit first segment dimension"))return 1;
    chain.dimensions.clear();
    if(!verify(chain.segments.size()==2&&chain.arcs.size()==1,"Line/arc/line polyline did not create the chain"))return 1;
    if(!verify(chain.points.size()==5,"Polyline contains an extra point"))return 1;
    const auto& arc=chain.arcs.front();
    for(const auto& segment:chain.segments){
        if(!verify(segment.first_point_id==arc.start_point_id||segment.first_point_id==arc.end_point_id||
                   segment.second_point_id==arc.start_point_id||segment.second_point_id==arc.end_point_id,
                   "Polyline arc and line do not share their contact point"))return 1;
    }
    if(!verify(std::ranges::count_if(chain.constraints,[](const auto& c){return c.kind==zima::sketcher::ConstraintKind::Tangent;})==2,"Polyline arc-to-line transition lost tangency"))return 1;
    for(bool points:{false,true}){
        auto dimension_sketch=chain;
        const auto segment=dimension_sketch.segments.front().id;
        dialog->set_sketch(0,dimension_sketch);
        dialog->findChild<QPushButton*>("sweep2dSketch0")->click();application.processEvents();
        QEventLoop dimension_animation;QTimer::singleShot(950,&dimension_animation,&QEventLoop::quit);dimension_animation.exec();
        window.findChild<QAction*>("sketchDimensionAction")->trigger();application.processEvents();
        const auto select=[&](const std::string& key){
            for(int y=4;y<view->height();y+=2)for(int x=4;x<view->width();x+=2){
                auto candidates=view->selection_candidates_at(QPointF(x,y));
                for(std::size_t i=0;i<candidates.size();++i)if(candidates[i].owner_id==dimension_sketch.id&&candidates[i].semantic_key==key){
                    const QPointF p(x,y);view->clear_selection();
                    QMouseEvent move(QEvent::MouseMove,p,p,p,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&move);
                    for(std::size_t j=0;j<i;++j){QMouseEvent press(QEvent::MouseButtonPress,p,p,p,Qt::RightButton,Qt::RightButton,Qt::NoModifier);QMouseEvent release(QEvent::MouseButtonRelease,p,p,p,Qt::RightButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);}
                    mouse(p,Qt::LeftButton);return true;
                }
            }
            return false;
        };
        if(points){
            if(!verify(select("point:"+dimension_sketch.segments.front().first_point_id)&&select("point:"+dimension_sketch.segments.front().second_point_id),"Cannot select axis-aligned segment endpoints"))return 1;
        }else if(!verify(select("segment:"+segment),"Cannot select axis-aligned segment"))return 1;
        mouse(QPointF(45,45),Qt::LeftButton);
        finish->trigger();application.processEvents();
        const auto dimension_result=zima::sketcher::Sketch::from_serialized(dialog->pending.sweep2d.path_sketch);
        if(!verify(dimension_result.dimensions.size()==1,"Empty click did not commit dimension on axis from origin"))return 1;
    }


    dialog->buttons()->button(QDialogButtonBox::Cancel)->click();application.processEvents();
    std::cout<<"2D Sweep UI contracts passed\n";return 0;
}

int verify_helical_sweep_command(QApplication& application,zima::app::AssemblyWorkspaceWindow& window,
        const std::filesystem::path& directory) {
    auto doc=zima::document::PartDocument::create_default();const auto path=directory/"helical-ui.prtz";doc.save(path);
    window.show();if(!verify(window.open_document_path(QString::fromStdString(path.string())),"Cannot open helical fixture"))return 1;
    application.processEvents();auto* action=window.findChild<QAction*>("helicalSweepAction");
    if(!verify(action&&action->isEnabled(),"Helical Sweep command missing"))return 1;
    action->trigger();application.processEvents();
    auto* dialog=dynamic_cast<zima::app::HelicalSweepDialog*>(window.findChild<QDialog*>("helicalSweepDialog"));
    if(!verify(dialog!=nullptr,"Helical properties did not open"))return 1;
    const auto feature_id=dialog->pending.id;
    auto* add_operation=dialog->findChild<QPushButton*>("primitiveAddOperation");
    auto* subtract_operation=dialog->findChild<QPushButton*>("primitiveSubtractOperation");
    if(!verify(add_operation&&subtract_operation&&add_operation->isChecked(),"Sweep operation buttons missing"))return 1;
    subtract_operation->click();
    if(!verify(dialog->pending.combine_mode==zima::document::CombineMode::Subtract&&subtract_operation->isChecked()&&!add_operation->isChecked(),"Sweep Subtract button did not select operation"))return 1;
    add_operation->click();
    if(!verify(dialog->pending.combine_mode==zima::document::CombineMode::Add&&add_operation->isChecked()&&!subtract_operation->isChecked(),"Sweep Add button did not select operation"))return 1;

    auto* placement=dialog->findChild<QTableWidget*>("sweepPlacementReferences");auto* orientation=dialog->findChild<QTableWidget*>("sweepPlacementOrientation");
    auto* placement_view=dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
    if(!verify(placement&&placement->rowCount()>=1&&placement->rowCount()<=3&&orientation&&orientation->rowCount()==2&&placement_view,"Sweep has no standard placement controls"))return 1;
    const auto camera=placement_view->camera_state();
    dialog->findChild<QDoubleSpinBox*>("sweepTranslation0")->setValue(12);
    dialog->findChild<QDoubleSpinBox*>("sweepTranslation1")->setValue(-3);
    dialog->findChild<QDoubleSpinBox*>("sweepTranslation2")->setValue(8);
    dialog->findChild<QDoubleSpinBox*>("sweepRotation1")->setValue(25);application.processEvents();
    if(!verify(dialog->pending.placement.x==12&&dialog->pending.placement.y==-3&&dialog->pending.placement.z==8&&std::abs(dialog->pending.placement.rotation_y-25)<1e-6&&placement_view->camera_state()==camera,"Sweep placement fields did not update or moved camera"))return 1;
    auto base=zima::sketcher::Sketch::from_serialized(dialog->pending.helical.sketches[0]);
    static_cast<void>(base.add_circle(0,0,5));static_cast<void>(base.add_point(5,0));dialog->set_sketch(0,base);
    if(!verify(!dialog->pending.helical.start_point_id.empty(),"Unique initial Point not selected"))return 1;
    auto guide=zima::sketcher::Sketch::from_serialized(dialog->pending.helical.sketches[1]);static_cast<void>(guide.add_segment(0,0,-1,6));dialog->set_sketch(1,guide);
    auto section=zima::sketcher::Sketch::from_serialized(dialog->pending.helical.sketches[2]);static_cast<void>(section.add_circle(0,0,.4));dialog->set_sketch(2,section);
    auto* finish=window.findChild<QAction*>("finishSketchAction");
    for(unsigned stage=0;stage<3;++stage){
        dialog->findChild<QPushButton*>(QString("helicalSketch%1").arg(stage))->click();application.processEvents();
        if(!verify(!dialog->isVisible()&&finish&&finish->isEnabled(),"Helical owned Sketch did not enter Sketcher"))return 1;
        finish->trigger();application.processEvents();if(!verify(dialog->isVisible(),"Sketch did not return to pending Helical Sweep"))return 1;
    }
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
    if(!verify(!dialog->isVisible(),"Helical Sweep OK did not commit"))return 1;
    auto* save=window.findChild<QAction*>("saveDocumentAction");save->trigger();application.processEvents();
    auto stored=zima::document::PartDocument::load(path);
    if(!verify(stored.history.back().placement.x==12&&stored.history.back().placement.y==-3&&stored.history.back().placement.z==8&&std::abs(stored.history.back().placement.rotation_y-25)<1e-6,"Sweep placement not persisted"))return 1;
    if(!verify(stored.history.size()==1&&stored.history.back().id==feature_id,"Helical Sweep history not saved"))return 1;
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* item=nullptr;int sketches=0;
    for(QTreeWidgetItemIterator i(tree);*i;++i){if((*i)->data(0,Qt::UserRole+3).toString()=="part-helical-sketch")++sketches;
        if((*i)->data(0,Qt::UserRole).toString().toStdString()==feature_id&&(*i)->data(0,Qt::UserRole+3).toString()=="part-container")item=*i;}
    if(!verify(item&&sketches==3,"Owned Helical Sweep Sketches absent from Tree"))return 1;
    for(unsigned stage=0;stage<3;++stage) {
        QTreeWidgetItem* sketch_item{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)
            if((*i)->data(0,Qt::UserRole+3).toString()=="part-helical-sketch" &&
                (*i)->data(0,Qt::UserRole+6).toUInt()==stage){sketch_item=*i;break;}
        window.show_tree_item_properties(sketch_item);application.processEvents();
        if(!verify(finish->isEnabled(),"Embedded Tree Sketch did not activate"))return 1;
        finish->trigger();application.processEvents();
        auto* reopened=dynamic_cast<zima::app::HelicalSweepDialog*>(window.findChild<QDialog*>("helicalSweepDialog"));
        if(!verify(reopened && reopened->isVisible(),"Embedded Tree Sketch did not return to properties"))return 1;
        reopened->buttons()->button(QDialogButtonBox::Cancel)->click();application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    item=nullptr;
    for(QTreeWidgetItemIterator i(tree);*i;++i)
        if((*i)->data(0,Qt::UserRole).toString().toStdString()==feature_id &&
            (*i)->data(0,Qt::UserRole+3).toString()=="part-container"){item=*i;break;}
    window.show_tree_item_properties(item);application.processEvents();
    dialog=dynamic_cast<zima::app::HelicalSweepDialog*>(window.findChild<QDialog*>("helicalSweepDialog"));
    if(!verify(dialog!=nullptr,"Helical edit did not reopen same dialog"))return 1;
    dialog->findChild<QDoubleSpinBox*>("sweepTranslation0")->setValue(99);
    dialog->findChild<QDoubleSpinBox*>("helicalPitch")->setValue(7);
    dialog->buttons()->button(QDialogButtonBox::Cancel)->click();application.processEvents();save->trigger();application.processEvents();
    stored=zima::document::PartDocument::load(path);
    if(!verify(stored.history.back().placement.x==12&&stored.history.back().helical.pitch==5,"Cancel committed pending helical pitch"))return 1;
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    action->trigger();application.processEvents();
    dialog=dynamic_cast<zima::app::HelicalSweepDialog*>(window.findChild<QDialog*>("helicalSweepDialog"));
    auto* view=dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
    if(!verify(dialog&&view,"Cannot test helical reference selection"))return 1;
    view->set_standard_view(zima::viewer::StandardView::Isometric);view->fit_all();dialog->request_placement(0);application.processEvents();
    QEventLoop isometric_animation;QTimer::singleShot(950,&isometric_animation,&QEventLoop::quit);isometric_animation.exec();
    std::optional<QPointF> hit;
    std::size_t cap_index{};
    for(int y=4;y<view->height()&&!hit;y+=2)for(int x=4;x<view->width();x+=2){
        auto candidates=view->selection_candidates_at(QPointF(x,y));
        for(std::size_t i=0;i<candidates.size();++i){const auto& candidate=candidates[i];
            if(candidate.owner_id==feature_id&&candidate.kind==zima::viewer::CandidateKind::Face&&(candidate.semantic_key.starts_with("start:from:")||candidate.semantic_key.starts_with("end:from:"))){hit=QPointF(x,y);cap_index=i;break;}}
        if(hit)break;
    }
    if(!verify(hit.has_value(),"Start/end plane not offered to another Helical Sweep"))return 1;
    view->clear_selection();
    QMouseEvent move(QEvent::MouseMove,*hit,*hit,*hit,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&move);
    for(std::size_t i=0;i<cap_index;++i){
        QMouseEvent cycle_press(QEvent::MouseButtonPress,*hit,*hit,*hit,Qt::RightButton,Qt::RightButton,Qt::NoModifier);
        QMouseEvent cycle_release(QEvent::MouseButtonRelease,*hit,*hit,*hit,Qt::RightButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&cycle_press);QApplication::sendEvent(view,&cycle_release);
    }
    QMouseEvent press(QEvent::MouseButtonPress,*hit,*hit,*hit,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,*hit,*hit,*hit,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);application.processEvents();
    if(!verify(std::ranges::any_of(dialog->pending.placement.references,[&](const auto& ref){return ref.owner_id==feature_id&&(ref.semantic_key.starts_with("start:from:")||ref.semantic_key.starts_with("end:from:"));}),"Plane reference click did not confirm offered cap"))return 1;
    dialog->buttons()->button(QDialogButtonBox::Cancel)->click();application.processEvents();
    std::cout<<"Helical Sweep UI contracts passed\n";return 0;
}

int verify_shaft_thread_command(QApplication& application,zima::app::AssemblyWorkspaceWindow& window,
        const std::filesystem::path& directory) {
    auto document=zima::document::PartDocument::create_default();
    auto profile=zima::sketcher::Sketch::create_default();
    const auto circle_id=profile.add_circle(0,0,5);
    auto shaft=zima::document::PartDocument::create_extrusion_container(profile.id);
    shaft.extrusion.length_forward=30;
    document.sketches.push_back(profile);
    document.insert_history_entry(zima::document::PartHistoryKind::Sketch,profile.id);
    document.history.push_back(shaft);
    document.insert_history_entry(zima::document::PartHistoryKind::Feature,shaft.id);
    auto support=zima::document::PartDocument::create_box_container();
    support.box.length=20;support.box.width=20;support.box.height=5;
    support.placement.x=-10;support.placement.y=-10;support.placement.z=-5;
    document.history.push_back(support);
    document.insert_history_entry(zima::document::PartHistoryKind::Feature,support.id);
    zima::kernel::OcctKernel kernel;
    const auto bodies=kernel.evaluate_history(document.kernel_operations());
    const auto path=directory/"shaft-thread-ui.prtz";
    document.save(path,bodies);
    window.show();
    if (!verify(window.open_document_path(QString::fromStdString(path.string())),"Cannot open shaft fixture")) return 1;
    application.processEvents();
    auto* action=window.findChild<QAction*>("shaftThreadAction");
    auto* view=dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
    if (!verify(action && action->isEnabled() && view,"Standalone shaft thread command is missing")) return 1;
    action->trigger();application.processEvents();
    auto* dialog=dynamic_cast<zima::app::ShaftThreadDialog*>(window.findChild<QDialog*>("shaftThreadDialog"));
    // This class intentionally has no Q_OBJECT; use the known dialog child via RTTI.
    if (!dialog) for (auto* child : window.findChildren<QDialog*>())
        if (child->objectName()=="shaftThreadDialog") dialog=dynamic_cast<zima::app::ShaftThreadDialog*>(child);
    if (!verify(dialog!=nullptr,"Shaft thread properties did not open")) return 1;
    const auto click_face=[&](const std::string& key) {
        std::optional<QPointF> position;
        for (int y=8;y<view->height() && !position;y+=6)
            for (int x=8;x<view->width();x+=6) {
                const QPointF point(x,y);
                const auto candidates=view->selection_candidates_at(point);
                if (!candidates.empty() && candidates.front().owner_id==shaft.id &&
                    candidates.front().semantic_key==key) { position=point;break; }
            }
        if (!position) return false;
        QMouseEvent move(QEvent::MouseMove,*position,*position,*position,Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&move);
        QMouseEvent press(QEvent::MouseButtonPress,*position,*position,*position,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(view,&press);
        QMouseEvent release(QEvent::MouseButtonRelease,*position,*position,*position,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&release);application.processEvents();
        return true;
    };
    if (!verify(click_face("generated:"+circle_id) && dialog->pending().shaft_thread.cylinder.valid(),
            "Shaft cylinder cannot be selected from the common View candidates")) return 1;
    auto* references=dialog->findChild<QTableWidget*>("shaftThreadReferences");
    QMetaObject::invokeMethod(references,"cellClicked",Q_ARG(int,1),Q_ARG(int,1));
    application.processEvents();
    if (!verify(click_face("end:from:47:profile-region:"+circle_id) && dialog->pending().shaft_thread.start.valid(),
            "Shaft start plane cannot be selected from the common View candidates")) return 1;
    const auto feature_id=dialog->pending().id;
    zima::viewer::ViewerCandidate dimension;
    dimension.kind=zima::viewer::CandidateKind::Dimension;
    dimension.owner_id=feature_id;dimension.semantic_key="parameter:root_diameter";
    const auto position=view->candidate_dimension_label_position(dimension);
    if (!verify(position.has_value(),"Shaft root diameter is missing in View")) return 1;
    QMouseEvent double_click(QEvent::MouseButtonDblClick,QPointF(*position),QPointF(*position),QPointF(*position),
        Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(view,&double_click);application.processEvents();
    auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");
    if (!verify(editor && editor->isVisible(),"Shaft diameter did not open numeric editing")) return 1;
    editor->setText("8.050");
    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
    QApplication::sendEvent(editor,&enter);application.processEvents();
    if (!verify(dialog->isVisible() && std::abs(dialog->pending().shaft_thread.root_diameter-8.05)<1e-8,
            "View diameter editing did not remain pending in the same dialog")) return 1;
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
    if (dialog->isVisible())
        if (const auto* error=dialog->findChild<QLabel*>("propertiesSubmitError"))
            std::cerr << error->text().toStdString() << '\n';
    if (!verify(!dialog->isVisible(),"Valid shaft thread did not commit")) return 1;
    auto* save=window.findChild<QAction*>("saveDocumentAction");
    if (!verify(save!=nullptr,"Save command is missing")) return 1;
    save->trigger();application.processEvents();
    const auto loaded=zima::document::PartDocument::load(path);
    if (!verify(loaded.history.size()==3 &&
            loaded.history.back().feature_kind==zima::document::FeatureKind::ShaftThread &&
            std::abs(loaded.history.back().shaft_thread.root_diameter-8.05)<1e-8,
            "Shaft thread did not save the edited root diameter")) return 1;
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    QTreeWidgetItem* item=nullptr;
    for (QTreeWidgetItemIterator i(tree);*i;++i)
        if ((*i)->data(0,Qt::UserRole).toString().toStdString()==feature_id) { item=*i;break; }
    if (!verify(item!=nullptr,"Shaft thread is missing from Tree")) return 1;
    window.show_tree_item_properties(item);application.processEvents();
    dialog=dynamic_cast<zima::app::ShaftThreadDialog*>(window.findChild<QDialog*>("shaftThreadDialog"));
    if (!verify(dialog && std::abs(dialog->pending().shaft_thread.root_diameter-8.05)<1e-8 &&
            view->candidate_dimension_label_position(dimension).has_value(),
            "Reopened shaft thread lost its custom root diameter or rollback reference geometry")) return 1;
    references=dialog->findChild<QTableWidget*>("shaftThreadReferences");
    QMetaObject::invokeMethod(references,"cellClicked",Q_ARG(int,0),Q_ARG(int,1));
    application.processEvents();
    if (!verify(click_face("generated:"+circle_id) && dialog->pending().shaft_thread.cylinder.valid(),
            "Reopened shaft thread cannot replace its cylinder reference")) return 1;
    dialog->set_numeric("root_diameter",8.0);
    dialog->buttons()->button(QDialogButtonBox::Cancel)->click();application.processEvents();
    save->trigger();application.processEvents();
    const auto canceled=zima::document::PartDocument::load(path);
    if (!verify(std::abs(canceled.history.back().shaft_thread.root_diameter-8.05)<1e-8,
            "Cancel committed shaft thread preview")) return 1;
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    window.show_parameter_dimensions(feature_id);application.processEvents();
    if (!verify(!view->container_inspection_wire().empty() && !view->confirmed_candidate(),
            "Dimension inspection lost its wire or blocked dimension picking")) return 1;
    const auto camera=view->camera_state();
    window.edit_dimension_inline(dimension);application.processEvents();
    editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");
    if (!verify(editor && editor->isVisible(),"Closed shaft thread does not support numeric View editing")) return 1;
    editor->setText("8.030");
    QApplication::sendEvent(editor,&enter);application.processEvents();
    save->trigger();application.processEvents();
    const auto inline_edited=zima::document::PartDocument::load(path);
    if (!verify(!view->container_inspection_wire().empty(),
            "Inline edit lost the inspected container wire after scene refresh")) return 1;
    if (!verify(std::abs(inline_edited.history.back().shaft_thread.root_diameter-8.03)<1e-8 &&
            inline_edited.history.back().shaft_thread.designation=="M10" && view->camera_state()==camera,
            "Closed View edit lost its numeric diameter, catalog designation or camera")) return 1;
    auto missing=inline_edited;
    missing.document_id=zima::document::PartDocument::create_default().document_id;
    missing.history={missing.history.back()};missing.history_order.clear();missing.history_cursor=0;
    missing.sketches.clear();
    const auto missing_path=directory/"shaft-thread-missing-ui.prtz";
    missing.save(missing_path,kernel.evaluate_history(missing.kernel_operations()));
    if (!verify(window.open_document_path(QString::fromStdString(missing_path.string())),
            "Cannot reopen retained thread without its source")) return 1;
    application.processEvents();
    item=nullptr;
    for (QTreeWidgetItemIterator i(tree);*i;++i)
        if ((*i)->data(0,Qt::UserRole).toString().toStdString()==feature_id) { item=*i;break; }
    if (!verify(item && item->data(0,zima::app::missing_reference_role).toBool(),
            "Missing shaft references were not marked red after reopening")) return 1;
    window.show_tree_item_properties(item);application.processEvents();
    dialog=dynamic_cast<zima::app::ShaftThreadDialog*>(window.findChild<QDialog*>("shaftThreadDialog"));
    if (!verify(dialog && !dialog->pending().shaft_thread.cylinder.valid() &&
            !dialog->pending().shaft_thread.start.valid() && dialog->active_reference()==0,
            "Missing shaft properties did not request new references")) return 1;
    dialog->buttons()->button(QDialogButtonBox::Cancel)->click();application.processEvents();
    save->trigger();application.processEvents();
    const auto retained=zima::document::PartDocument::load(missing_path);
    if (!verify(retained.history.back().shaft_thread.cylinder.valid() &&
            retained.history.back().shaft_thread.cylinder.surface &&
            retained.history.back().shaft_thread.start.surface,
            "Cancel erased the durable missing-reference fallback")) return 1;
    return 0;
}

int verify_unresolved_sweep_sketches(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima::document;
    for(bool planar:{true,false}) {
        auto doc=PartDocument::create_default();
        auto prior=PartDocument::create_box_container();
        auto sweep=planar?PartDocument::create_sweep2d_container():PartDocument::create_helical_sweep_container();
        if(planar) {
            auto guide=zima::sketcher::Sketch::from_serialized(sweep.sweep2d.path_sketch);
            static_cast<void>(guide.add_segment(0,0,0,10));sweep.sweep2d.path_sketch=guide.serialized();
            const auto station=PartDocument::sweep2d_route(sweep).stations.front();
            PartDocument::ensure_sweep2d_profile(sweep,station.point_id,station.incoming);
            auto section=zima::sketcher::Sketch::from_serialized(sweep.sweep2d.sketch_data(1));
            static_cast<void>(section.add_circle(0,0,2));sweep.sweep2d.sketch_data(1)=section.serialized();
        } else {
            auto base=zima::sketcher::Sketch::from_serialized(sweep.helical.sketches[0]);
            static_cast<void>(base.add_circle(0,0,5));sweep.helical.circle_id=base.circles.front().id;sweep.helical.start_point_id=base.add_point(5,0);sweep.helical.sketches[0]=base.serialized();
            auto guide=zima::sketcher::Sketch::from_serialized(sweep.helical.sketches[1]);
            static_cast<void>(guide.add_segment(0,0,-1,6));sweep.helical.sketches[1]=guide.serialized();
            auto section=zima::sketcher::Sketch::from_serialized(sweep.helical.sketches[2]);
            static_cast<void>(section.add_circle(0,0,.4));sweep.helical.sketches[2]=section.serialized();
            PartDocument::reframe_helical_sketches(sweep);
        }
        sweep.placement.references={{{},"missing-source","missing-plane"}};
        doc.history={prior,sweep};BodyHistoryGraph graph;static_cast<void>(graph.create_body("Body"));
        graph.insert({PartHistoryKind::Feature,prior.id});graph.insert({PartHistoryKind::Feature,sweep.id});doc.set_body_history(graph);
        const auto path=directory/(planar?"unresolved-sweep2d-ui.prtz":"unresolved-helical-ui.prtz");doc.save(path);
        zima::app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1100,850);window.show();
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        if(!verify(window.open_document_path(QString::fromStdString(path.string())),"Cannot open unresolved Sweep fixture"))return 1;
        flush();auto* tree=window.findChild<QTreeWidget*>("documentTree");
        for(unsigned stage=0;stage<(planar?2:3);++stage) {
            QTreeWidgetItem* item{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole+3).toString()==
                (planar?"part-sweep2d-sketch":"part-helical-sketch") && (*i)->data(0,Qt::UserRole+6).toUInt()==stage){item=*i;break;}
            if(!verify(item!=nullptr,"Unresolved Sweep lost its Sketch tree entry"))return 1;
            window.show_tree_item_properties(item);flush();
            auto* finish=window.findChild<QAction*>("finishSketchAction");
            if(!verify(finish && finish->isEnabled(),"Missing previous geometry blocked embedded Sketch entry"))return 1;
            finish->trigger();flush();
            zima::app::SweepPlacementDialog* dialog{};
            for(auto* child:window.findChildren<QDialog*>())if(auto* value=dynamic_cast<zima::app::SweepPlacementDialog*>(child);value && value->isVisible()){dialog=value;break;}
            if(!verify(dialog!=nullptr,"Unresolved Sketch did not return to Sweep properties"))return 1;
            dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        }
        if(!verify(PartDocument::load(path).history==doc.history,"Unresolved Sketch Cancel changed stored history"))return 1;
    }
    std::cout << "Unresolved Sweep Sketch contracts passed\n";return 0;
}

int verify_external_circle_selection(QApplication& application,
    const std::filesystem::path& directory) {
    using namespace zima::document;
    for(bool face:{false,true}) {
        auto doc=PartDocument::create_default();
        auto container=PartDocument::create_sketch_container();
        auto sketch=zima::sketcher::Sketch::create_default();sketch.owner_container_id=container.id;
        static_cast<void>(sketch.add_circle(12,8,2));
        const auto circle_id=sketch.circles.front().id;
        zima::sketcher::SketchExternalReference ref;
        ref.id="external-circle";ref.source_document_id="remote-part";
        ref.source_owner_id="source-sweep";ref.source_semantic_key="cap-rim";
        ref.kind=face ? zima::sketcher::ExternalReferenceKind::Face : zima::sketcher::ExternalReferenceKind::Edge;
        std::vector<std::array<double,2>> points;
        for(int i=0;i<=48;++i){const double a=2*std::acos(-1.0)*i/48;points.push_back({5*std::cos(a),5*std::sin(a)});}
        if(face)ref.cached_paths.push_back(points);else ref.cached_points=points;
        sketch.external_references.push_back(ref);
        doc.history={container};doc.sketches={sketch};
        const auto path=directory/(face ? "external-face-circle-ui.prtz" : "external-edge-circle-ui.prtz");
        doc.save(path);
        zima::app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));
        window.resize(1100,850);window.show();
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        if(!verify(window.open_document_path(QString::fromStdString(path.string())),"Cannot open external circle fixture"))return 1;
        flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* item{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)
            if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id &&
                (*i)->data(0,Qt::UserRole+3).toString()=="part-sketch"){item=*i;break;}
        if(!verify(item!=nullptr,"External circle Sketch missing"))return 1;
        window.show_tree_item_properties(item);flush();
        auto* open=window.findChild<QPushButton*>("sketchOpenButton");
        if(!verify(open!=nullptr,"Cannot enter external circle Sketch"))return 1;
        open->click();flush();
        auto* view=dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
        const auto pick=[&](const std::string& key) {
            for(int y=15;y<view->height()-15;y+=3)for(int x=15;x<view->width()-15;x+=3){
                const QPointF pos(x,y);const auto candidates=view->selection_candidates_at(pos);
                if(candidates.empty() || candidates.front().semantic_key!=key)continue;
                const QPointF global(view->mapToGlobal(pos.toPoint()));
                QMouseEvent press(QEvent::MouseButtonPress,pos,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease,pos,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);flush();return true;
            }return false;
        };
        const auto reference_key=std::string(face?"external_face:":"external_edge:")+ref.id;
        window.findChild<QAction*>("sketchEqualAction")->trigger();flush();
        if(!verify(pick("circle:"+circle_id) && pick(reference_key),"Equal radius cannot select external circle"))return 1;
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&window,&escape);flush();
        window.findChild<QAction*>("sketchConcentricAction")->trigger();flush();
        if(!verify(pick("circle:"+circle_id) && pick(reference_key),"Concentric cannot select external circle"))return 1;
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        const auto stored=PartDocument::load(path);
        const auto& result=stored.sketches.front();const auto* center=result.find_point(result.circles.front().center_point_id);
        if(!verify(result.constraints.size()==2 && center && std::hypot(center->x,center->y)<1e-7 &&
                std::abs(result.circles.front().radius-5)<1e-7,"External circle UI constraints did not persist"))return 1;
    }
    std::cout << "External circle selection contracts passed\n";return 0;
}

int verify_spline_tangent_selection(QApplication& application,
    const std::filesystem::path& directory) {
    using namespace zima::document;
    auto doc=PartDocument::create_default();
    auto container=PartDocument::create_sketch_container();
    auto sketch=zima::sketcher::Sketch::create_default();
    sketch.owner_container_id=container.id;
    const auto first=sketch.add_bspline({{0,0},{4,0},{6,2},{10,0}},3);
    const auto second=sketch.add_bspline({{10,0},{12,5},{18,4},{20,0}},3);
    const auto loop=sketch.add_bspline({{0,15},{10,15},{10,25},{-5,20},{0,15}},3);
    doc.history={container};doc.sketches={sketch};
    const auto path=directory/"spline-tangent-ui.prtz";
    doc.save(path);
    zima::app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));
    window.resize(1000,800);window.show();
    if(!verify(window.open_document_path(QString::fromStdString(path.string())),
        "Cannot open spline tangent fixture"))return 1;
    application.processEvents();
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    QTreeWidgetItem* item{};
    for(QTreeWidgetItemIterator i(tree);*i;++i)
        if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id &&
           (*i)->data(0,Qt::UserRole+3).toString()=="part-sketch") {item=*i;break;}
    if(!verify(item!=nullptr,"Spline fixture Sketch is missing"))return 1;
    window.show_tree_item_properties(item);application.processEvents();
    auto* open=window.findChild<QPushButton*>("sketchOpenButton");
    if(!verify(open!=nullptr,"Cannot enter spline Sketch"))return 1;
    open->click();application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    auto* view=dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
    auto* tangent=window.findChild<QAction*>("sketchTangentAction");
    if(!verify(tangent && tangent->isEnabled(),"Spline tangent command unavailable"))return 1;
    tangent->trigger();application.processEvents();
    const auto pick=[&](const std::string& id,bool double_click=false) {
        for(int y=20;y<view->height()-20;y+=5)
            for(int x=20;x<view->width()-20;x+=5) {
                const QPointF pos(x,y);
                const auto candidates=view->selection_candidates_at(pos);
                if(candidates.empty() || candidates.front().semantic_key!="bspline:"+id)continue;
                const QPointF global(view->mapToGlobal(pos.toPoint()));
                QMouseEvent event(double_click?QEvent::MouseButtonDblClick:QEvent::MouseButtonPress,
                    pos,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease,pos,global,
                    Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&event);
                QApplication::sendEvent(view,&release);
                application.processEvents();return true;
            }
        return false;
    };
    if(!verify(pick(first) && pick(second),"Connected splines cannot be selected for tangency"))return 1;
    if(!verify(pick(loop) && pick(loop,true),"Self tangent cannot select the same spline twice"))return 1;
    auto* save=window.findChild<QAction*>("saveDocumentAction");
    save->trigger();application.processEvents();
    const auto loaded=PartDocument::load(path);
    if(!verify(loaded.sketches.front().constraints.size()==2,
        "Spline tangent clicks did not persist both tangent constraints"))return 1;
    std::size_t expected_splines=3;
    for(const auto* action_name : {"sketchBSplineAction","sketchInterpolatingSplineAction"}) {
    window.findChild<QAction*>(action_name)->trigger();
    application.processEvents();
    std::vector<QPointF> inputs{
        {view->width()*.22,view->height()*.72},
        {view->width()*.35,view->height()*.66},
        {view->width()*.40,view->height()*.83},
        {view->width()*.20,view->height()*.88}};
    const auto click=[&](const QPointF& pos) {
        const QPointF global(view->mapToGlobal(pos.toPoint()));
        QMouseEvent press(QEvent::MouseButtonPress,pos,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,pos,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);
        application.processEvents();
    };
    // Find a real offered sketch-axis candidate for the third input.
    bool axis_found=false;
    for(int y=25;y<view->height()-25 && !axis_found;y+=5)
        for(int x=25;x<view->width()-25 && !axis_found;x+=5) {
            const QPointF pos(x,y);
            const auto candidates=view->selection_candidates_at(pos);
            if(!candidates.empty() && candidates.front().kind==zima::viewer::CandidateKind::SketchAxis) {
                inputs[2]=pos;axis_found=true;
            }
        }
    if(!verify(axis_found,"No axis available for spline crash regression"))return 1;
    for(const auto& pos:inputs) {
        click(pos);
        // Repeated mouse moves over the accepted point used to append that
        // point again to the preview and terminate Qt with an uncaught error.
        for(const auto offset : {QPointF{},QPointF(.000001,0),QPointF{}}) {
            const auto hover=pos+offset;
            QMouseEvent move(QEvent::MouseMove,hover,
                QPointF(view->mapToGlobal(hover.toPoint())),
                Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&move);
            application.processEvents();
        }
    }
    const auto closure_candidates=view->selection_candidates_at(inputs.front());
    if(!verify(!closure_candidates.empty() &&
        closure_candidates.front().semantic_key=="point:pending-spline-start",
        "Drawn spline start was not offered for closing"))return 1;
    click(inputs.front());
    QMouseEvent finish(QEvent::MouseButtonDblClick,inputs.front(),
        QPointF(view->mapToGlobal(inputs.front().toPoint())),
        Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
    QApplication::sendEvent(view,&finish);application.processEvents();
    save->trigger();application.processEvents();
    const auto closed_doc=PartDocument::load(path);
    const auto& splines=closed_doc.sketches.front().bsplines;
    if(!verify(splines.size()==++expected_splines && splines.back().control_point_ids.size()==5 &&
        splines.back().control_point_ids.front()==splines.back().control_point_ids.back(),
        "Clicking the first spline point did not persist a single shared endpoint"))return 1;
    }
    return 0;
}

int verify_curve_radius_edit(QApplication& application,
    const std::filesystem::path& directory) {
    using namespace zima::document;
    auto doc = PartDocument::create_default();
    auto curve = PartDocument::create_construction(ConstructionKind::Curve3D);
    curve.curve_type = Curve3DType::Polyline;
    curve.curve_rounding_enabled = true;
    for (const auto origin : std::vector<zima::kernel::Vec3>{{0,0,0},{0,0,30},{30,0,30}}) {
        auto point = PartDocument::create_construction(ConstructionKind::Point);
        point.parent_construction_id = curve.id;
        point.origin = origin;
        curve.curve_points.push_back(point);
    }
    curve.curve_points[1].curve_radius = 5;
    doc.constructions.push_back(curve);
    const auto path = directory / "curve-radius-inline.prtz";
    doc.save(path);
    zima::app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));
    window.resize(1000,800);
    window.show();
    if (!verify(window.open_document_path(QString::fromStdString(path.string())),
            "Cannot open radius fixture")) return 1;
    application.processEvents();
    auto* view = dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
    auto* save = window.findChild<QAction*>("saveDocumentAction");
    zima::viewer::ViewerCandidate dimension;
    dimension.kind = zima::viewer::CandidateKind::Dimension;
    dimension.owner_id = curve.curve_points[1].id;
    dimension.semantic_key = "parameter:radius";
    const auto edit = [&](const char* text) {
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        const auto position = view->candidate_dimension_label_position(dimension);
        if (!position) return false;
        const QPointF local(*position);
        const QPointF global(view->mapToGlobal(*position));
        QMouseEvent press(QEvent::MouseButtonPress,local,global,
            Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,local,global,
            Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QMouseEvent double_click(QEvent::MouseButtonDblClick,local,global,
            Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(view,&press);
        QApplication::sendEvent(view,&release);
        QApplication::sendEvent(view,&double_click);
        QApplication::sendEvent(view,&release);
        application.processEvents();
        auto* editor = view->findChild<QLineEdit*>("inlineDimensionValueEdit");
        if (!editor || !editor->isVisible()) return false;
        editor->setText(text);
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(editor,&enter);
        application.processEvents();
        return true;
    };
    const auto stored_radius = [&] {
        save->trigger();
        application.processEvents();
        return PartDocument::load(path).find_construction(curve.id)->curve_points[1].curve_radius;
    };
    window.show_parameter_dimensions(curve.id);
    application.processEvents();
    if (!verify(edit("7") && stored_radius()==7 &&
            view->candidate_dimension_value(dimension)==7,
            "Inspection radius edit did not persist or refresh its annotation")) return 1;
    if (!verify(edit("100") && stored_radius()==7,
            "Invalid radius changed the document")) return 1;
    auto* tree = window.findChild<QTreeWidget*>("documentTree");
    QTreeWidgetItem* item{};
    for (QTreeWidgetItemIterator i(tree); *i; ++i)
        if ((*i)->data(0,Qt::UserRole).toString().toStdString()==curve.id) { item=*i; break; }
    window.show_tree_item_properties(item);
    application.processEvents();
    zima::app::ConstructionPropertiesDialog* dialog{};
    for (auto* candidate : window.findChildren<QDialog*>())
        if (auto* properties = dynamic_cast<zima::app::ConstructionPropertiesDialog*>(candidate))
            if (properties->isVisible()) { dialog=properties; break; }
    if (!verify(dialog && edit("9") && dialog->pending_value().curve_points[1].curve_radius==9 &&
            dialog->findChild<QDoubleSpinBox*>("curve3DRadius2")->value()==9,
            "Properties radius edit did not update pending point and table")) return 1;
    if (!verify(edit("8") && dialog->pending_value().curve_points[1].curve_radius==8,
            "Preview refresh blocked the next radius double click")) return 1;
    view->confirm_current_pointer();
    application.processEvents();
    if (!verify(edit("9") && dialog->pending_value().curve_points[1].curve_radius==9,
            "Finishing reference entry blocked radius double click")) return 1;
    dialog->buttons()->button(QDialogButtonBox::Cancel)->click();
    application.processEvents();
    if (!verify(stored_radius()==7, "Cancel committed an inline radius preview")) return 1;
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    window.show_parameter_dimensions(curve.id);
    application.processEvents();
    if (!verify(edit("0") && stored_radius()==0 &&
            !view->candidate_dimension_value(dimension),
            "Zero radius did not restore the sharp corner")) return 1;
    return 0;
}

int verify_body_curve_references(QApplication& application, const std::filesystem::path& directory) {
    using namespace zima::document;
    for (bool sweep : {false,true}) {
        auto part=PartDocument::create_default();
        auto box=PartDocument::create_box_container();box.box={10,10,10};part.history={box};
        BodyHistoryGraph graph;
        const auto body_id=graph.create_body("Reference body");
        graph.insert({PartHistoryKind::Feature,box.id});
        auto body=*graph.find(body_id);body.scope.placement={20,30,40};graph.update_body(body);
        part.set_body_history(graph);part.resolve_constructions();
        zima::kernel::OcctKernel kernel;
        const auto path=directory/(sweep ? "body-sweep-reference.prtz" : "body-curve-reference.prtz");
        part.save(path,kernel.evaluate_history(part.kernel_operations()));
        zima::app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));
        window.resize(1200,900);window.show();
        const auto flush=[&] { application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents(); };
        if (!verify(window.open_document_path(QString::fromStdString(path.string())),"Cannot open Body reference fixture")) return 1;
        flush();
        window.findChild<QAction*>(sweep ? "sweep3DAction" : "curve3DAction")->trigger();flush();
        const auto dialog=[&]() -> zima::app::ConstructionPropertiesDialog* {
            for(auto* child:window.findChildren<QDialog*>())
                if(auto* value=dynamic_cast<zima::app::ConstructionPropertiesDialog*>(child);value && value->isVisible()) return value;
            return nullptr;
        };
        auto* parent=dialog();
        if (!verify(parent!=nullptr,"Cannot open Curve/Sweep properties in Body")) return 1;
        parent->findChild<QPushButton*>("curve3DAddPoint")->click();flush();
        auto* point=dialog();
        if (!verify(point && point!=parent,"Cannot add Curve/Sweep point in Body")) return 1;
        auto* view=dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
        std::optional<QPointF> hit;
        zima::viewer::ViewerCandidate chosen;
        for(int y=15;y<view->height()-15 && !hit;y+=3)
            for(int x=15;x<view->width()-15 && !hit;x+=3) {
                const auto candidates=view->selection_candidates_at(QPointF(x,y));
                if(!candidates.empty() && candidates.front().kind==zima::viewer::CandidateKind::Vertex &&
                        candidates.front().owner_id==box.id) {hit=QPointF(x,y);chosen=candidates.front();}
            }
        if (!verify(hit.has_value(),"Box endpoint is not offered to Curve/Sweep point")) return 1;
        const auto picked_vertex=std::ranges::find_if(view->mesh().original_references.points,[&](const auto& point) {
            return point.reference.owner_id==chosen.owner_id && point.reference.semantic_key==chosen.semantic_key;
        });
        if (!verify(picked_vertex!=view->mesh().original_references.points.end(),"Picked endpoint has no persisted geometry")) return 1;
        const auto expected=zima::kernel::Vec3{picked_vertex->position.x-20,
            picked_vertex->position.y-30,picked_vertex->position.z-40};
        QMouseEvent press(QEvent::MouseButtonPress,*hit,QPointF(view->mapToGlobal(hit->toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(view,&press);flush();
        QMouseEvent release(QEvent::MouseButtonRelease,*hit,QPointF(view->mapToGlobal(hit->toPoint())),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&release);flush();
        const auto refs=point->populated_references();
        if (!verify(!refs.empty() && refs.front().owner_id==box.id && refs.front().semantic_key==chosen.semantic_key,
                "Curve/Sweep point did not store the exact picked endpoint")) return 1;
        point->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        if (!verify(dialog()==parent && parent->pending_value().curve_points.size()==1,
                "Curve/Sweep point reference failed to validate in Body")) return 1;
        const auto located=parent->pending_value().curve_points.front().origin;
        if (!verify(std::hypot(std::hypot(located.x-expected.x,located.y-expected.y),located.z-expected.z)<1e-7,
                "Endpoint reference did not resolve in the translated Body frame")) return 1;
        parent->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        const auto canceled=PartDocument::load(path);
        if (!verify(canceled.history.size()==1 && canceled.constructions.empty(),
                "Cancel persisted the Curve/Sweep reference preview")) return 1;
    }
    std::cout << "Body Curve/Sweep reference contracts passed\n";
    return 0;
}

int verify_body_pending_tree(QApplication& application, const std::filesystem::path& directory) {
    using namespace zima::document;
    auto document = PartDocument::create_default();
    const auto box = PartDocument::create_box_container();
    auto other_box = PartDocument::create_box_container(); other_box.placement.x = 80;
    document.history = {box, other_box};
    BodyHistoryGraph graph;
    const auto body_id = graph.create_body("Active body");
    graph.insert({PartHistoryKind::Feature, box.id});
    const auto other_body_id = graph.create_body("Other body");
    graph.insert({PartHistoryKind::Feature, other_box.id});
    graph.activate(body_id); document.set_body_history(graph);
    zima::kernel::OcctKernel kernel;
    const auto path = directory / "body-pending-tree.prtz";
    document.save(path, kernel.evaluate_history(document.kernel_operations()));
    zima::app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));
    window.resize(1100, 850); window.show();
    if (!verify(window.open_document_path(QString::fromStdString(path.string())),
            "Cannot open body pending Tree fixture")) return 1;
    const auto flush = [&] {
        application.processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
    };
    flush();
    auto* tree = window.findChild<QTreeWidget*>("documentTree");
    const auto pending_rows = [&] {
        std::vector<QTreeWidgetItem*> rows;
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->data(0, Qt::UserRole + 12).toBool()) rows.push_back(*it);
        return rows;
    };
    const auto unique_in_body = [&](const std::string& id, const std::string& body) {
        int count = 0; bool owned = false;
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            const auto role = (*it)->data(0, Qt::UserRole + 3).toString();
            if (role != "part-container" && role != "part-construction" && role != "part-sketch") continue;
            if ((*it)->data(0, Qt::UserRole).toString().toStdString() != id) continue;
            ++count;
            owned = (*it)->parent() && (*it)->parent()->data(0, Qt::UserRole + 3).toString() == "part-body" &&
                (*it)->parent()->data(0, Qt::UserRole).toString().toStdString() == body;
        }
        return count == 1 && owned;
    };
    const auto visible_dialog = [&]() -> QDialog* {
        for (auto* child : window.findChildren<QDialog*>()) if (child->isVisible()) return child;
        return nullptr;
    };
    for (const char* name : {"sweep2dAction", "sweep3DAction", "helicalSweepAction", "boxAction",
            "threadAction", "sketchAction", "extrusionAction", "revolutionAction", "curve3DAction",
            "constructionPointAction", "constructionAxisAction", "constructionPlaneAction", "shellAction"}) {
        tree->clearSelection(); tree->setCurrentItem(nullptr);
        auto* action = window.findChild<QAction*>(name);
        if (!verify(action && action->isEnabled(), name)) return 1;
        action->trigger(); flush();
        auto* dialog = visible_dialog();
        const auto rows = pending_rows();
        if (!dialog || rows.size() != 1) {
            std::cerr << name << ": visible dialog=" << (dialog != nullptr) << ", pending rows=" << rows.size() << '\n';
            return verify(false, "Expected one pending row after command activation") ? 0 : 1;
        }
        const auto id = rows.front()->data(0, Qt::UserRole).toString().toStdString();
        if (!verify(unique_in_body(id, body_id), "Pending feature duplicated or outside its owning body")) return 1;
        if (!verify(part_insertion_marker_count(tree) == 0, "Insert here remained visible during creation")) return 1;
        if (std::string_view(name) == "sweep2dAction")
            window.grab().save(QString::fromStdString((directory / "body-pending-tree.png").string()));
        window.findChild<QAction*>("showAxesAction")->trigger(); flush();
        if (!verify(pending_rows().size() == 1 && unique_in_body(id, body_id),
                "Tree refresh duplicated or moved the pending feature")) return 1;
        if (!verify(part_insertion_marker_count(tree) == 0, "Tree refresh restored Insert here during creation")) return 1;
        if (std::string_view(name) == "sweep2dAction" || std::string_view(name) == "helicalSweepAction") {
            auto* open_sketch = dialog->findChild<QPushButton*>(
                std::string_view(name) == "sweep2dAction" ? "sweep2dSketch0" : "helicalSketch0");
            if (!verify(open_sketch, "Missing owned Sketch button in pending container")) return 1;
            open_sketch->click(); flush();
            if (!verify(part_insertion_marker_count(tree) == 0,
                    "Owned Sketch editor restored Insert here while its container was pending")) return 1;
            window.findChild<QAction*>("finishSketchAction")->trigger(); flush();
            if (!verify(dialog->isVisible() && part_insertion_marker_count(tree) == 0,
                    "Returning from owned Sketch restored Insert here before container confirmation")) return 1;
        }
        if (std::string_view(name) == "sweep3DAction" || std::string_view(name) == "curve3DAction") {
            dialog->findChild<QPushButton*>("curve3DAddPoint")->click(); flush();
            auto* point_dialog = visible_dialog();
            if (!verify(point_dialog && point_dialog != dialog && part_insertion_marker_count(tree) == 0,
                    "Nested Point editor restored Insert here")) return 1;
            point_dialog->reject(); flush();
            if (!verify(dialog->isVisible() && part_insertion_marker_count(tree) == 0,
                    "Point Cancel restored Insert here before the parent finished")) return 1;
        }
        dialog->reject(); flush();
        if (!verify(part_insertion_marker_count(tree) == 1, "Cancel did not restore the single body insertion marker")) return 1;
        if (!verify(pending_rows().empty() && unique_in_body(box.id, body_id),
                "Cancel retained the pending row or changed committed body history")) return 1;
    }
    QTreeWidgetItem* edited = nullptr;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->data(0, Qt::UserRole + 3).toString() == "part-container" &&
                (*it)->data(0, Qt::UserRole).toString().toStdString() == other_box.id) edited = *it;
    if (!verify(edited, "Other body fixture is absent from Tree")) return 1;
    window.show_tree_item_properties(edited); flush();
    auto* dialog = visible_dialog();
    if (!verify(dialog && pending_rows().size() == 1 && unique_in_body(other_box.id, other_body_id),
            "Editing another body's feature projected a duplicate under the active body")) return 1;
    if (!verify(part_insertion_marker_count(tree) == 0,
            "Editing another body's feature retained Insert here in the active body")) return 1;
    dialog->reject(); flush();
    if (!verify(part_insertion_marker_count(tree) == 1, "Edit Cancel failed to restore Insert here")) return 1;
    window.findChild<QAction*>("saveDocumentAction")->trigger(); flush();
    const auto saved = PartDocument::load(path);
    if (!verify(saved.history.size() == 2 && saved.body_history.bodies().size() == 2,
            "Cancelled feature creation changed persisted body history")) return 1;
    std::cout << "Body pending Tree ownership contracts passed\n";
    return 0;
}

int verify_pending_container_tree(QApplication& application,
    const std::filesystem::path& directory) {
    if (verify_body_pending_tree(application, directory) != 0) return 1;
    using namespace zima::document;
    for (const bool assembly : {false, true}) {
        const auto path = directory / (assembly ? "pending-tree.asmz" : "pending-tree.prtz");
        if (assembly) zima::assembly::AssemblyDocument::create_default().save(path);
        else PartDocument::create_default().save(path);
        zima::app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));
        window.resize(1100, 850); window.show();
        if (!verify(window.open_document_path(QString::fromStdString(path.string())),
                "Cannot open pending Tree fixture")) return 1;
        const auto flush = [&] {
            application.processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            application.processEvents();
        };
        flush();
        auto* tree = window.findChild<QTreeWidget*>("documentTree");
        const auto find = [&](const QString& role, const std::string& id = {}) -> QTreeWidgetItem* {
            QTreeWidgetItemIterator iterator(tree);
            while (*iterator) {
                auto* row = *iterator++;
                if (row->data(0, Qt::UserRole + 3).toString() == role &&
                    (id.empty() || row->data(0, Qt::UserRole).toString().toStdString() == id)) return row;
            }
            return nullptr;
        };
        const auto marker = assembly ? "assembly-insert-here" : "part-insert-here";
        const auto dialog = [&]() -> zima::app::ConstructionPropertiesDialog* {
            for (auto* child : window.findChildren<QDialog*>())
                if (auto* value = dynamic_cast<zima::app::ConstructionPropertiesDialog*>(child);
                    value && value->isVisible()) return value;
            return nullptr;
        };
        const auto click_row = [&](QTreeWidgetItem* row) {
            tree->clearSelection(); tree->setCurrentItem(row);
            flush();
        };
        if (!verify(find(marker), "Idle Tree has no insertion marker")) return 1;
        window.findChild<QAction*>(assembly ? "curve3DAction" : "sweep3DAction")->trigger();
        flush();
        auto* parent_dialog = dialog();
        if (!verify(parent_dialog, "Missing pending Curve/Sweep dialog")) return 1;
        const auto parent_id = assembly ? parent_dialog->pending_value().id
            : parent_dialog->pending_sweep_value().id;
        const auto origin_id = assembly ? parent_dialog->pending_value().container_origin.id
            : parent_dialog->pending_sweep_value().container_origin.id;
        auto* pending = find(assembly ? "assembly-construction" : "part-container", parent_id);
        if (!verify(pending && pending->foreground(0).color() == QColor(70,190,95) &&
                !find(marker), "Pending container did not replace insertion marker in green")) return 1;
        click_row(find("construction-origin", origin_id));
        if (!verify(parent_dialog->populated_references().empty(),
                "Container accepted its own local origin and introduced a cycle")) return 1;
        click_row(find("document-origin"));
        if (!verify(parent_dialog->populated_references().size() == 3,
                "Document Origin did not fill the parent placement triad")) return 1;
        if (!verify(parent_dialog->set_inline_parameter_value("reference_offset:0", 12.5),
                "Cannot offset the pending parent fixture")) return 1;
        flush();
        parent_dialog->findChild<QPushButton*>("curve3DAddPoint")->click(); flush();
        auto* point = dialog();
        if (!verify(point && point != parent_dialog && find("construction-origin", origin_id) &&
                !find(marker), "Nested Point lost its pending parent origin in Tree")) return 1;
        const auto point_id = point->pending_value().id;
        auto* origin_mode = point->findChild<QPushButton*>("containerOriginSelectionButton");
        auto* model_view = dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
        if (!verify(origin_mode && model_view, "Nested Point has no shared Origin action")) return 1;
        const auto reference_contract = model_view->selection_contract();
        origin_mode->click(); flush();
        if (!verify(origin_mode->isChecked() && model_view->selection_contract() ==
                std::vector{zima::viewer::CandidateKind::Container},
                "Nested Point Origin action did not enter origin selection")) return 1;
        origin_mode->click(); flush();
        if (!verify(!origin_mode->isChecked() && model_view->selection_contract() == reference_contract,
                "Origin action did not restore nested Point reference entry")) return 1;

        click_row(find("construction-origin", point->pending_value().container_origin.id));
        if (!verify(point->populated_references().empty(), "Point accepted its own Origin")) return 1;
        click_row(find("construction-origin", origin_id));
        if (!verify(point->populated_references().size() == 3,
                "Whole pending local Origin did not populate all three Point references")) return 1;
        const auto local = point->pending_value().origin;
        if (!verify(std::abs(local.x) + std::abs(local.y) + std::abs(local.z) < 1e-8,
                "Point on an offset parent Origin was not resolved in the parent frame")) return 1;
        for (const auto& reference : point->populated_references())
            if (!verify(reference.owner_id == origin_id,
                    "Point reference was redirected away from its parent Origin")) return 1;
        if (!verify(find("curve3d-point", point_id), "Pending Point is missing from its parent Tree")) return 1;
        point->buttons()->button(QDialogButtonBox::Ok)->click(); flush();
        if (!verify(parent_dialog->isVisible() && parent_dialog->pending_value().curve_points.size() == 1,
                "Point OK did not return to the pending parent")) return 1;
        parent_dialog->buttons()->button(QDialogButtonBox::Cancel)->click(); flush();
        if (!verify(find(marker) && !find(assembly ? "assembly-construction" : "part-container", parent_id),
                "Cancel retained a draft or failed to restore insertion marker")) return 1;
        window.findChild<QAction*>("saveDocumentAction")->trigger(); flush();
        if (assembly) {
            if (!verify(zima::assembly::AssemblyDocument::load(path).constructions.empty(),
                    "Cancelled Assembly draft was persisted")) return 1;
        } else if (!verify(PartDocument::load(path).find_container(parent_id) == nullptr,
                "Cancelled Part draft was persisted")) return 1;

        window.findChild<QAction*>("constructionPointAction")->trigger(); flush();
        auto* created = dialog();
        const auto created_id = created->pending_value().id;
        click_row(find("document-origin"));
        created->buttons()->button(QDialogButtonBox::Ok)->click(); flush();
        auto* committed = find(assembly ? "assembly-construction" : "part-construction", created_id);
        if (!verify(committed && find(marker), "OK did not keep the container and restore marker")) return 1;
        window.show_tree_item_properties(committed); flush();
        if (!verify(dialog() && !find(marker), "Editing an existing container retained insertion marker")) return 1;
        dialog()->buttons()->button(QDialogButtonBox::Cancel)->click(); flush();
        if (!verify(find(marker) && find(assembly ? "assembly-construction" : "part-construction", created_id),
                "Edit Cancel removed the committed container")) return 1;
        if (!assembly) {
            window.findChild<QAction*>("boxAction")->trigger(); flush();
            zima::app::PrimitivePropertiesDialog* box_dialog{};
            for (auto* child : window.findChildren<QDialog*>())
                if (auto* value = dynamic_cast<zima::app::PrimitivePropertiesDialog*>(child);
                    value && value->isVisible()) box_dialog = value;
            if (!verify(box_dialog, "Missing box fixture for Origin action")) return 1;
            box_dialog->buttons()->button(QDialogButtonBox::Ok)->click(); flush();
            window.findChild<QAction*>("shellAction")->trigger(); flush();
            zima::app::PrimitivePropertiesDialog* shell_dialog{};
            for (auto* child : window.findChildren<QDialog*>())
                if (auto* value = dynamic_cast<zima::app::PrimitivePropertiesDialog*>(child);
                    value && value->isVisible()) shell_dialog = value;
            if (!verify(shell_dialog, "Missing Shell properties for Origin action")) return 1;
            auto* shell_origin = shell_dialog->findChild<QPushButton*>("containerOriginSelectionButton");
            if (!verify(shell_origin, "Shell properties have no Origin action")) return 1;
            const auto face_contract = model_view->selection_contract();
            shell_origin->click(); flush();
            if (!verify(model_view->selection_contract() == std::vector{zima::viewer::CandidateKind::Container},
                    "Shell Origin action did not suspend face picking")) return 1;
            shell_origin->click(); flush();
            if (!verify(model_view->selection_contract() == face_contract,
                    "Shell Origin action failed to restore face picking")) return 1;
            shell_dialog->buttons()->button(QDialogButtonBox::Cancel)->click(); flush();
        }
        // Closing the application with a nested editor must retire its hidden parent too.
        window.findChild<QAction*>(assembly ? "curve3DAction" : "sweep3DAction")->trigger(); flush();
        dialog()->findChild<QPushButton*>("curve3DAddPoint")->click(); flush();
        dialog()->findChild<QPushButton*>("containerOriginSelectionButton")->click(); flush();
    }
    std::cout << "Pending container Tree contracts passed\n";
    return 0;
}

int verify_part_deletion(QApplication& application, const std::filesystem::path& directory) {
    const auto source = qEnvironmentVariable("ZIMA_VERIFY_DELETE_DOCUMENT").toStdString();
    const auto original = zima::document::PartDocument::load(source);
    QTemporaryDir temporary(QString::fromStdString((directory / "delete-part-XXXXXX").string()));
    if (!verify(temporary.isValid(), "Cannot create isolated deletion directory")) return 1;
    const auto path = std::filesystem::path(temporary.path().toStdString()) / "delete.prtz";
    for (auto feature = original.history.rbegin(); feature != original.history.rend(); ++feature) {
        std::filesystem::copy_file(source, path, std::filesystem::copy_options::overwrite_existing);
        zima::app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));
        window.resize(1100,850); window.show();
        if (!verify(window.open_document_path(QString::fromStdString(path.string())), "Cannot open deletion fixture")) return 1;
        application.processEvents();
        auto* tree = window.findChild<QTreeWidget*>("documentTree");
        auto* view = dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
        const auto row = [&](const std::string& id) -> QTreeWidgetItem* {
            for (QTreeWidgetItemIterator it(tree); *it; ++it)
                if ((*it)->data(0,Qt::UserRole).toString().toStdString() == id) return *it;
            return nullptr;
        };
        for (const auto& body : original.body_history.bodies()) {
            auto* body_item = row(body.scope.id);
            if (!body_item) continue;
            tree->setCurrentItem(body_item); body_item->setSelected(true);
            application.processEvents();
            const auto confirmed = view->confirmed_candidate();
            QMouseEvent move(QEvent::MouseMove,QPointF(10,10),QPointF(10,10),QPointF(10,10),
                Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&move);
            if (!verify(confirmed && view->confirmed_candidate() == confirmed,
                    "Selected body disappeared on pointer movement")) return 1;
            QMouseEvent middle(QEvent::MouseButtonDblClick,QPointF(10,10),QPointF(10,10),QPointF(10,10),
                Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
            QApplication::sendEvent(view,&middle);
            if (!verify(!view->confirmed_candidate() && tree->selectedItems().empty(),
                    "Middle double-click did not clear View and Tree together")) return 1;
        }
        auto* item = row(feature->id);
        if (!verify(item && view, "Missing deletion tree/view")) return 1;
        for (auto* parent = item->parent(); parent; parent=parent->parent()) tree->expandItem(parent);
        tree->setCurrentItem(item); item->setSelected(true); tree->scrollToItem(item);
        application.processEvents();
        const auto selected = view->confirmed_candidate();
        QMouseEvent move(QEvent::MouseMove,QPointF(10,10),QPointF(10,10),QPointF(10,10),
            Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&move);
        if (!verify(view->confirmed_candidate() == selected, "Tree selection vanished on hover")) return 1;
        QString failure;
        bool invoked = false;
        QTimer messages;
        QObject::connect(&messages,&QTimer::timeout,[&] {
            if (auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                if (box->standardButtons().testFlag(QMessageBox::Yes)) box->button(QMessageBox::Yes)->click();
                else { failure=box->text(); box->accept(); }
            }
        });
        messages.start(20);
        QTimer::singleShot(0,&window,[&] {
            auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu) return;
            for (auto* action : menu->actions()) if (action->text() == QStringLiteral("Odstranit")) {
                invoked=true; menu->setActiveAction(action);
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
                QApplication::sendEvent(menu,&enter); return;
            }
            menu->close();
        });
        tree->customContextMenuRequested(tree->visualItemRect(item).center());
        application.processEvents();
        if (!verify(invoked && failure.isEmpty() && !row(feature->id), "Tree deletion failed")) {
            std::cerr << "delete=" << feature->id << " invoked=" << invoked << " row=" << (row(feature->id)!=nullptr) << " error=" << failure.toStdString() << std::endl; return 1;
        }
        window.findChild<QAction*>("undoAction")->trigger(); application.processEvents();
        if (!verify(row(feature->id) != nullptr, "Undo failed to restore deleted feature")) return 1;
        window.findChild<QAction*>("redoAction")->trigger(); application.processEvents();
        if (!verify(row(feature->id) == nullptr, "Redo failed to delete feature")) return 1;
        window.findChild<QAction*>("saveDocumentAction")->trigger();
        application.processEvents();
        if (!verify(failure.isEmpty(), "Deleted document could not be saved")) return 1;
        const auto saved=zima::document::PartDocument::load(path);
        saved.validate_body_ownership();
        if (!verify(saved.history.size()+1 == original.history.size() &&
                !saved.body_history.owner(feature->id), "Deletion changed unrelated history")) return 1;
    }
    std::cout << "Part deletion and persistent selection contracts passed\n";
    return 0;
}

int verify_reopened_sweep_document(QApplication& application, const std::filesystem::path& directory) {
    const auto source=qEnvironmentVariable("ZIMA_VERIFY_REOPEN_DOCUMENT").toStdString();
    QTemporaryDir temporary(QString::fromStdString((directory/"reopen-sweep-XXXXXX").string()));
    if(!verify(temporary.isValid(),"Cannot create isolated reopen directory"))return 1;
    const auto path=std::filesystem::path(temporary.path().toStdString())/"reopened.prtz";
    std::filesystem::copy_file(source,path);
    const auto original=zima::document::PartDocument::load(path);
    for(int pass=0;pass<3;++pass) {
        zima::app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));
        window.resize(1100,850);window.show();
        if(!verify(window.open_document_path(QString::fromStdString(path.string())),"Cannot reopen Sweep document"))return 1;
        application.processEvents();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        for(const auto& feature:original.history) {
            if(feature.feature_kind!=zima::document::FeatureKind::Sweep3D)continue;
            QTreeWidgetItem* row=nullptr;
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id &&
                    (*it)->data(0,Qt::UserRole+3).toString()=="part-container") {row=*it;break;}
            if(!verify(row && !row->data(0,zima::app::missing_reference_role).toBool(),
                "Sweep has a false missing-reference warning after reopening"))return 1;
        }
        window.findChild<QAction*>("saveDocumentAction")->trigger();application.processEvents();
        const auto saved=zima::document::PartDocument::load(path);
        if(!verify(saved.history==original.history,"Save/reopen changed Sweep definitions"))return 1;
    }
    std::cout << "Sweep document save/reopen contracts passed\n";return 0;
}

int verify_body_placement_offsets(QApplication& application, const std::filesystem::path& directory) {
    using namespace zima::document;
    for (const bool moved : {false,true}) {
        auto part=PartDocument::create_default();
        auto box=PartDocument::create_box_container();part.history.push_back(box);
        BodyHistoryGraph graph;const auto body_id=graph.create_body("Body");
        graph.insert({PartHistoryKind::Feature,box.id});
        if (moved) { auto body=*graph.find(body_id);body.scope.placement={80,40,25};
            body.scope.placement.absolute_rotation_y=90;body.scope.placement.rotation_y=90;graph.update_body(body); }
        part.set_body_history(graph);
        const auto path=directory/"body-placement-offsets.prtz";
        zima::kernel::OcctKernel kernel;part.save(path,kernel.evaluate_history(part.kernel_operations()));
        zima::app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1100,850);window.show();
        if (!verify(window.open_document_path(QString::fromStdString(path.string())),"Cannot open offset fixture")) return 1;
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        const auto row=[&](const std::string& id)->QTreeWidgetItem* {
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==id)return *it;
            return nullptr;
        };
        for (const auto* action_name : {"boxAction","extrusionAction","revolutionAction","constructionPointAction","sweep3DAction","sweep2dAction","helicalSweepAction"}) {
            if (std::string_view(action_name)=="boxAction") window.show_tree_item_properties(row(box.id));
            else {auto* action=window.findChild<QAction*>(action_name);if(!verify(action!=nullptr,"Missing offset fixture command"))return 1;action->trigger();}
            flush();
            zima::ui::PropertiesSubWindow* dialog=nullptr;
            for(auto* candidate:window.findChildren<QDialog*>())
                if(auto* value=dynamic_cast<zima::ui::PropertiesSubWindow*>(candidate);value&&value->isVisible())dialog=value;
            if(!verify(dialog!=nullptr,"Missing offset dialog"))return 1;
            if (std::string_view(action_name)=="extrusionAction" || std::string_view(action_name)=="revolutionAction") {
                const auto* primitive=dynamic_cast<zima::app::PrimitivePropertiesDialog*>(dialog);
                const auto* view=dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
                if(!verify(primitive && view && std::ranges::none_of(view->mesh().axes,[&](const auto& axis) {
                        return axis.reference.owner_id==primitive->pending_value().feature_id &&
                            axis.reference.semantic_key=="axis";
                    }),"Profile feature still displays the extra cyan preview axis"))return 1;
            }
            zima::ui::ContainerPlacementSection* placement=nullptr;
            for(auto* child:dialog->findChildren<QObject*>())
                if(auto* value=dynamic_cast<zima::ui::ContainerPlacementSection*>(child))placement=value;
            if(!verify(placement!=nullptr,"Missing shared placement section"))return 1;
            tree->clearSelection();tree->setCurrentItem(row(body_id+":origin"));flush();
            if(!verify(placement->populated_references().size()==3,"Body Origin did not populate triad"))return 1;
            for(auto* field:placement->translation_fields())
                if(!verify(!field->isEnabled(),"Body Origin left XYZ editable")){std::cerr<<action_name<<'\n';return 1;}
            for(auto* field:placement->rotation_fields())
                if(field && !verify(!field->isEnabled(),"Body Origin left absolute rotation editable")){std::cerr<<action_name<<'\n';return 1;}
            if(!verify(placement->set_reference_offset(2,25.25),"Body plane offset is disabled"))return 1;flush();
            if(!verify(std::abs(placement->numeric_placement().x-25.25)<1e-8 &&
                    std::abs(placement->numeric_placement().y)<1e-8 && std::abs(placement->numeric_placement().z)<1e-8,
                    "Body plane offset was not resolved in Body-local coordinates")){std::cerr<<action_name<<'\n';return 1;}
            dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        }
    }
    std::cout<<"Body placement offset UI contracts passed\n";return 0;
}

int verify_save_copy_ui(QApplication& application, const std::filesystem::path& parent_directory) {
    using zima::document::PartDocument;
    QTemporaryDir temporary(QString::fromStdString((parent_directory/"save-copy-ui-XXXXXX").string()));
    if (!verify(temporary.isValid(),"Cannot create isolated Save As fixture directory")) return 1;
    const std::filesystem::path directory=temporary.path().toStdString();
    auto part=PartDocument::create_default();
    part.user_parameters["COPY_SOURCE"]=part.document_id;
    part.history.push_back(PartDocument::create_box_container());
    zima::kernel::OcctKernel kernel;
    const auto boundaries=kernel.evaluate_history(part.kernel_operations());
    const auto source=directory/"save-copy-source.prtz";
    const auto target=directory/"save-copy-target.prtz";
    part.save(source,boundaries);
    auto drawing=zima::drawing::DrawingDocument::create_default();
    drawing.source_document_id=part.document_id;drawing.source_path=source;
    drawing.sheets.front().views.push_back(zima::drawing::DrawingDocument::create_view(
        part.document_id,source,boundaries.back().mesh,zima::drawing::ViewOrientation::Front));
    drawing.save(directory/"save-copy-source.drwz");
    zima::app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));
    window.resize(1100,850);window.show();
    if (!verify(window.open_document_path(QString::fromStdString(source.string())),
            "Cannot open Save As fixture")) return 1;
    application.processEvents();
    auto* tabs=window.findChild<QTabBar*>("documentTabs");
    const auto tab_count=tabs->count();
    const auto tab_label=tabs->tabText(tabs->currentIndex());
    bool chose_file=false;
    QTimer::singleShot(0,&window,[&] {
        auto* dialog=window.findChild<QFileDialog*>();
        if (!dialog) return;
        dialog->selectFile(QString::fromStdString(target.string()));
        chose_file=true;
        QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    window.findChild<QAction*>("saveDocumentAsAction")->trigger();
    application.processEvents();
    if (!verify(chose_file && std::filesystem::exists(target) &&
            std::filesystem::exists(directory/"save-copy-target.drwz"),
            "Save As action failed to publish model and Drawing copies")) return 1;
    const auto copy=PartDocument::load(target);
    const auto drawing_copy=zima::drawing::DrawingDocument::load(directory/"save-copy-target.drwz");
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    if (!verify(copy.document_id!=part.document_id && copy.user_parameters.at("COPY_SOURCE")==part.document_id &&
            drawing_copy.source_document_id==copy.document_id &&
            tree->topLevelItem(0)->data(0,Qt::UserRole).toString().toStdString()==part.document_id &&
            tabs->count()==tab_count && tabs->tabText(tabs->currentIndex())==tab_label,
            "Save As changed the original active document or detached its copied Drawing")) return 1;
    std::cout << "Save Copy UI contracts passed\n";
    return 0;
}

int verify_body_history_ui(QApplication& application, const std::filesystem::path& directory) {
    using namespace zima::document;
    auto part = PartDocument::create_default();
    part.document_precision["decimal_places"]="2";
    auto first = PartDocument::create_box_container(); first.box = {10,10,10};
    auto second = PartDocument::create_box_container(); second.box = {10,10,10};
    auto extra = PartDocument::create_box_container(); extra.box = {2,2,2}; extra.placement.x = 20;
    part.history = {first,second,extra};
    BodyHistoryGraph graph;
    const auto a = graph.create_body("Polotovar"); graph.insert({PartHistoryKind::Feature, first.id});
    const auto b = graph.create_body("Nástroj"); graph.insert({PartHistoryKind::Feature, second.id});
    graph.insert({PartHistoryKind::Feature,extra.id});
    auto tool = *graph.find(b); tool.scope.placement.x = 5; graph.update_body(tool);
    part.set_body_history(graph);
    zima::kernel::OcctKernel kernel;
    const auto path = directory / "body-ui.prtz";
    part.save(path, kernel.evaluate_history(part.kernel_operations()));
    zima::app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));
    window.resize(1100,850); window.show();
    const auto flush = [&] { application.processEvents(); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete); application.processEvents(); };
    if (!verify(window.open_document_path(QString::fromStdString(path.string())), "Cannot open body UI fixture")) return 1;
    flush();
    auto* tree = window.findChild<QTreeWidget*>("documentTree");
    const auto row = [&](const char* kind, const std::string& id = {}) -> QTreeWidgetItem* {
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->data(0,Qt::UserRole+3).toString() == kind &&
                (id.empty() || (*it)->data(0,Qt::UserRole).toString().toStdString() == id)) return *it;
        return nullptr;
    };
    const auto body_dialog = [&]() -> zima::app::BodyPropertiesDialog* {
        for (auto* dialog : window.findChildren<QDialog*>())
            if (auto* body = dynamic_cast<zima::app::BodyPropertiesDialog*>(dialog); body && body->isVisible()) return body;
        return nullptr;
    };
    if (!verify(row("part-body",a) && row("part-body",b) && row("part-container",first.id)->parent() == row("part-body",a),
            "Tree did not group features into their body")) return 1;
    if (!verify(!tree->rootIndex().isValid() && tree->topLevelItem(0)->text(0) == "body-ui.prtz", "Part filename root is hidden")) return 1;
    tree->setCurrentItem(tree->topLevelItem(0)); flush();
    zima::viewer::MeshView* viewer = nullptr;
    for (auto* widget : window.findChildren<QWidget*>())
        if (auto* view = dynamic_cast<zima::viewer::MeshView*>(widget)) { viewer = view; break; }
    if (!verify(viewer && !viewer->confirmed_component_wire().empty(), "Document root did not highlight the stored result")) return 1;
    zima::kernel::ViewerDimension clipped_dimension;
    clipped_dimension.reference={"projection-check","parameter:placement:x",{}};
    clipped_dimension.value=1;
    clipped_dimension.witness_first={1e308,0,0};
    clipped_dimension.witness_second={1e308,1,0};
    clipped_dimension.line_first={1e308,0,1};
    clipped_dimension.line_second={1e308,1,1};
    viewer->set_transient_dimensions({clipped_dimension});
    viewer->repaint();
    if (!verify(!viewer->grabFramebuffer().isNull(),
            "Unprojectable dimension interrupted viewport rendering")) return 1;
    viewer->set_transient_dimensions({});
    zima::viewer::ViewerCandidate passive_candidate;
    passive_candidate.owner_id = first.id;
    if (!verify(viewer->candidate_filter() && !viewer->candidate_filter()(passive_candidate),
            "Inactive body is offered as an editable ordinary candidate")) return 1;
    passive_candidate.owner_id = second.id;
    if (!verify(viewer->candidate_filter()(passive_candidate), "Active body was excluded from ordinary selection")) return 1;
    const auto activate_from_tree = [&](QTreeWidgetItem* item, const char* action_name) {
        if (!item) return false;
        for (auto* parent=item->parent();parent;parent=parent->parent()) tree->expandItem(parent);
        tree->scrollToItem(item);
        bool invoked=false;
        QTimer::singleShot(0,&window,[&] {
            auto* menu=window.findChild<QMenu*>("partActivationMenu");
            if (!menu) return;
            auto* action=menu->findChild<QAction*>(action_name);
            if (!action || !action->isEnabled()) { menu->close();return; }
            invoked=true;menu->setActiveAction(action);
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
            QApplication::sendEvent(menu,&enter);
        });
        tree->customContextMenuRequested(tree->visualItemRect(item).center());flush();
        return invoked;
    };
    const auto click_command = [&](const char* name) {
        auto* action=window.findChild<QAction*>(name);
        if (!action || !action->isEnabled()) return false;
        for (auto* button : window.findChildren<QToolButton*>())
            if (button->defaultAction()==action && button->isVisible() && button->isEnabled()) {
                button->click();flush();return true;
            }
        return false;
    };
    if (!verify(activate_from_tree(tree->topLevelItem(0),"activatePartAction") &&
            !row("part-insert-here") && row("part-body-insert-here"),
            "Part root context menu did not activate document-level modeling")) return 1;
    const auto origin_extent = [&](const std::string& owner) {
        double extent = 0;
        for (const auto& axis : viewer->mesh().axes)
            if (axis.reference.owner_id == owner && axis.reference.semantic_key.starts_with("origin:"))
                extent = std::max(extent, axis.display_length);
        return extent;
    };
    const auto document_origin_extent = origin_extent(part.document_id + ":origin");
    if (!verify(document_origin_extent > 0 && origin_extent(a+":origin")==0 && origin_extent(b+":origin")==0,
            "Document activation must display only Document Origin")) return 1;
    if (!verify(activate_from_tree(row("part-body",a),"activateBodyAction"),
            "First Body context menu did not activate its history")) return 1;
    if (!verify(origin_extent(a+":origin") > 0 && origin_extent(b+":origin")==0 &&
            origin_extent(part.document_id+":origin")==0 &&
            std::abs(document_origin_extent/origin_extent(a+":origin")-1.25)<1e-9,
            "Active Body origin visibility or Document/Body size ratio is incorrect")) return 1;
    window.findChild<QAction*>("constructionPointAction")->trigger();flush();
    zima::app::ConstructionPropertiesDialog* origin_point_dialog = nullptr;
    for (auto* dialog : window.findChildren<QDialog*>())
        if (auto* value=dynamic_cast<zima::app::ConstructionPropertiesDialog*>(dialog); value && value->isVisible())
            origin_point_dialog=value;
    if (!verify(origin_point_dialog!=nullptr,"Origin policy fixture could not open Point")) return 1;
    zima::viewer::ViewerCandidate forbidden_origin;
    forbidden_origin.kind=zima::viewer::CandidateKind::Plane;
    forbidden_origin.geometry=zima::viewer::CandidateGeometry::OriginalReference;
    forbidden_origin.owner_id=part.document_id+":origin";
    forbidden_origin.semantic_key="origin:plane:xy";
    if (!verify(viewer->candidate_filter() && !viewer->candidate_filter()(forbidden_origin),
            "Document Origin is offered to a feature inside a Body")) return 1;
    tree->setCurrentItem(row("document-origin",part.document_id+":origin"));flush();
    if (!verify(origin_point_dialog->references_without(99).empty(),
            "Tree Document Origin bypassed Body feature placement policy")) return 1;
    auto* origin_button=origin_point_dialog->findChild<QPushButton*>("containerOriginSelectionButton");
    origin_button->click();flush();
    auto* other_body_row=row("part-body",b);
    tree->itemClicked(other_body_row,0);flush();
    if (!verify(origin_extent(b+":origin")>0,"Origin mode could not reveal another Body origin")) return 1;
    origin_button->click();flush();
    tree->setCurrentItem(row("document-origin",a+":origin"));flush();
    if (!verify(origin_point_dialog->references_without(99).size()>=3,
            "Feature did not accept the owning Body Origin shortcut")) return 1;
    origin_point_dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
    if (!verify(origin_extent(b+":origin")==0 && origin_extent(a+":origin")>0,
            "Cancel retained a temporarily exposed inactive Body origin")) return 1;

    passive_candidate.owner_id=first.id;
    if (!verify(viewer->candidate_filter()(passive_candidate) && row("part-insert-here"),
            "Activating first Body did not restore its tools and selection")) return 1;
    if (!verify(activate_from_tree(row("part-body",b),"activateBodyAction"),
            "Second Body context menu did not activate its history")) return 1;
    if (!verify(!window.findChild<QAction*>("createBodyAction")->isEnabled() &&
            !row("part-body-insert-here") && row("part-insert-here"),
            "Active Body exposes document-level creation or a second insertion cursor")) return 1;
    for(auto* button:window.findChildren<QToolButton*>())
        if(!verify(button->defaultAction()!=window.findChild<QAction*>("createBodyAction") || !button->isVisible(),
                "Create Body button is visible inside active Body"))return 1;
    if(!verify(activate_from_tree(tree->topLevelItem(0),"activatePartAction"),"Cannot activate Part before new Body"))return 1;
    if (!verify(click_command("createBodyAction"),"Visible Create Body button is disabled or missing")) return 1;
    if (!verify(body_dialog() && body_dialog()->windowFlags().testFlag(Qt::SubWindow), "Body creation did not open shared properties")) return 1;
    if (!verify(body_dialog()->findChild<QPushButton*>("containerOriginSelectionButton")!=nullptr,
            "Body placement lost its Origin control")) return 1;
    for (auto* field : body_dialog()->findChildren<QDoubleSpinBox*>())
        if (!verify(field->decimals()==2,"Body placement field ignored document precision")) return 1;
    tree->setCurrentItem(row("document-origin",part.document_id+":origin"));flush();
    if (!verify(body_dialog()->first_empty_position_index()==3,
            "New Body did not accept all three document Origin planes without arming a field")) return 1;
    const auto picked_body=body_dialog()->pending_value();
    for (std::size_t i=0;i<3;++i)
        if (!verify(picked_body.scope.placement.references[i].owner_id==part.document_id+":origin",
                "Body Origin shortcut stored a different reference owner")) return 1;
    for (auto* field : body_dialog()->findChildren<QDoubleSpinBox*>())
        if (!verify(field->decimals()==2,"Body reference offset ignored document precision")) return 1;
    body_dialog()->buttons()->button(QDialogButtonBox::Cancel)->click(); flush();
    if (!verify(!row("part-body-boolean"), "Cancel inserted an unrelated Boolean")) return 1;
    if(!verify(activate_from_tree(row("part-body",b),"activateBodyAction"),"Cannot reactivate Body after canceled creation"))return 1;
    window.show_tree_item_properties(row("part-body",b)); flush();
    body_dialog()->findChild<QLineEdit*>("bodyName")->setText("Upravený nástroj");
    auto* body_references=body_dialog()->findChild<QTableWidget*>("bodyReferenceTable");
    if (!verify(body_references!=nullptr,"Body does not use the shared placement reference table")) return 1;
    tree->setCurrentItem(row("document-origin",part.document_id+":origin"));flush();
    if (!verify(body_dialog()->first_empty_position_index()==3,"Body Origin selection did not fill its three planes")) return 1;
    auto* offset=qobject_cast<QDoubleSpinBox*>(body_references->cellWidget(2,2));
    if (!verify(offset && offset->isEnabled(),"Body placement does not expose the plane offset")) return 1;
    offset->setValue(5);flush();
    zima::viewer::ViewerCandidate offset_dimension;
    offset_dimension.kind=zima::viewer::CandidateKind::Dimension;
    offset_dimension.owner_id=b;
    offset_dimension.semantic_key="parameter:placement:reference_offset:2";
    if (!verify(viewer->candidate_dimension_value(offset_dimension)==5,
            "Body reference offset is missing from View")) return 1;
    if (!verify(!viewer->candidate_filter() || viewer->candidate_filter()(offset_dimension),
            "Body placement picker excludes its editable dimension")) return 1;
    if (!verify(!body_dialog()->owns_parameter_owner(second.id),
            "Body properties claim parameters of their child feature")) return 1;
    auto locked_dimension=offset_dimension;
    locked_dimension.semantic_key="parameter:placement:x";
    const auto find_dimension=[&](zima::viewer::ViewerCandidate& candidate) {
        const auto& dimensions=viewer->mesh().dimensions;
        for (std::size_t index=0;index<dimensions.size();++index) {
            if (dimensions[index].reference.owner_id==candidate.owner_id &&
                    dimensions[index].reference.semantic_key==candidate.semantic_key) {
                candidate.geometry_index=index;
                return true;
            }
        }
        return false;
    };
    if (!verify(!find_dimension(locked_dimension) &&
            !body_dialog()->set_inline_parameter_value("placement:x",12),
            "Body offers a locked placement coordinate for editing")) return 1;
    const auto edit_body_offset=[&](const QString& text,double expected) {
        window.edit_dimension_inline(offset_dimension);flush();
        auto* editor=viewer->findChild<QLineEdit*>("inlineDimensionValueEdit");
        if (!editor || !editor->isVisible()) return false;
        editor->setText(text);
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(editor,&enter);flush();
        if (!body_dialog()) return false;
        // Updating constraints rebuilds the reference table's cell editors.
        const auto* current_offset=qobject_cast<QDoubleSpinBox*>(body_references->cellWidget(2,2));
        return current_offset && std::abs(current_offset->value()-expected)<1e-8 &&
            viewer->candidate_dimension_value(offset_dimension)==expected;
    };
    if (!verify(edit_body_offset("3,25",3.25) && edit_body_offset("5.0",5),
            "Body View offset edit did not accept decimal input or prematurely committed properties")) return 1;
    if (!verify(body_dialog()->set_inline_parameter_value("placement:rotation_z",15),
            "Body angular correction is not editable")) return 1;
    flush();
    auto angle_dimension=offset_dimension;
    angle_dimension.semantic_key="parameter:placement:rotation_z";
    if (!verify(find_dimension(angle_dimension) &&
            std::abs(viewer->candidate_dimension_value(angle_dimension).value_or(-999)-15)<1e-8 &&
            body_dialog()->set_inline_parameter_value("placement:rotation_z",0),
            "Body angular correction did not update its View dimension")) return 1;
    flush();
    if (!verify(!find_dimension(angle_dimension),
            "Zero body rotation left a stale View dimension")) return 1;
    if (!verify(!body_dialog()->set_reference(0,{{},tool.origin().id,"origin:point"},"Vlastní počátek"),
            "Body accepted its own Origin as a placement reference")) return 1;

    QMouseEvent confirm_body(QEvent::MouseButtonDblClick,QPointF(40,40),QPointF(40,40),QPointF(40,40),
        Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
    QApplication::sendEvent(&window, &confirm_body); flush();
    if (!verify(!body_dialog() && row("part-body",b)->text(0) == "Upravený nástroj", "Body edit failed to commit")) return 1;
    if (!verify(!viewer->candidate_dimension_value(offset_dimension),
            "Closing body properties left transient placement dimensions in View")) return 1;
    auto* reorder_tree = dynamic_cast<zima::app::HistoryTreeWidget*>(tree);
    if (!verify(reorder_tree && !reorder_tree->reorder_requested(row("part-container",extra.id),QString::fromStdString(first.id),false),
            "Feature drag offered another body as its destination")) return 1;
    if (!verify(reorder_tree->reorder_requested(row("part-container",extra.id),QString::fromStdString(second.id),true),
            "Feature reorder inside the active body failed")) return 1;
    flush();
    auto* boolean_action=window.findChild<QAction*>("bodyBooleanAction");
    auto* modeling_toolbar=window.findChild<QToolBar*>("toolsToolbar");
    if (!verify(modeling_toolbar && !modeling_toolbar->actions().contains(boolean_action),
            "Boolean is visible while a Body is active")) return 1;
    if (!verify(activate_from_tree(tree->topLevelItem(0),"activatePartAction"),
            "Cannot activate Part before creating a document-level Boolean")) return 1;
    if (!verify(click_command("bodyBooleanAction"),"Visible Boolean button is disabled with two available bodies")) return 1;
    zima::app::BodyBooleanPropertiesDialog* boolean = nullptr;
    for (auto* dialog : window.findChildren<QDialog*>())
        if (auto* value = dynamic_cast<zima::app::BodyBooleanPropertiesDialog*>(dialog); value && value->isVisible()) boolean = value;
    if (!verify(boolean && row("part-body-boolean"), "Boolean draft is missing from properties or Tree")) return 1;
    if (!verify(!boolean->findChild<QPushButton*>("containerOriginSelectionButton"),
            "Boolean exposes an Origin control despite having no placement")) return 1;
    boolean->buttons()->button(QDialogButtonBox::Ok)->click(); flush();
    window.findChild<QAction*>("saveDocumentAction")->trigger(); flush();
    std::vector<zima::kernel::BodyResult> boundaries;
    const auto saved = PartDocument::load(path, &boundaries);
    if (!verify(saved.body_history.find(b)->scope.placement.references.size()>=3 &&
            std::abs(saved.body_history.find(b)->scope.placement.x-5)<1e-7,
            "Body placement references or offset were not persisted")) return 1;
    if (!verify(saved.body_history.bodies().size() == 2 && saved.body_history.booleans().size() == 1 &&
            std::abs(boundaries.back().volume - 500) < 1e-7, "Body UI did not save the correct Boolean result")) return 1;
    if (!verify(!row("part-insert-here") && row("part-body-insert-here"), "Inactive Part exposes a body feature cursor")) return 1;
    if (!verify(click_command("createBodyAction"),"Visible Create Body button stayed disabled after Boolean OK")) return 1;
    if (!verify(body_dialog() != nullptr, "Cannot create body after Boolean")) return 1;
    body_dialog()->buttons()->button(QDialogButtonBox::Ok)->click(); flush();
    window.findChild<QAction*>("saveDocumentAction")->trigger(); flush();
    boundaries.clear();
    const auto continued = PartDocument::load(path, &boundaries);
    if (!verify(continued.body_history.bodies().size() == 3 && !continued.body_history.active_body_id().empty() &&
            std::abs(boundaries.back().volume - 500) < 1e-7 && row("part-insert-here"),
            "Creating a new body after Boolean changed the previous result or failed activation")) return 1;
    auto* history_tree = dynamic_cast<zima::app::HistoryTreeWidget*>(tree);
    const auto c = continued.body_history.active_body_id();
    const auto boolean_id = continued.body_history.booleans().front().id;
    if (!verify(history_tree && history_tree->reorder_enabled(row("part-body",c)), "Body row does not allow dragging")) return 1;
    if (!verify(!history_tree->reorder_requested(row("part-body-boolean",boolean_id),QString::fromStdString(a),false),
            "Boolean drag offered a boundary before its inputs")) return 1;
    if (!verify(history_tree->reorder_requested(row("part-body",c),QString::fromStdString(a),false),
            "Independent body drag rejected an available boundary")) return 1;
    if (!verify(history_tree->reorder_requested(row("part-body",c),QString::fromStdString(a),true),
            "Independent body drag failed to commit")) return 1;
    flush();
    window.findChild<QAction*>("saveDocumentAction")->trigger(); flush();
    const auto moved = PartDocument::load(path);
    if (!verify(moved.body_history.order().front()==c && moved.body_history.find_boolean(boolean_id)->target_id==a,
            "Body reorder lost its order or detached Boolean inputs")) return 1;
    history_tree->body_cursor_moved({},1); flush();
    window.findChild<QAction*>("saveDocumentAction")->trigger(); flush();
    if (!verify(PartDocument::load(path).body_history.insertion_cursor()==1,
            "Document-level body cursor was not persisted")) return 1;
    window.findChild<QAction*>("newDocumentAction")->trigger();flush();
    auto* new_document=window.findChild<QDialog*>("newDocumentDialog");
    if (!verify(new_document!=nullptr,"New document dialog is unavailable")) return 1;
    const auto fresh_name=QStringLiteral("body-new-document-ui-")+QUuid::createUuid().toString(QUuid::Id128);
    new_document->findChild<QLineEdit*>("newDocumentFileName")->setText(fresh_name);
    new_document->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    const auto fresh=PartDocument::load(directory/(fresh_name.toStdString()+".prtz"));
    if (!verify(fresh.body_history.bodies().size()==1 &&
            fresh.body_history.active_body_id()==fresh.body_history.bodies().front().scope.id &&
            row("part-body",fresh.body_history.active_body_id()) && row("part-insert-here") &&
            window.findChild<QAction*>("sketchAction")->isEnabled(),
            "New Part did not start with its first Body active and Sketch available")) return 1;
    if(!verify(!row("part-body-insert-here") &&
            activate_from_tree(tree->topLevelItem(0),"activatePartAction"),
            "New Part exposes duplicate cursor or cannot activate its root"))return 1;
    if (!verify(click_command("createBodyAction"),
            "Create Body stayed disabled after activating new Part root")) return 1;
    if (!verify(body_dialog()!=nullptr,"Create Body button did not open properties in a new Part")) return 1;
    body_dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
    std::cout << "Body history UI contracts passed\n";
    return 0;
}

int verify_body_sketch_ui(QApplication& application, const std::filesystem::path& directory) {
    using namespace zima::document;
    auto document=PartDocument::create_default();
    auto container=PartDocument::create_sketch_container();
    auto sketch=zima::sketcher::Sketch::create_default();
    sketch.owner_container_id=container.id;
    static_cast<void>(sketch.add_segment(0,0,20,0));
    static_cast<void>(sketch.add_segment(0,0,0,10));
    const auto tangent_first=sketch.add_circle(5,0,2);
    const auto tangent_second=sketch.add_circle(15,0,2);
    for (const auto circle : std::vector(sketch.circles)) {
        sketch.set_point_fixed(circle.center_point_id,true);
        sketch.apply_dimension(sketch.create_circle_radius_dimension(circle.id));
    }
    document.history={container}; document.sketches={sketch};
    auto source_point=PartDocument::create_construction(ConstructionKind::Point);
    source_point.origin={-25,40,15};
    auto later_point=PartDocument::create_construction(ConstructionKind::Point);
    later_point.origin={65,40,15};
    document.constructions={source_point,later_point};
    BodyHistoryGraph graph;
    const auto source_body_id=graph.create_body("Zdroj reference");
    graph.insert({PartHistoryKind::Construction,source_point.id});
    auto source_body=*graph.find(source_body_id);
    source_body.scope.placement.x=100;graph.update_body(source_body);
    const auto body_id=graph.create_body("Otočené těleso");
    graph.insert({PartHistoryKind::Feature,container.id});
    auto body=*graph.find(body_id);
    body.scope.placement={80,40,25};
    body.scope.placement.absolute_rotation_y=90;
    body.scope.placement.absolute_rotation_z=90;
    graph.update_body(body);
    static_cast<void>(graph.create_body("Pozdější těleso"));
    graph.insert({PartHistoryKind::Construction,later_point.id});
    graph.activate(body_id);document.set_body_history(graph);
    document.resolve_constructions();
    const auto path=directory/"body-sketch-ui.prtz";document.save(path);
    zima::app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));
    window.resize(1100,850);window.show();
    const auto flush=[&] { application.processEvents(); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete); application.processEvents(); };
    if (!verify(window.open_document_path(QString::fromStdString(path.string())),"Cannot open body Sketch fixture")) return 1;
    flush();
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    window.findChild<QAction*>("sketchAction")->trigger();flush();
    zima::app::SketchPropertiesDialog* new_sketch_dialog{};
    for (auto* candidate : window.findChildren<QDialog*>())
        if (auto* value=dynamic_cast<zima::app::SketchPropertiesDialog*>(candidate);value && value->isVisible())
            new_sketch_dialog=value;
    if (!verify(new_sketch_dialog && new_sketch_dialog->set_reference(0,
            {{},body.origin().id,"origin:plane:xy",0,true,"front",true},"Rovina XY tělesa"),
            "New Sketch cannot reference the active Body plane")) return 1;
    auto* new_offset=new_sketch_dialog->findChild<QDoubleSpinBox*>("sketchPlaneOffset");
    new_offset->setValue(2);flush();
    const auto new_owner=new_sketch_dialog->pending_value().first.owner_container_id;
    auto* new_viewer=dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
    if (!verify(std::ranges::any_of(new_viewer->mesh().dimensions,[&](const auto& dimension) {
            return dimension.reference.owner_id==new_owner &&
                dimension.reference.semantic_key=="parameter:profile_offset" &&
                std::abs(dimension.witness_second.y-42)<1e-6;
        }),"New Sketch preview does not use the active Body frame")) return 1;
    bool draft_in_body=false;
    for (QTreeWidgetItemIterator i(tree);*i;++i)
        if ((*i)->data(0,Qt::UserRole).toString().toStdString()==new_owner && (*i)->parent())
            draft_in_body=(*i)->parent()->data(0,Qt::UserRole).toString().toStdString()==body_id;
    if (!verify(draft_in_body,"Pending Sketch is outside its Body in Tree")) return 1;
    new_sketch_dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
    if (!verify(std::ranges::none_of(new_viewer->mesh().dimensions,[&](const auto& dimension) {
            return dimension.reference.owner_id==new_owner;
        }),"Cancel left pending Sketch dimensions in View")) return 1;
    QTreeWidgetItem* row{};
    for (QTreeWidgetItemIterator i(tree);*i;++i)
        if ((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id &&
                (*i)->data(0,Qt::UserRole+3).toString()=="part-sketch") { row=*i;break; }
    if (!verify(row!=nullptr,"Body Sketch is missing from Tree")) return 1;
    window.show_tree_item_properties(row);flush();
    auto* open=window.findChild<QPushButton*>("sketchOpenButton");
    if (!verify(open!=nullptr,"Cannot enter body Sketch")) return 1;
    zima::app::SketchPropertiesDialog* sketch_dialog{};
    for (auto* dialog : window.findChildren<QDialog*>())
        if (auto* value=dynamic_cast<zima::app::SketchPropertiesDialog*>(dialog);value && value->isVisible())
            sketch_dialog=value;
    if (!verify(sketch_dialog && sketch_dialog->set_reference(0,
            {{},body.origin().id,"origin:point"},"Počátek tělesa"),
            "Sketch placement does not accept its Body origin")) return 1;
    auto* plane_offset=sketch_dialog->findChild<QDoubleSpinBox*>("sketchPlaneOffset");
    plane_offset->setValue(3.25);flush();
    auto* preview_viewer=dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
    const auto preview_near=[](const auto& p,const zima::kernel::Vec3& q) {
        return std::hypot(std::hypot(p.x-q.x,p.y-q.y),p.z-q.z)<1e-6;
    };
    const auto& pending_placement=sketch_dialog->pending_value().second;
    if (!verify(preview_near(zima::kernel::Vec3{pending_placement.x,pending_placement.y,pending_placement.z},{}),
            "Sketch Properties solved Body origin in document instead of Body coordinates")) return 1;
    if (!verify(std::ranges::any_of(preview_viewer->mesh().dimensions,[&](const auto& dimension) {
            return dimension.reference.owner_id==container.id &&
                dimension.reference.semantic_key=="parameter:profile_offset" &&
                preview_near(dimension.witness_first,{80,40,25}) &&
                preview_near(dimension.witness_second,{80,43.25,25});
        }),"Sketch offset preview is not in its owning Body frame")) return 1;
    plane_offset->setValue(0);flush();
    open->click();flush();
    QEventLoop sketch_animation;
    QTimer::singleShot(950,&sketch_animation,&QEventLoop::quit);
    sketch_animation.exec();
    auto* viewer=dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
    const auto near=[](const auto& p,const zima::kernel::Vec3& q) {
        return std::hypot(std::hypot(p.x-q.x,p.y-q.y),p.z-q.z)<1e-6;
    };
    if (!verify(viewer && std::ranges::any_of(viewer->mesh().points,[&](const auto& point) {
            return point.reference.owner_id==sketch.id && near(point.position,{80,40,5});
        }),"Body Sketch was not transformed exactly once into document coordinates")) return 1;
    if (!verify(std::ranges::any_of(viewer->mesh().axes,[&](const auto& axis) {
            return axis.reference.owner_id==container.id+":origin";
        }) && std::ranges::none_of(viewer->mesh().axes,[&](const auto& axis) {
            return axis.reference.owner_id==body_id+":origin";
        }),"Sketcher displays Body Origin instead of its container Origin")) return 1;
    for (const int quarter : {1,3}) {
        window.findChild<QAction*>("sketchSegmentAction")->trigger();flush();
        for (const auto& curve : {tangent_first,tangent_second}) {
            const auto key="sketch_curve_keypoint:circle:"+curve+":"+std::to_string(quarter);
            std::optional<QPointF> hit;
            for (int y=10;y<viewer->height()-10 && !hit;y+=2)
                for (int x=10;x<viewer->width()-10 && !hit;x+=2) {
                    const auto offered=viewer->selection_candidates_at(QPointF(x,y));
                    if (!offered.empty() && offered.front().semantic_key==key) hit=QPointF(x,y);
                }
            if (!verify(hit.has_value(),"Segment does not offer the selected circle keypoint")) {std::cerr << key << "\n";return 1;}
            const QPointF global(viewer->mapToGlobal(hit->toPoint()));
            QMouseEvent move(QEvent::MouseMove,*hit,global,Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(viewer,&move);flush();
            QMouseEvent press(QEvent::MouseButtonPress,*hit,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease,*hit,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(viewer,&press);QApplication::sendEvent(viewer,&release);flush();
        }
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        const auto lower_saved=PartDocument::load(path);
        if(!verify(lower_saved.sketches.front().segments.size()==sketch.segments.size()+1,
            "Segment between circle keypoints was not created"))return 1;
        const auto& committed=lower_saved.sketches.front();
        const auto& line=committed.segments.back();
        for (const auto& curve : {tangent_first,tangent_second}) {
            if(!verify(std::ranges::any_of(committed.constraints,[&](const auto& c) {
                return c.kind==zima::sketcher::ConstraintKind::Tangent &&
                    c.geometry_id==curve && c.second_geometry_id==line.id;
            }),"Common contact lost its tangent constraint"))return 1;
        }
        if(!verify(std::ranges::none_of(committed.constraints,[&](const auto& c) {
            return c.kind==zima::sketcher::ConstraintKind::PointReference &&
                (c.first_point_id==line.first_point_id || c.first_point_id==line.second_point_id);
        }),"Common tangent contact was locked to a quadrant"))return 1;
        sketch=committed;
    }
    auto* point_action=window.findChild<QAction*>("sketchPointAction");
    if (!verify(point_action && point_action->isEnabled(),"Body Sketch point command unavailable")) return 1;
    point_action->trigger();flush();
    QPointF position(viewer->width()*0.7,viewer->height()*0.65);
    for (int y=viewer->height()/3;y<viewer->height()*2/3;y+=17) {
        const QPointF candidate(viewer->width()*0.7,y);
        if (viewer->selection_candidates_at(candidate).empty()) { position=candidate;break; }
    }
    const QPointF global(viewer->mapToGlobal(position.toPoint()));
    QMouseEvent move(QEvent::MouseMove,position,global,Qt::NoButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(viewer,&move);flush();
    QMouseEvent press(QEvent::MouseButtonPress,position,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,position,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(viewer,&press);QApplication::sendEvent(viewer,&release);flush();
    const auto candidates=viewer->selection_candidates_at(position);
    if (!verify(std::ranges::any_of(candidates,[&](const auto& candidate) {
            return candidate.owner_id==sketch.id && candidate.kind==zima::viewer::CandidateKind::SketchPoint;
        }),"Point created by a View ray does not return to the same screen position")) return 1;
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    const auto saved=PartDocument::load(path);
    const auto& result=saved.sketches.front();
    if (!verify(result.points.size()==sketch.points.size()+1 && near(result.resolved_origin,{}),
            "Body Sketch input was not persisted in local coordinates")) return 1;
    auto* external=window.findChild<QAction*>("sketchExternalReferenceAction");
    if (!verify(external && external->isEnabled(),"External reference command unavailable in Body Sketch")) return 1;
    external->trigger();flush();
    std::optional<zima::viewer::ViewerCandidate> source_candidate;
    QPointF source_position;
    for (int y=15;y<viewer->height()-15 && !source_candidate;y+=3)
        for (int x=15;x<viewer->width()-15 && !source_candidate;x+=3) {
            const auto offered=viewer->selection_candidates_at(QPointF(x,y));
            if (!offered.empty() && offered.front().owner_id==source_point.container_origin.id &&
                    offered.front().semantic_key=="point") {
                source_candidate=offered.front();source_position=QPointF(x,y);
            }
        }
    if (!verify(source_candidate.has_value(),"Previous body's persisted point is not offered as a Sketch reference")) return 1;
    auto future_candidate=*source_candidate;
    future_candidate.owner_id=later_point.container_origin.id;
    if (!verify(viewer->candidate_filter() && !viewer->candidate_filter()(future_candidate),
            "Body Sketch offers a later body's geometry as a reference")) return 1;
    const QPointF source_global(viewer->mapToGlobal(source_position.toPoint()));
    QMouseEvent source_move(QEvent::MouseMove,source_position,source_global,Qt::NoButton,Qt::NoButton,Qt::NoModifier);
    QMouseEvent source_press(QEvent::MouseButtonPress,source_position,source_global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent source_release(QEvent::MouseButtonRelease,source_position,source_global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(viewer,&source_move);QApplication::sendEvent(viewer,&source_press);
    QApplication::sendEvent(viewer,&source_release);flush();
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    const auto referenced=PartDocument::load(path);
    const auto& references=referenced.sketches.front().external_references;
    if (!verify(references.size()==1 && references.front().cached_points.size()==1 &&
            std::hypot(references.front().cached_points.front()[0]-10,
                references.front().cached_points.front()[1]-5)<1e-6,
            "Cross-body reference was not projected and persisted in target Sketch coordinates")) return 1;
    window.findChild<QAction*>("regenerateDocumentAction")->trigger();flush();
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    const auto regenerated=PartDocument::load(path);
    const auto& refreshed=regenerated.sketches.front().external_references.front();
    if (!verify(!refreshed.broken && refreshed.cached_points.size()==1 &&
            std::hypot(refreshed.cached_points.front()[0]-10,refreshed.cached_points.front()[1]-5)<1e-6 &&
            std::ranges::find(regenerated.body_history.find(body_id)->dependencies,source_body_id)!=
                regenerated.body_history.find(body_id)->dependencies.end(),
            "Regenerate lost the projected reference or its Body dependency")) return 1;
    std::cout << "Body Sketch frame UI contracts passed\n";
    return 0;
}

int verify_nested_body_sketch_ui(QApplication& application, const std::filesystem::path& directory) {
    using namespace zima::document;
    auto part=PartDocument::create_default();
    auto first=PartDocument::create_box_container();first.box={5,5,5};
    auto middle=PartDocument::create_box_container();middle.box={2,2,2};
    auto later=PartDocument::create_box_container();later.box={3,3,3};later.placement.x=30;
    auto container=PartDocument::create_sketch_container();
    auto sketch=zima::sketcher::Sketch::create_default();sketch.owner_container_id=container.id;
    static_cast<void>(sketch.add_segment(0,0,20,0));
    static_cast<void>(sketch.add_segment(0,0,0,10));
    part.history={first,middle,container,later};part.sketches={sketch};
    BodyHistoryGraph graph;
    static_cast<void>(graph.create_body("První těleso"));graph.insert({PartHistoryKind::Feature,first.id});
    const auto body_id=graph.create_body("Těleso skici");
    graph.insert({PartHistoryKind::Feature,middle.id});graph.insert({PartHistoryKind::Feature,container.id});
    auto body=*graph.find(body_id);body.scope.placement={80,40,25};
    body.scope.placement.absolute_rotation_y=90;body.scope.placement.absolute_rotation_z=90;
    graph.update_body(body);
    static_cast<void>(graph.create_body("Pozdější těleso"));graph.insert({PartHistoryKind::Feature,later.id});
    graph.activate(body_id);part.set_body_history(graph);part.resolve_constructions();
    zima::kernel::OcctKernel kernel;
    const auto calculated=kernel.evaluate_history(part.kernel_operations());
    const auto part_path=directory/"nested-body-part.prtz";
    part.save(part_path,calculated);
    zima::workspace::Workspace fixture;
    fixture.add_part(part,calculated,part_path);
    auto sub=zima::assembly::AssemblyDocument::create_default();
    const auto sub_id=sub.document_id;
    const auto sub_path=directory/"nested-body-sub.asmz";
    fixture.add_assembly(sub,sub_path);
    const auto inner=fixture.insert_open_part(sub_id,part.document_id,"Vnořený díl");
    sub=fixture.open_assembly(sub_id)->session.document();
    sub.find_occurrence(inner)->placement={10,0,0,0,0,90};
    fixture.open_assembly(sub_id)->session.commit(sub);sub.save(sub_path);
    auto top=zima::assembly::AssemblyDocument::create_default();
    const auto top_id=top.document_id;
    const auto top_path=directory/"nested-body-top.asmz";
    fixture.add_assembly(top,top_path);
    const auto outer=fixture.insert_open_assembly(top_id,sub_id,"Podsestava");
    const auto passive=fixture.insert_open_part(top_id,part.document_id,"Druhý výskyt stejného dílu");
    top=fixture.open_assembly(top_id)->session.document();
    top.find_occurrence(outer)->placement={0,50,0,90,0,0};
    top.find_occurrence(passive)->placement={200,0,0,0,0,0};top.save(top_path);
    const auto active_path=zima::assembly::InstancePath{{outer,inner}}.encoded();
    const auto passive_path=zima::assembly::InstancePath{{passive}}.encoded();
    zima::app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));
    window.resize(1200,900);window.show();
    const auto flush=[&] { application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents(); };
    if (!verify(window.open_document_path(QString::fromStdString(part_path.string())) &&
            window.open_document_path(QString::fromStdString(sub_path.string())) &&
            window.open_document_path(QString::fromStdString(top_path.string())) &&
            window.activate_occurrence_for_test(active_path),"Cannot activate nested multi-body Part")) return 1;
    flush();
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    QTreeWidgetItem* row{};
    for (QTreeWidgetItemIterator i(tree);*i;++i)
        if ((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id &&
                (*i)->data(0,Qt::UserRole+3).toString()=="part-sketch") { row=*i;break; }
    if (!verify(row!=nullptr,"Nested Body Sketch missing from Tree")) return 1;
    window.show_tree_item_properties(row);flush();
    auto* open=window.findChild<QPushButton*>("sketchOpenButton");
    if (!verify(open && open->isVisible(),"Cannot edit nested Body Sketch")) return 1;
    open->click();flush();
    QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
    auto* view=dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
    const auto has_later=[&](const std::string& path) {
        return std::ranges::any_of(view->mesh().triangle_references,[&](const auto& ref) {
            return ref.owner_id==later.id && ref.instance_path==path;
        });
    };
    if (!verify(view && !has_later(active_path) && has_later(passive_path),
            "Nested Body history did not isolate the active occurrence from passive context")) return 1;
    if (!verify(std::ranges::any_of(view->mesh().points,[&](const auto& point) {
            return point.reference.owner_id==sketch.id && point.reference.instance_path==active_path &&
                std::hypot(std::hypot(point.position.x+30,point.position.y-45),point.position.z-80)<1e-6;
        }),"Nested Sketch did not compose Body, Part and Assembly frames exactly once")) return 1;
    std::optional<QPointF> picked;
    for (int y=20;y<view->height()-20 && !picked;y+=3)
        for (int x=20;x<view->width()-20 && !picked;x+=3) {
            const auto candidates=view->selection_candidates_at(QPointF(x,y));
            if (!candidates.empty() && candidates.front().kind==zima::viewer::CandidateKind::SketchPoint &&
                    candidates.front().owner_id==sketch.id && candidates.front().instance_path==active_path &&
                    candidates.front().semantic_key=="point:"+sketch.points[1].id) picked=QPointF(x,y);
        }
    if (!verify(picked.has_value(),"Nested Sketch point is not offered in its exact occurrence")) return 1;
    const auto send=[&](QEvent::Type type,QPointF position,Qt::MouseButton button,Qt::MouseButtons buttons) {
        QMouseEvent event(type,position,QPointF(view->mapToGlobal(position.toPoint())),button,buttons,Qt::NoModifier);
        QApplication::sendEvent(view,&event);flush();
    };
    send(QEvent::MouseMove,*picked,Qt::NoButton,Qt::NoButton);
    send(QEvent::MouseButtonPress,*picked,Qt::LeftButton,Qt::LeftButton);
    send(QEvent::MouseMove,*picked+QPointF(18,12),Qt::NoButton,Qt::LeftButton);
    if (!verify(!has_later(active_path) && has_later(passive_path),
            "Dragging a nested Sketch point discarded Assembly context or restored later Body history")) return 1;
    send(QEvent::MouseButtonRelease,*picked+QPointF(18,12),Qt::LeftButton,Qt::NoButton);
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    const auto edited=PartDocument::load(part_path);
    const auto* moved=edited.sketches.front().find_point(sketch.points[1].id);
    if (!verify(moved && std::hypot(moved->x-sketch.points[1].x,moved->y-sketch.points[1].y)>1e-4 &&
            window.active_occurrence_path_for_test()==active_path && has_later(passive_path),
            "Nested Sketch drag did not persist into its source Part or changed occurrence ownership")) return 1;
    window.findChild<QAction*>("sketchTrimAction")->trigger();flush();
    bool has_trim_piece=false;
    for (const auto& edge : view->mesh().edges) {
        if (edge.reference.owner_id!=sketch.id || edge.reference.instance_path!=active_path ||
                !edge.reference.semantic_key.starts_with("trim_piece:")) continue;
        has_trim_piece=true;
        for (const auto& point : edge.points)
            if (!verify(std::abs(point.x+30)<1e-6,
                    "Nested Sketch trim candidates escaped the placed Body plane")) return 1;
    }
    if (!verify(has_trim_piece,"Nested Sketch trim did not offer editable curve pieces")) return 1;
    std::optional<QPointF> trim_pick;
    for (int y=20;y<view->height()-20 && !trim_pick;y+=3)
        for (int x=20;x<view->width()-20 && !trim_pick;x+=3) {
            const auto candidates=view->selection_candidates_at(QPointF(x,y));
            if (!candidates.empty() && candidates.front().kind==zima::viewer::CandidateKind::SketchTrimPiece &&
                    candidates.front().owner_id==sketch.id && candidates.front().instance_path==active_path)
                trim_pick=QPointF(x,y);
        }
    if (!verify(trim_pick.has_value(),"Placed trim piece is not offered by the common viewer picker")) return 1;
    send(QEvent::MouseMove,*trim_pick,Qt::NoButton,Qt::NoButton);
    send(QEvent::MouseButtonPress,*trim_pick,Qt::LeftButton,Qt::LeftButton);
    send(QEvent::MouseButtonRelease,*trim_pick,Qt::LeftButton,Qt::NoButton);
    const auto remaining=std::ranges::count_if(view->mesh().edges,[&](const auto& edge) {
        return edge.reference.owner_id==sketch.id && edge.reference.instance_path==active_path &&
            edge.reference.semantic_key.starts_with("trim_piece:");
    });
    if (!verify(remaining==1 && has_later(passive_path),
            "Trim click did not remove exactly one placed curve or lost passive context")) return 1;
    QKeyEvent cancel_trim(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
    QApplication::sendEvent(&window,&cancel_trim);flush();
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    const auto canceled_trim=PartDocument::load(part_path);
    if (!verify(canceled_trim.sketches.front().serialized()==edited.sketches.front().serialized(),
            "Cancel committed the pending nested Sketch trim")) return 1;
    std::cout << "Nested Body Sketch UI contracts passed\n";
    return 0;
}

int verify_component_reference_document(QApplication& application, const std::filesystem::path& directory) {
    using namespace zima;
    const auto input=assembly::AssemblyDocument::load(qEnvironmentVariable("ZIMA_VERIFY_COMPONENT_DOCUMENT").toStdString());
    const auto copy_path=directory/"component-reference-document.asmz";input.save(copy_path);
    app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
    const auto flush=[&] {application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    QString failure;QTimer messages;
    QObject::connect(&messages,&QTimer::timeout,[&] {
        if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){failure=box->text();box->accept();}
    });messages.start(50);
    if(!verify(window.open_document_path(QString::fromStdString(copy_path.string())),"Cannot open a copy of the requested assembly"))return 1;
    flush();
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    for(const auto& component:input.components) {
        const auto path=assembly::InstancePath{{component.occurrence_id}}.encoded();QTreeWidgetItem* item{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)
            if((*i)->data(0,Qt::UserRole).toString().toStdString()==component.occurrence_id &&
                (*i)->data(0,Qt::UserRole+1).toString().toStdString()==path){item=*i;break;}
        if(!verify(item!=nullptr,"Requested component is missing from Tree"))return 1;
        window.show_tree_item_properties(item);flush();app::ComponentPropertiesDialog* dialog{};
        for(auto* child:window.findChildren<QDialog*>())
            if(auto* d=dynamic_cast<app::ComponentPropertiesDialog*>(child);d && d->isVisible()){dialog=d;break;}
        if(!verify(dialog!=nullptr,"Requested component properties did not open"))return 1;
        std::cout<<component.name<<": "<<dialog->findChild<QLabel*>("componentDegreesOfFreedom")->text().toStdString()<<'\n';
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        if(!verify(failure.isEmpty(),"Requested component placement failed to commit")){std::cerr<<failure.toStdString()<<'\n';return 1;}
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    }
    const auto saved=assembly::AssemblyDocument::load(copy_path);
    for(const auto& component:saved.components) {
        for(const auto& row:component.placement_references) {
            const bool angular=row.mate_type==assembly::MateKind::AxisAngle || row.mate_type==assembly::MateKind::PlaneAngle;
            const auto expected=row.component_reference.kind==assembly::MateReferenceKind::Point ? assembly::MateKind::PointCoincident :
                row.component_reference.kind==assembly::MateReferenceKind::Axis ? assembly::MateKind::AxisCoincident : assembly::MateKind::PlaneCoincident;
            if(!verify(angular || row.mate_type==expected,"Saved reference kind and mate type disagree"))return 1;
            const auto resolves = [&](const assembly::MateReference& reference) {
                return reference.kind==assembly::MateReferenceKind::Point ? saved.resolve_point(reference).status :
                    reference.kind==assembly::MateReferenceKind::Axis ? saved.resolve_axis(reference).status : saved.resolve_plane(reference).status;
            };
            if(!verify(resolves(row.component_reference)==assembly::MateStatus::Valid && resolves(row.target_reference)==assembly::MateStatus::Valid,
                "A reference in the requested assembly does not resolve after confirmation"))return 1;
        }
        std::cout<<component.name<<": saved DOF="<<saved.component_constraint_state(component.occurrence_id).remaining_dof<<'\n';
    }
    std::cout<<"Requested assembly checked through properties and saved to a test copy\n";return 0;
}

int verify_component_references(QApplication& application, const std::filesystem::path& directory) {
    using namespace zima;
    auto part=document::PartDocument::create_default();
    auto box=document::PartDocument::create_box_container();box.box={10,10,10};part.history={box};
    kernel::OcctKernel kernel;
    const auto calculated=kernel.evaluate_history(part.kernel_operations());
    const auto part_path=directory/"component-reference.prtz";part.save(part_path,calculated);
    workspace::Workspace fixture;fixture.add_part(part,calculated,part_path);
    auto assembly=assembly::AssemblyDocument::create_default();const auto assembly_id=assembly.document_id;
    const auto assembly_path=directory/"component-reference.asmz";fixture.add_assembly(assembly,assembly_path);
    const auto first=fixture.insert_open_part(assembly_id,part.document_id,"Pevný díl");
    const auto second=fixture.insert_open_part(assembly_id,part.document_id,"Pohyblivý díl");
    assembly=fixture.open_assembly(assembly_id)->session.document();
    assembly.find_occurrence(second)->placement={50,30,20,0,0,0};
    fixture.open_assembly(assembly_id)->session.commit(assembly);assembly.save(assembly_path);
    app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
    const auto flush=[&] {application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    if(!verify(window.open_document_path(QString::fromStdString(assembly_path.string())),"Cannot open component reference fixture"))return 1;
    flush();
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
    const auto find=[&](const std::string& id,const std::string& path,const std::string& key=std::string{}) -> QTreeWidgetItem* {
        for(QTreeWidgetItemIterator i(tree);*i;++i)
            if((*i)->data(0,Qt::UserRole).toString().toStdString()==id &&
                (*i)->data(0,Qt::UserRole+1).toString().toStdString()==path &&
                (*i)->data(0,Qt::UserRole+5).toString().toStdString()==key)return *i;
        return nullptr;
    };
    const auto dialog=[&]() -> app::ComponentPropertiesDialog* {
        for(auto* child:window.findChildren<QDialog*>())
            if(auto* d=dynamic_cast<app::ComponentPropertiesDialog*>(child);d && d->isVisible())return d;
        return nullptr;
    };
    const auto pick=[&](int row,bool component,const std::string& id,const std::string& path,const std::string& key=std::string{}) {
        auto* d=dialog();if(!d)return false;
        d->findChild<QTableWidget*>("componentPlacementTable")->cellClicked(row,component?1:2);flush();
        auto* item=find(id,path,key);if(!item){std::cerr<<"Missing reference tree item: "<<id<<" / "<<path<<" / "<<key<<'\n';return false;}
        tree->clearSelection();tree->setCurrentItem(item);flush();return true;
    };
    const auto check_freedom=[&](int count,const std::array<bool,6>& editable) {
        auto* d=dialog();if(!d)return false;
        const auto translations=d->findChildren<QDoubleSpinBox*>("componentTranslation");
        const auto rotations=d->findChildren<QDoubleSpinBox*>("componentRotation");
        if(translations.size()!=3 || rotations.size()!=3 ||
            !d->findChild<QLabel*>("componentDegreesOfFreedom")->text().endsWith(QString::number(count))){
            std::cerr<<"Expected DOF="<<count<<", dialog="<<d->findChild<QLabel*>("componentDegreesOfFreedom")->text().toStdString()<<'\n';return false;}
        for(int i=0;i<6;++i)if((i<3?translations[i]:rotations[i-3])->isEnabled()!=editable[i]){
            std::cerr<<"Coordinate "<<i<<" expected editable="<<editable[i]<<'\n';return false;}
        return true;
    };
    const auto first_path=assembly::InstancePath{{first}}.encoded(),second_path=assembly::InstancePath{{second}}.encoded();
    const auto origin=part.document_id+":origin";
    window.show_tree_item_properties(find(second,second_path));flush();
    if(!verify(dialog()!=nullptr,"Cannot open component properties"))return 1;
    dialog()->findChild<QTableWidget*>("componentPlacementTable")->cellClicked(0,1);flush();
    for(const auto& [kind,key]:std::array{
            std::pair{viewer::CandidateKind::Vertex,"origin:point"},
            std::pair{viewer::CandidateKind::Axis,"origin:axis:z"},
            std::pair{viewer::CandidateKind::Plane,"origin:plane:xy"}}) {
        viewer::ViewerCandidate candidate;candidate.geometry=viewer::CandidateGeometry::Display;
        candidate.kind=kind;candidate.owner_id=origin;candidate.semantic_key=key;candidate.instance_path=second_path;
        if(!verify(view->candidate_filter() && view->candidate_filter()(candidate),"View rejects persisted component origin geometry"))return 1;
        candidate.instance_path=first_path;
        if(!verify(!view->candidate_filter()(candidate),"Source picker accepts another occurrence of the same Part"))return 1;
    }
    std::optional<QPointF> origin_axis_hit;
    for(int y=10;y<view->height()-10 && !origin_axis_hit;y+=3)
        for(int x=10;x<view->width()-10 && !origin_axis_hit;x+=3) {
            const auto candidates=view->selection_candidates_at(QPointF(x,y));
            if(!candidates.empty() && candidates.front().kind==viewer::CandidateKind::Axis &&
                candidates.front().owner_id==origin && candidates.front().instance_path==second_path &&
                candidates.front().semantic_key=="origin:axis:z")origin_axis_hit=QPointF(x,y);
        }
    if(!verify(origin_axis_hit.has_value(),"Component origin Z axis is missing from the common View picker"))return 1;
    for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
        QMouseEvent event(type,*origin_axis_hit,QPointF(view->mapToGlobal(origin_axis_hit->toPoint())),
            type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
            type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&event);flush();
    }
    if(!verify(!dialog()->placement_references().empty() &&
        dialog()->placement_references()[0].component_reference.semantic_key=="origin:axis:z" &&
        dialog()->placement_references()[0].component_reference.instance_path.encoded()==second_path,
        "View click did not store the exact offered origin axis"))return 1;
    if(!verify(pick(0,true,origin,second_path,"origin:axis:z") && pick(0,false,origin,first_path,"origin:axis:z") &&
        dialog()->placement_references()[0].mate_type==assembly::MateKind::AxisCoincident &&
        check_freedom(2,{false,false,true,false,false,true}),"Tree axis pair does not solve and expose two freedoms"))return 1;
    if(!verify(pick(1,true,origin,second_path,"origin:plane:xy") && pick(1,false,origin,first_path,"origin:plane:xy") &&
        check_freedom(1,{false,false,false,false,false,true}),"Origin plane does not remove axial translation"))return 1;
    dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    auto saved=assembly::AssemblyDocument::load(assembly_path);
    if(!verify(saved.find_occurrence(second)->placement_references.empty() && saved.find_occurrence(second)->placement.x==50,
        "Cancel changed the component placement or reference rows"))return 1;
    window.show_tree_item_properties(find(second,second_path));flush();
    if(!verify(pick(0,true,origin,second_path) && dialog()->placement_references().size()==3 &&
        dialog()->placement_references()[0].target_reference.owner_id.empty(),"Whole source origin did not fill only its selected side"))return 1;
    pick(0,false,origin,second_path);
    if(!verify(dialog()->placement_references()[0].target_reference.owner_id.empty(),"Origin shortcut accepts self-mating"))return 1;
    if(!verify(pick(0,false,assembly_id+":origin",{}) && check_freedom(0,{false,false,false,false,false,false}),
        "Assembly origin shortcut does not fully constrain the component"))return 1;
    window.grab().save(QString::fromStdString((directory/"component-origin-references.png").string()));
    dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    if(!verify(!dialog(),"Component origin placement did not commit"))return 1;
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();saved=assembly::AssemblyDocument::load(assembly_path);
    if(!verify(saved.component_constraint_state(second).remaining_dof==0 && saved.find_occurrence(second)->placement_references[0].mate_type==assembly::MateKind::PointCoincident,
        "Origin mate types or constraints were lost after saving"))return 1;

    // An active nested Assembly owns its local datum and keeps the top-level scene visible.
    fixture.open_assembly(assembly_id)->session.commit(assembly);
    auto top=assembly::AssemblyDocument::create_default();const auto top_id=top.document_id;
    const auto top_path=directory/"component-reference-top.asmz";fixture.add_assembly(top,top_path);
    const auto outer=fixture.insert_open_assembly(top_id,assembly_id,"Podsestava");
    const auto passive=fixture.insert_open_part(top_id,part.document_id,"Kontext");
    top=fixture.open_assembly(top_id)->session.document();top.find_occurrence(passive)->placement.x=200;top.save(top_path);
    const auto outer_path=assembly::InstancePath{{outer}}.encoded(),nested_path=assembly::InstancePath{{outer,second}}.encoded();
    if(!verify(window.open_document_path(QString::fromStdString(top_path.string())) && window.activate_occurrence_for_test(outer_path),
        "Cannot activate owning nested Assembly"))return 1;
    flush();window.show_tree_item_properties(find(second,nested_path));flush();
    if(!verify(dialog() && pick(0,true,origin,nested_path) && pick(0,false,assembly_id+":origin",outer_path) &&
        check_freedom(0,{false,false,false,false,false,false}),"Nested Assembly origin is not accepted as a local target"))return 1;
    const auto passive_path=assembly::InstancePath{{passive}}.encoded();
    if(!verify(std::ranges::any_of(view->mesh().triangle_references,[&](const auto& r){return r.instance_path==passive_path;}),
        "Component preview discarded top-level passive context"))return 1;
    dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
    std::cout<<"Component origin, axis and freedom UI contracts passed\n";return 0;
}

int verify_startup_contract(
    QApplication& application, zima::app::AssemblyWorkspaceWindow& window,
    const std::filesystem::path& test_directory,
    const QString& part_capture_path = {}, const QString& drawing_capture_path = {}) {
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_COMPONENT_DOCUMENT")) return verify_component_reference_document(application,test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_COMPONENT_REFERENCE_ONLY")) return verify_component_references(application,test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_UNRESOLVED_SWEEP_ONLY")) return verify_unresolved_sweep_sketches(application,test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_EXTERNAL_CIRCLE_ONLY")) return verify_external_circle_selection(application,test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_DELETE_DOCUMENT"))
        return verify_part_deletion(application,test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_REOPEN_DOCUMENT"))
        return verify_reopened_sweep_document(application,test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_REFERENCE_DOCUMENT")) {
        QString failure;
        QTimer messages;
        QObject::connect(&messages,&QTimer::timeout,[&] {
            if (auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                failure=box->text(); box->accept();
            }
        });
        messages.start(50);
        window.show();
        if (!window.open_document_path(qEnvironmentVariable("ZIMA_VERIFY_REFERENCE_DOCUMENT"))) return 1;
        application.processEvents();
        window.findChild<QAction*>("regeneratePartAction")->trigger();
        application.processEvents();
        if (!failure.isEmpty()) {std::cerr << failure.toStdString() << '\n';return 1;}
        std::cout << "Reference document regeneration passed\n";
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_CENTERLINE_PICK")) {
            auto* view=dynamic_cast<zima::viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
            if(!verify(view && std::ranges::any_of(view->mesh().edges,[](const auto& edge){return edge.reference.semantic_key.starts_with("centerline:from:") && edge.dash_dot;}),
                    "Regenerated Sweep centerlines did not reach the document View"))return 1;
            for (const auto& axis : view->mesh().edges) {
                if (!axis.reference.semantic_key.starts_with("centerline:from:")) continue;
                if (!verify(std::ranges::none_of(view->mesh().edges,[&](const auto& edge) {
                        return edge.display_owner_id == axis.display_owner_id &&
                            zima::viewer::is_curve3d_edge(edge.reference.semantic_key);
                    }),"Solid Sweep axis is obscured by its original trajectory")) return 1;
            }
            view->grabFramebuffer().save("/tmp/zima-sweep-centerlines.png");
            window.findChild<QAction*>("constructionPointAction")->trigger();application.processEvents();
            zima::app::ConstructionPropertiesDialog* dialog{};
            for(auto* child:window.findChildren<QDialog*>())if(auto* value=dynamic_cast<zima::app::ConstructionPropertiesDialog*>(child);value && value->isVisible()){dialog=value;break;}
            if(!verify(dialog!=nullptr,"Cannot open Point for Sweep axis reference"))return 1;
            std::optional<QPointF> hit;zima::viewer::ViewerCandidate chosen;
            for(int y=10;y<view->height()-10 && !hit;y+=3)for(int x=10;x<view->width()-10 && !hit;x+=3) {
                const auto candidates=view->selection_candidates_at(QPointF(x,y));
                if(!candidates.empty() && candidates.front().kind==zima::viewer::CandidateKind::Axis &&
                    candidates.front().semantic_key.starts_with("centerline:from:")){hit=QPointF(x,y);chosen=candidates.front();}
            }
            if(!verify(hit.has_value(),"Sweep centerline axis is not offered for placement"))return 1;
            QMouseEvent press(QEvent::MouseButtonPress,*hit,QPointF(view->mapToGlobal(hit->toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease,*hit,QPointF(view->mapToGlobal(hit->toPoint())),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);application.processEvents();
            const auto references=dialog->populated_references();
            if(!verify(!references.empty() && references.front().owner_id==chosen.owner_id && references.front().semantic_key==chosen.semantic_key,
                    "Sweep axis click did not store the exact source reference"))return 1;
            dialog->buttons()->button(QDialogButtonBox::Cancel)->click();application.processEvents();
            std::cout << "Sweep centerline document picking passed\n";return 0;
        }
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        tree->scrollToItem(tree->topLevelItem(0));
        QTimer::singleShot(0,&window,[&] {
            if(auto* menu=window.findChild<QMenu*>("partActivationMenu")) {
                menu->setActiveAction(menu->findChild<QAction*>("activatePartAction"));
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);
            }
        });
        tree->customContextMenuRequested(tree->visualItemRect(tree->topLevelItem(0)).center());application.processEvents();
        auto* create=window.findChild<QAction*>("createBodyAction");
        if(!verify(create && create->isEnabled(),"New Body command unavailable after regeneration"))return 1;
        create->trigger();application.processEvents();
        zima::app::BodyPropertiesDialog* body{};
        for(auto* child:window.findChildren<QDialog*>())
            if(auto* dialog=dynamic_cast<zima::app::BodyPropertiesDialog*>(child);dialog && dialog->isVisible()){body=dialog;break;}
        if(!verify(body!=nullptr,"New Body properties did not open"))return 1;
        const auto input=zima::document::PartDocument::load(qEnvironmentVariable("ZIMA_VERIFY_REFERENCE_DOCUMENT").toStdString());
        const std::array<std::string,3> planes{"origin:plane:xz","origin:plane:xy","origin:plane:yz"};
        for(std::size_t i=0;i<planes.size();++i) {
            zima::document::ConstructionReference reference;
            reference.owner_id=input.document_id+":origin";reference.semantic_key=planes[i];reference.supports_offset=true;
            if(!verify(body->set_reference(i,reference,QString::fromStdString(planes[i])),"New Body origin reference rejected"))return 1;
        }
        body->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();application.processEvents();
        if(!verify(!body->isVisible(),"New Body failed to commit after reference regeneration"))return 1;
        std::cout << "Reference document new Body passed\n";
        return 0;
    }
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_BODY_CURVE_REFERENCE_ONLY"))
        return verify_body_curve_references(application,test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_NESTED_BODY_ONLY"))
        return verify_nested_body_sketch_ui(application, test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_BODY_SKETCH_ONLY"))
        return verify_body_sketch_ui(application, test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_BODY_OFFSET_ONLY"))
        return verify_body_placement_offsets(application,test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_SAVE_COPY_ONLY"))
        return verify_save_copy_ui(application,test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_BODY_UI_ONLY"))
        return verify_body_history_ui(application, test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_PENDING_TREE_ONLY"))
        return verify_pending_container_tree(application, test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_DERIVED_COPY_ONLY"))
        return verify_derived_copy_commands(application,window,test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_SWEEP2D_ONLY"))
        return verify_sweep2d_command(application,window,test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_HELICAL_SWEEP_ONLY"))
        return verify_helical_sweep_command(application,window,test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_SHAFT_THREAD_ONLY"))
        return verify_shaft_thread_command(application,window,test_directory);
    if (qEnvironmentVariableIsSet("ZIMA_VERIFY_HISTORY_DRAG_ONLY"))
        return verify_history_tree_drag(application,test_directory);
    if (verify_component_references(application, test_directory) != 0) return 1;
    if (verify_save_copy_ui(application, test_directory) != 0) return 1;
    if (verify_body_placement_offsets(application, test_directory) != 0) return 1;
    if (verify_body_history_ui(application, test_directory) != 0) return 1;
    if (verify_body_curve_references(application,test_directory) != 0) return 1;
    if (verify_body_sketch_ui(application, test_directory) != 0) return 1;
    if (verify_nested_body_sketch_ui(application, test_directory) != 0) return 1;
    if (verify_pending_container_tree(application, test_directory) != 0) return 1;
    if (verify_unresolved_sweep_sketches(application,test_directory) != 0) return 1;
    if (verify_external_circle_selection(application,test_directory) != 0) return 1;
    if (verify_spline_tangent_selection(application,test_directory) != 0) return 1;
    if (verify_curve_radius_edit(application,test_directory) != 0) return 1;
    window.show();
    application.processEvents();

    if (!verify(window.findChild<QAction*>("holeAction") == nullptr &&
                window.findChild<QAction*>("threadAction") != nullptr &&
                window.findChild<QAction*>("threadAction")->text() == QObject::tr("Otvor"),
            "only the combined opening/thread command should be exposed")) return 1;

    auto* tabs = window.findChild<QTabBar*>("documentTabs");
    auto* tree = window.findChild<QTreeWidget*>("documentTree");
    auto* splitter = window.findChild<QSplitter*>("documentSplitter");
    auto* main_toolbar = window.findChild<QToolBar*>("mainToolbar");
    auto* view_toolbar = window.findChild<QToolBar*>("viewToolbar");
    auto* tools_toolbar = window.findChild<QToolBar*>("toolsToolbar");
    auto* box = window.findChild<QAction*>("boxAction");
    auto* construction_point = window.findChild<QAction*>("constructionPointAction");
    auto* construction_axis = window.findChild<QAction*>("constructionAxisAction");
    auto* construction_plane = window.findChild<QAction*>("constructionPlaneAction");
    auto* sketch = window.findChild<QAction*>("sketchAction");
    auto* sketch_normal = window.findChild<QAction*>("sketchNormalViewAction");
    auto* sketch_point = window.findChild<QAction*>("sketchPointAction");
    auto* sketch_construction = window.findChild<QAction*>("sketchConstructionAction");
    auto* sketch_segment = window.findChild<QAction*>("sketchSegmentAction");
    auto* sketch_polyline = window.findChild<QAction*>("sketchPolylineAction");
    auto* sketch_rectangle = window.findChild<QAction*>("sketchRectangleAction");
    auto* sketch_polygon = window.findChild<QAction*>("sketchPolygonAction");
    auto* sketch_circle = window.findChild<QAction*>("sketchCircleAction");
    auto* sketch_arc = window.findChild<QAction*>("sketchArcAction");
    auto* sketch_ellipse = window.findChild<QAction*>("sketchEllipseAction");
    auto* sketch_trim = window.findChild<QAction*>("sketchTrimAction");
    auto* sketch_mirror = window.findChild<QAction*>("sketchMirrorAction");
    auto* sketch_elliptical_arc =
        window.findChild<QAction*>("sketchEllipticalArcAction");
    auto* sketch_bspline = window.findChild<QAction*>("sketchBSplineAction");
    auto* sketch_interpolating_spline =
        window.findChild<QAction*>("sketchInterpolatingSplineAction");
    auto* sketch_midpoint = window.findChild<QAction*>("sketchMidpointAction");
    auto* sketch_symmetric = window.findChild<QAction*>("sketchSymmetricAction");
    auto* sketch_concentric = window.findChild<QAction*>("sketchConcentricAction");
    auto* sketch_tangent = window.findChild<QAction*>("sketchTangentAction");
    auto* sketch_equal = window.findChild<QAction*>("sketchEqualAction");
    auto* sketch_text = window.findChild<QAction*>("sketchTextAction");
    auto* sketch_external_reference =
        window.findChild<QAction*>("sketchExternalReferenceAction");
    auto* sketch_constraints =
        window.findChild<QAction*>("sketchConstraintsAction");
    auto* sketch_dimensions =
        window.findChild<QAction*>("sketchDimensionsAction");
    auto* sketch_dimension =
        window.findChild<QAction*>("sketchDimensionAction");
    auto* sketch_universal_dimension =
        window.findChild<QAction*>("sketchUniversalDimensionAction");
    auto* sketch_horizontal =
        window.findChild<QAction*>("sketchHorizontalAction");
    auto* sketch_vertical =
        window.findChild<QAction*>("sketchVerticalAction");
    auto* sketch_fix_point =
        window.findChild<QAction*>("sketchFixPointAction");
    auto* workspace_state = window.findChild<QLabel*>("workspaceState");
    auto* file_progress = window.findChild<QProgressBar*>("fileOperationProgress");
    auto* finish_sketch = window.findChild<QAction*>("finishSketchAction");
    auto* extrusion = window.findChild<QAction*>("extrusionAction");
    auto* about = window.findChild<QAction*>("aboutAction");
    auto* save_as = window.findChild<QAction*>("saveDocumentAsAction");
    auto* rename_document = window.findChild<QAction*>("renameDocumentAction");
    auto* delete_file_menu = window.findChild<QMenu*>("deleteFileMenu");
    auto* delete_current_file = window.findChild<QAction*>("deleteCurrentFileAction");
    auto* delete_all_versions = window.findChild<QAction*>("deleteAllVersionsAction");
    auto* delete_old_versions = window.findChild<QAction*>("deleteOldVersionsAction");
    auto* delete_old_versions_keep_latest =
        window.findChild<QAction*>("deleteOldVersionsKeepLatestAction");
    auto* delete_working_directory_menu =
        window.findChild<QMenu*>("deleteWorkingDirectoryMenu");
    auto* delete_working_directory_old_versions =
        window.findChild<QAction*>("deleteWorkingDirectoryOldVersionsAction");
    auto* delete_working_directory_keep_latest =
        window.findChild<QAction*>("deleteWorkingDirectoryKeepLatestAction");
    auto* working_directory = window.findChild<QAction*>("workingDirectoryAction");
    auto* new_document = window.findChild<QAction*>("newDocumentAction");
    auto* undo = window.findChild<QAction*>("undoAction");
    auto* redo = window.findChild<QAction*>("redoAction");
    auto* save = window.findChild<QAction*>("saveDocumentAction");
    auto* close = window.findChild<QAction*>("closeDocumentAction");
    auto* parameters = window.findChild<QAction*>("documentParametersAction");
    auto* export_document = window.findChild<QAction*>("exportDocumentAction");
    auto* global_settings = window.findChild<QAction*>("globalSettingsAction");
    auto* standard_views = window.findChild<QMenu*>("standardViewsMenu");
    auto* colors_menu = window.findChild<QMenu*>("colorsMenu");
    auto* fit_view = window.findChild<QAction*>("fitViewAction");
    auto* view_selection = window.findChild<QAction*>("viewSelectionAction");
    auto* orthographic_camera =
        window.findChild<QAction*>("orthographicCameraAction");
    auto* perspective_camera =
        window.findChild<QAction*>("perspectiveCameraAction");
    auto* fly_camera = window.findChild<QAction*>("flyCameraAction");
    auto* import_document = window.findChild<QAction*>("importDocumentAction");
    auto* material = window.findChild<QAction*>("materialAction");
    auto* relations = window.findChild<QAction*>("relationsAction");
    auto* family_table = window.findChild<QAction*>("familyTableAction");
    auto* file_settings = window.findChild<QAction*>("fileSettingsAction");
    auto* window_menu = window.findChild<QMenu*>("windowMenu");
    if (!verify(tabs != nullptr && tree != nullptr, "document navigation is missing") ||
        !verify(splitter != nullptr && main_toolbar != nullptr &&
                    view_toolbar != nullptr && tools_toolbar != nullptr,
                "Python-compatible workspace shell is missing") ||
        !verify(box != nullptr && sketch != nullptr && sketch_normal != nullptr &&
                    sketch_point != nullptr && sketch_construction != nullptr &&
                    sketch_segment != nullptr && sketch_polyline != nullptr &&
                    sketch_rectangle != nullptr &&
                    sketch_polygon != nullptr && sketch_polygon->menu() == nullptr &&
                    sketch_trim != nullptr &&
                    sketch_mirror != nullptr &&
                    sketch_elliptical_arc != nullptr &&
                    sketch_midpoint != nullptr &&
                    sketch_symmetric != nullptr &&
                    sketch_concentric != nullptr &&
                    sketch_tangent != nullptr &&
                    sketch_equal != nullptr &&
                    sketch_text != nullptr &&
                    sketch_external_reference != nullptr &&
                    sketch_constraints != nullptr &&
                    sketch_constraints->menu() == nullptr && sketch_equal != nullptr &&
                    sketch_dimensions != nullptr &&
                    sketch_dimension != nullptr &&
                    sketch_universal_dimension != nullptr &&
                    sketch_horizontal != nullptr && sketch_vertical != nullptr &&
                    sketch_fix_point != nullptr &&
                    workspace_state != nullptr &&
                    file_progress != nullptr && !file_progress->isVisible() &&
                    finish_sketch != nullptr &&
                    extrusion != nullptr && about != nullptr && save_as != nullptr &&
                    rename_document != nullptr && delete_file_menu != nullptr &&
                    delete_working_directory_menu != nullptr &&
                    working_directory != nullptr && new_document != nullptr &&
                    undo != nullptr && redo != nullptr &&
                    save != nullptr && close != nullptr && parameters != nullptr,
                "primary actions are missing") ||
        !verify(tabs->count() == 0 && !splitter->isVisible() &&
                    window.windowTitle() == QStringLiteral("ZIMA-CAD — Bez dokumentu"),
                "application must start without a document") ||
        !verify(main_toolbar->isVisible() && !save_as->isEnabled() &&
                    working_directory->isEnabled(),
                "startup file-command state is invalid")) {
        return 1;
    }
    if (!verify(export_document != nullptr && !export_document->isEnabled() &&
                    parameters != nullptr && !parameters->isEnabled(),
                "document-only commands must be disabled without a document") ||
        !verify(import_document != nullptr && import_document->isEnabled() &&
                    working_directory->isEnabled() && about->isEnabled(),
                "document-independent commands must remain available") ||
        !verify(standard_views != nullptr &&
                    !standard_views->menuAction()->isEnabled() &&
                    colors_menu != nullptr &&
                    !colors_menu->menuAction()->isEnabled() &&
                    fit_view != nullptr && !fit_view->isEnabled() &&
                    view_selection != nullptr && !view_selection->isEnabled() &&
                    orthographic_camera != nullptr &&
                    !orthographic_camera->isEnabled() &&
                    perspective_camera != nullptr &&
                    !perspective_camera->isEnabled() &&
                    fly_camera != nullptr && !fly_camera->isEnabled() &&
                    orthographic_camera->isCheckable() &&
                    perspective_camera->isCheckable() &&
                    fly_camera->isCheckable() &&
                    orthographic_camera->isChecked() &&
                    !perspective_camera->isChecked() &&
                    !fly_camera->isChecked() &&
                    !orthographic_camera->icon().isNull() &&
                    !perspective_camera->icon().isNull() &&
                    !fly_camera->icon().isNull(),
                "viewer commands must be disabled without a visible document") ||
        !verify(global_settings != nullptr && global_settings->isEnabled(),
                "Global Settings must remain functional without a document")) {
        return 1;
    }
    if (!verify(material != nullptr && !material->isEnabled() &&
                    relations != nullptr && !relations->isEnabled() &&
                    family_table != nullptr && !family_table->isEnabled() &&
                    file_settings != nullptr && !file_settings->isEnabled(),
                "unavailable document tools must remain visibly disabled")) {
        return 1;
    }
    for (int index = 0; index < 6; ++index) {
        auto* action = window.findChild<QAction*>(
            QStringLiteral("applicationModeAction%1").arg(index));
        if (!verify(action != nullptr && !action->isEnabled(),
                    "application modes must be disabled without a document")) {
            return 1;
        }
    }
    if (!verify(window_menu != nullptr &&
                    QMetaObject::invokeMethod(window_menu, "aboutToShow",
                                              Qt::DirectConnection),
                "Window menu cannot be refreshed")) {
        return 1;
    }
    auto* new_window = window.findChild<QAction*>("newWindowAction");
    if (!verify(new_window != nullptr && new_window->isEnabled(),
                "New Window must remain functional without a document")) {
        return 1;
    }
    global_settings->trigger();
    application.processEvents();
    auto* global_dialog = window.findChild<QDialog*>("globalSettingsDialog");
    auto* global_language = global_dialog == nullptr
        ? nullptr : global_dialog->findChild<QComboBox*>("globalSettingsLanguage");
    auto* global_buttons = global_dialog == nullptr
        ? nullptr : global_dialog->findChild<QDialogButtonBox*>();
    if (!verify(global_dialog != nullptr &&
                    global_dialog->windowFlags().testFlag(Qt::SubWindow) &&
                    global_language != nullptr && global_language->count() == 4 &&
                    global_language->findText("cs") >= 0 &&
                    global_language->findText("de") >= 0 &&
                    global_language->findText("en") >= 0 &&
                    global_language->findText("fr") >= 0 &&
                    global_dialog->findChildren<QLineEdit*>().size() == 5 &&
                    global_dialog->findChild<QLineEdit*>(
                        "globalPathWorkingDirectory") != nullptr &&
                    global_buttons != nullptr &&
                    global_buttons->buttons().size() == 2,
                "Global Settings must implement the Python startup contract")) {
        return 1;
    }
    global_buttons->button(QDialogButtonBox::Cancel)->click();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    bool new_dialog_validation_checked = false;
    const auto create_document = [&](const QString& type, const QString& name) {
        new_document->trigger();
        application.processEvents();
        auto* dialog = window.findChild<QDialog*>("newDocumentDialog");
        auto* name_field = dialog == nullptr
            ? nullptr : dialog->findChild<QLineEdit*>("newDocumentFileName");
        auto* buttons = dialog == nullptr
            ? nullptr : dialog->findChild<QDialogButtonBox*>();
        const auto type_radios = dialog == nullptr
            ? QList<QRadioButton*>{} : dialog->findChildren<QRadioButton*>();
        if (!verify(dialog != nullptr && name_field != nullptr && buttons != nullptr,
                    "New must open the shared in-application document dialog") ||
            !verify(dialog->windowFlags().testFlag(Qt::SubWindow),
                    "new document dialog must be an internal SubWindow") ||
            !verify(type_radios.size() == 5 &&
                        std::all_of(type_radios.begin(), type_radios.end(),
                            [](const auto* radio) { return radio->isEnabled(); }),
                    "New must expose all five Python document types")) {
            return false;
        }
        if (!new_dialog_validation_checked) {
            name_field->clear();
            buttons->button(QDialogButtonBox::Ok)->click();
            application.processEvents();
            auto* validation_error = dialog->findChild<QLabel*>("newDocumentError");
            if (!verify(dialog->isVisible() && validation_error != nullptr &&
                            validation_error->isVisible() &&
                            !validation_error->text().isEmpty(),
                        "New must reject an empty file name inside the dialog")) {
                return false;
            }
            new_dialog_validation_checked = true;
        }
        name_field->setText(name);
        for (auto* radio : dialog->findChildren<QRadioButton*>()) {
            radio->setChecked(radio->property("documentType").toString() == type);
        }
        buttons->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        return true;
    };

    const QString identity = QUuid::createUuid().toString(QUuid::Id128);
    const QString part_name = QStringLiteral("DIL-STARTUP-") + identity;
    const QString assembly_name = QStringLiteral("SESTAVA-STARTUP-") + identity;
    const QString nested_assembly_name = QStringLiteral("NADSESTAVA-STARTUP-") + identity;
    const QString drawing_name = QStringLiteral("VYKRES-STARTUP-") + identity;
    if (!create_document(QStringLiteral("part"),
                         part_name + QStringLiteral(".prtz")) ||
        !verify(tabs->count() == 1 &&
                    tabs->tabText(0) == part_name + QStringLiteral(".prtz"),
                "new Part must open in the common document tabs") ||
        !verify(splitter->isVisible() && box->isEnabled() && tools_toolbar->isVisible(),
                "Part workspace and Modeling commands must become visible") ||
        !verify(save_as->isEnabled(), "Save As must be available for an open document")) {
        return 1;
    }

    parameters->trigger();
    application.processEvents();
    auto* parameters_dialog =
        window.findChild<QDialog*>("documentParametersDialog");
    auto* parameters_table = parameters_dialog == nullptr ? nullptr :
        parameters_dialog->findChild<QTableWidget*>("documentParametersTable");
    auto* parameter_language = parameters_dialog == nullptr ? nullptr :
        parameters_dialog->findChild<QComboBox*>("parameterLanguage");
    if (!verify(parameters->isEnabled() && parameters_dialog != nullptr &&
                    parameters_dialog->windowFlags().testFlag(Qt::SubWindow) &&
                    parameters_table != nullptr && parameters_table->rowCount() >= 12 &&
                    parameters_table->columnCount() == 4 &&
                    parameter_language != nullptr && parameter_language->count() >= 4 &&
                    parameters_dialog->findChild<QTableWidget*>(
                        "documentRelationsTable") == nullptr,
                "Parameters must match the localized Python table contract")) {
        return 1;
    }
    parameters_dialog->reject();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();

    const std::array tool_dialogs{
        std::pair{material, QStringLiteral("materialDialog")},
        std::pair{relations, QStringLiteral("relationsDialog")},
        std::pair{family_table, QStringLiteral("familyTableDialog")},
        std::pair{file_settings, QStringLiteral("fileSettingsDialog")}};
    for (const auto& [action, object_name] : tool_dialogs) {
        if (!verify(action != nullptr && action->isEnabled(),
                    "document tool must be enabled for an open Part")) return 1;
        action->trigger(); application.processEvents();
        auto* tool_dialog = window.findChild<QDialog*>(object_name);
        auto* tool_buttons = tool_dialog == nullptr
            ? nullptr : tool_dialog->findChild<QDialogButtonBox*>();
        if (!verify(tool_dialog != nullptr &&
                        tool_dialog->windowFlags().testFlag(Qt::SubWindow) &&
                        tool_buttons != nullptr && tool_buttons->buttons().size() == 2,
                    "document tool must use the shared OK/Cancel SubWindow")) {
            return 1;
        }
        tool_buttons->button(QDialogButtonBox::Cancel)->click();
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    new_document->trigger();
    application.processEvents();
    auto* duplicate_dialog = window.findChild<QDialog*>("newDocumentDialog");
    auto* duplicate_name = duplicate_dialog == nullptr
        ? nullptr : duplicate_dialog->findChild<QLineEdit*>("newDocumentFileName");
    auto* duplicate_buttons = duplicate_dialog == nullptr
        ? nullptr : duplicate_dialog->findChild<QDialogButtonBox*>();
    auto* duplicate_error = duplicate_dialog == nullptr
        ? nullptr : duplicate_dialog->findChild<QLabel*>("newDocumentError");
    if (!verify(duplicate_name != nullptr && duplicate_buttons != nullptr &&
                    duplicate_error != nullptr,
                "new document validation controls are missing")) {
        return 1;
    }
    duplicate_name->setText(part_name);
    duplicate_buttons->button(QDialogButtonBox::Ok)->click();
    application.processEvents();
    if (!verify(duplicate_dialog->isVisible() && duplicate_error->isVisible() &&
                    tabs->count() == 1,
                "duplicate document path must keep the New dialog transactional")) {
        return 1;
    }
    duplicate_buttons->button(QDialogButtonBox::Cancel)->click();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();

    box->trigger();
    application.processEvents();
    auto* properties = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    auto* buttons = properties == nullptr
        ? nullptr : properties->findChild<QDialogButtonBox*>();
    if (!verify(buttons != nullptr, "Box must open the shared Properties window")) {
        return 1;
    }
    buttons->button(QDialogButtonBox::Ok)->click();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    const auto part_history_root = [&]() -> QTreeWidgetItem* {
        for (QTreeWidgetItemIterator it(tree);*it;++it)
            if ((*it)->data(0,Qt::UserRole+3).toString()=="part-body") return *it;
        return nullptr;
    };
    if (!verify(part_history_root()!=nullptr,"New Part has no Body history branch")) return 1;
    QTreeWidgetItem* box_tree_item{};
    if (tree->topLevelItemCount() == 1) {
        auto* root = part_history_root();
        for (int index = 0; index < root->childCount(); ++index) {
            if (root->child(index)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-container")) {
                box_tree_item = root->child(index);
                break;
            }
        }
    }
    if (!verify(box_tree_item != nullptr,
                "confirming Box must create the first Part history item")) {
        return 1;
    }
    const auto original_box_id=box_tree_item->data(0,Qt::UserRole).toString().toStdString();
    auto* selection_viewer = dynamic_cast<zima::viewer::MeshView*>(
        window.findChild<QOpenGLWidget*>("modelWorkspace"));
    tree->setCurrentItem(box_tree_item);
    application.processEvents();
    const auto tree_confirmed = selection_viewer == nullptr
        ? std::optional<zima::viewer::ViewerCandidate>{}
        : selection_viewer->confirmed_candidate();
    if (!verify(tree_confirmed && tree_confirmed->kind ==
                    zima::viewer::CandidateKind::Container &&
                    tree_confirmed->owner_id ==
                        box_tree_item->data(0, Qt::UserRole).toString().toStdString(),
                "Tree selectionChanged did not synchronize the View candidate")) {
        return 1;
    }
    std::optional<QPointF> empty_view_position;
    for (int y = 4; y < selection_viewer->height() && !empty_view_position; y += 8) {
        for (int x = 4; x < selection_viewer->width(); x += 8) {
            const QPointF position{static_cast<qreal>(x), static_cast<qreal>(y)};
            if (selection_viewer->selection_candidates_at(position).empty()) {
                empty_view_position = position;
                break;
            }
        }
    }
    if (!verify(empty_view_position.has_value(),
                "Viewer selection contract exposed no empty click position")) {
        return 1;
    }
    QMouseEvent empty_view_press(QEvent::MouseButtonPress, *empty_view_position,
        selection_viewer->mapToGlobal(empty_view_position->toPoint()),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(selection_viewer, &empty_view_press);
    application.processEvents();
    if (!verify(!selection_viewer->confirmed_candidate() &&
                    tree->selectedItems().empty() && tree->currentItem() == nullptr,
                "Empty View click did not clear the shared View and Tree selection")) {
        return 1;
    }
    {
        // RMB cycling must consume the exact same ordered candidate list as
        // hover/LMB and remain active only until an LMB confirms one of the
        // offered candidates. The default (Container-only) selection filter
        // offers exactly one whole-body candidate per Box, so switch to the
        // Face filter to expose genuinely overlapping Face candidates near
        // a shared edge, matching real multi-candidate cycling.
        auto* selection_filter_combo =
            window.findChild<QComboBox*>("selectionFilterCombo");
        if (!verify(selection_filter_combo != nullptr,
                    "the real workspace has no selectionFilterCombo")) {
            return 1;
        }
        selection_filter_combo->setCurrentIndex(1);
        application.processEvents();
        std::optional<QPointF> multi_candidate_position;
        std::vector<zima::viewer::ViewerCandidate> multi_candidates;
        for (int y = 4; y < selection_viewer->height() && !multi_candidate_position;
             y += 4) {
            for (int x = 4; x < selection_viewer->width(); x += 4) {
                const QPointF position{static_cast<qreal>(x), static_cast<qreal>(y)};
                auto candidates = selection_viewer->selection_candidates_at(position);
                if (candidates.size() > 1) {
                    multi_candidate_position = position;
                    multi_candidates = std::move(candidates);
                    break;
                }
            }
        }
        if (!verify(multi_candidate_position.has_value(),
                    "Box scene offered no overlapping candidates for RMB cycling")) {
            return 1;
        }
        QMouseEvent hover_move(QEvent::MouseMove, *multi_candidate_position,
            selection_viewer->mapToGlobal(multi_candidate_position->toPoint()),
            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(selection_viewer, &hover_move);
        // sendEvent is synchronous. Inspect the exact state produced by this
        // move before pumping unrelated native Wayland pointer events, which
        // may legitimately replace a synthetic test hover with the real
        // cursor position.
        const auto initial_hover = selection_viewer->hovered_candidate();
        if (!verify(initial_hover.has_value(),
                    "Overlapping position did not offer an initial hover candidate")) {
            return 1;
        }
        const auto send_rmb_press = [&] {
            QMouseEvent press(QEvent::MouseButtonPress, *multi_candidate_position,
                selection_viewer->mapToGlobal(multi_candidate_position->toPoint()),
                Qt::RightButton, Qt::RightButton, Qt::NoModifier);
            QApplication::sendEvent(selection_viewer, &press);
        };
        send_rmb_press();
        const auto after_first_cycle = selection_viewer->hovered_candidate();
        if (!verify(after_first_cycle.has_value() &&
                        !selection_viewer->confirmed_candidate() &&
                        (after_first_cycle->kind != initial_hover->kind ||
                            after_first_cycle->semantic_key !=
                                initial_hover->semantic_key ||
                            after_first_cycle->owner_id != initial_hover->owner_id),
                    "RMB before LMB confirmation did not cycle to the next "
                    "candidate in the shared ordered list")) {
            return 1;
        }
        // Cycling all the way back to the original candidate (once per
        // remaining entry) must reproduce the exact same candidate, proving
        // RMB consumes one fixed ordered list rather than recomputing a
        // different candidate on each press.
        for (std::size_t step = 1; step < multi_candidates.size(); ++step) {
            send_rmb_press();
        }
        const auto cycled_back = selection_viewer->hovered_candidate();
        if (!verify(cycled_back.has_value() &&
                        cycled_back->kind == initial_hover->kind &&
                        cycled_back->semantic_key == initial_hover->semantic_key &&
                        cycled_back->owner_id == initial_hover->owner_id,
                    "RMB cycling did not return to the original candidate after "
                    "visiting the complete ordered list")) {
            return 1;
        }
        QMouseEvent lmb_press(QEvent::MouseButtonPress, *multi_candidate_position,
            selection_viewer->mapToGlobal(multi_candidate_position->toPoint()),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(selection_viewer, &lmb_press);
        application.processEvents();
        const auto confirmed_after_cycle = selection_viewer->confirmed_candidate();
        if (!verify(confirmed_after_cycle.has_value() &&
                        confirmed_after_cycle->kind == initial_hover->kind &&
                        confirmed_after_cycle->semantic_key ==
                            initial_hover->semantic_key &&
                        confirmed_after_cycle->owner_id == initial_hover->owner_id,
                    "LMB did not confirm the exact RMB-cycled candidate")) {
            return 1;
        }
        QMouseEvent clear_press(QEvent::MouseButtonPress, *empty_view_position,
            selection_viewer->mapToGlobal(empty_view_position->toPoint()),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(selection_viewer, &clear_press);
        application.processEvents();
        selection_filter_combo->setCurrentIndex(0);
        application.processEvents();
        if (!verify(!selection_viewer->confirmed_candidate(),
                    "Clearing selection after RMB cycling left a stale candidate")) {
            return 1;
        }
        // Leave the pointer over empty space so no stale hover candidate
        // carries into the next scenario's own hover search.
        QMouseEvent settle_move(QEvent::MouseMove, *empty_view_position,
            selection_viewer->mapToGlobal(empty_view_position->toPoint()),
            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(selection_viewer, &settle_move);
        application.processEvents();
        selection_viewer->repaint();
        application.processEvents();
    }
    construction_point->trigger();
    application.processEvents();
    auto* point_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    auto* point_references = point_dialog == nullptr
        ? nullptr : point_dialog->findChild<QTableWidget*>(
            "constructionReferenceTable");
    auto* point_viewer = dynamic_cast<zima::viewer::MeshView*>(
        window.findChild<QOpenGLWidget*>("modelWorkspace"));
    auto* point_properties =
        dynamic_cast<zima::app::ConstructionPropertiesDialog*>(point_dialog);
    std::optional<zima::viewer::ViewerCandidate> own_point_candidate;
    if (point_viewer != nullptr && point_properties != nullptr) {
        for (int y = 2; y < point_viewer->height() && !own_point_candidate; y += 2) {
            for (int x = 2; x < point_viewer->width(); x += 2) {
                for (const auto& candidate : point_viewer->selection_candidates_at(
                         {static_cast<qreal>(x), static_cast<qreal>(y)})) {
                    // The Point's own parameter dimensions deliberately stay
                    // interactive while its placement-reference command is
                    // armed. Only its own reference geometry must be excluded.
                    if (candidate.kind !=
                            zima::viewer::CandidateKind::Dimension &&
                        point_properties->owns_reference_owner(candidate.owner_id)) {
                        own_point_candidate = candidate;
                        break;
                    }
                }
                if (own_point_candidate) break;
            }
        }
    }
    if (!verify(point_properties != nullptr && !own_point_candidate,
                "Point placement offered its own Container Origin geometry")) {
        if (own_point_candidate) {
            std::cerr << "  owner='" << own_point_candidate->owner_id
                      << "' key='" << own_point_candidate->semantic_key
                      << "' geometry=" << static_cast<int>(
                             own_point_candidate->geometry) << '\n';
        }
        return 1;
    }
    if (!verify(point_references != nullptr &&
                    !point_viewer->selection_candidates_at(
                        QPointF(point_viewer->width() / 2.0,
                                point_viewer->height() / 2.0)).empty(),
                "Point Properties did not automatically arm its first "
                "reference field")) {
        return 1;
    }
    // Opening Properties owns the first useful empty reference field. The
    // same candidate universe is then consumed by hover, RMB cycling and LMB.
    std::optional<QPointF> origin_axis_hover_position;
    if (point_viewer != nullptr) {
        for (int y = 2; y < point_viewer->height() && !origin_axis_hover_position; y += 4) {
            for (int x = 2; x < point_viewer->width(); x += 4) {
                const QPointF local{static_cast<qreal>(x), static_cast<qreal>(y)};
                const auto candidates = point_viewer->selection_candidates_at(local);
                if (!candidates.empty() &&
                    candidates.front().kind == zima::viewer::CandidateKind::Axis &&
                    candidates.front().semantic_key.starts_with("origin:axis:")) {
                    origin_axis_hover_position = local;
                    break;
                }
            }
        }
    }
    if (!verify(origin_axis_hover_position.has_value(),
                "Point placement did not offer an Origin axis in View")) return 1;
    const auto origin_axis_candidates =
        point_viewer->selection_candidates_at(*origin_axis_hover_position);
    if (!verify(!origin_axis_candidates.empty() &&
                    !point_properties->owns_reference_owner(
                        origin_axis_candidates.front().owner_id) &&
                    origin_axis_candidates.front().owner_id.ends_with(":origin"),
                "Overlapped Point placement axis was not owned by the document Origin")) {
        return 1;
    }
    QMouseEvent origin_axis_move(QEvent::MouseMove, *origin_axis_hover_position,
        point_viewer->mapToGlobal(origin_axis_hover_position->toPoint()),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(point_viewer, &origin_axis_move);
    point_viewer->repaint();
    if (!verify(point_viewer->hovered_candidate() &&
                    point_viewer->hovered_candidate()->semantic_key.starts_with(
                        "origin:axis:") &&
                    contains_orange_hover(point_viewer->grabFramebuffer()),
                "Origin axis hover was not rendered orange during placement")) return 1;
    auto* point_x_field =
        point_dialog->findChild<QDoubleSpinBox*>("constructionX");
    if (!verify(point_x_field != nullptr,
                "Point Properties did not expose its X field")) return 1;
    // Zero container dimensions are intentionally absent. Give X a nonzero
    // value to verify the same live Properties/View editing contract without
    // reviving a misleading 0 mm annotation.
    point_x_field->setValue(5.0);
    application.processEvents();
    std::optional<QPointF> point_x_dimension_position;
    for (int y = 2; y < point_viewer->height() &&
            !point_x_dimension_position; y += 2) {
        for (int x = 2; x < point_viewer->width(); x += 2) {
            const QPointF position{static_cast<qreal>(x), static_cast<qreal>(y)};
            const auto candidates = point_viewer->selection_candidates_at(position);
            // A click confirms the offered first candidate, not an occluded
            // dimension farther down the shared candidate list.
            if (!candidates.empty() && candidates.front().kind == zima::viewer::CandidateKind::Dimension &&
                candidates.front().owner_id == point_properties->construction_id() &&
                candidates.front().semantic_key == "parameter:placement:x") {
                point_x_dimension_position = position;
                break;
            }
        }
    }
    if (!verify(point_x_dimension_position.has_value(),
                "Point X dimension was not an interactive View candidate")) return 1;
    QMouseEvent point_dimension_double_click(
        QEvent::MouseButtonDblClick, *point_x_dimension_position,
        point_viewer->mapToGlobal(point_x_dimension_position->toPoint()),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(point_viewer, &point_dimension_double_click);
    application.processEvents();
    auto* inline_point_dimension =
        point_viewer->findChild<QLineEdit*>("inlineDimensionValueEdit");
    if (!verify(inline_point_dimension != nullptr,
                "Point dimension double-click did not open its inline editor")) return 1;
    inline_point_dimension->setText(QStringLiteral("12"));
    QKeyEvent submit_point_dimension(
        QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(inline_point_dimension, &submit_point_dimension);
    application.processEvents();
    if (!verify(std::abs(point_x_field->value() - 12.0) < 1.0e-9,
                "Point dimension edit did not update the pending X field")) return 1;
    point_x_field->setValue(0.0);
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    bool zero_point_x_dimension_visible = false;
    for (int y = 2; y < point_viewer->height() &&
            !zero_point_x_dimension_visible; y += 2) {
        for (int x = 2; x < point_viewer->width(); x += 2) {
            const auto candidates = point_viewer->selection_candidates_at(
                {static_cast<qreal>(x), static_cast<qreal>(y)});
            zero_point_x_dimension_visible = std::any_of(
                candidates.begin(), candidates.end(), [&](const auto& candidate) {
                    return candidate.kind ==
                            zima::viewer::CandidateKind::Dimension &&
                        candidate.owner_id ==
                            point_properties->construction_id() &&
                        candidate.semantic_key ==
                            "parameter:placement:x";
                });
            if (zero_point_x_dimension_visible) break;
        }
    }
    if (!verify(!zero_point_x_dimension_visible,
                "Zero Point X dimension remained visible in Properties")) return 1;
    point_viewer->clear_selection();
    point_viewer->reset_candidate_cycle();
    std::optional<zima::viewer::ViewerCandidate> point_hover;
    QPointF point_hover_position;
    if (point_viewer != nullptr) {
        for (int y = 8; y < point_viewer->height() - 8 && !point_hover; y += 8) {
            for (int x = 8; x < point_viewer->width() - 8 && !point_hover; x += 8) {
                const QPointF local{static_cast<qreal>(x), static_cast<qreal>(y)};
                QMouseEvent move(QEvent::MouseMove, local,
                    point_viewer->mapToGlobal(local.toPoint()), Qt::NoButton,
                    Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(point_viewer, &move);
                point_hover = point_viewer->hovered_candidate();
                if (point_hover) point_hover_position = local;
            }
        }
    }
    if (!verify(point_dialog != nullptr && point_references != nullptr &&
                    point_viewer != nullptr && point_hover.has_value() &&
                    zima::app::placement_reference_candidate_has_stable_geometry(
                        *point_hover),
                "Point command did not offer a persisted viewer candidate on hover")) {
        return 1;
    }
    QMouseEvent point_press(QEvent::MouseButtonPress, point_hover_position,
        point_viewer->mapToGlobal(point_hover_position.toPoint()), Qt::LeftButton,
        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(point_viewer, &point_press);
    application.processEvents();
    auto* selected_reference_item = dynamic_cast<zima::ui::ReferenceCellItem*>(
        point_references->item(0, 1));
    if (!verify(selected_reference_item != nullptr &&
                    selected_reference_item->has_reference() &&
                    selected_reference_item->reference() ==
                        QString::fromStdString(point_hover->semantic_key),
                "Point command did not pass the confirmed viewer candidate to its dialog")) {
        return 1;
    }
    if (auto* point_buttons = point_dialog->findChild<QDialogButtonBox*>()) {
        point_buttons->button(QDialogButtonBox::Ok)->click();
    }
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    QTreeWidgetItem* point_tree_item{};
    box_tree_item = nullptr;
    if (tree->topLevelItemCount() == 1) {
        auto* root = part_history_root();
        for (int index = 0; index < root->childCount(); ++index) {
            auto* child = root->child(index);
            const auto item_kind = child->data(0, Qt::UserRole + 3).toString();
            if (item_kind == QStringLiteral("part-container") &&
                child->text(0) == QStringLiteral("+ Kvádr")) {
                box_tree_item = child;
            } else if (item_kind ==
                    QStringLiteral("part-construction")) {
                point_tree_item = child;
            }
        }
    }
    if (!verify(box_tree_item != nullptr && point_tree_item != nullptr,
                "Point refresh did not preserve the Box and Point history items")) {
        return 1;
    }
    const auto click_tree_item = [&](QTreeWidgetItem* item) {
        tree->scrollToItem(item);
        application.processEvents();
        const QPoint position = tree->visualItemRect(item).center();
        QMouseEvent press(QEvent::MouseButtonPress, position,
            tree->viewport()->mapToGlobal(position), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, position,
            tree->viewport()->mapToGlobal(position), Qt::LeftButton,
            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &release);
        application.processEvents();
    };
    // Only the active Body Origin is normally displayed inside a Body.
    auto* document_origin = part_history_root()->child(0);
    auto* document_x_axis = document_origin == nullptr ||
            document_origin->childCount() < 2
        ? nullptr : document_origin->child(1);
    if (!verify(document_x_axis != nullptr &&
                    document_origin->child(0)->text(0) == QStringLiteral("Point"),
                "Part Origin tree does not use the Python Point/axis structure")) {
        return 1;
    }
    tree->expandItem(document_origin);
    application.processEvents();
    click_tree_item(document_x_axis);
    const auto selected_origin_axis = point_viewer->confirmed_candidate();
    if (!verify(selected_origin_axis &&
                    selected_origin_axis->kind ==
                        zima::viewer::CandidateKind::Axis &&
                    selected_origin_axis->semantic_key == "origin:axis:x" &&
                    contains_cyan_selection(point_viewer->grabFramebuffer()),
                "Tree Origin axis did not select its complete View presentation")) {
        return 1;
    }
    if (!part_capture_path.isEmpty() &&
        !verify(point_viewer->grabFramebuffer().save(
                    part_capture_path + QStringLiteral(".origin-axis.png")),
                "Origin axis selection framebuffer save failed")) return 1;
    auto* container_origin_point = point_tree_item->child(0)->child(0);
    tree->expandItem(point_tree_item);
    tree->expandItem(point_tree_item->child(0));
    application.processEvents();
    click_tree_item(container_origin_point);
    const auto selected_container_origin_point = point_viewer->confirmed_candidate();
    if (!part_capture_path.isEmpty()) {
        point_viewer->grabFramebuffer().save(
            part_capture_path + QStringLiteral(".container-origin-point.png"));
    }
    if (!verify(selected_container_origin_point &&
                    selected_container_origin_point->kind ==
                        zima::viewer::CandidateKind::Vertex &&
                    selected_container_origin_point->semantic_key == "point" &&
                    contains_cyan_selection(point_viewer->grabFramebuffer()),
                "Point container Origin Point did not select its View marker")) {
        return 1;
    }
    click_tree_item(box_tree_item);
    click_tree_item(point_tree_item);
    application.processEvents();
    const auto confirmed_point_container = point_viewer->confirmed_candidate();
    if (!verify(confirmed_point_container &&
                    confirmed_point_container->kind ==
                        zima::viewer::CandidateKind::Container &&
                    confirmed_point_container->owner_id ==
                        point_tree_item->data(0, Qt::UserRole)
                            .toString().toStdString(),
                "Tree Point selection did not confirm its container marker in View")) {
        return 1;
    }
    point_viewer->clear_selection();
    click_tree_item(point_tree_item);
    const auto repeated_point_confirmation = point_viewer->confirmed_candidate();
    if (!verify(repeated_point_confirmation &&
                    repeated_point_confirmation->kind ==
                        zima::viewer::CandidateKind::Container &&
                    repeated_point_confirmation->owner_id ==
                        point_tree_item->data(0, Qt::UserRole)
                            .toString().toStdString(),
                "repeated LMB on the current Tree Point did not confirm it in View")) {
        return 1;
    }
    const auto point_container_id =
        point_tree_item->data(0, Qt::UserRole).toString().toStdString();
    point_viewer->clear_selection();
    tree->clearSelection();
    tree->setCurrentItem(nullptr);
    std::optional<QPointF> point_offer_position;
    for (int y = 2; y < point_viewer->height() && !point_offer_position; y += 4) {
        for (int x = 2; x < point_viewer->width(); x += 4) {
            const QPointF position{static_cast<qreal>(x), static_cast<qreal>(y)};
            const auto candidates = point_viewer->selection_candidates_at(position);
            if (!candidates.empty() &&
                candidates.front().kind == zima::viewer::CandidateKind::Container &&
                candidates.front().owner_id == point_container_id) {
                point_offer_position = position;
                break;
            }
        }
    }
    if (!verify(point_offer_position.has_value(),
                "ordinary View hover did not offer the Point container")) {
        return 1;
    }
    QMouseEvent point_container_move(QEvent::MouseMove, *point_offer_position,
        point_viewer->mapToGlobal(point_offer_position->toPoint()),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(point_viewer, &point_container_move);
    const auto hovered_point_container = point_viewer->hovered_candidate();
    if (!verify(hovered_point_container &&
                    hovered_point_container->kind ==
                        zima::viewer::CandidateKind::Container &&
                    hovered_point_container->owner_id == point_container_id,
                "Point marker did not become the orange View hover candidate")) {
        return 1;
    }
    application.processEvents();
    point_viewer->repaint();
    application.processEvents();
    if (!part_capture_path.isEmpty()) {
        point_viewer->grabFramebuffer().save(
            part_capture_path + QStringLiteral(".point-hover.png"));
    }
    if (!verify(contains_orange_hover(point_viewer->grabFramebuffer()),
                "Point hover candidate was not rendered orange")) return 1;
    QMouseEvent point_container_press(QEvent::MouseButtonPress,
        *point_offer_position,
        point_viewer->mapToGlobal(point_offer_position->toPoint()),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(point_viewer, &point_container_press);
    application.processEvents();
    if (!verify(contains_cyan_selection(point_viewer->grabFramebuffer()),
                "Point LMB confirmation was not rendered cyan")) return 1;
    const auto valid_origin_branch = [](QTreeWidgetItem* container) {
        if (container == nullptr || container->childCount() < 1) return false;
        auto* origin = container->child(0);
        return origin->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("construction-origin") &&
            origin->childCount() == 7;
    };
    if (!verify(valid_origin_branch(box_tree_item) &&
                    box_tree_item->childCount() == 2 &&
                    box_tree_item->child(1)->data(0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("part-container-entity") &&
                    valid_origin_branch(point_tree_item) &&
                    point_tree_item->childCount() == 1,
                "Box and Point do not expose the Python container hierarchy")) {
        return 1;
    }
    const auto create_construction = [&](QAction* action) {
        action->trigger();
        application.processEvents();
        auto* dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
        auto* dialog_buttons = dialog == nullptr
            ? nullptr : dialog->findChild<QDialogButtonBox*>();
        if (dialog_buttons == nullptr) return false;
        dialog_buttons->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        return true;
    };
    if (!verify(create_construction(construction_axis) &&
                    create_construction(construction_plane),
                "Axis or Plane container creation failed")) return 1;
    int datum_container_count = 0;
    if (tree->topLevelItemCount() == 1) {
        auto* root = part_history_root();
        for (int index = 0; index < root->childCount(); ++index) {
            auto* child = root->child(index);
            if (child->data(0, Qt::UserRole + 3).toString() !=
                    QStringLiteral("part-construction")) continue;
            if (valid_origin_branch(child) && child->childCount() == 2 &&
                child->child(1)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("construction-entity")) {
                ++datum_container_count;
            }
        }
    }
    if (!verify(datum_container_count == 2,
                "Axis and Plane do not expose Origin plus their datum entity")) {
        return 1;
    }
    point_viewer->clear_selection();
    tree->clearSelection();
    tree->setCurrentItem(nullptr);
    QApplication::sendEvent(point_viewer, &point_container_move);
    application.processEvents();
    if (!part_capture_path.isEmpty() &&
        !verify(window.grab().save(part_capture_path),
                "native Qt window capture failed")) {
        return 1;
    }
    {
        QOpenGLWidget* model_viewer{};
        for (auto* child : window.findChildren<QObject*>()) {
            if (child->objectName() == QStringLiteral("modelWorkspace")) {
                model_viewer = dynamic_cast<QOpenGLWidget*>(child);
                break;
            }
        }
        if (!verify(model_viewer != nullptr, "model viewer widget is missing")) {
            return 1;
        }
        const QImage framebuffer = model_viewer->grabFramebuffer();
        if (!part_capture_path.isEmpty() &&
            !verify(framebuffer.save(
                        part_capture_path + QStringLiteral(".viewer.png")),
                    "native Qt viewer framebuffer save failed")) return 1;
        if (!verify(!framebuffer.isNull(),
                    "Wayland OpenGL viewer framebuffer is null") ||
            !verify(contains_rendered_geometry(framebuffer),
                    "Wayland OpenGL viewer framebuffer contains no body geometry") ||
            !verify(contains_orange_hover(framebuffer),
                    "Wayland OpenGL framebuffer contains no orange Point hover")) {
            return 1;
        }
    }

    sketch->trigger();
    application.processEvents();
    properties = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    buttons = properties == nullptr
        ? nullptr : properties->findChild<QDialogButtonBox*>();
    if (!verify(buttons != nullptr,
                "Sketch must use the shared in-application Properties window")) {
        return 1;
    }
    auto* open_sketch = properties->findChild<QPushButton*>("sketchOpenButton");
    if (!verify(open_sketch != nullptr,
                "Sketch Properties must expose its SKETCH entry action")) {
        return 1;
    }
    open_sketch->click();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    if (!verify(tree->headerItem()->text(0).startsWith("SKETCHER") &&
                    tree->topLevelItemCount() >= 4,
                "confirming Sketch must add it to the Part tree") ||
        !verify(sketch_normal->isEnabled() && sketch_point->isEnabled() &&
                    sketch_construction->isEnabled() && sketch_segment->isEnabled() &&
                    sketch_polyline->isEnabled() && sketch_rectangle->isEnabled() &&
                    sketch_polygon->isEnabled() && sketch_circle->isEnabled() &&
                    sketch_arc->isEnabled() && sketch_ellipse->isEnabled() &&
                    sketch_trim->isEnabled() &&
                    sketch_mirror->isEnabled() &&
                    sketch_elliptical_arc->isEnabled() &&
                    sketch_bspline->isEnabled() &&
                    sketch_interpolating_spline->isEnabled() &&
                    sketch_midpoint->isEnabled() &&
                    sketch_symmetric->isEnabled() &&
                    sketch_concentric->isEnabled() &&
                    sketch_tangent->isEnabled() &&
                    sketch_horizontal->isEnabled() &&
                    sketch_vertical->isEnabled() &&
                    sketch_text->isEnabled() &&
                    sketch_external_reference->isEnabled() &&
                    sketch_constraints->isEnabled() &&
                    sketch_dimensions->isEnabled() &&
                    finish_sketch->isEnabled(),
                "active Sketch is missing its basic editing command set") ||
        !verify(tools_toolbar->actions().contains(finish_sketch) &&
                    tools_toolbar->actions().contains(sketch_rectangle) &&
                    tools_toolbar->actions().contains(sketch_polygon) &&
                    tools_toolbar->actions().contains(sketch_trim) &&
                    tools_toolbar->actions().contains(sketch_mirror) &&
                    tools_toolbar->actions().contains(sketch_elliptical_arc) &&
                    tools_toolbar->actions().contains(sketch_text) &&
                    tools_toolbar->actions().contains(sketch_external_reference) &&
                    tools_toolbar->actions().contains(sketch_constraints) &&
                    tools_toolbar->actions().contains(
                        sketch_universal_dimension) &&
                    sketch_constraints->menu()==nullptr,
                "Sketch commands must be exposed in the shared right toolbar")) {
        return 1;
    }
    sketch_constraints->trigger();application.processEvents();
    QPointer<QDialog> constraints_dialog=window.findChild<QDialog*>("sketchConstraintsDialog");
    auto* horizontal_automatic=constraints_dialog ? constraints_dialog->findChild<QPushButton*>("sketchHorizontalActionAutomatic") : nullptr;
    if (!verify(constraints_dialog && horizontal_automatic && horizontal_automatic->isChecked() &&
            constraints_dialog->windowFlags().testFlag(Qt::SubWindow),"Constraint command did not open its internal activity window")) return 1;
    horizontal_automatic->click();
    constraints_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();
    sketch_constraints->trigger();application.processEvents();
    constraints_dialog=window.findChild<QDialog*>("sketchConstraintsDialog");
    horizontal_automatic=constraints_dialog ? constraints_dialog->findChild<QPushButton*>("sketchHorizontalActionAutomatic") : nullptr;
    if (!verify(horizontal_automatic && horizontal_automatic->isChecked(),"Canceled constraint activity change leaked into Sketcher")) return 1;
    auto* manual_horizontal=constraints_dialog->findChild<QPushButton*>("sketchHorizontalActionChoose");
    manual_horizontal->click();application.processEvents();
    if (!verify(constraints_dialog->isVisible() && manual_horizontal->isChecked() &&
            sketch_constraints->isChecked() && constraints_dialog->width()<360,
            "Manual constraint must keep its compact window and green command active")) return 1;
    auto* manual_vertical=constraints_dialog->findChild<QPushButton*>("sketchVerticalActionChoose");
    manual_vertical->click();application.processEvents();
    if (!verify(manual_vertical->isChecked() && !manual_horizontal->isChecked(),
            "Switching constraints did not synchronize the green row")) return 1;
    QMouseEvent constraints_middle(QEvent::MouseButtonDblClick,QPointF(40,40),
        QPointF(40,40),QPointF(40,40),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
    QApplication::sendEvent(window.findChild<QOpenGLWidget*>("modelWorkspace"),&constraints_middle);application.processEvents();
    // OK may destroy the window during event delivery. Exercise that lifetime
    // explicitly instead of inspecting a dangling raw QWidget pointer.
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();
    if (!verify((!constraints_dialog || !constraints_dialog->isVisible()) && !sketch_constraints->isChecked(),
            "Middle double-click did not finish the manual constraint session")) return 1;
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();
    sketch_external_reference->trigger();
    application.processEvents();
    if (!verify(sketch_external_reference->isChecked(),
                "Sketch external-reference command must enter its viewer mode")) {
        return 1;
    }
    sketch_external_reference->trigger();
    application.processEvents();
    if (!verify(!sketch_external_reference->isChecked(),
                "Sketch external-reference command must leave its viewer mode")) {
        return 1;
    }
    sketch_text->trigger();
    application.processEvents();
    auto* text_value = window.findChild<QPlainTextEdit*>("sketchTextValue");
    auto* text_dialog = text_value == nullptr
        ? nullptr : qobject_cast<QDialog*>(text_value->parentWidget());
    auto* text_buttons = text_dialog == nullptr
        ? nullptr : text_dialog->findChild<QDialogButtonBox*>();
    auto* text_angle = text_dialog == nullptr
        ? nullptr : text_dialog->findChild<QDoubleSpinBox*>("sketchTextAngle");
    if (!verify(text_dialog != nullptr && text_buttons != nullptr &&
                    text_angle != nullptr && text_angle->value() == 0.0 &&
                    text_dialog->windowFlags().testFlag(Qt::SubWindow) &&
                    text_buttons->button(QDialogButtonBox::Ok) != nullptr &&
                    text_buttons->button(QDialogButtonBox::Cancel) != nullptr &&
                    text_buttons->button(QDialogButtonBox::Apply) == nullptr,
                "Sketch Text must use the shared internal OK/Cancel properties contract")) {
        return 1;
    }
    text_buttons->button(QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    auto* model_viewer = window.findChild<QOpenGLWidget*>("modelWorkspace");
    if (!verify(model_viewer != nullptr,
                "Sketch parity workflow is missing the model viewer")) {
        return 1;
    }
    const auto sketch_click_at = [&](const QPointF& local) {
        QMouseEvent move(QEvent::MouseMove, local,
            model_viewer->mapToGlobal(local.toPoint()), Qt::NoButton,
            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(model_viewer, &move);
        application.processEvents();
        QMouseEvent press(QEvent::MouseButtonPress, local,
            model_viewer->mapToGlobal(local.toPoint()), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(model_viewer, &press);
        application.processEvents();
        QMouseEvent release(QEvent::MouseButtonRelease, local,
            model_viewer->mapToGlobal(local.toPoint()), Qt::LeftButton,
            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(model_viewer, &release);
        application.processEvents();
    };
    const auto sketch_click = [&](double x_ratio, double y_ratio) {
        sketch_click_at({model_viewer->width() * x_ratio,
                         model_viewer->height() * y_ratio});
    };
    auto* sketch_viewer = dynamic_cast<zima::viewer::MeshView*>(model_viewer);
    if (!verify(sketch_viewer != nullptr,
                "Sketch interaction regression is missing MeshView")) {
        return 1;
    }
    const auto finish_sketch_tool_with_middle_double_click = [&] {
        const QPointF local{model_viewer->width() * 0.78,
                            model_viewer->height() * 0.24};
        QMouseEvent middle_double(QEvent::MouseButtonDblClick, local,
            model_viewer->mapToGlobal(local.toPoint()),
            Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(model_viewer, &middle_double);
        application.processEvents();
        QMouseEvent middle_release(QEvent::MouseButtonRelease, local,
            model_viewer->mapToGlobal(local.toPoint()),
            Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(model_viewer, &middle_release);
        application.processEvents();
    };
    struct SketchFinishFixture {
        QAction* action{};
        const char* name{};
        bool confirm_first_step{};
    };
    const std::array sketch_finish_fixtures{
        SketchFinishFixture{sketch_point, "Point", true},
        SketchFinishFixture{sketch_construction, "Construction Segment", true},
        SketchFinishFixture{sketch_segment, "Segment", true},
        SketchFinishFixture{sketch_polyline, "Polyline", true},
        SketchFinishFixture{sketch_rectangle, "Rectangle", true},
        SketchFinishFixture{sketch_polygon, "Regular Polygon", true},
        SketchFinishFixture{sketch_circle, "Circle", true},
        SketchFinishFixture{sketch_arc, "Arc", true},
        SketchFinishFixture{sketch_ellipse, "Ellipse", true},
        SketchFinishFixture{sketch_elliptical_arc, "Elliptical Arc", true},
        SketchFinishFixture{sketch_bspline, "B-spline", true},
        SketchFinishFixture{sketch_interpolating_spline,
            "Interpolating Spline", true},
        SketchFinishFixture{sketch_trim, "Trim", false},
        SketchFinishFixture{sketch_mirror, "Mirror", false},
        SketchFinishFixture{sketch_external_reference,
            "External Reference", false},
        SketchFinishFixture{sketch_midpoint, "Midpoint", false},
        SketchFinishFixture{sketch_symmetric, "Symmetric", false},
        SketchFinishFixture{sketch_concentric, "Concentric", false},
        SketchFinishFixture{sketch_tangent, "Tangent", false},
        SketchFinishFixture{sketch_equal, "Equal", false},
        SketchFinishFixture{sketch_dimension, "Dimension", false},
        SketchFinishFixture{sketch_universal_dimension,
            "Universal Dimension", false}};
    for (const auto& fixture : sketch_finish_fixtures) {
        const std::string activation_message =
            std::string{"Sketch finish matrix cannot activate "} + fixture.name;
        if (!verify(fixture.action != nullptr && fixture.action->isEnabled(),
                    activation_message.c_str())) {
            return 1;
        }
        fixture.action->trigger();
        application.processEvents();
        if (fixture.confirm_first_step) sketch_click(0.76, 0.28);
        finish_sketch_tool_with_middle_double_click();
        const std::string finish_message =
            std::string{"Middle-button double-click did not cleanly finish "} +
            fixture.name;
        if (!verify(!fixture.action->isChecked() &&
                        sketch_viewer->sketch_selection().empty() &&
                        workspace_state->text().contains(QStringLiteral("Výběr")),
                    finish_message.c_str())) {
            return 1;
        }
    }
    const auto spline_tree_count = [&] {
        int count{};
        QTreeWidgetItemIterator item(tree);
        while (*item != nullptr) {
            if ((*item)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("sketch-geometry") &&
                (*item)->text(0).contains(
                    QStringLiteral("spline"), Qt::CaseInsensitive)) {
                ++count;
            }
            ++item;
        }
        return count;
    };
    const int splines_before_three_points = spline_tree_count();
    sketch_bspline->trigger();
    application.processEvents();
    sketch_click(0.70, 0.32);
    sketch_click(0.76, 0.38);
    sketch_click(0.82, 0.31);
    finish_sketch_tool_with_middle_double_click();
    if (!verify(spline_tree_count() == splines_before_three_points + 1 &&
                    !sketch_bspline->isChecked() &&
                    sketch_viewer->sketch_selection().empty() &&
                    workspace_state->text().contains(QStringLiteral("Výběr")),
                "Three-point B-spline was not committed and cleanly finished")) {
        return 1;
    }
    const int splines_before_interpolation = spline_tree_count();
    sketch_interpolating_spline->trigger();
    application.processEvents();
    sketch_click(0.68, 0.30);
    sketch_click(0.75, 0.40);
    sketch_click(0.83, 0.29);
    finish_sketch_tool_with_middle_double_click();
    if (!verify(spline_tree_count() == splines_before_interpolation + 1 &&
                    !sketch_interpolating_spline->isChecked() &&
                    sketch_viewer->sketch_selection().empty() &&
                    workspace_state->text().contains(QStringLiteral("Výběr")),
                "Three-point interpolating spline was not committed and cleanly finished")) {
        return 1;
    }
    // Exact first-entity regression: a new Sketch point is created on the
    // persisted local origin offered by the common View candidate list.
    sketch_point->trigger();
    application.processEvents();
    std::optional<QPointF> sketch_origin_position;
    double sketch_origin_screen_distance = 1.0e300;
    const QPointF sketch_view_center{
        sketch_viewer->width() * 0.5, sketch_viewer->height() * 0.5};
    for (int y = 2; y < sketch_viewer->height(); y += 4) {
        for (int x = 2; x < sketch_viewer->width(); x += 4) {
            const QPointF position{static_cast<qreal>(x),
                                   static_cast<qreal>(y)};
            const auto candidates = sketch_viewer->selection_candidates_at(position);
            if (!candidates.empty() &&
                candidates.front().kind ==
                    zima::viewer::CandidateKind::SketchExternalReference &&
                candidates.front().semantic_key ==
                    "external_point:sketch_origin") {
                const QPointF delta = position - sketch_view_center;
                const double distance =
                    delta.x() * delta.x() + delta.y() * delta.y();
                if (distance < sketch_origin_screen_distance) {
                    sketch_origin_screen_distance = distance;
                    sketch_origin_position = position;
                }
            }
        }
    }
    if (!verify(sketch_origin_position.has_value(),
                "new Sketch did not offer its local origin for first-entity snapping")) {
        return 1;
    }
    sketch_click_at(*sketch_origin_position);
    QKeyEvent cancel_first_entity(QEvent::KeyPress, Qt::Key_Escape,
        Qt::NoModifier);
    QApplication::sendEvent(&window, &cancel_first_entity);
    application.processEvents();
    bool first_entity_referenced_origin{};
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        auto* group = tree->topLevelItem(index);
        if (group->text(0) != QStringLiteral("Vazby")) continue;
        for (int child = 0; child < group->childCount(); ++child) {
            if (group->child(child)->text(0).startsWith(
                    QStringLiteral("Reference bodu"))) {
                first_entity_referenced_origin = true;
                break;
            }
        }
    }
    if (!verify(first_entity_referenced_origin,
                "first Sketch entity did not persist its hidden origin reference")) {
        return 1;
    }
    const auto horizontal_marker_count=[&] {
        return std::ranges::count_if(sketch_viewer->mesh().constraint_markers,
            [](const auto& marker){return marker.label=="H";});
    };
    for (const bool enabled : {true,false}) {
        if (!enabled) {
            sketch_constraints->trigger();application.processEvents();
            auto* dialog=window.findChild<QDialog*>("sketchConstraintsDialog");
            dialog->findChild<QPushButton*>("sketchHorizontalActionAutomatic")->click();
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();
        }
        sketch_point->trigger();application.processEvents();
        sketch_click(0.76,0.29);
        const auto count_before=horizontal_marker_count();
        sketch_click_at({model_viewer->width()*0.88,model_viewer->height()*0.29+0.5});
        if (!verify(enabled ? horizontal_marker_count()>count_before : horizontal_marker_count()==count_before,
                "New Sketch point did not respect automatic Horizontal activity")) return 1;
        QApplication::sendEvent(&window,&cancel_first_entity);application.processEvents();
        undo->trigger();application.processEvents();undo->trigger();application.processEvents();
    }
    sketch_constraints->trigger();application.processEvents();
    constraints_dialog=window.findChild<QDialog*>("sketchConstraintsDialog");
    constraints_dialog->findChild<QPushButton*>("sketchHorizontalActionAutomatic")->click();
    constraints_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();
    sketch_rectangle->trigger();
    application.processEvents();
    sketch_click(0.42, 0.42);
    sketch_click(0.62, 0.62);
    QTreeWidgetItem* first_sketch_geometry{};
    QTreeWidgetItem* rectangle_segment{};
    // QTreeWidgetItemIterator registers itself with Qt's tree model. Keep it
    // strictly inside this lookup scope: later dialog teardown rebuilds the
    // Tree, and a still-live iterator makes Qt's item removal re-entrant and
    // invalid (observed as ensureValidIterator() crashing after Extrusion OK).
    {
        QTreeWidgetItemIterator sketch_item(tree);
        while (*sketch_item != nullptr) {
            if ((*sketch_item)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("sketch-geometry")) {
                if (first_sketch_geometry == nullptr)
                    first_sketch_geometry = *sketch_item;
                if ((*sketch_item)->text(0).startsWith(QStringLiteral("Úsečka"))) {
                    rectangle_segment = *sketch_item;
                    break;
                }
            }
            ++sketch_item;
        }
    }
    if (!verify(first_sketch_geometry != nullptr && rectangle_segment != nullptr &&
                    rectangle_segment->childCount() == 2 &&
                    rectangle_segment->child(0)->data(
                        0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("sketch-geometry") &&
                    rectangle_segment->child(1)->data(
                        0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("sketch-geometry"),
                "created Rectangle or its endpoint links are missing from the Sketch Tree")) {
        return 1;
    }
    // Exercise the real Sketcher command chain, not only action presence:
    // create an independent segment, constrain it, dimension it, and verify
    // both persisted relations appear in the shared Tree.
    // Use an auxiliary segment so the later body calculation still consumes
    // only the closed Rectangle profile.
    sketch_construction->trigger();
    application.processEvents();
    sketch_click(0.34, 0.70);
    sketch_click(0.57, 0.70);
    QKeyEvent cancel_construction(QEvent::KeyPress, Qt::Key_Escape,
        Qt::NoModifier);
    QApplication::sendEvent(&window, &cancel_construction);
    application.processEvents();
    QString dimensioned_segment_id;
    {
        QTreeWidgetItemIterator item(tree);
        while (*item != nullptr) {
            if ((*item)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("sketch-geometry") &&
                (*item)->text(0).contains(QStringLiteral("Úsečka"))) {
                dimensioned_segment_id =
                    (*item)->data(0, Qt::UserRole).toString();
            }
            ++item;
        }
    }
    if (!verify(!dimensioned_segment_id.isEmpty(),
                "Sketch Segment command did not create selectable Tree geometry")) {
        return 1;
    }
    const auto find_sketch_tree_item = [&](const QString& role,
                                           const QString& id = {})
        -> QTreeWidgetItem* {
        QTreeWidgetItemIterator item(tree);
        while (*item != nullptr) {
            if ((*item)->data(0, Qt::UserRole + 3).toString() == role &&
                (id.isEmpty() || (*item)->data(0, Qt::UserRole).toString() == id)) {
                return *item;
            }
            ++item;
        }
        return nullptr;
    };
    auto* constraint_group = [&]() -> QTreeWidgetItem* {
        for (int index = 0; index < tree->topLevelItemCount(); ++index) {
            if (tree->topLevelItem(index)->text(0) == QStringLiteral("Vazby"))
                return tree->topLevelItem(index);
        }
        return nullptr;
    }();
    const bool has_inferred_horizontal = constraint_group != nullptr && [&] {
        for (int index = 0; index < constraint_group->childCount(); ++index) {
            if (constraint_group->child(index)->text(0).startsWith(
                    QStringLiteral("Vodorovnost"))) return true;
        }
        return false;
    }();
    if (!verify(constraint_group != nullptr && constraint_group->childCount() >= 5 &&
                    has_inferred_horizontal,
                "Sketch inference did not persist its offered Horizontal constraint")) {
        return 1;
    }
    tree->clearSelection();
    tree->setCurrentItem(nullptr);
    sketch_viewer->clear_selection();
    application.processEvents();
    sketch_dimension->trigger();
    application.processEvents();
    std::optional<QPointF> dimension_segment_position;
    std::string dimension_segment_semantic_key;
    for (int y = 2; y < sketch_viewer->height() &&
                        !dimension_segment_position; y += 4) {
        for (int x = 2; x < sketch_viewer->width(); x += 4) {
            const QPointF position{static_cast<qreal>(x),
                                   static_cast<qreal>(y)};
            const auto candidates =
                sketch_viewer->selection_candidates_at(position);
            if (!candidates.empty() &&
                candidates.front().kind ==
                    zima::viewer::CandidateKind::SketchSegment) {
                dimension_segment_position = position;
                dimension_segment_semantic_key = candidates.front().semantic_key;
                break;
            }
        }
    }
    if (!verify(dimension_segment_position.has_value(),
                "Dimension command did not offer the requested Sketch segment in View")) {
        return 1;
    }
    // The first scanned hit lies at the edge of the 9 px picking tolerance.
    // Average the complete hit band of this exact segment so the synthetic
    // click lands on its visible center and remains stable across repaints.
    double dimension_screen_x{};
    double dimension_screen_y{};
    std::size_t dimension_screen_samples{};
    for (int y = 2; y < sketch_viewer->height(); y += 4) {
        for (int x = 2; x < sketch_viewer->width(); x += 4) {
            const QPointF position{static_cast<qreal>(x),
                                   static_cast<qreal>(y)};
            const auto candidates =
                sketch_viewer->selection_candidates_at(position);
            if (!candidates.empty() &&
                candidates.front().kind ==
                    zima::viewer::CandidateKind::SketchSegment &&
                candidates.front().semantic_key ==
                    dimension_segment_semantic_key) {
                dimension_screen_x += position.x();
                dimension_screen_y += position.y();
                ++dimension_screen_samples;
            }
        }
    }
    if (!verify(dimension_screen_samples > 0,
                "Dimension segment had no stable View hit area")) {
        return 1;
    }
    dimension_segment_position = QPointF{
        dimension_screen_x / static_cast<double>(dimension_screen_samples),
        dimension_screen_y / static_cast<double>(dimension_screen_samples)};
    const auto stable_dimension_candidates =
        sketch_viewer->selection_candidates_at(*dimension_segment_position);
    if (!verify(!stable_dimension_candidates.empty() &&
                    stable_dimension_candidates.front().kind ==
                        zima::viewer::CandidateKind::SketchSegment &&
                    stable_dimension_candidates.front().semantic_key ==
                        dimension_segment_semantic_key,
                "Dimension segment center was not stable in the common View candidate list")) {
        return 1;
    }
    // Exact user regression: activate Dimension with no prior Tree selection,
    // then confirm a segment in View. The handler changes the viewer filter
    // while processing this candidate and must not invalidate its reference.
    // The segment path still creates the dimension from both persisted endpoint IDs.
    sketch_click_at(*dimension_segment_position);
    std::optional<QPointF> empty_dimension_placement;
    for (int y = 20; y < sketch_viewer->height() - 20 &&
            !empty_dimension_placement; y += 20) {
        for (int x = 20; x < sketch_viewer->width() - 20; x += 20) {
            const QPointF position{static_cast<qreal>(x),
                                   static_cast<qreal>(y)};
            if (sketch_viewer->selection_candidates_at(position).empty()) {
                empty_dimension_placement = position;
                break;
            }
        }
    }
    if (!verify(empty_dimension_placement.has_value(),
                "Sketch Dimension test found no empty View placement point")) {
        return 1;
    }
    sketch_click_at(*empty_dimension_placement);
    if (!verify(window.findChild<QDialog*>("zimaPropertiesSubWindow") == nullptr,
                "View-selected Sketch segment unexpectedly opened Dimension Properties")) {
        return 1;
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    QTreeWidgetItem* dimension_group{};
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        if (tree->topLevelItem(index)->text(0) == QStringLiteral("Kóty")) {
            dimension_group = tree->topLevelItem(index);
            break;
        }
    }
    if (!verify(dimension_group != nullptr && dimension_group->childCount() == 1 &&
                    !workspace_state->text().contains(QStringLiteral("první bod")),
                "segment Dimension did not persist directly in View or restarted a point command")) {
        return 1;
    }
    // Universal Dimension must distinguish a reference click from an empty
    // placement click. MeshView calls the world-click callback before it
    // confirms the candidate from the same press, so Segment -> Axis used to
    // commit the provisional segment length and never reach angular input.
    sketch_universal_dimension->trigger();
    application.processEvents();
    sketch_click_at(*dimension_segment_position);
    application.processEvents();
    std::optional<QPointF> universal_axis_position;
    for (int y = 2; y < sketch_viewer->height() &&
                        !universal_axis_position; y += 4) {
        for (int x = 2; x < sketch_viewer->width(); x += 4) {
            const QPointF position{static_cast<qreal>(x),
                                   static_cast<qreal>(y)};
            const auto candidates =
                sketch_viewer->selection_candidates_at(position);
            if (!candidates.empty() && candidates.front().kind ==
                    zima::viewer::CandidateKind::SketchAxis) {
                universal_axis_position = position;
                break;
            }
        }
    }
    if (!verify(universal_axis_position.has_value(),
                "Universal Dimension did not offer a Sketch axis after its first segment")) {
        return 1;
    }
    dimension_group = nullptr;
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        if (tree->topLevelItem(index)->text(0) == QStringLiteral("Kóty")) {
            dimension_group = tree->topLevelItem(index);
            break;
        }
    }
    if (!verify(dimension_group != nullptr,
                "Universal Dimension lost the Sketch dimension group")) {
        return 1;
    }
    const int dimensions_before_universal_angle = dimension_group->childCount();
    sketch_click_at(*universal_axis_position);
    application.processEvents();
    dimension_group = nullptr;
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        if (tree->topLevelItem(index)->text(0) == QStringLiteral("Kóty")) {
            dimension_group = tree->topLevelItem(index);
            break;
        }
    }
    if (!verify(dimension_group != nullptr &&
                    dimension_group->childCount() ==
                        dimensions_before_universal_angle,
                "Universal Dimension committed segment length before confirming its second axis")) {
        return 1;
    }
    if (!verify(workspace_state->text().contains(
                    QStringLiteral("klikněte do prostoru")),
                "Universal Dimension did not enter angular placement after Segment -> Axis")) {
        return 1;
    }
    // A -> B -> A is the single-geometry symmetric contract: selecting the
    // same real line again asks for its implicit mirror about axis B.  The
    // final empty click places one DistanceLineSymmetric/AngleSymmetric item.
    sketch_click_at(*dimension_segment_position);
    application.processEvents();
    if (!verify(workspace_state->text().contains(
                    QStringLiteral("symetrická kóta"), Qt::CaseInsensitive),
                "Universal Dimension did not enter symmetric placement from "
                "the A -> B -> A sequence")) {
        return 1;
    }
    QKeyEvent cancel_universal_angle(QEvent::KeyPress, Qt::Key_Escape,
        Qt::NoModifier);
    QApplication::sendEvent(&window, &cancel_universal_angle);
    application.processEvents();
    QApplication::sendEvent(&window, &cancel_universal_angle);
    application.processEvents();
    sketch_universal_dimension->trigger();
    application.processEvents();
    std::map<std::string, std::tuple<double, double, std::size_t>>
        universal_point_samples;
    for (int y = 2; y < sketch_viewer->height(); y += 4) {
        for (int x = 2; x < sketch_viewer->width(); x += 4) {
            const QPointF position{static_cast<qreal>(x),
                                   static_cast<qreal>(y)};
            const auto candidates =
                sketch_viewer->selection_candidates_at(position);
            if (candidates.empty() || candidates.front().kind !=
                    zima::viewer::CandidateKind::SketchPoint) continue;
            auto& [sum_x, sum_y, count] =
                universal_point_samples[candidates.front().semantic_key];
            sum_x += position.x();
            sum_y += position.y();
            ++count;
        }
    }
    std::vector<std::pair<std::string, QPointF>> universal_point_hits;
    for (const auto& [semantic_key, samples] : universal_point_samples) {
        const auto& [sum_x, sum_y, count] = samples;
        if (count != 0) {
            universal_point_hits.emplace_back(semantic_key,
                QPointF{sum_x / static_cast<double>(count),
                        sum_y / static_cast<double>(count)});
        }
    }
    std::optional<std::array<QPointF, 3>> universal_angle_points;
    for (std::size_t first = 0; first < universal_point_hits.size() &&
            !universal_angle_points; ++first) {
        for (std::size_t second = first + 1;
             second < universal_point_hits.size() && !universal_angle_points;
             ++second) {
            for (std::size_t third = second + 1;
                 third < universal_point_hits.size(); ++third) {
                const auto a = universal_point_hits[second].second -
                    universal_point_hits[first].second;
                const auto b = universal_point_hits[third].second -
                    universal_point_hits[first].second;
                if (std::abs(a.x() * b.y() - a.y() * b.x()) > 25.0) {
                    universal_angle_points = std::array{
                        universal_point_hits[first].second,
                        universal_point_hits[second].second,
                        universal_point_hits[third].second};
                    break;
                }
            }
        }
    }
    if (!verify(universal_angle_points.has_value(),
                "Universal Dimension test found no three non-collinear Sketch points")) {
        return 1;
    }
    dimension_group = nullptr;
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        if (tree->topLevelItem(index)->text(0) == QStringLiteral("Kóty")) {
            dimension_group = tree->topLevelItem(index);
            break;
        }
    }
    const int dimensions_before_three_point =
        dimension_group == nullptr ? 0 : dimension_group->childCount();
    for (const auto& point : *universal_angle_points) {
        sketch_click_at(point);
        application.processEvents();
    }
    dimension_group = nullptr;
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        if (tree->topLevelItem(index)->text(0) == QStringLiteral("Kóty")) {
            dimension_group = tree->topLevelItem(index);
            break;
        }
    }
    if (!verify(dimension_group != nullptr &&
                    dimension_group->childCount() ==
                        dimensions_before_three_point,
                "Universal Dimension committed two-point distance before confirming third point")) {
        return 1;
    }
    if (!verify(!workspace_state->text().contains(
                    QStringLiteral("umístění úhlové kóty")),
                "Universal Dimension created an angle before receiving four points")) {
        return 1;
    }
    std::optional<QPointF> universal_fourth_point;
    for (const auto& [semantic_key, position] : universal_point_hits) {
        static_cast<void>(semantic_key);
        if (std::ranges::all_of(*universal_angle_points,
                [&](const auto& selected) {
                    return QLineF(position, selected).length() > 3.0;
                })) {
            universal_fourth_point = position;
            break;
        }
    }
    if (!verify(universal_fourth_point.has_value(),
                "Universal Dimension test found no independent fourth point")) {
        return 1;
    }
    sketch_click_at(*universal_fourth_point);
    application.processEvents();
    if (!verify(workspace_state->text().contains(
                    QStringLiteral("umístění úhlové kóty")),
                "Universal Dimension did not create its angle after four points")) {
        return 1;
    }
    QApplication::sendEvent(&window, &cancel_universal_angle);
    application.processEvents();
    QApplication::sendEvent(&window, &cancel_universal_angle);
    application.processEvents();
    sketch_universal_dimension->trigger();
    application.processEvents();
    struct UniversalSegmentHit {
        QPointF screen;
        zima::kernel::Vec3 direction;
    };
    std::map<std::string, std::tuple<double, double, std::size_t,
        zima::kernel::Vec3>> universal_segment_samples;
    for (int y = 2; y < sketch_viewer->height(); y += 4) {
        for (int x = 2; x < sketch_viewer->width(); x += 4) {
            const QPointF position{static_cast<qreal>(x),
                                   static_cast<qreal>(y)};
            const auto candidates =
                sketch_viewer->selection_candidates_at(position);
            if (candidates.empty() || candidates.front().kind !=
                    zima::viewer::CandidateKind::SketchSegment) continue;
            const auto edge = sketch_viewer->candidate_edge(candidates.front());
            if (!edge || edge->points.size() < 2) continue;
            auto& [sum_x, sum_y, count, direction] =
                universal_segment_samples[candidates.front().semantic_key];
            sum_x += position.x();
            sum_y += position.y();
            ++count;
            direction = {
                edge->points.back().x - edge->points.front().x,
                edge->points.back().y - edge->points.front().y,
                edge->points.back().z - edge->points.front().z};
        }
    }
    std::vector<UniversalSegmentHit> universal_segment_hits;
    for (const auto& [semantic_key, samples] : universal_segment_samples) {
        static_cast<void>(semantic_key);
        const auto& [sum_x, sum_y, count, direction] = samples;
        if (count != 0) {
            universal_segment_hits.push_back({
                {sum_x / static_cast<double>(count),
                 sum_y / static_cast<double>(count)}, direction});
        }
    }
    std::optional<std::array<QPointF, 2>> universal_angle_segments;
    for (std::size_t first = 0; first < universal_segment_hits.size() &&
            !universal_angle_segments; ++first) {
        for (std::size_t second = first + 1;
             second < universal_segment_hits.size(); ++second) {
            const auto& a = universal_segment_hits[first].direction;
            const auto& b = universal_segment_hits[second].direction;
            const zima::kernel::Vec3 cross{
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
            const double cross_length = std::sqrt(
                cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
            const double scale = std::sqrt(
                (a.x * a.x + a.y * a.y + a.z * a.z) *
                (b.x * b.x + b.y * b.y + b.z * b.z));
            if (scale > 1.0e-12 && cross_length / scale > 1.0e-3) {
                universal_angle_segments = std::array{
                    universal_segment_hits[first].screen,
                    universal_segment_hits[second].screen};
                break;
            }
        }
    }
    if (!verify(universal_angle_segments.has_value(),
                "Universal Dimension test found no two non-parallel Sketch segments")) {
        return 1;
    }
    dimension_group = nullptr;
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        if (tree->topLevelItem(index)->text(0) == QStringLiteral("Kóty")) {
            dimension_group = tree->topLevelItem(index);
            break;
        }
    }
    const int dimensions_before_segment_angle =
        dimension_group == nullptr ? 0 : dimension_group->childCount();
    for (const auto& segment_position : *universal_angle_segments) {
        sketch_click_at(segment_position);
        application.processEvents();
    }
    dimension_group = nullptr;
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        if (tree->topLevelItem(index)->text(0) == QStringLiteral("Kóty")) {
            dimension_group = tree->topLevelItem(index);
            break;
        }
    }
    if (!verify(dimension_group != nullptr &&
                    dimension_group->childCount() ==
                        dimensions_before_segment_angle &&
                    workspace_state->text().contains(
                        QStringLiteral("umístění úhlové kóty")),
                "Universal Dimension did not enter angular placement after two segments")) {
        return 1;
    }
    QApplication::sendEvent(&window, &cancel_universal_angle);
    application.processEvents();
    QApplication::sendEvent(&window, &cancel_universal_angle);
    application.processEvents();
    if (!part_capture_path.isEmpty()) {
        if (!verify(window.grab().save(
                        part_capture_path + QStringLiteral(".sketch.png")),
                    "Sketcher window capture failed") ||
            !verify(model_viewer->grabFramebuffer().save(
                        part_capture_path + QStringLiteral(".sketch-viewer.png")),
                    "Sketcher framebuffer capture failed")) {
            return 1;
        }
    }
    // The following selection-clear checks intentionally use a point.
    // Re-resolve it because every committed Sketch command rebuilds the Tree.
    first_sketch_geometry = find_sketch_tree_item(QStringLiteral("sketch-geometry"));
    tree->setCurrentItem(first_sketch_geometry);
    first_sketch_geometry->setSelected(true);
    application.processEvents();
    if (!verify(sketch_dimension->isEnabled() && sketch_fix_point->isEnabled(),
                "Tree geometry confirmation did not enable its Sketch commands")) {
        return 1;
    }
    tree->clearSelection();
    tree->setCurrentItem(nullptr);
    application.processEvents();
    if (!verify(sketch_dimension->isEnabled() && !sketch_fix_point->isEnabled(),
                "clearing Tree selection retained a stale Sketch geometry latch")) {
        return 1;
    }
    sketch_dimension->trigger();
    application.processEvents();
    if (!verify(workspace_state->text().contains(QStringLiteral("první bod")),
                "Sketch dimension cannot start without preselected geometry")) {
        return 1;
    }
    QKeyEvent cancel_dimension(QEvent::KeyPress, Qt::Key_Escape,
        Qt::NoModifier);
    QApplication::sendEvent(&window, &cancel_dimension);
    application.processEvents();
    finish_sketch->trigger();
    application.processEvents();
    if (!verify(!tools_toolbar->actions().contains(finish_sketch) &&
                    !sketch_segment->isEnabled() && extrusion->isEnabled(),
                "finishing Sketch must leave editing and return to its container")) {
        return 1;
    }
    properties = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    buttons = properties == nullptr
        ? nullptr : properties->findChild<QDialogButtonBox*>();
    if (!verify(buttons != nullptr,
                "finishing Sketch did not reopen its Container Properties")) {
        return 1;
    }
    buttons->button(QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    extrusion->trigger();
    application.processEvents();
    properties = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    buttons = properties == nullptr
        ? nullptr : properties->findChild<QDialogButtonBox*>();
    if (!verify(buttons != nullptr,
                "profile Sketch must open Extrusion Properties")) {
        return 1;
    }
    auto* own_sketch = properties->findChild<QPushButton*>();
    while (own_sketch != nullptr && own_sketch->text() != QStringLiteral("SKETCH")) {
        const auto buttons_in_dialog = properties->findChildren<QPushButton*>();
        own_sketch = nullptr;
        for (auto* candidate : buttons_in_dialog) {
            if (candidate->text() == QStringLiteral("SKETCH")) {
                own_sketch = candidate;
                break;
            }
        }
    }
    if (!verify(own_sketch != nullptr,
                "Extrusion Properties has no owned Sketch entry")) return 1;
    own_sketch->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    if (!verify(tools_toolbar->actions().contains(finish_sketch) &&
                    sketch_rectangle->isEnabled(),
                "owned Extrusion Sketch did not enter Sketcher")) return 1;
    sketch_rectangle->trigger();
    application.processEvents();
    sketch_click(0.44, 0.44);
    sketch_click(0.58, 0.58);
    bool owned_profile_rectangle_created = false;
    {
        QTreeWidgetItemIterator item(tree);
        while (*item != nullptr) {
            if ((*item)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("sketch-geometry") &&
                (*item)->text(0).startsWith(QStringLiteral("Úsečka"))) {
                owned_profile_rectangle_created = true;
                break;
            }
            ++item;
        }
    }
    if (!verify(owned_profile_rectangle_created,
                "Owned profile Rectangle did not create persisted geometry")) {
        return 1;
    }
    finish_sketch->trigger();
    application.processEvents();
    properties = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    buttons = properties == nullptr
        ? nullptr : properties->findChild<QDialogButtonBox*>();
    if (!verify(buttons != nullptr,
                "finishing owned Sketch did not return to Extrusion Properties")) {
        return 1;
    }
    const auto preceding_box_visible = [&] {
        return std::ranges::any_of(selection_viewer->mesh().triangle_references,[&](const auto& reference) {
            return reference.owner_id==original_box_id;
        });
    };
    if (!verify(preceding_box_visible(),"Pending extrusion hid the preceding calculated solid")) return 1;
    auto* pending_height=properties->findChild<QDoubleSpinBox*>("extrusionHeight");
    if (!verify(pending_height!=nullptr,"Pending extrusion has no length input")) return 1;
    pending_height->setValue(pending_height->value()+2);application.processEvents();
    if (!verify(preceding_box_visible(),"Changing extrusion length discarded its input solid")) return 1;
    buttons->button(QDialogButtonBox::Ok)->click();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    bool extrusion_in_tree = false;
    if (tree->topLevelItemCount() == 1) {
        const auto* root = part_history_root();
        for (int index = 0; index < root->childCount(); ++index) {
            const auto* child = root->child(index);
            if (child->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-container") &&
                child->text(0).endsWith(QStringLiteral("Vytažení"))) {
                extrusion_in_tree = true;
                break;
            }
        }
    }
    if (!verify(extrusion_in_tree,
                "Sketch rectangle must produce a committed Extrusion history item")) {
        return 1;
    }
    // Offering preceding container references must not invent an axis for
    // a rectangular extrusion. Real profile axes come from persisted geometry.
    std::string rectangular_extrusion_id;
    for (QTreeWidgetItemIterator it(tree);*it;++it)
        if ((*it)->data(0,Qt::UserRole+3).toString()=="part-container" &&
                (*it)->text(0).endsWith(QStringLiteral("Vytažení")))
            rectangular_extrusion_id=(*it)->data(0,Qt::UserRole).toString().toStdString();
    box->trigger();application.processEvents();
    auto* axis_check_dialog=window.findChild<QDialog*>("zimaPropertiesSubWindow");
    if (!verify(axis_check_dialog && !rectangular_extrusion_id.empty() &&
            std::ranges::none_of(selection_viewer->mesh().axes,[&](const auto& axis) {
                return axis.reference.owner_id==rectangular_extrusion_id;
            }),"Rectangular extrusion offered a synthetic extrusion axis")) return 1;
    axis_check_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();
    auto open_extrusion_properties = [&]() {
        auto* root = part_history_root();
        QTreeWidgetItem* item{};
        if (root != nullptr) {
            for (int index = 0; index < root->childCount(); ++index) {
                auto* candidate = root->child(index);
                if (candidate->data(0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("part-container") &&
                    candidate->text(0).endsWith(QStringLiteral("Vytažení"))) {
                    item = candidate;
                    break;
                }
            }
        }
        if (item != nullptr) window.show_tree_item_properties(item);
        application.processEvents();
        auto* dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
        return std::pair{dialog, dialog == nullptr
            ? nullptr : dialog->findChild<QDoubleSpinBox*>("extrusionHeight")};
    };
    auto [edit_dialog, edit_height] = open_extrusion_properties();
    if (!verify(edit_dialog != nullptr && edit_height != nullptr,
                "existing Extrusion did not reopen with its persisted value")) {
        return 1;
    }
    auto* up_to_combo = edit_dialog->findChild<QComboBox*>(
        "extrusionForwardEndCondition");
    auto* up_to_field = edit_dialog->findChild<QLineEdit*>(
        "extrusionForwardEndTarget");
    auto* up_to_viewer = dynamic_cast<zima::viewer::MeshView*>(
        window.findChild<QOpenGLWidget*>("modelWorkspace"));
    if (!verify(up_to_combo != nullptr && up_to_field != nullptr &&
                    up_to_viewer != nullptr && view_selection != nullptr,
                "Extrusion Up-to integration controls are missing")) {
        return 1;
    }
    const bool selection_was_checked = view_selection->isChecked();
    view_selection->setChecked(false);
    application.processEvents();
    up_to_combo->setCurrentIndex(up_to_combo->findData("up_to"));
    application.processEvents();
    std::optional<QPointF> up_to_face_position;
    for (int y = 4; y < up_to_viewer->height() && !up_to_face_position; y += 4) {
        for (int x = 4; x < up_to_viewer->width(); x += 4) {
            const QPointF position{
                static_cast<qreal>(x), static_cast<qreal>(y)};
            const auto candidates = up_to_viewer->selection_candidates_at(
                position);
            if (!candidates.empty() &&
                candidates.front().kind == zima::viewer::CandidateKind::Face &&
                zima::app::placement_reference_candidate_has_stable_geometry(
                    candidates.front())) {
                up_to_face_position = position;
                break;
            }
        }
    }
    if (!verify(up_to_field->styleSheet().contains("#42d66b"),
                "Selecting Up-to did not activate its green target field")) {
        return 1;
    }
    if (!verify(up_to_face_position.has_value(),
                "Selecting Up-to lost Face hover after the live preview refresh")) {
        return 1;
    }
    QMouseEvent up_to_hover(QEvent::MouseMove, *up_to_face_position,
        up_to_viewer->mapToGlobal(up_to_face_position->toPoint()),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(up_to_viewer, &up_to_hover);
    application.processEvents();
    QMouseEvent up_to_press(QEvent::MouseButtonPress, *up_to_face_position,
        up_to_viewer->mapToGlobal(up_to_face_position->toPoint()),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(up_to_viewer, &up_to_press);
    application.processEvents();
    if (!verify(!up_to_field->text().isEmpty() &&
                    !up_to_field->styleSheet().contains("#42d66b"),
                "LMB accepted the Up-to hover candidate but did not transfer "
                "its persisted Face reference into the target field")) {
        return 1;
    }
    up_to_combo->setCurrentIndex(up_to_combo->findData("length"));
    view_selection->setChecked(selection_was_checked);
    application.processEvents();
    const double original_extrusion_height = edit_height->value();
    edit_height->setValue(37.0);
    edit_dialog->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    std::tie(edit_dialog, edit_height) = open_extrusion_properties();
    if (!verify(edit_height != nullptr &&
                    edit_height->value() == original_extrusion_height,
                "Cancel changed the persisted Extrusion value")) {
        return 1;
    }
    edit_dialog->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Ok)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    std::tie(edit_dialog, edit_height) = open_extrusion_properties();
    edit_height->setValue(18.0);
    edit_dialog->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Ok)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    undo->trigger();
    application.processEvents();
    std::tie(edit_dialog, edit_height) = open_extrusion_properties();
    int extrusion_undo_steps = 1;
    // An edit session may add more than one legitimate history transaction
    // around its rollback boundary. Walk back until the previously persisted
    // feature value is reached, then replay exactly the same count below.
    while (edit_height != nullptr &&
           edit_height->value() != original_extrusion_height &&
           extrusion_undo_steps < 2) {
        edit_dialog->findChild<QDialogButtonBox*>()
            ->button(QDialogButtonBox::Cancel)->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        undo->trigger();
        application.processEvents();
        ++extrusion_undo_steps;
        std::tie(edit_dialog, edit_height) = open_extrusion_properties();
    }
    if (!verify(edit_height != nullptr &&
                    edit_height->value() == original_extrusion_height,
                "Undo did not restore the previous Extrusion value")) {
        return 1;
    }
    edit_dialog->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    for (int step = 0; step < extrusion_undo_steps; ++step) {
        redo->trigger();
        application.processEvents();
    }
    std::tie(edit_dialog, edit_height) = open_extrusion_properties();
    if (!verify(edit_height != nullptr && edit_height->value() == 18.0,
                "Redo did not restore the edited Extrusion value")) {
        return 1;
    }
    {
        // A middle-button double-click over the owning document view must
        // commit the open Properties dialog exactly like clicking OK,
        // whether or not the pointer is over the dialog itself.
        auto* model_workspace = window.findChild<QOpenGLWidget*>("modelWorkspace");
        if (!verify(model_workspace != nullptr,
                    "the real workspace has no modelWorkspace view")) {
            return 1;
        }
        const QPoint middle = model_workspace->rect().center();
        QMouseEvent middle_double_click(
            QEvent::MouseButtonDblClick, middle, middle,
            model_workspace->mapToGlobal(middle),
            Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(model_workspace, &middle_double_click);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        if (!verify(window.findChild<QDialog*>("zimaPropertiesSubWindow") == nullptr,
                    "middle-button double-click over the view did not commit "
                    "and close the open Extrusion Properties dialog")) {
            return 1;
        }
        std::tie(edit_dialog, edit_height) = open_extrusion_properties();
        if (!verify(edit_height != nullptr && edit_height->value() == 18.0,
                    "middle-button double-click OK changed the committed "
                    "Extrusion value")) {
            return 1;
        }
    }
    edit_dialog->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    box->trigger();
    application.processEvents();
    properties = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    buttons = properties == nullptr
        ? nullptr : properties->findChild<QDialogButtonBox*>();
    if (!verify(buttons != nullptr,
                "downstream Box did not open its shared Properties window")) {
        return 1;
    }
    buttons->button(QDialogButtonBox::Ok)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    std::tie(edit_dialog, edit_height) = open_extrusion_properties();
    auto* rollback_root = part_history_root();
    QTreeWidgetItem* rollback_extrusion{};
    QTreeWidgetItem* downstream_box{};
    if (rollback_root != nullptr) {
        for (int index = 0; index < rollback_root->childCount(); ++index) {
            auto* item = rollback_root->child(index);
            if (item->text(0).endsWith(QStringLiteral("Vytažení"))) {
                rollback_extrusion = item;
            } else if (rollback_extrusion != nullptr &&
                       item->data(0, Qt::UserRole + 3).toString() ==
                           QStringLiteral("part-container")) {
                downstream_box = item;
                break;
            }
        }
    }
    if (!verify(edit_dialog != nullptr && rollback_extrusion != nullptr &&
                    downstream_box != nullptr &&
                    rollback_extrusion->foreground(0).color() ==
                        QColor(70, 190, 95) &&
                    downstream_box->foreground(0).color() ==
                        QColor(125, 125, 125),
                "history edit did not keep the active item green and suppress downstream items")) {
        return 1;
    }
    edit_dialog->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    int saved_history_items = 0;
    if (const auto* root = part_history_root(); root != nullptr) {
        for (int index = 0; index < root->childCount(); ++index) {
            if (root->child(index)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-container")) ++saved_history_items;
        }
    }
    const auto saved_part_path = test_directory /
        (part_name.toStdString() + ".prtz");
    save->trigger();
    application.processEvents();
    if (!verify(std::filesystem::exists(saved_part_path) &&
                    file_progress->isVisible() &&
                    file_progress->format().contains(QStringLiteral("uložen"),
                        Qt::CaseInsensitive),
                "Save did not expose its completed status progress") ||
        !verify(std::filesystem::exists(saved_part_path),
                "Save did not persist the edited Part")) {
        return 1;
    }
    try {
        const auto template_part = zima::document::PartDocument::load(
            std::filesystem::current_path() /
            "config/templates/start_part.prtz");
        const auto created_part =
            zima::document::PartDocument::load(saved_part_path);
        if (!verify(created_part.document_id != template_part.document_id &&
                        created_part.physical_parameters.at("MATERIAL_NAME") ==
                            "S235JR" &&
                        created_part.physical_parameters.contains(
                            "YOUNG_MODULUS"),
                    "new Part did not clone the start template with a unique ID")) {
            return 1;
        }
    } catch (const std::exception&) {
        verify(false, "new Part or its start template could not be reopened");
        return 1;
    }
    close->trigger();
    application.processEvents();
    const bool reopened = tabs->count() == 0 && window.open_document_path(
        QString::fromStdString(saved_part_path.string()));
    int reopened_history_items = 0;
    if (reopened && tree->topLevelItemCount() == 1) {
        const auto* root = part_history_root();
        for (int index = 0; index < root->childCount(); ++index) {
            if (root->child(index)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-container")) ++reopened_history_items;
        }
    }
    if (!verify(reopened && file_progress->isVisible() &&
                    file_progress->format().startsWith(
                        QStringLiteral("Otevřeno")),
                "Open did not expose its completed status progress") ||
        !verify(reopened && tabs->count() == 1 && saved_history_items > 0 &&
                    reopened_history_items == saved_history_items,
                "saved Part did not close and reopen through the application")) {
        return 1;
    }
    const bool reopened_existing = window.open_document_path(
        QString::fromStdString(saved_part_path.string()));
    if (!verify(reopened_existing && tabs->count() == 1,
                "opening an already open path created a duplicate document tab")) {
        return 1;
    }
    std::tie(edit_dialog, edit_height) = open_extrusion_properties();
    if (!verify(edit_height != nullptr && edit_height->value() == 18.0,
                "reopened Part lost the edited Extrusion value")) {
        return 1;
    }
    edit_dialog->findChild<QDialogButtonBox*>()
        ->button(QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    std::filesystem::remove(saved_part_path);

    if (!create_document(QStringLiteral("assembly"), assembly_name)) {
        return 1;
    }
    auto* insert = window.findChild<QAction*>("insertComponentAction");
    auto* insert_menu = window.findChild<QMenu*>("insertComponentMenu");
    if (!verify(tabs->count() == 2 &&
                    tabs->tabText(tabs->currentIndex()) ==
                        assembly_name + QStringLiteral(".asmz"),
                "New Assembly must become a visible second document") ||
        !verify(insert != nullptr && insert->isEnabled() && insert_menu != nullptr,
                "calculated open Part must be insertable into the Assembly") ||
        !verify(construction_point != nullptr && construction_point->isEnabled() &&
                    construction_axis != nullptr && construction_axis->isEnabled() &&
                    construction_plane != nullptr && construction_plane->isEnabled() &&
                    tools_toolbar->actions().contains(construction_point) &&
                    tools_toolbar->actions().contains(construction_axis) &&
                    tools_toolbar->actions().contains(construction_plane),
                "Assembly toolbar is missing its Point/Axis/Plane commands")) {
        return 1;
    }
    if (!verify(sketch->isEnabled(),
                "Assembly must expose the shared Sketch command")) return 1;
    sketch->trigger();
    application.processEvents();
    QDialog* assembly_sketch_dialog{};
    for (auto* candidate : window.findChildren<QDialog*>()) {
        if (candidate->findChild<QLineEdit*>("sketchName") != nullptr) {
            assembly_sketch_dialog = candidate;
            break;
        }
    }
    auto* assembly_open_sketch = assembly_sketch_dialog == nullptr
        ? nullptr
        : assembly_sketch_dialog->findChild<QPushButton*>("sketchOpenButton");
    if (!verify(assembly_sketch_dialog != nullptr &&
                    assembly_sketch_dialog->windowFlags().testFlag(Qt::SubWindow) &&
                    assembly_open_sketch != nullptr,
                "Assembly Sketch must use the shared internal Sketch dialog")) {
        return 1;
    }
    assembly_open_sketch->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    sketch_rectangle->trigger();
    application.processEvents();
    sketch_click(0.44, 0.44);
    sketch_click(0.58, 0.58);
    bool assembly_rectangle_created = false;
    {
        QTreeWidgetItemIterator item(tree);
        while (*item != nullptr) {
            if ((*item)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("sketch-geometry") &&
                (*item)->text(0).startsWith(QStringLiteral("Úsečka"))) {
                assembly_rectangle_created = true;
                break;
            }
            ++item;
        }
    }
    if (!verify(assembly_rectangle_created,
                "Assembly Sketch Rectangle did not create persisted geometry")) {
        return 1;
    }
    finish_sketch->trigger();
    application.processEvents();
    bool assembly_sketch_in_tree = false;
    if (tree->topLevelItemCount() == 1) {
        const auto* root = tree->topLevelItem(0);
        for (int index = 0; index < root->childCount(); ++index) {
            if (root->child(index)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("assembly-sketch")) {
                assembly_sketch_in_tree = true;
                break;
            }
        }
    }
    if (!verify(assembly_sketch_in_tree,
                "confirming Assembly Sketch must create an Assembly-owned sketch")) {
        return 1;
    }
    QAction* source_action{};
    for (auto* action : insert_menu->actions()) {
        if (action->objectName() == QStringLiteral("insertSourceAction") &&
            action->isEnabled()) {
            source_action = action;
            break;
        }
    }
    if (!verify(source_action != nullptr, "Assembly insertion has no Part source")) {
        return 1;
    }
    source_action->trigger();
    application.processEvents();
    bool inserted_occurrence_in_tree = false;
    if (tree->topLevelItemCount() == 1) {
        const auto* root = tree->topLevelItem(0);
        for (int index = 0; index < root->childCount(); ++index) {
            if (root->child(index)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-occurrence")) {
                inserted_occurrence_in_tree = true;
                break;
            }
        }
    }
    if (!verify(inserted_occurrence_in_tree,
                "inserting the open Part must create an Assembly occurrence")) {
        return 1;
    }
    auto* inserted_component_dialog =
        window.findChild<QDialog*>("zimaPropertiesSubWindow");
    auto* inserted_component_buttons = inserted_component_dialog == nullptr
        ? nullptr : inserted_component_dialog->findChild<QDialogButtonBox*>();
    if (!verify(inserted_component_buttons != nullptr,
                "inserting a component must open its internal Properties dialog")) {
        return 1;
    }
    inserted_component_buttons->button(QDialogButtonBox::Ok)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    QTreeWidgetItem* assembly_profile_sketch{};
    if (tree->topLevelItemCount() == 1) {
        auto* root = tree->topLevelItem(0);
        for (int index = 0; index < root->childCount(); ++index) {
            if (root->child(index)->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("assembly-sketch")) {
                assembly_profile_sketch = root->child(index);
                break;
            }
        }
    }
    if (!verify(assembly_profile_sketch != nullptr,
                "Assembly profile Sketch disappeared after component insertion")) {
        return 1;
    }
    tree->setCurrentItem(assembly_profile_sketch);
    assembly_profile_sketch->setSelected(true);
    application.processEvents();
    if (!verify(extrusion->isEnabled(),
                "Assembly Sketch must enable the shared Extrusion command")) {
        return 1;
    }
    extrusion->trigger();
    application.processEvents();
    auto* assembly_cut_dialog = window.findChild<QDialog*>(
        "zimaPropertiesSubWindow");
    auto* assembly_cut_targets = assembly_cut_dialog == nullptr
        ? nullptr : assembly_cut_dialog->findChild<QListWidget*>(
            "assemblyCutTargets");
    auto* assembly_cut_buttons = assembly_cut_dialog == nullptr
        ? nullptr : assembly_cut_dialog->findChild<QDialogButtonBox*>();
    if (!verify(assembly_cut_targets != nullptr &&
                    assembly_cut_targets->count() == 1 &&
                    assembly_cut_targets->item(0)->checkState() == Qt::Checked &&
                    assembly_cut_buttons != nullptr,
                "Assembly Extrusion must use the shared dialog with one exact target")) {
        return 1;
    }
    assembly_cut_buttons->button(QDialogButtonBox::Ok)->click();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    bool cut_in_tree = false;
    for (int index = 0; index < tree->topLevelItem(0)->childCount(); ++index) {
        if (tree->topLevelItem(0)->child(index)->data(
                0, Qt::UserRole + 3).toString() == QStringLiteral("assembly-cut")) {
            cut_in_tree = true;
            break;
        }
    }
    if (!verify(cut_in_tree,
                "Assembly Extrusion OK must calculate and create one cut")) {
        return 1;
    }
    QTreeWidgetItem* assembly_cut_item{};
    for (int index = 0; index < tree->topLevelItem(0)->childCount(); ++index) {
        auto* candidate = tree->topLevelItem(0)->child(index);
        if (candidate->data(0, Qt::UserRole + 3).toString() ==
                QStringLiteral("assembly-cut")) {
            assembly_cut_item = candidate;
            break;
        }
    }
    if (!verify(assembly_cut_item != nullptr,
                "Calculated Assembly cut is missing from the tree")) return 1;
    window.show_tree_item_properties(assembly_cut_item);
    application.processEvents();
    auto* rollback_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    QTreeWidgetItem* rollback_cut_item{};
    for (int index = 0; index < tree->topLevelItem(0)->childCount(); ++index) {
        auto* candidate = tree->topLevelItem(0)->child(index);
        if (candidate->data(0, Qt::UserRole + 3).toString() ==
                QStringLiteral("assembly-cut")) {
            rollback_cut_item = candidate;
            break;
        }
    }
    if (!verify(rollback_dialog != nullptr &&
                    rollback_dialog->findChild<QListWidget*>(
                        "assemblyCutTargets") != nullptr &&
                    rollback_cut_item != nullptr &&
                    rollback_cut_item->foreground(0).color() == QColor(70, 190, 95),
                "Assembly cut Properties did not enter persisted rollback")) {
        return 1;
    }
    rollback_dialog->findChild<QDialogButtonBox*>()->button(
        QDialogButtonBox::Cancel)->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();

    {
        // Nested Assembly occurrence activation must be reachable through the
        // real window (context-menu "Aktivovat komponentu"/"Aktivovat
        // podsestavu"), not only through the underlying Workspace unit
        // tests. Exercise the same public entry point the context menu uses.
        QTreeWidgetItem* occurrence_item{};
        for (int index = 0; index < tree->topLevelItem(0)->childCount(); ++index) {
            auto* candidate = tree->topLevelItem(0)->child(index);
            if (candidate->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-occurrence")) {
                occurrence_item = candidate;
                break;
            }
        }
        if (!verify(occurrence_item != nullptr,
                    "Assembly tree lost its inserted Part occurrence")) {
            return 1;
        }
        const std::string instance_path =
            occurrence_item->data(0, Qt::UserRole + 1).toString().toStdString();
        if (!verify(!instance_path.empty(),
                    "Assembly occurrence tree item has no instance path")) {
            return 1;
        }
        if (!verify(window.activate_occurrence_for_test(instance_path),
                    "activating an Assembly occurrence through the real window failed")) {
            return 1;
        }
        if (!verify(window.active_occurrence_path_for_test() == instance_path,
                    "activation did not record the exact activated instance path")) {
            return 1;
        }
        bool has_return_to_assembly = false;
        for (auto* action : window.findChildren<QAction*>()) {
            if (action->text() == QStringLiteral("Zpět do sestavy")) {
                has_return_to_assembly = true;
                break;
            }
        }
        if (!verify(has_return_to_assembly,
                    "activated occurrence did not expose the Zpět do sestavy toolbar action")) {
            return 1;
        }
        window.deactivate_active_occurrence_for_test();
        if (!verify(window.active_occurrence_path_for_test().empty(),
                    "returning to the Assembly did not clear the active occurrence path")) {
            return 1;
        }
    }

    {
        // Multi-level (nested-within-nested) occurrence activation must also
        // be reachable through the real window: create a second, outer
        // Assembly, insert the already-populated Assembly above as one of
        // its own components (a subassembly occurrence), then activate the
        // leaf Part occurrence two levels deep through the exact same
        // public entry point the context menu uses.
        if (!create_document(QStringLiteral("assembly"), nested_assembly_name)) {
            return 1;
        }
        if (!verify(tabs->count() == 3 &&
                        tabs->tabText(tabs->currentIndex()) ==
                            nested_assembly_name + QStringLiteral(".asmz"),
                    "New outer Assembly must become a visible third document")) {
            return 1;
        }
        QAction* subassembly_source_action{};
        for (auto* action : insert_menu->actions()) {
            if (action->objectName() == QStringLiteral("insertSourceAction") &&
                action->isEnabled() &&
                action->text().startsWith(assembly_name)) {
                subassembly_source_action = action;
                break;
            }
        }
        if (!verify(subassembly_source_action != nullptr,
                    "outer Assembly insertion has no inner Assembly source")) {
            return 1;
        }
        subassembly_source_action->trigger();
        application.processEvents();
        QTreeWidgetItem* subassembly_item{};
        if (tree->topLevelItemCount() == 1) {
            const auto* root = tree->topLevelItem(0);
            for (int index = 0; index < root->childCount(); ++index) {
                auto* candidate = root->child(index);
                if (candidate->data(0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("assembly-occurrence")) {
                    subassembly_item = candidate;
                    break;
                }
            }
        }
        if (!verify(subassembly_item != nullptr,
                    "inserting the inner Assembly must create a subassembly occurrence")) {
            return 1;
        }
        subassembly_item->setExpanded(true);
        application.processEvents();
        QTreeWidgetItem* nested_leaf_item{};
        for (int index = 0; index < subassembly_item->childCount(); ++index) {
            auto* candidate = subassembly_item->child(index);
            if (candidate->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-occurrence")) {
                nested_leaf_item = candidate;
                break;
            }
        }
        if (!verify(nested_leaf_item != nullptr,
                    "outer Assembly did not expose the inner Assembly's leaf Part occurrence")) {
            return 1;
        }
        const std::string nested_instance_path =
            nested_leaf_item->data(0, Qt::UserRole + 1).toString().toStdString();
        if (!verify(!nested_instance_path.empty() &&
                        nested_instance_path.find(':') != std::string::npos,
                    "multi-level occurrence tree item has no composed instance path")) {
            return 1;
        }
        if (!verify(window.activate_occurrence_for_test(nested_instance_path),
                    "activating a two-level-deep Assembly occurrence through the real window failed")) {
            return 1;
        }
        if (!verify(window.active_occurrence_path_for_test() == nested_instance_path,
                    "multi-level activation did not record the exact composed instance path")) {
            return 1;
        }
        bool has_nested_return_to_assembly = false;
        for (auto* action : window.findChildren<QAction*>()) {
            if (action->text() == QStringLiteral("Zpět do sestavy")) {
                has_nested_return_to_assembly = true;
                break;
            }
        }
        if (!verify(has_nested_return_to_assembly,
                    "activated two-level-deep occurrence did not expose the Zpět do sestavy toolbar action")) {
            return 1;
        }
        window.deactivate_active_occurrence_for_test();
        if (!verify(window.active_occurrence_path_for_test().empty(),
                    "returning from a two-level-deep occurrence did not clear the active occurrence path")) {
            return 1;
        }

        // Save/close/reopen must preserve the outer Assembly's nested
        // subassembly structure through the real window, matching the
        // existing Part save/reopen coverage above.
        const auto saved_nested_assembly_path = test_directory /
            (nested_assembly_name.toStdString() + ".asmz");
        save->trigger();
        application.processEvents();
        if (!verify(std::filesystem::exists(saved_nested_assembly_path),
                    "Save did not persist the outer Assembly")) {
            return 1;
        }
        try {
            const auto template_assembly =
                zima::assembly::AssemblyDocument::load(
                    std::filesystem::current_path() /
                    "config/templates/start_assembly.asmz");
            const auto created_assembly =
                zima::assembly::AssemblyDocument::load(
                    saved_nested_assembly_path);
            if (!verify(created_assembly.document_id !=
                            template_assembly.document_id &&
                            created_assembly.user_parameters.contains("mass") &&
                            created_assembly.relations.size() == 1 &&
                            created_assembly.relations.front().target == "mass",
                        "new Assembly did not clone the start template with a unique ID")) {
                return 1;
            }
        } catch (const std::exception&) {
            verify(false,
                "new Assembly or its start template could not be reopened");
            return 1;
        }
        close->trigger();
        application.processEvents();
        const bool reopened_nested_assembly = window.open_document_path(
            QString::fromStdString(saved_nested_assembly_path.string()));
        bool reopened_subassembly_in_tree = false;
        bool reopened_nested_leaf_in_tree = false;
        if (reopened_nested_assembly && tree->topLevelItemCount() == 1) {
            const auto* root = tree->topLevelItem(0);
            for (int index = 0; index < root->childCount(); ++index) {
                auto* candidate = root->child(index);
                if (candidate->data(0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("assembly-occurrence")) {
                    reopened_subassembly_in_tree = true;
                    candidate->setExpanded(true);
                    application.processEvents();
                    for (int nested_index = 0;
                         nested_index < candidate->childCount(); ++nested_index) {
                        if (candidate->child(nested_index)->data(
                                0, Qt::UserRole + 3).toString() ==
                                QStringLiteral("part-occurrence")) {
                            reopened_nested_leaf_in_tree = true;
                            break;
                        }
                    }
                    break;
                }
            }
        }
        if (!verify(reopened_nested_assembly && reopened_subassembly_in_tree &&
                        reopened_nested_leaf_in_tree,
                    "saved outer Assembly did not close and reopen its nested subassembly structure")) {
            return 1;
        }
    }

    {
        // Insert two more occurrences of the same Part into the inner Assembly
        // to provide a repeated-occurrence fixture for the free-drag coverage
        // below (uses any single occurrence) and the BOM seed/regeneration
        // tests later (expects 3 occurrences at insertion time, 4 after
        // regeneration). This covers the GUI insertion flow for repeated
        // occurrences (same Part inserted multiple times).
        int inner_assembly_tab_index = -1;
        for (int index = 0; index < tabs->count(); ++index) {
            if (tabs->tabText(index).startsWith(
                    assembly_name + QStringLiteral(".asmz"))) {
                inner_assembly_tab_index = index;
                break;
            }
        }
        if (!verify(inner_assembly_tab_index >= 0,
                    "inner Assembly tab is no longer open for repeated-occurrence insertion")) {
            return 1;
        }
        tabs->setCurrentIndex(inner_assembly_tab_index);
        application.processEvents();
        auto* insertion_tree = window.findChild<QTreeWidget*>("documentTree");
        if (!verify(insertion_tree != nullptr && insertion_tree->topLevelItemCount() == 1,
                    "reopened inner Assembly is missing its tree root")) {
            return 1;
        }
        int part_occurrence_count_before = 0;
        for (int index = 0; index < insertion_tree->topLevelItem(0)->childCount(); ++index) {
            auto* candidate_child = insertion_tree->topLevelItem(0)->child(index);
            if (candidate_child->data(0, Qt::UserRole + 3).toString() ==
                    QStringLiteral("part-occurrence")) {
                ++part_occurrence_count_before;
            }
        }
        if (!verify(part_occurrence_count_before == 1,
                    "reopened inner Assembly did not have exactly one Part occurrence")) {
            return 1;
        }
        auto* insert_menu = window.findChild<QMenu*>("insertComponentMenu");
        auto* insert_action = window.findChild<QAction*>("insertComponentAction");
        if (!verify(insert_action != nullptr && insert_action->isEnabled() &&
                        insert_menu != nullptr,
                    "reopened inner Assembly cannot insert a second Part occurrence")) {
            return 1;
        }
        QAction* second_source_action{};
        for (auto* action : insert_menu->actions()) {
            if (action->objectName() == QStringLiteral("insertSourceAction") &&
                action->isEnabled()) {
                second_source_action = action;
                break;
            }
        }
        if (!verify(second_source_action != nullptr,
                    "reopened inner Assembly has no Part source for a second occurrence")) {
            return 1;
        }
        second_source_action->trigger();
        application.processEvents();
        auto* second_component_dialog =
            window.findChild<QDialog*>("zimaPropertiesSubWindow");
        auto* second_component_buttons = second_component_dialog == nullptr
            ? nullptr : second_component_dialog->findChild<QDialogButtonBox*>();
        if (!verify(second_component_buttons != nullptr,
                    "second insertion did not open Component Properties")) {
            return 1;
        }
        const auto second_translation_fields =
            second_component_dialog->findChildren<QDoubleSpinBox*>(
                "componentTranslation");
        if (!verify(second_translation_fields.size() == 3,
                    "second insertion has no component translation fields")) {
            return 1;
        }
        second_translation_fields[0]->setValue(40.0);
        second_component_buttons->button(QDialogButtonBox::Ok)->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        int part_occurrence_count_after_first = 0;
        if (insertion_tree->topLevelItemCount() == 1) {
            const auto* root = insertion_tree->topLevelItem(0);
            for (int index = 0; index < root->childCount(); ++index) {
                auto* child = root->child(index);
                if (child->data(0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("part-occurrence")) {
                    ++part_occurrence_count_after_first;
                }
            }
        }
        if (!verify(part_occurrence_count_after_first == 2,
                    "inserting the same Part again did not create a second repeated occurrence")) {
            return 1;
        }
        // Insert a third occurrence to match the BOM seed expectation (the
        // Drawing view insertion test later expects quantity=3 from three
        // Part occurrences in the Assembly).
        QAction* third_source_action{};
        for (auto* action : insert_menu->actions()) {
            if (action->objectName() == QStringLiteral("insertSourceAction") &&
                action->isEnabled()) {
                third_source_action = action;
                break;
            }
        }
        if (!verify(third_source_action != nullptr,
                    "inner Assembly has no Part source for a third occurrence")) {
            return 1;
        }
        third_source_action->trigger();
        application.processEvents();
        auto* third_component_dialog =
            window.findChild<QDialog*>("zimaPropertiesSubWindow");
        auto* third_component_buttons = third_component_dialog == nullptr
            ? nullptr : third_component_dialog->findChild<QDialogButtonBox*>();
        if (!verify(third_component_buttons != nullptr,
                    "third insertion did not open Component Properties")) {
            return 1;
        }
        const auto third_translation_fields =
            third_component_dialog->findChildren<QDoubleSpinBox*>(
                "componentTranslation");
        if (!verify(third_translation_fields.size() == 3,
                    "third insertion has no component translation fields")) {
            return 1;
        }
        third_translation_fields[0]->setValue(-40.0);
        third_component_buttons->button(QDialogButtonBox::Ok)->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        int part_occurrence_count_after_second = 0;
        if (insertion_tree->topLevelItemCount() == 1) {
            const auto* root = insertion_tree->topLevelItem(0);
            for (int index = 0; index < root->childCount(); ++index) {
                auto* child = root->child(index);
                if (child->data(0, Qt::UserRole + 3).toString() ==
                        QStringLiteral("part-occurrence")) {
                    ++part_occurrence_count_after_second;
                }
            }
        }
        if (!verify(part_occurrence_count_after_second == 3,
                    "inserting the same Part a third time did not create a third occurrence")) {
            return 1;
        }
    }

    {
        // Free-component drag: while an occurrence's own Properties dialog
        // is open, dragging that occurrence in the viewer must translate it
        // and live-update the dialog's fields (mirrors Python's
        // `_on_insertion_origin_dragged`). Escape must cancel the gesture
        // without persisting any change; releasing the mouse must commit it.
        int drag_assembly_tab_index = -1;
        for (int index = 0; index < tabs->count(); ++index) {
            if (tabs->tabText(index).startsWith(
                    assembly_name + QStringLiteral(".asmz"))) {
                drag_assembly_tab_index = index;
                break;
            }
        }
        if (!verify(drag_assembly_tab_index >= 0,
                    "inner Assembly tab is no longer open for free-drag coverage")) {
            return 1;
        }
        tabs->setCurrentIndex(drag_assembly_tab_index);
        application.processEvents();
        auto* drag_viewer = dynamic_cast<zima::viewer::MeshView*>(
            window.findChild<QOpenGLWidget*>("modelWorkspace"));
        auto* drag_filter_combo = window.findChild<QComboBox*>("selectionFilterCombo");
        if (!verify(drag_viewer != nullptr && drag_filter_combo != nullptr,
                    "free-drag coverage requires the shared viewer and selection filter")) {
            return 1;
        }
        drag_filter_combo->setCurrentIndex(0);  // Vše (offers Occurrence candidates)
        drag_viewer->fit_all();
        application.processEvents();
        std::string drag_instance_path;
        for (int y = 4; y < drag_viewer->height() && drag_instance_path.empty(); y += 4) {
            for (int x = 4; x < drag_viewer->width(); x += 4) {
                const QPointF position{static_cast<qreal>(x), static_cast<qreal>(y)};
                const auto candidates = drag_viewer->selection_candidates_at(position);
                if (!candidates.empty() && candidates.front().kind ==
                        zima::viewer::CandidateKind::Occurrence) {
                    drag_instance_path = candidates.front().instance_path;
                    break;
                }
            }
        }
        if (!verify(!drag_instance_path.empty(),
                    "viewer did not offer a front-most occurrence for free-drag coverage")) {
            return 1;
        }
        // Each Properties confirm/cancel triggers a refresh_scene()/
        // refresh_tabs() that rebuilds the tree, invalidating any previously
        // located QTreeWidgetItem*, so re-locate the occurrence's own tree
        // item fresh every time.
        const auto find_drag_occurrence_item = [&]() -> QTreeWidgetItem* {
            auto* current_tree = window.findChild<QTreeWidget*>("documentTree");
            if (current_tree == nullptr || current_tree->topLevelItemCount() != 1) {
                return nullptr;
            }
            auto* root = current_tree->topLevelItem(0);
            for (int index = 0; index < root->childCount(); ++index) {
                auto* child = root->child(index);
                if (child->data(0, Qt::UserRole + 3).toString() !=
                        QStringLiteral("part-occurrence")) {
                    continue;
                }
                if (drag_instance_path.empty()) return child;
                if (child->data(0, Qt::UserRole + 1).toString().toStdString() ==
                        drag_instance_path) {
                    return child;
                }
            }
            return nullptr;
        };
        auto* drag_occurrence_item = find_drag_occurrence_item();
        if (!verify(drag_occurrence_item != nullptr,
                    "inner Assembly is missing a Part occurrence for free-drag coverage")) {
            return 1;
        }
        drag_instance_path =
            drag_occurrence_item->data(0, Qt::UserRole + 1).toString().toStdString();
        if (!verify(!drag_instance_path.empty(),
                    "free-drag coverage's occurrence tree item has no instance path")) {
            return 1;
        }
        window.show_tree_item_properties(drag_occurrence_item);
        application.processEvents();
        auto* drag_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
        const auto drag_translation_fields = drag_dialog == nullptr
            ? QList<QDoubleSpinBox*>{}
            : drag_dialog->findChildren<QDoubleSpinBox*>("componentTranslation");
        if (!verify(drag_dialog != nullptr && drag_translation_fields.size() == 3,
                    "free-drag coverage requires the real ComponentPropertiesDialog")) {
            return 1;
        }
        const std::array<double, 3> drag_start_translation{
            drag_translation_fields[0]->value(),
            drag_translation_fields[1]->value(),
            drag_translation_fields[2]->value()};
        const auto translation_changed = [&](const QList<QDoubleSpinBox*>& fields) {
            return fields.size() == 3 &&
                (fields[0]->value() != drag_start_translation[0] ||
                 fields[1]->value() != drag_start_translation[1] ||
                 fields[2]->value() != drag_start_translation[2]);
        };
        const auto translation_restored = [&](const QList<QDoubleSpinBox*>& fields) {
            return fields.size() == 3 &&
                fields[0]->value() == drag_start_translation[0] &&
                fields[1]->value() == drag_start_translation[1] &&
                fields[2]->value() == drag_start_translation[2];
        };
        const auto find_occurrence_position = [&]() -> std::optional<QPointF> {
            for (int y = 4; y < drag_viewer->height(); y += 4) {
                for (int x = 4; x < drag_viewer->width(); x += 4) {
                    const QPointF position{static_cast<qreal>(x), static_cast<qreal>(y)};
                    const auto candidates =
                        drag_viewer->selection_candidates_at(position);
                    // A click/drag consumes the active (front) member of the
                    // common ordered candidate list. Merely finding the
                    // occurrence deeper in an overlap does not mean this
                    // position can start its gesture without RMB cycling.
                    if (!candidates.empty() &&
                        candidates.front().kind ==
                            zima::viewer::CandidateKind::Occurrence &&
                        candidates.front().instance_path == drag_instance_path) {
                        return position;
                    }
                }
            }
            return std::nullopt;
        };
        const auto drag_occurrence_position = find_occurrence_position();
        if (!verify(drag_occurrence_position.has_value(),
                    "viewer did not offer the dialog's own occurrence as a draggable candidate")) {
            return 1;
        }
        QMouseEvent cancel_gesture_press(QEvent::MouseButtonPress, *drag_occurrence_position,
            drag_viewer->mapToGlobal(drag_occurrence_position->toPoint()), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(drag_viewer, &cancel_gesture_press);
        const QPointF cancel_gesture_target{
            drag_occurrence_position->x() +
                (drag_occurrence_position->x() + 30.0 < drag_viewer->width()
                    ? 30.0 : -30.0),
            drag_occurrence_position->y() +
                (drag_occurrence_position->y() + 18.0 < drag_viewer->height()
                    ? 18.0 : -18.0)};
        QMouseEvent cancel_gesture_move(QEvent::MouseMove, cancel_gesture_target,
            drag_viewer->mapToGlobal(cancel_gesture_target.toPoint()), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(drag_viewer, &cancel_gesture_move);
        application.processEvents();
        if (!translation_changed(drag_translation_fields)) {
            // In an orthographic view one screen direction can project to a
            // negligible world delta. Try an independent direction before
            // declaring the live gesture broken.
            const QPointF alternate_target{
                drag_occurrence_position->x(),
                drag_occurrence_position->y() +
                    (drag_occurrence_position->y() + 36.0 < drag_viewer->height()
                        ? 36.0 : -36.0)};
            QMouseEvent alternate_move(QEvent::MouseMove, alternate_target,
                drag_viewer->mapToGlobal(alternate_target.toPoint()), Qt::LeftButton,
                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(drag_viewer, &alternate_move);
            application.processEvents();
        }
        if (!verify(translation_changed(drag_translation_fields),
                    "dragging the occurrence did not live-update the open Properties dialog")) {
            return 1;
        }
        QKeyEvent cancel_gesture_escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&window, &cancel_gesture_escape);
        application.processEvents();
        if (auto* cancel_gesture_buttons = drag_dialog->findChild<QDialogButtonBox*>()) {
            cancel_gesture_buttons->button(QDialogButtonBox::Cancel)->click();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        auto* reopened_drag_occurrence_item = find_drag_occurrence_item();
        if (!verify(reopened_drag_occurrence_item != nullptr,
                    "occurrence tree item vanished after cancelling the free drag")) {
            return 1;
        }
        window.show_tree_item_properties(reopened_drag_occurrence_item);
        application.processEvents();
        auto* reopened_drag_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
        const auto reopened_translation_fields = reopened_drag_dialog == nullptr
            ? QList<QDoubleSpinBox*>{}
            : reopened_drag_dialog->findChildren<QDoubleSpinBox*>("componentTranslation");
        if (!verify(reopened_drag_dialog != nullptr &&
                        translation_restored(reopened_translation_fields),
                    "Escape during a free drag must not persist the occurrence's placement")) {
            return 1;
        }
        const auto committed_occurrence_position = find_occurrence_position();
        if (!verify(committed_occurrence_position.has_value(),
                    "cancelled drag did not restore the occurrence's draggable candidate")) {
            return 1;
        }
        QMouseEvent commit_gesture_press(QEvent::MouseButtonPress, *committed_occurrence_position,
            drag_viewer->mapToGlobal(committed_occurrence_position->toPoint()), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(drag_viewer, &commit_gesture_press);
        const QPointF commit_gesture_target{
            committed_occurrence_position->x() - 24.0,
            committed_occurrence_position->y() + 16.0};
        QMouseEvent commit_gesture_move(QEvent::MouseMove, commit_gesture_target,
            drag_viewer->mapToGlobal(commit_gesture_target.toPoint()), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(drag_viewer, &commit_gesture_move);
        application.processEvents();
        QMouseEvent commit_gesture_release(QEvent::MouseButtonRelease, commit_gesture_target,
            drag_viewer->mapToGlobal(commit_gesture_target.toPoint()), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(drag_viewer, &commit_gesture_release);
        application.processEvents();
        if (auto* reopened_drag_buttons =
                reopened_drag_dialog->findChild<QDialogButtonBox*>()) {
            reopened_drag_buttons->button(QDialogButtonBox::Cancel)->click();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        auto* committed_drag_occurrence_item = find_drag_occurrence_item();
        if (!verify(committed_drag_occurrence_item != nullptr,
                    "occurrence tree item vanished after committing the free drag")) {
            return 1;
        }
        window.show_tree_item_properties(committed_drag_occurrence_item);
        application.processEvents();
        auto* committed_drag_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
        const auto committed_translation_fields = committed_drag_dialog == nullptr
            ? QList<QDoubleSpinBox*>{}
            : committed_drag_dialog->findChildren<QDoubleSpinBox*>("componentTranslation");
        if (!verify(committed_drag_dialog != nullptr &&
                        translation_changed(committed_translation_fields),
                    "releasing a free drag did not persist the occurrence's new placement")) {
            return 1;
        }
        if (auto* committed_drag_buttons =
                committed_drag_dialog->findChild<QDialogButtonBox*>()) {
            committed_drag_buttons->button(QDialogButtonBox::Cancel)->click();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
    }

    if (!create_document(QStringLiteral("drawing"), drawing_name)) {
        return 1;
    }
    auto* stack = window.findChild<QStackedWidget*>("workspaceStack");
    auto* drawing_canvas = window.findChild<QWidget*>("drawingCanvas");
    auto* drawing_toolbar = window.findChild<QToolBar*>("drawingToolbar");
    auto* insert_view = window.findChild<QAction*>("insertDrawingViewAction");
    if (!verify(tabs->count() == 4 && stack != nullptr &&
                    stack->currentWidget()->objectName() == QStringLiteral("drawingWorkspace"),
                "New Drawing must open inside the common workspace") ||
        !verify(drawing_toolbar != nullptr && drawing_toolbar->isHidden() &&
                    tools_toolbar->isVisible() && insert_view != nullptr &&
                    insert_view->isEnabled(),
                "Drawing commands must move into the shared right toolbar")) {
        return 1;
    }
    application.processEvents();
    const QImage drawing_snapshot = drawing_canvas == nullptr
        ? QImage{} : drawing_canvas->grab().toImage();
    if (!verify(!drawing_snapshot.isNull(), "Drawing canvas cannot be rendered") ||
        !verify(drawing_canvas->palette().color(QPalette::Window) == QColor("#000000"),
                "Drawing workspace palette must be black") ||
        !verify(drawing_snapshot.pixelColor(drawing_snapshot.width() / 2,
                                            drawing_snapshot.height() / 2) ==
                    QColor("#000000"),
                "Drawing paper must retain the black workspace background")) {
        return 1;
    }
    if (!drawing_capture_path.isEmpty() &&
        !verify(window.grab().save(drawing_capture_path),
                "native Qt Drawing capture failed")) {
        return 1;
    }

    // Save/close/reopen must preserve the Drawing's sheet structure through
    // the real window, matching the existing Part/Assembly save/reopen
    // coverage above.
    const int sheet_count_before_reopen = tree->topLevelItemCount() == 1
        ? tree->topLevelItem(0)->childCount() : -1;
    const auto saved_drawing_path = test_directory /
        (drawing_name.toStdString() + ".drwz");
    save->trigger();
    application.processEvents();
    if (!verify(std::filesystem::exists(saved_drawing_path),
                "Save did not persist the Drawing")) {
        return 1;
    }
    close->trigger();
    application.processEvents();
    const bool reopened_drawing = tabs->count() == 3 && window.open_document_path(
        QString::fromStdString(saved_drawing_path.string()));
    if (!verify(reopened_drawing && tabs->count() == 4 &&
                    tree->topLevelItemCount() == 1 &&
                    tree->topLevelItem(0)->childCount() ==
                        sheet_count_before_reopen &&
                    sheet_count_before_reopen > 0,
                "saved Drawing did not close and reopen its sheet structure")) {
        return 1;
    }

    {
        // Advanced Drawing parity: a Drawing view inserted from an
        // Assembly source must seed the sheet's BOM from that Assembly's
        // components, and later regenerating the view (an explicit user
        // action) must re-derive the BOM from the Assembly's *current*
        // component list rather than leaving it frozen at insertion time.
        auto* drawing_window = static_cast<zima::app::DrawingWindow*>(
            window.findChild<QWidget*>("drawingWorkspace"));
        if (!verify(drawing_window != nullptr,
                    "Drawing document has no embedded DrawingWindow for BOM coverage")) {
            return 1;
        }
        insert_view->trigger();
        application.processEvents();
        auto* source_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
        auto* source_combo = source_dialog == nullptr
            ? nullptr : source_dialog->findChild<QComboBox*>();
        if (!verify(source_dialog != nullptr && source_combo != nullptr,
                    "inserting a Drawing view did not open the source-selection dialog")) {
            return 1;
        }
        int assembly_source_index = -1;
        for (int index = 0; index < source_combo->count(); ++index) {
            if (source_combo->itemText(index).startsWith(assembly_name)) {
                assembly_source_index = index;
                break;
            }
        }
        if (!verify(assembly_source_index >= 0,
                    "Drawing view source dialog did not offer the inner Assembly")) {
            return 1;
        }
        source_combo->setCurrentIndex(assembly_source_index);
        source_dialog->findChild<QDialogButtonBox*>()
            ->button(QDialogButtonBox::Ok)->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        auto* view_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
        if (!verify(view_dialog != nullptr,
                    "selecting the Assembly source did not open the view Properties dialog")) {
            return 1;
        }
        view_dialog->findChild<QDialogButtonBox*>()
            ->button(QDialogButtonBox::Ok)->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        const int bom_quantity_after_insertion =
            drawing_window->document_for_test().sheets.empty() ||
                    drawing_window->document_for_test().sheets.front().bom_rows.empty()
                ? 0
                : drawing_window->document_for_test()
                      .sheets.front().bom_rows.front().quantity;
        if (!verify(bom_quantity_after_insertion == 3,
                    "inserting an Assembly view did not seed the BOM from its two Part occurrences")) {
            return 1;
        }
        // Insert a third occurrence into the Assembly directly through the
        // workspace model (not the GUI, since the Assembly tab is not the
                // Insert a third occurrence into the Assembly through the real GUI
        // insertion flow (switch to its tab, trigger Insert, switch back
        // to the Drawing tab), so the source Assembly's component list
        // genuinely changes between view insertion and regeneration.
        const std::string bom_assembly_document_id =
            drawing_window->document_for_test().sheets.front()
                .views.front().source_document_id;
        int bom_assembly_tab_index = -1;
        for (int index = 0; index < tabs->count(); ++index) {
            if (tabs->tabText(index).startsWith(
                    assembly_name + QStringLiteral(".asmz"))) {
                bom_assembly_tab_index = index;
                break;
            }
        }
        const int drawing_tab_index = tabs->currentIndex();
        if (!verify(bom_assembly_tab_index >= 0,
                    "inner Assembly tab is no longer open for BOM regeneration coverage")) {
            return 1;
        }
        tabs->setCurrentIndex(bom_assembly_tab_index);
        application.processEvents();
        auto* bom_insert_menu = window.findChild<QMenu*>("insertComponentMenu");
        QAction* bom_source_action{};
        if (bom_insert_menu != nullptr) {
            for (auto* action : bom_insert_menu->actions()) {
                if (action->objectName() == QStringLiteral("insertSourceAction") &&
                    action->isEnabled()) {
                    bom_source_action = action;
                    break;
                }
            }
        }
        if (!verify(bom_source_action != nullptr,
                    "inner Assembly has no Part source for a third occurrence")) {
            return 1;
        }
        bom_source_action->trigger();
        application.processEvents();
        tabs->setCurrentIndex(drawing_tab_index);
        application.processEvents();
        auto* regenerate_view = window.findChild<QAction*>("regenerateDrawingViewAction");
        if (!verify(regenerate_view != nullptr,
                    "Drawing regeneration coverage requires its action")) {
            return 1;
        }
        // Select the sole inserted view directly (production code exposes
        // this only through the canvas' internal hit-testing, which the
        // GUI test drives via a dedicated test-only accessor instead of
        // guessing pixel coordinates).
        const auto& inserted_view_id = drawing_window->document_for_test()
            .sheets.front().views.front().id;
        drawing_window->select_view_for_test(inserted_view_id);
        application.processEvents();
        if (!verify(regenerate_view->isEnabled(),
                    "selecting the inserted Drawing view did not enable regeneration")) {
            return 1;
        }
        regenerate_view->trigger();
        application.processEvents();
        const int bom_quantity_after_regeneration =
            drawing_window->document_for_test().sheets.empty() ||
                    drawing_window->document_for_test().sheets.front().bom_rows.empty()
                ? 0
                : drawing_window->document_for_test()
                      .sheets.front().bom_rows.front().quantity;
        if (!verify(bom_quantity_after_regeneration == 4,
                    "regenerating the Drawing view did not re-derive the BOM from the "
                    "Assembly's updated component list")) {
            return 1;
        }
    }

    {
        // Frame/title-block template load and remove actions (Python's
        // remove_format/remove_title_block, previously missing in C++).
        auto* drawing_window_for_template = static_cast<zima::app::DrawingWindow*>(
            window.findChild<QWidget*>("drawingWorkspace"));
        auto* remove_frame_action = window.findChild<QAction*>("removeDrawingFrameAction");
        auto* remove_title_block_action =
            window.findChild<QAction*>("removeDrawingTitleBlockAction");
        if (!verify(drawing_window_for_template != nullptr &&
                        remove_frame_action != nullptr &&
                        remove_title_block_action != nullptr,
                    "Drawing template removal coverage requires the window and actions")) {
            return 1;
        }
        drawing_window_for_template->load_frame_for_test("config/formats/ZE-A4.frmz");
        drawing_window_for_template->load_title_block_for_test(
            "config/formats/ZE-TITLE-BLOCK.tblz");
        application.processEvents();
        if (!verify(!drawing_window_for_template->document_for_test()
                            .sheets.front().frame_lines.empty() &&
                        !drawing_window_for_template->document_for_test()
                             .sheets.front().title_block_fields.empty(),
                    "loading a frame/title-block template did not populate the sheet")) {
            return 1;
        }
        remove_frame_action->trigger();
        application.processEvents();
        remove_title_block_action->trigger();
        application.processEvents();
        if (!verify(drawing_window_for_template->document_for_test()
                            .sheets.front().frame_lines.empty() &&
                        drawing_window_for_template->document_for_test()
                            .sheets.front().title_block_fields.empty(),
                    "removing the frame/title-block did not clear the sheet's template geometry")) {
            return 1;
        }
    }

    // File-management parity: rename, delete-current-file, delete old
    // versions (with and without keep-latest), and the working-directory
    // wide equivalents must all be wired to real handlers operating on the
    // reopened Drawing document, mirroring the Python reference workflow.
    {
        if (!verify(rename_document != nullptr && rename_document->isEnabled() &&
                        delete_current_file != nullptr && delete_current_file->isEnabled() &&
                        delete_all_versions != nullptr && delete_all_versions->isEnabled() &&
                        delete_working_directory_old_versions != nullptr &&
                        delete_working_directory_old_versions->isEnabled() &&
                        delete_working_directory_keep_latest != nullptr &&
                        delete_working_directory_keep_latest->isEnabled(),
                    "file-management actions must enable once a saved Drawing is active")) {
            return 1;
        }
        if (!verify(!delete_old_versions->isEnabled() &&
                        !delete_old_versions_keep_latest->isEnabled(),
                    "delete-old-versions actions must stay disabled without archive files")) {
            return 1;
        }

        // Create two versioned archive files ("<name>.1", "<name>.2") next
        // to the saved Drawing, as ZIMA-CAD's save/version rotation would.
        const auto archive_one = std::filesystem::path(
            saved_drawing_path.string() + ".1");
        const auto archive_two = std::filesystem::path(
            saved_drawing_path.string() + ".2");
        std::filesystem::copy_file(saved_drawing_path, archive_one,
            std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(saved_drawing_path, archive_two,
            std::filesystem::copy_options::overwrite_existing);
        // Re-trigger any refresh path that recomputes action enablement
        // (closing and reopening is the simplest reliable trigger already
        // exercised above).
        close->trigger();
        application.processEvents();
        const bool reopened_for_versions = window.open_document_path(
            QString::fromStdString(saved_drawing_path.string()));
        application.processEvents();
        if (!verify(reopened_for_versions && delete_old_versions->isEnabled() &&
                        delete_old_versions_keep_latest->isEnabled(),
                    "delete-old-versions actions must enable once archive files exist")) {
            return 1;
        }

        // "Staré verze kromě nejnovější" must remove only the older archive
        // and keep the newest one (archive_two).
        QTimer::singleShot(0, &window, [] {
            if (auto* box = qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget())) {
                if (auto* yes_button = box->button(QMessageBox::Yes)) {
                    yes_button->click();
                } else {
                    box->accept();
                }
            }
        });
        delete_old_versions_keep_latest->trigger();
        application.processEvents();
        if (!verify(!std::filesystem::exists(archive_one) &&
                        std::filesystem::exists(archive_two),
                    "delete-old-versions-keep-latest must remove only older archives")) {
            return 1;
        }

        // "Staré verze" must remove every remaining archive but keep the
        // primary saved Drawing file itself.
        QTimer::singleShot(0, &window, [] {
            if (auto* box = qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget())) {
                if (auto* yes_button = box->button(QMessageBox::Yes)) {
                    yes_button->click();
                } else {
                    box->accept();
                }
            }
        });
        delete_old_versions->trigger();
        application.processEvents();
        if (!verify(!std::filesystem::exists(archive_two) &&
                        std::filesystem::exists(saved_drawing_path),
                    "delete-old-versions must remove all archives but keep the current file")) {
            return 1;
        }

        // Rename must move the file on disk and keep the document open
        // under its new name/path.
        const auto renamed_path = saved_drawing_path.parent_path() /
            (drawing_name.toStdString() + "-renamed.drwz");
        rename_document->trigger();
        application.processEvents();
        auto* rename_dialog = window.findChild<QDialog*>("renameDocumentDialog");
        if (!verify(rename_dialog != nullptr,
                    "rename must open the shared in-application dialog")) {
            return 1;
        }
        auto* rename_field = rename_dialog->findChild<QLineEdit*>("renameDocumentName");
        rename_field->setText(QString::fromStdString(renamed_path.filename().string()));
        auto* rename_buttons = rename_dialog->findChild<QDialogButtonBox*>();
        rename_buttons->button(QDialogButtonBox::Ok)->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        if (!verify(!std::filesystem::exists(saved_drawing_path) &&
                        std::filesystem::exists(renamed_path),
                    "rename must move the document file on disk")) {
            return 1;
        }

        // The rename must also rewrite references in documents saved on
        // disk but not currently open, matching Python's
        // _rename_document_file_to (which scans the file's directory and
        // the working directory for other documents referencing the
        // renamed path). Build a throwaway closed Assembly fixture whose
        // component references the Drawing's current (already-renamed-once)
        // path (an artificial but sufficient stand-in, since AssemblyDocument
        // components reference any source_path uniformly) and confirm the
        // rename rewrote it even though it was never opened in the workspace.
        const auto closed_reference_assembly_path = saved_drawing_path.parent_path() /
            (QStringLiteral("REFERENCE-STARTUP-") + identity).toStdString().append(".asmz");
        {
            auto reference_document = zima::assembly::AssemblyDocument::create_default();
            reference_document.components.push_back(
                zima::assembly::AssemblyDocument::create_part_occurrence(
                    "closed-reference", "unused-source-id", renamed_path, {}));
            reference_document.save(closed_reference_assembly_path);
        }
        const auto renamed_again_path = renamed_path.parent_path() /
            (drawing_name.toStdString() + "-renamed-again.drwz");
        rename_document->trigger();
        application.processEvents();
        auto* second_rename_dialog = window.findChild<QDialog*>("renameDocumentDialog");
        if (!verify(second_rename_dialog != nullptr,
                    "rename must open the shared in-application dialog a second time")) {
            return 1;
        }
        second_rename_dialog->findChild<QLineEdit*>("renameDocumentName")
            ->setText(QString::fromStdString(renamed_again_path.filename().string()));
        second_rename_dialog->findChild<QDialogButtonBox*>()
            ->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        bool closed_reference_rewritten = false;
        try {
            const auto reloaded_reference_document =
                zima::assembly::AssemblyDocument::load(closed_reference_assembly_path);
            for (const auto& component : reloaded_reference_document.components) {
                if (std::filesystem::absolute(component.source_path).lexically_normal() ==
                        std::filesystem::absolute(renamed_again_path).lexically_normal()) {
                    closed_reference_rewritten = true;
                    break;
                }
            }
        } catch (const std::exception&) {
        }
        if (!verify(std::filesystem::exists(renamed_again_path) &&
                        closed_reference_rewritten,
                    "rename must rewrite references in Assembly documents saved on disk "
                    "but not currently open")) {
            return 1;
        }
        std::filesystem::remove(closed_reference_assembly_path);

        // Working-directory wide deletion: create archives for both the
        // renamed Drawing and an unrelated saved Part, then verify
        // "keep latest" removes only the older ones across all documents.
        const auto renamed_archive_one = std::filesystem::path(
            renamed_again_path.string() + ".1");
        const auto renamed_archive_two = std::filesystem::path(
            renamed_again_path.string() + ".2");
        std::filesystem::copy_file(renamed_again_path, renamed_archive_one,
            std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(renamed_again_path, renamed_archive_two,
            std::filesystem::copy_options::overwrite_existing);
        const auto part_saved_path = test_directory /
            (part_name.toStdString() + ".prtz");
        std::filesystem::path part_archive_one;
        std::filesystem::path part_archive_two;
        const bool part_saved_exists = std::filesystem::exists(part_saved_path);
        if (part_saved_exists) {
            part_archive_one = std::filesystem::path(part_saved_path.string() + ".1");
            part_archive_two = std::filesystem::path(part_saved_path.string() + ".2");
            std::filesystem::copy_file(part_saved_path, part_archive_one,
                std::filesystem::copy_options::overwrite_existing);
            std::filesystem::copy_file(part_saved_path, part_archive_two,
                std::filesystem::copy_options::overwrite_existing);
        }
        QTimer::singleShot(0, &window, [] {
            if (auto* box = qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget())) {
                if (auto* yes_button = box->button(QMessageBox::Yes)) {
                    yes_button->click();
                } else {
                    box->accept();
                }
            }
        });
        delete_working_directory_keep_latest->trigger();
        application.processEvents();
        const bool working_directory_keep_latest_ok =
            !std::filesystem::exists(renamed_archive_one) &&
            std::filesystem::exists(renamed_archive_two) &&
            (!part_saved_exists ||
             (!std::filesystem::exists(part_archive_one) &&
              std::filesystem::exists(part_archive_two)));
        if (!verify(working_directory_keep_latest_ok,
                    "working-directory keep-latest delete must remove only older "
                    "archives across every document")) {
            return 1;
        }
        if (part_saved_exists && std::filesystem::exists(part_archive_two)) {
            std::filesystem::remove(part_archive_two);
        }
    }

    // Tree route/segment selection consumes the saved operation input; Delete
    // is one undoable model edit for both edge-treatment kinds.
    for (const bool fillet : {false,true}) {
        auto document=zima::document::PartDocument::create_default();
        auto box=zima::document::PartDocument::create_box_container();
        document.history={box};
        zima::kernel::OcctKernel kernel;
        auto input=kernel.evaluate_history(document.kernel_operations());
        std::vector<zima::kernel::EdgeReference> vertical;
        for (const auto& edge : input.back().mesh.edges)
            if (edge.points.size()==2 && edge.reference.valid() &&
                std::abs(edge.points.front().x-edge.points.back().x)<1e-7 &&
                std::abs(edge.points.front().y-edge.points.back().y)<1e-7 &&
                std::abs(edge.points.front().z-edge.points.back().z)>1)
                vertical.push_back(edge.reference);
        if (!verify(vertical.size()>=3,"Treatment Tree fixture has no vertical edges")) return 1;
        auto treatment=fillet
            ? zima::document::PartDocument::create_fillet_container({vertical[0],vertical[1]})
            : zima::document::PartDocument::create_chamfer_container({vertical[0],vertical[1]});
        treatment.edge_treatment.routes.push_back({vertical[2]});
        document.history.push_back(treatment);
        const auto path=test_directory/(fillet ? "fillet-tree.prtz" : "chamfer-tree.prtz");
        document.save(path,kernel.evaluate_history(document.kernel_operations()));
        zima::app::AssemblyWorkspaceWindow route_window(QString::fromStdString(test_directory.string()));
        route_window.resize(1100,800);route_window.show();
        if (!verify(route_window.open_document_path(QString::fromStdString(path.string())),
                "Treatment Tree fixture could not open")) return 1;
        application.processEvents();
        auto* tree=route_window.findChild<QTreeWidget*>("documentTree");
        auto* view=dynamic_cast<zima::viewer::MeshView*>(route_window.findChild<QOpenGLWidget*>("modelWorkspace"));
        const auto find_item=[&](int route,int segment) -> QTreeWidgetItem* {
            for (QTreeWidgetItemIterator i(tree);*i;++i)
                if ((*i)->data(0,Qt::UserRole+3).toString()=="part-treatment-component" &&
                    (*i)->data(0,Qt::UserRole).toString().toStdString()==treatment.id &&
                    (*i)->data(0,Qt::UserRole+6).toInt()==route &&
                    (*i)->data(0,Qt::UserRole+7).toInt()==segment) return *i;
            return nullptr;
        };
        auto* route=find_item(0,-1);
        if (!verify(route && route->childCount()==2 && !route->isExpanded(),
                "Treatment Tree does not expose collapsed routes with segments")) return 1;
        tree->setCurrentItem(route);application.processEvents();
        if (!verify(view && view->confirmed_component_wire().size()==2,
                "Tree route did not select exactly its two input edges")) return 1;
        tree->setCurrentItem(find_item(0,0));application.processEvents();
        const auto selected=view->confirmed_component_wire();
        if (!verify(selected.size()==1 && selected.front().reference==vertical[0],
                "Tree segment selected a different input edge")) return 1;
        const auto remove_item=[&](int route,int segment) {
            auto* item=find_item(route,segment);
            if (!item) return false;
            for (auto* parent=item->parent();parent;parent=parent->parent()) tree->expandItem(parent);
            tree->scrollToItem(item);tree->setCurrentItem(item);
            bool invoked=false;
            QTimer::singleShot(0,&route_window,[&] {
                auto* menu=route_window.findChild<QMenu*>("treatmentComponentMenu");
                if (!menu) return;
                auto* remove=menu->findChild<QAction*>("deleteTreatmentComponent");
                if (!remove) {menu->close();return;}
                invoked=true;menu->setActiveAction(remove);
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
                QApplication::sendEvent(menu,&enter);
            });
            tree->customContextMenuRequested(tree->visualItemRect(item).center());
            application.processEvents();
            return invoked;
        };
        if (!verify(remove_item(0,0) && find_item(0,-1) && find_item(0,-1)->childCount()==1 &&
                find_item(1,-1),"Deleting one segment damaged the other treatment routes")) return 1;
        if (!verify(remove_item(1,-1) && !find_item(1,-1) && find_item(0,-1),
                "Deleting a route damaged the remaining route")) return 1;
        if (!verify(remove_item(0,-1) && !find_item(0,-1),
                "Deleting the last route retained an invalid empty treatment")) return 1;
        route_window.findChild<QAction*>("undoAction")->trigger();application.processEvents();
        if (!verify(find_item(0,-1) && find_item(0,-1)->childCount()==1,
                "Undo failed to restore the last deleted treatment route")) return 1;
        route_window.findChild<QAction*>("saveDocumentAction")->trigger();application.processEvents();
        const auto saved=zima::document::PartDocument::load(path);
        const auto* restored=saved.find_container(treatment.id);
        if (!verify(restored && restored->edge_treatment.routes.size()==1 &&
                restored->edge_treatment.routes.front()==std::vector<zima::kernel::EdgeReference>{vertical[1]},
                "Edited treatment route did not survive save/reload")) return 1;
        route_window.close();application.processEvents();
    }

    if (verify_history_tree_drag(application,test_directory)!=0) return 1;

    // Lost-reference acknowledgement belongs to successful Properties OK.
    {
        auto document=zima::document::PartDocument::create_default();
        auto box=zima::document::PartDocument::create_box_container();
        box.placement.reference_valid=false;
        document.history={box};
        zima::kernel::OcctKernel kernel;
        const auto calculated=kernel.evaluate_history(document.kernel_operations());
        const auto path=test_directory / "missing-reference-tree.prtz";
        document.save(path,calculated);
        zima::app::AssemblyWorkspaceWindow reference_window(
            QString::fromStdString(test_directory.string()));
        reference_window.show();
        if (!verify(reference_window.open_document_path(QString::fromStdString(path.string())),
                "Missing-reference fixture did not open")) return 1;
        const auto find_row=[&]() -> QTreeWidgetItem* {
            auto* tree=reference_window.findChild<QTreeWidget*>("documentTree");
            for (QTreeWidgetItemIterator i(tree);*i;++i)
                if ((*i)->data(0,Qt::UserRole).toString().toStdString()==box.id &&
                    (*i)->data(0,Qt::UserRole+3).toString()=="part-container") return *i;
            return nullptr;
        };
        for (const auto button : {QDialogButtonBox::Cancel,QDialogButtonBox::Ok}) {
            auto* row=find_row();
            if (!verify(row && row->data(0,zima::app::missing_reference_role).toBool(),
                    "Missing reference or Cancel did not retain red Tree row")) return 1;
            reference_window.show_tree_item_properties(row);
            application.processEvents();
            zima::app::PrimitivePropertiesDialog* properties=nullptr;
            for (auto* dialog : reference_window.findChildren<QDialog*>())
                if (auto* primitive=dynamic_cast<zima::app::PrimitivePropertiesDialog*>(dialog))
                    properties=primitive;
            if (!verify(properties != nullptr,"Reference Properties did not open")) return 1;
            properties->buttons()->button(button)->click();
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
            application.processEvents();
        }
        if (!verify(find_row() && !find_row()->data(0,zima::app::missing_reference_role).toBool(),
                "Successful Properties OK did not restore normal Tree background")) return 1;
        reference_window.close();application.processEvents();
    }

    // Exercise the real 3D parameter display, including opening Properties
    // from a catalog dimension while no dialog is active.
    {
        auto document=zima::document::PartDocument::create_default();
        auto block=zima::document::PartDocument::create_box_container();
        block.box={40.0,40.0,40.0};
        auto opening=zima::document::PartDocument::create_thread_container();
        opening.placement.z=-20.0;
        document.history={block,opening};
        zima::kernel::OcctKernel opening_kernel;
        const auto calculated=opening_kernel.evaluate_history(document.kernel_operations());
        const auto opening_path=test_directory / "opening-dimensions.prtz";
        document.save(opening_path,calculated);
        zima::app::AssemblyWorkspaceWindow opening_window(
            QString::fromStdString(test_directory.string()));
        opening_window.resize(1100,800);
        opening_window.show();
        if (!verify(opening_window.open_document_path(
                QString::fromStdString(opening_path.string())),
                "Opening dimension fixture could not be opened")) return 1;
        opening_window.show_parameter_dimensions(opening.id);
        application.processEvents();
        auto* view=dynamic_cast<zima::viewer::MeshView*>(
            opening_window.findChild<QOpenGLWidget*>("modelWorkspace"));
        if (!verify(view != nullptr,"Opening View is missing")) return 1;
        const auto has_parameter_dimensions = [&] {
            return std::any_of(view->mesh().dimensions.begin(), view->mesh().dimensions.end(),
                [&](const auto& dimension) { return dimension.reference.owner_id == opening.id; });
        };
        if (!verify(has_parameter_dimensions() && view->confirm_current_pointer() &&
                !has_parameter_dimensions(), "Short MMB did not end container dimension inspection")) return 1;
        opening_window.show_parameter_dimensions(opening.id);
        QMouseEvent finish_dimensions(QEvent::MouseButtonDblClick,QPointF(40,40),
            QPointF(40,40),QPointF(40,40),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(view,&finish_dimensions);
        application.processEvents();
        if (!verify(!has_parameter_dimensions(), "Double MMB did not end container dimension inspection")) return 1;
        opening_window.show_parameter_dimensions(opening.id);
        application.processEvents();
        const auto candidate_for=[&](const std::string& key) {
            std::optional<zima::viewer::ViewerCandidate> result;
            for (std::size_t index=0;index<100;++index) {
                zima::viewer::ViewerCandidate candidate;
                candidate.kind=zima::viewer::CandidateKind::Dimension;
                candidate.owner_id=opening.id;
                candidate.semantic_key=key;
                candidate.geometry_index=index;
                if (view->candidate_dimension_value(candidate)) { result=candidate; break; }
            }
            return result;
        };
        const auto bore=candidate_for("measurement:bore_diameter");
        const auto catalog=candidate_for("parameter:thread_designation");
        if (!verify(bore && catalog &&
                std::abs(*view->candidate_dimension_value(*bore)-opening.thread.profile_diameter)<1e-6,
                "Closed opening Properties lost the informational bore dimension")) return 1;
        view->repaint();
        const auto image=view->grabFramebuffer();
        int measured_pixels=0;
        for (int y=0;y<image.height();++y)
            for (int x=0;x<image.width();++x) {
                const auto color=image.pixelColor(x,y);
                if (std::abs(color.red()-173)<=2 && std::abs(color.green()-110)<=2 &&
                    std::abs(color.blue()-46)<=2) ++measured_pixels;
            }
        image.save(QString::fromStdString((test_directory / "opening-dimensions.png").string()));
        if (!verify(measured_pixels>15,
                "3D View did not paint the informational diameter in measured brown")) return 1;
        const auto wait_for_popup=[&] {
            QEventLoop events;
            QTimer::singleShot(40,&events,&QEventLoop::quit);
            events.exec();
        };
        const auto properties_visible=[&] {
            for (auto* child : opening_window.findChildren<QDialog*>())
                if (dynamic_cast<zima::app::PrimitivePropertiesDialog*>(child) && child->isVisible())
                    return true;
            return false;
        };
        opening_window.edit_dimension_inline(*catalog);
        wait_for_popup();
        auto* sizes=opening_window.findChild<QComboBox*>("inlineThreadSizeEdit");
        // Synthetic Qt input has no native Wayland grab serial. Check the
        // inline editor and drive its normal activation signal below; popup
        // grab availability is not a feature-state contract.
        if (!verify(sizes && sizes->isVisible() && !properties_visible(),
                "View Edit opened Properties instead of an inline catalog")) return 1;
        const int m12=sizes->findText("M12");
        sizes->setCurrentIndex(m12);
        QMetaObject::invokeMethod(sizes,"activated",Q_ARG(int,m12));
        sizes->hidePopup();
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        application.processEvents();
        const auto edited_bore=candidate_for("measurement:bore_diameter");
        const auto edited_catalog=candidate_for("parameter:thread_designation");
        if (!verify(edited_bore && edited_catalog && !properties_visible() &&
                std::abs(*view->candidate_dimension_value(*edited_catalog)-12.0)<1e-6,
                "Catalog selection did not commit and return to View Edit")) return 1;
        opening_window.edit_dimension_inline(*edited_catalog);
        wait_for_popup();
        sizes=opening_window.findChild<QComboBox*>("inlineThreadSizeEdit");
        if (!verify(sizes != nullptr,"Repeated inline catalog edit failed")) return 1;
        sizes->hidePopup();
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        application.processEvents();
        const auto restored_catalog=candidate_for("parameter:thread_designation");
        if (!verify(restored_catalog && !properties_visible() &&
                std::abs(*view->candidate_dimension_value(*restored_catalog)-12.0)<1e-6,
                "Dismissing the inline catalog changed the thread or left View Edit")) return 1;
        auto* opening_tree=opening_window.findChild<QTreeWidget*>("documentTree");
        QTreeWidgetItem* opening_item=nullptr;
        {
            QTreeWidgetItemIterator item(opening_tree);
            while (*item) {
                if ((*item)->data(0,Qt::UserRole).toString().toStdString()==opening.id) {
                    opening_item=*item;
                    break;
                }
                ++item;
            }
        }
        if (!verify(opening_item != nullptr,"Opening history item is missing")) return 1;
        opening_window.show_tree_item_properties(opening_item);
        application.processEvents();
        zima::app::PrimitivePropertiesDialog* opening_properties=nullptr;
        for (auto* child : opening_window.findChildren<QDialog*>())
            if (auto* primitive=dynamic_cast<zima::app::PrimitivePropertiesDialog*>(child))
                opening_properties=primitive;
        if (!verify(opening_properties != nullptr,"Opening Properties did not open")) return 1;
        auto* ending=opening_properties->findChild<QComboBox*>("threadEnd");
        auto* target=opening_properties->findChild<QLineEdit*>("threadBoreEndTarget");
        const auto active_placement_cells=[&] {
            int active=0;
            for (auto* table : opening_properties->findChildren<QTableWidget*>())
                for (int row=0;row<table->rowCount();++row)
                    for (int column=0;column<table->columnCount();++column)
                        if (const auto* cell=dynamic_cast<zima::ui::ReferenceCellItem*>(table->item(row,column));
                            cell && cell->is_active_input()) ++active;
            return active;
        };
        if (!verify(ending && target && active_placement_cells()>0,
                "Opening fixture did not start with incomplete active placement")) return 1;
        ending->setCurrentIndex(ending->findData("up_to"));
        application.processEvents();
        if (!verify(active_placement_cells()==0 && target->styleSheet().contains("#42d66b"),
                "Opening Up To did not take exclusive input from placement")) return 1;
        const auto* up_to_tip=opening_properties->findChild<QCheckBox*>("threadDrillPoint");
        if (!verify(up_to_tip && !up_to_tip->isChecked() && !up_to_tip->isEnabled(),
                "Opening Up To retained a drill point past its target surface")) return 1;
        ending->setCurrentIndex(ending->findData("length"));
        application.processEvents();
        if (!verify(active_placement_cells()==0 && !target->styleSheet().contains("#42d66b"),
                "Leaving Opening Up To retained a stale selection state")) return 1;
        ending->setCurrentIndex(ending->findData("up_to"));
        application.processEvents();
        std::optional<QPointF> target_position;
        for (int y=4;y<view->height() && !target_position;y+=6)
            for (int x=4;x<view->width();x+=6) {
                const QPointF position(x,y);
                const auto offered=view->selection_candidates_at(position);
                if (!offered.empty() && offered.front().kind==zima::viewer::CandidateKind::Face &&
                    offered.front().semantic_key=="z_max") { target_position=position; break; }
            }
        if (!verify(target_position.has_value(),"Opening Up To offered no target face")) return 1;
        QMouseEvent move(QEvent::MouseMove,*target_position,
            view->mapToGlobal(target_position->toPoint()),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&move);
        QMouseEvent press(QEvent::MouseButtonPress,*target_position,
            view->mapToGlobal(target_position->toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(view,&press);
        QMouseEvent release(QEvent::MouseButtonRelease,*target_position,
            view->mapToGlobal(target_position->toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&release);
        application.processEvents();
        if (!verify(!target->text().isEmpty() && !target->styleSheet().contains("#42d66b") &&
                active_placement_cells()==0 && opening_properties->first_empty_position_index()==0,
                "Up To click did not set its target independently of placement")) return 1;
        auto* thread_end=opening_properties->findChild<QComboBox*>("threadLengthEnd");
        auto* thread_target=opening_properties->findChild<QLineEdit*>("threadLengthEndTarget");
        if (!verify(thread_end && thread_target,"Thread length target controls missing")) return 1;
        thread_end->setCurrentIndex(thread_end->findData("up_to"));
        application.processEvents();
        if (!verify(thread_target->styleSheet().contains("#42d66b") &&
                !target->styleSheet().contains("#42d66b") && active_placement_cells()==0,
                "Thread Up To did not exclusively own target entry")) return 1;
        QApplication::sendEvent(view,&move);
        QApplication::sendEvent(view,&press);
        QApplication::sendEvent(view,&release);
        application.processEvents();
        if (!verify(!thread_target->text().isEmpty() &&
                !opening_properties->findChild<QDoubleSpinBox*>("threadRunoutPitchFactor")->isVisible(),
                "Thread Up To target missing or runout still visible")) return 1;
        const QPointer<zima::app::PrimitivePropertiesDialog> committed_dialog(opening_properties);
        opening_properties->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        if (!verify(committed_dialog.isNull() || !committed_dialog->isVisible(),
                "Opening Up To failed to calculate on OK")) return 1;
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        application.processEvents();
        auto* save_opening=opening_window.findChild<QAction*>("saveDocumentAction");
        if (!verify(save_opening != nullptr,"Opening save action is missing")) return 1;
        save_opening->trigger();
        application.processEvents();
        std::vector<zima::kernel::BodyResult> up_to_results;
        const auto up_to_document=zima::document::PartDocument::load(opening_path,&up_to_results);
        const auto* saved_opening=up_to_document.find_container(opening.id);
        if (!verify(saved_opening && !up_to_results.empty() &&
                saved_opening->thread.end_condition_forward==zima::document::EndCondition::UpTo &&
                saved_opening->thread.length_end_condition==zima::document::EndCondition::UpTo &&
                saved_opening->thread.length_end_targets.size()==1 &&
                saved_opening->thread.length_end_targets.front().reference.semantic_key=="z_max" &&
                saved_opening->placement.references.empty() &&
                saved_opening->thread.end_targets_forward.front().reference.semantic_key=="z_max",
                "Opening Up To target or untouched placement did not survive save")) return 1;
        const double drill_radius=saved_opening->thread.profile_diameter*0.5;
        const double expected_volume=64000.0-std::acos(-1.0)*drill_radius*drill_radius*40.0-
            std::acos(-1.0)*(drill_radius+1.0/3.0);
        if (!verify(std::abs(up_to_results.back().volume-expected_volume)<1e-5,
                "Opening Up To did not actually cut to its selected top face")) return 1;
        const auto find_component=[&](const QString& role) -> QTreeWidgetItem* {
            for (QTreeWidgetItemIterator i(opening_tree);*i;++i)
                if ((*i)->data(0,Qt::UserRole+3).toString()=="part-opening-component" &&
                    (*i)->data(0,Qt::UserRole).toString().toStdString()==opening.id &&
                    (*i)->data(0,Qt::UserRole+5).toString()==role) return *i;
            return nullptr;
        };
        const auto component_menu=[&](const QString& role,bool remove,bool edit=false) {
            auto* item=find_component(role);
            if (!item) return false;
            opening_tree->expandItem(item->parent());
            opening_tree->scrollToItem(item);
            opening_tree->setCurrentItem(item);
            application.processEvents();
            bool menu_valid=false;
            QTimer::singleShot(0,&opening_window,[&] {
                auto* menu=opening_window.findChild<QMenu*>("openingComponentMenu");
                if (!menu) return;
                auto* action=menu->findChild<QAction*>("deleteOpeningComponent");
                auto* edit_action=menu->findChild<QAction*>("editOpeningComponent");
                menu_valid=edit_action && (action!=nullptr)==(role!="bore");
                if ((remove && action) || (edit && edit_action)) {
                    menu->setActiveAction(edit ? edit_action : action);
                    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
                    QApplication::sendEvent(menu,&enter);
                } else menu->close();
            });
            opening_tree->customContextMenuRequested(
                opening_tree->visualItemRect(item).center());
            application.processEvents();
            return menu_valid;
        };
        for (const QString role : {QStringLiteral("bore"),QStringLiteral("thread"),QStringLiteral("chamfer")}) {
            auto* item=find_component(role);
            if (!verify(item!=nullptr,"Opening component is missing from Tree")) return 1;
            opening_tree->setCurrentItem(item);
            application.processEvents();
            const auto selected=view->confirmed_candidate();
            if (!verify(selected && selected->semantic_key=="component:"+role.toStdString() &&
                    !view->confirmed_component_edge_indices().empty(),
                    "Opening Tree component did not select its exact View wire")) return 1;
        }
        if (!verify(component_menu("bore",false,true) && candidate_for("measurement:bore_diameter") &&
                !candidate_for("parameter:thread_designation") && !properties_visible(),
                "Tree bore Edit did not show only bore dimensions in View")) return 1;
        if (!verify(component_menu("thread",false,true) && candidate_for("parameter:thread_designation") &&
                !candidate_for("measurement:bore_diameter") && !properties_visible(),
                "Tree thread Edit did not show only thread dimensions in View")) return 1;
        if (!verify(component_menu("chamfer",false,true) && candidate_for("parameter:chamfer_depth") &&
                !candidate_for("parameter:thread_designation") && !properties_visible(),
                "Tree chamfer Edit did not show only chamfer dimensions in View")) return 1;
        if (!verify(component_menu("bore",false),"Mandatory bore exposes Delete")) return 1;
        if (!verify(component_menu("chamfer",true) && !find_component("chamfer") && find_component("thread"),
                "Deleting chamfer did not disable only that opening component")) return 1;
        if (!verify(component_menu("thread",true) && !find_component("thread") && find_component("bore"),
                "Deleting thread did not switch the opening to a plain bore")) return 1;
        const auto plain_icon=find_component("bore")->parent()->icon(0).pixmap(24,24).toImage();
        if (!verify(plain_icon==zima::app::resource_icon("cylinder").pixmap(24,24).toImage(),
                "Plain opening did not use the bore icon")) return 1;
        opening_window.findChild<QAction*>("saveDocumentAction")->trigger();
        application.processEvents();
        const auto plain_document=zima::document::PartDocument::load(opening_path);
        const auto* plain_opening=plain_document.find_container(opening.id);
        if (!verify(plain_opening && !plain_opening->thread.enabled &&
                !plain_opening->thread.chamfer_enabled &&
                std::abs(plain_opening->thread.nominal_diameter-2*drill_radius)<1e-6,
                "Tree Delete did not persist opening options and bore diameter")) return 1;
        opening_window.findChild<QAction*>("undoAction")->trigger();
        application.processEvents();
        if (!verify(find_component("thread") && !find_component("chamfer"),
                "Undo did not restore the deleted thread")) return 1;
        opening_window.show_tree_item_properties(find_component("bore"));
        application.processEvents();
        zima::app::PrimitivePropertiesDialog* restored_properties=nullptr;
        for (auto* child : opening_window.findChildren<QDialog*>())
            if (child->isVisible())
                if (auto* dialog=dynamic_cast<zima::app::PrimitivePropertiesDialog*>(child))
                    restored_properties=dialog;
        if (!verify(restored_properties &&
                !restored_properties->findChild<QCheckBox*>("threadChamferEnabled")->isChecked(),
                "Deleted chamfer remains checked in opening Properties")) return 1;
        restored_properties->findChild<QCheckBox*>("threadChamferEnabled")->setChecked(true);
        restored_properties->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        application.processEvents();
        if (!verify(find_component("chamfer")!=nullptr,
                "Opening Properties did not restore the disabled component in Tree")) return 1;
        view->clear_selection();
        if (!verify(view->confirmed_component_edge_indices().empty(),
                "Clearing selection retained opening component highlights")) return 1;
        opening_window.close();
    }

    about->trigger();
    application.processEvents();
    auto* about_dialog = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    auto* about_buttons = about_dialog == nullptr
        ? nullptr : about_dialog->findChild<QDialogButtonBox*>();
    if (!verify(about_buttons != nullptr &&
                    about_dialog->windowFlags().testFlag(Qt::SubWindow),
                "About must use the shared in-application SubWindow contract")) {
        return 1;
    }
    about_buttons->button(QDialogButtonBox::Ok)->click();
    application.processEvents();
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication application(argc, argv);
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == QStringLiteral("windows") &&
        qEnvironmentVariableIsEmpty("QT_STYLE_OVERRIDE")) {
        if (auto* windows_style = QStyleFactory::create(QStringLiteral("windows11"))) {
            application.setStyle(windows_style);
        }
    }
#endif
    application.setApplicationName("ZIMA-CAD");
    application.setDesktopFileName("zima-cad");
    application.setWindowIcon(zima::app::application_icon());
    QString startup_directory;
    const auto arguments = application.arguments();
    for (int index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (argument == QStringLiteral("--working-directory") ||
            argument == QStringLiteral("-w")) {
            if (index + 1 < arguments.size()) {
                startup_directory = QFileInfo(arguments.at(++index))
                    .absoluteFilePath();
            }
            continue;
        }
        if (argument.startsWith(QStringLiteral("--working-directory="))) {
            startup_directory = QFileInfo(
                argument.section('=', 1)).absoluteFilePath();
            continue;
        }
        if (argument.startsWith('-')) continue;
        const QFileInfo candidate(argument);
        if (candidate.isDir()) {
            startup_directory = candidate.absoluteFilePath();
            continue;
        }
        if (argument.endsWith(".prtz", Qt::CaseInsensitive) ||
            argument.endsWith(".asmz", Qt::CaseInsensitive) ||
            argument.endsWith(".drwz", Qt::CaseInsensitive)) {
            startup_directory = candidate.absolutePath();
            break;
        }
    }
    // Keep automated UI artifacts out of the repository root and out of the
    // user's configured project directory.  The contract intentionally uses
    // one stable, user-approved test workspace so interrupted runs are easy
    // to inspect and clean up.
    const bool runs_startup_contract =
        arguments.contains(QStringLiteral("--verify-startup")) ||
        std::any_of(arguments.cbegin(), arguments.cend(), [](const QString& argument) {
            return argument.startsWith(QStringLiteral("--capture-part=")) ||
                argument.startsWith(QStringLiteral("--capture-drawing="));
        });
    if (startup_directory.isEmpty() && runs_startup_contract) {
        const QString test_directory =
            QDir::current().absoluteFilePath(QStringLiteral("Projects/test"));
        if (!QDir().mkpath(test_directory)) {
            std::cerr << "Cannot prepare Projects/test for startup verification\n";
            return 1;
        }
        startup_directory = test_directory;
    }
    zima::app::AssemblyWorkspaceWindow window(startup_directory);
    QString part_capture_path;
    QString drawing_capture_path;
    const QString part_capture_prefix = QStringLiteral("--capture-part=");
    const QString drawing_capture_prefix = QStringLiteral("--capture-drawing=");
    for (const auto& argument : application.arguments()) {
        if (argument.startsWith(part_capture_prefix)) {
            part_capture_path = argument.mid(part_capture_prefix.size());
        } else if (argument.startsWith(drawing_capture_prefix)) {
            drawing_capture_path = argument.mid(drawing_capture_prefix.size());
        }
    }
    if (application.arguments().contains("--verify-startup") ||
        !part_capture_path.isEmpty() || !drawing_capture_path.isEmpty()) {
        return verify_startup_contract(
            application, window, std::filesystem::path(startup_directory.toStdString()),
            part_capture_path, drawing_capture_path);
    }
    // A document supplied by Windows file association/double-click must show
    // the same live status progress as File > Open.  The old order loaded it
    // while the window was still hidden, so even a responsive worker had no
    // visible surface to paint into.
    window.showMaximized();
    application.processEvents();
    for (const auto& argument : application.arguments().mid(1)) {
        if (argument.startsWith('-')) continue;
        if (argument.endsWith(".prtz", Qt::CaseInsensitive) ||
            argument.endsWith(".asmz", Qt::CaseInsensitive) ||
            argument.endsWith(".drwz", Qt::CaseInsensitive)) {
            if (!window.open_document_path(argument)) return 1;
        }
    }
    return application.exec();
}
