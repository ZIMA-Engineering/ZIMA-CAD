#include "drawing_window.hpp"
#include <zima/workspace/workspace.hpp>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if(!value) throw std::runtime_error(message); }
void flush() { QApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete); }
void mouse(QWidget* widget, QEvent::Type type, QPointF point, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type,point,QPointF(widget->mapToGlobal(point.toPoint())),button,buttons,Qt::NoModifier);
    QApplication::sendEvent(widget,&event); flush();
}
void click(QWidget* widget,QPointF point) {
    mouse(widget,QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton);
    mouse(widget,QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton);
}
}

int verify_drawing_ui() {
    try {
        zima::workspace::Workspace workspace;
        auto part=zima::document::PartDocument::create_default(); part.name="Drawing source";
        zima::kernel::BodyResult cache;
        cache.mesh.edges={
            {{{-30,0,-20},{30,0,-20}},{"profile","curve:bottom",""}},
            {{{30,0,-20},{30,0,20}},{"profile","curve:right",""}},
            {{{30,0,20},{-30,0,20}},{"profile","curve:top",""}},
            {{{-30,0,20},{-30,0,-20}},{"profile","curve:left",""}}};
        workspace.add_part(part,{cache},"source.prtz");
        auto drawing=zima::drawing::DrawingDocument::create_default();
        drawing.source_document_id=part.document_id; drawing.source_path="source.prtz";
        drawing.source_name=part.name; workspace.add_drawing(drawing);
        workspace.activate(drawing.document_id); workspace.display_top_level(drawing.document_id);
        zima::app::DrawingWindow window(&workspace,false);
        window.edit_workspace_document(drawing.document_id); window.resize(1920,1000); window.show(); flush();
        auto* canvas=window.findChild<QWidget*>("drawingCanvas");
        const auto action=[&](const char* id) {
            auto* result=window.findChild<QAction*>(id); require(result,"Drawing action missing"); return result;
        };
        const auto dialog=[&]() {
            for(auto* value:window.findChildren<QDialog*>("drawingViewProperties")) if(value->isVisible()) return value;
            return static_cast<QDialog*>(nullptr);
        };
        const auto& state=window.document_for_test();
        const auto count=[&]{return state.sheets.front().views.size();};
        auto* variants=window.findChild<QComboBox*>("drawingSourceVariant");
        require(variants && variants->count()==1 && variants->currentText()=="source.prtz","Source variant placeholder is not the source filename");
        const QPointF center=canvas->rect().center();
        action("insertDrawingViewAction")->trigger(); flush();
        require(!dialog() && count()==0,"Insert View opened a dialog or persisted before placement");
        click(canvas,center);
        require(dialog() && count()==0,"Placement must open transient unified Properties");
        require(dialog()->findChild<QComboBox*>("drawingViewOrientation")->currentIndex()==6,"First view is not isometric");
        dialog()->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click(); flush();
        require(count()==0 && workspace.open_drawing(drawing.document_id)->document.sheets.front().views.empty(),"Cancel inserted a view");
        action("insertDrawingViewAction")->trigger(); click(canvas,center);
        auto* properties=dialog(); require(properties,"Second placement has no properties");
        properties->findChild<QComboBox*>("drawingViewOrientation")->setCurrentIndex(0);
        properties->findChild<QLineEdit*>("drawingViewName")->setText("Front test");
        properties->findChild<QCheckBox*>()->setChecked(true);
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); flush();
        require(count()==1 && !dialog(),"OK did not commit one view");
        const auto original=state.sheets.front().views.front();
        require(original.show_caption && original.name=="Front test","Caption properties were not persisted");
        require(std::abs(original.x-state.sheets.front().width_mm()/2)<1.0 &&
                std::abs(original.y-state.sheets.front().height_mm()/2)<1.0,"Click location was not converted to sheet coordinates");
        // The middle of this wire rectangle is far from all four edges.
        click(canvas,QPointF(3,3));
        require(!action("editDrawingViewAction")->isEnabled(),"Empty click did not clear the selection");
        mouse(canvas,QEvent::MouseMove,center,Qt::NoButton,Qt::NoButton);
        click(canvas,center);
        require(action("editDrawingViewAction")->isEnabled(),"Click inside empty rectangle interior did not select its view");
        action("editDrawingViewAction")->trigger(); flush();
        require(dialog(),"Cannot edit selected view");
        dialog()->findChild<QDoubleSpinBox*>("drawingViewX")->setValue(original.x+20);
        require(state.sheets.front().views.front().x==original.x,"Property preview changed the persisted position");
        dialog()->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click(); flush();
        require(state.sheets.front().views.front().x==original.x,"Cancel failed to restore position");
        QContextMenuEvent context(QContextMenuEvent::Mouse,center.toPoint(),canvas->mapToGlobal(center.toPoint()));
        QApplication::sendEvent(canvas,&context); flush();
        auto* menu=canvas->findChild<QMenu*>("drawingViewContextMenu");
        require(menu && menu->actions().contains(action("projectDrawingViewAction")),"View context menu lacks projected view");
        menu->close(); flush();
        action("projectDrawingViewAction")->trigger();
        const QPointF right=center+QPointF(220,0);
        mouse(canvas,QEvent::MouseMove,right,Qt::NoButton,Qt::NoButton); click(canvas,right);
        require(dialog() && count()==1,"Projection placement must remain transient");
        require(!dialog()->findChild<QComboBox*>("drawingViewOrientation")->isEnabled(),"Derived orientation must be owned by projection");
        // Shared MMB-double-click confirmation must also work over the drawing canvas.
        mouse(canvas,QEvent::MouseButtonPress,right,Qt::MiddleButton,Qt::MiddleButton);
        mouse(canvas,QEvent::MouseButtonRelease,right,Qt::MiddleButton,Qt::NoButton);
        require(dialog() && count()==1,"Short middle click committed a view");
        mouse(canvas,QEvent::MouseButtonDblClick,right,Qt::MiddleButton,Qt::MiddleButton);
        mouse(canvas,QEvent::MouseButtonRelease,right,Qt::MiddleButton,Qt::NoButton);
        require(count()==2 && !dialog(),"Middle double click did not confirm view properties");
        const auto child=state.sheets.front().views.back();
        require(child.parent_view_id==original.id && child.projection_direction==zima::drawing::ProjectionDirection::Right &&
            std::abs(child.y-original.y)<1e-6,"Projected view did not keep parent/ray placement");
        window.select_view_for_test(original.id); action("editDrawingViewAction")->trigger(); flush();
        properties=dialog(); properties->findChild<QDoubleSpinBox*>("drawingViewX")->setValue(original.x+10);
        properties->findChild<QComboBox*>("drawingViewScaleMode")->setCurrentIndex(1);
        properties->findChild<QDoubleSpinBox*>("drawingViewScale")->setValue(0.5);
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); flush();
        require(std::abs(state.sheets.front().views.back().x-child.x-10)<1e-6,"Parent position edit left the projected view behind");
        action("editDrawingSheetAction")->trigger(); flush();
        QDialog* sheet_properties{};
        for(auto* candidate:window.findChildren<QDialog*>()) if(candidate->isVisible()) sheet_properties=candidate;
        require(sheet_properties,"Sheet properties did not open");
        sheet_properties->findChild<QDoubleSpinBox*>()->setValue(0.25);
        sheet_properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); flush();
        require(state.sheets.front().views.front().scale==0.5 && state.sheets.front().views.back().scale==0.25,
            "Sheet scale did not update inherited views independently of local scale");
        const auto directory=std::filesystem::current_path()/"Projects/test/drawing-ui";
        std::filesystem::create_directories(directory);
        const auto file=directory/"view-workflow.drwz";
        state.save(file); const auto reopened=zima::drawing::DrawingDocument::load(file);
        require(reopened.sheets.front().views.front().show_caption && !reopened.sheets.front().views.front().use_sheet_scale &&
            reopened.sheets.front().views.front().scale==0.5,"View caption/local scale did not survive reopening");
        // Both pickers must start at the owner-provided global Formats directory.
        window.set_formats_directory(QString::fromStdString(directory.string()));
        for(const auto* name:{"drawingAddFormatButton","drawingAddTitleBlockButton"}) {
            bool correct=false;
            QTimer::singleShot(0,[&] {
                for(auto* widget:QApplication::topLevelWidgets()) if(auto* picker=qobject_cast<QFileDialog*>(widget)) {
                    correct=QDir(picker->directory()).canonicalPath()==QDir(QString::fromStdString(directory.string())).canonicalPath();
                    picker->reject();
                }
            });
            window.findChild<QPushButton*>(name)->click(); flush();
            require(correct,"Template picker ignored global Formats directory");
        }
        window.grab().save(QString::fromStdString((directory/"drawing-workflow.png").string()));
        require(workspace.open_part(part.document_id)->session.revision()==0,"Drawing interaction modified the source Part");
        std::cout<<"Drawing placement, rectangular selection, projection, Cancel, MMB, persistence and global paths passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
