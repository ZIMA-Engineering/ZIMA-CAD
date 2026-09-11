#include <zima/drawing/measurement_dimension.hpp>
#include "drawing_shading.hpp"
#include <zima/kernel/occt_kernel.hpp>
#include "drawing_projection_fixture.hpp"
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
#include <QMessageBox>
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
        QString modal_error;
        QTimer modal_catcher;
        QObject::connect(&modal_catcher,&QTimer::timeout,[&] {
            if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                modal_error=box->text();std::cerr<<"Drawing modal: "<<modal_error.toStdString()<<'\n';box->accept();
            }
        });
        modal_catcher.start(30);
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
        require(!workspace.open_drawing(drawing.document_id)->is_dirty(),"Opening Drawing marked it dirty");
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
        require(count()==0 && workspace.open_drawing(drawing.document_id)->document().sheets.front().views.empty(),"Cancel inserted a view");
        require(!workspace.open_drawing(drawing.document_id)->is_dirty(),"Cancel marked Drawing dirty");
        action("insertDrawingViewAction")->trigger(); click(canvas,center);
        auto* properties=dialog(); require(properties,"Second placement has no properties");
        properties->findChild<QComboBox*>("drawingViewOrientation")->setCurrentIndex(0);
        auto* display_mode=properties->findChild<QComboBox*>("drawingViewDisplay");
        auto* hidden_style=properties->findChild<QComboBox*>("drawingHiddenEdgeStyle");
        require(display_mode&&display_mode->count()==4&&hidden_style&&hidden_style->count()==2,"View display choices are incomplete");
        auto* tangent_style=properties->findChild<QComboBox*>("drawingTangentEdgeStyle");
        require(tangent_style&&tangent_style->count()==3,"Tangent edge choices are incomplete");
        tangent_style->setCurrentIndex(1);
        display_mode->setCurrentIndex(3);hidden_style->setCurrentIndex(1);flush();
        require(count()==0,"Changing view display committed the pending preview");
        display_mode->setCurrentIndex(0);
        properties->findChild<QLineEdit*>("drawingViewName")->setText("Front test");
        properties->findChild<QCheckBox*>()->setChecked(true);
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); flush();
        require(workspace.open_drawing(drawing.document_id)->is_dirty(),"Confirmed view creation was not tracked");
        const auto tracked_revision=workspace.open_drawing(drawing.document_id)->revision();
        window.edit_workspace_document(drawing.document_id);flush();
        require(workspace.open_drawing(drawing.document_id)->revision()==tracked_revision,"Displaying current drawing created another edit");
        require(count()==1 && !dialog(),"OK did not commit one view");
        const auto original=state.sheets.front().views.front();
        require(original.tangent_edge_style==zima::drawing::TangentEdgeStyle::Thin,"Tangent edge property did not persist on OK");
        require(original.show_caption && original.name=="Front test","Caption properties were not persisted");
        require(std::abs(original.x-state.sheets.front().width_mm()/2)<1.0 &&
                std::abs(original.y-state.sheets.front().height_mm()/2)<1.0,"Click location was not converted to sheet coordinates");
        // Labels live above the model bounds and move independently in paper units.
        const auto caption=window.view_label_center_for_test(original.id);require(caption&&caption->y()<center.y(),"Default caption is not above the view");
        mouse(canvas,QEvent::MouseMove,*caption,Qt::NoButton,Qt::NoButton);
        const auto hover_image=canvas->grab().toImage();bool orange_handle=false;
        // QWidget positions are logical pixels; the grabbed image uses device pixels.
        const auto handle_pixel=(*caption*hover_image.devicePixelRatio()).toPoint();
        const int handle_radius=qCeil(6*hover_image.devicePixelRatio());
        for(int y=-handle_radius;y<=handle_radius;++y)for(int x=-handle_radius;x<=handle_radius;++x){const auto pixel=hover_image.pixelColor(handle_pixel+QPoint(x,y));orange_handle|=pixel.red()>180&&pixel.green()>60&&pixel.green()<190&&pixel.blue()<120;}
        if(!orange_handle){std::filesystem::create_directories("Projects/test/drawing-ui");hover_image.save("Projects/test/drawing-ui/caption-hover-failure.png");}
        require(orange_handle,"Hover did not highlight the caption manipulation point");
        mouse(canvas,QEvent::MouseButtonPress,*caption+QPointF(18,0),Qt::LeftButton,Qt::LeftButton);
        mouse(canvas,QEvent::MouseMove,*caption+QPointF(48,0),Qt::NoButton,Qt::LeftButton);mouse(canvas,QEvent::MouseButtonRelease,*caption+QPointF(48,0),Qt::LeftButton,Qt::NoButton);
        require(!state.sheets.front().views.front().caption_position,"Text body drag bypassed its manipulation point");
        click(canvas,*caption);require(!state.sheets.front().views.front().caption_position,"Selecting a caption changed its automatic placement");
        const auto selected_caption=canvas->grab().toImage();
        const auto caption_point=selected_caption.pixelColor((*caption*selected_caption.devicePixelRatio()).toPoint());
        require(caption_point.red()>150&&caption_point.blue()>200&&caption_point.green()<150,"Selected text manipulation point is not a solid purple dot");
        mouse(canvas,QEvent::MouseButtonPress,*caption,Qt::LeftButton,Qt::LeftButton);
        mouse(canvas,QEvent::MouseMove,*caption+QPointF(60,-25),Qt::NoButton,Qt::LeftButton);
        mouse(canvas,QEvent::MouseButtonRelease,*caption+QPointF(60,-25),Qt::LeftButton,Qt::NoButton);
        require(state.sheets.front().views.front().caption_position.has_value(),"Caption drag was not saved");
        require(std::abs(window.view_label_center_for_test(original.id)->x()-caption->x()-60)<1e-6&&std::abs(window.view_label_center_for_test(original.id)->y()-caption->y()+25)<1e-6,"Caption does not follow the mouse");
        require(state.sheets.front().views.front().x==original.x&&state.sheets.front().views.front().y==original.y,"Caption drag moved the model");
        // The middle of this wire rectangle is far from all four edges.
        click(canvas,QPointF(3,3));
        require(!action("editDrawingViewAction")->isEnabled(),"Empty click did not clear the selection");
        mouse(canvas,QEvent::MouseMove,center,Qt::NoButton,Qt::NoButton);
        click(canvas,center);
        require(action("editDrawingViewAction")->isEnabled(),"Click inside empty rectangle interior did not select its view");
        action("editDrawingViewAction")->trigger(); flush();
        require(dialog(),"Cannot edit selected view");
        const auto rectangle=window.view_rectangle_center_for_test(original.id);require(rectangle.has_value(),"Preview has no view rectangle");
        mouse(canvas,QEvent::MouseButtonPress,*rectangle,Qt::LeftButton,Qt::LeftButton);
        mouse(canvas,QEvent::MouseMove,*rectangle+QPointF(35,-25),Qt::NoButton,Qt::LeftButton);
        mouse(canvas,QEvent::MouseButtonRelease,*rectangle+QPointF(35,-25),Qt::LeftButton,Qt::NoButton);
        const auto moved_preview=window.view_rectangle_center_for_test(original.id);
        require(moved_preview&&std::abs(moved_preview->x()-rectangle->x()-35)<.02&&std::abs(moved_preview->y()-rectangle->y()+25)<.02,"View properties preview did not follow its rectangle drag");
        require(dialog()->findChild<QDoubleSpinBox*>("drawingViewX")->value()!=original.x&&state.sheets.front().views.front().x==original.x,"Preview drag did not update fields or committed prematurely");
        dialog()->findChild<QDoubleSpinBox*>("drawingViewX")->setValue(original.x+20);
        require(state.sheets.front().views.front().x==original.x,"Property preview changed the persisted position");
        dialog()->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click(); flush();
        require(state.sheets.front().views.front().x==original.x,"Cancel failed to restore position");
        require(std::abs(window.view_rectangle_center_for_test(original.id)->x()-rectangle->x())<.02&&std::abs(window.view_rectangle_center_for_test(original.id)->y()-rectangle->y())<.02,"Cancel retained the dragged preview position");
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
        auto* turn=properties->findChild<QPushButton*>("drawingRotateRight");require(turn&&turn->isEnabled(),"Root view has no relative rotation");turn->click();flush();
        require(state.sheets.front().views.front().camera.depth.y==original.camera.depth.y,"Rotation preview committed before OK");
        properties->findChild<QComboBox*>("drawingViewScaleMode")->setCurrentIndex(1);
        properties->findChild<QDoubleSpinBox*>("drawingViewScale")->setValue(0.5);
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); flush();
        require(std::abs(state.sheets.front().views.back().x-child.x-10)<1e-6,"Parent position edit left the projected view behind");
        require(std::abs(state.sheets.front().views.front().camera.depth.x+1)<1e-9&&std::abs(state.sheets.front().views.front().camera.depth.y)<1e-9,"Relative right rotation is not 90 degrees");
        require(std::abs(state.sheets.front().views.back().camera.depth.y+1)<1e-9&&std::abs(state.sheets.front().views.back().camera.depth.x)<1e-9,"Projected child did not follow the rotated parent camera");
        // Four quarter turns from an oblique view restore all axes. Cancel keeps the saved view.
        window.select_view_for_test(original.id);action("editDrawingViewAction")->trigger();flush();properties=dialog();
        properties->findChild<QComboBox*>("drawingViewOrientation")->setCurrentIndex(6);
        for(int i=0;i<4;++i)properties->findChild<QPushButton*>("drawingRotateRight")->click();
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        require(std::abs(state.sheets.front().views.front().camera.depth.x+1)<1e-9,"Cancel changed the saved camera");
        action("editDrawingSheetAction")->trigger(); flush();
        QDialog* sheet_properties{};
        for(auto* candidate:window.findChildren<QDialog*>()) if(candidate->isVisible()) sheet_properties=candidate;
        require(sheet_properties,"Sheet properties did not open");
        sheet_properties->findChild<QDoubleSpinBox*>()->setValue(0.25);
        mouse(canvas,QEvent::MouseButtonPress,right,Qt::MiddleButton,Qt::MiddleButton);
        mouse(canvas,QEvent::MouseButtonRelease,right,Qt::MiddleButton,Qt::NoButton);
        require(sheet_properties->isVisible(),"Short MMB committed sheet properties");
        mouse(canvas,QEvent::MouseButtonDblClick,right,Qt::MiddleButton,Qt::MiddleButton);flush();
        require(state.sheets.front().views.front().scale==0.5 && state.sheets.front().views.back().scale==0.25,
            "Sheet scale did not update inherited views independently of local scale");
        const auto directory=std::filesystem::current_path()/"Projects/test/drawing-ui";
        std::filesystem::create_directories(directory);
        const auto file=directory/"view-workflow.drwz";
        state.save(file); const auto reopened=zima::drawing::DrawingDocument::load(file);
        require(reopened.sheets.front().views.front().show_caption && !reopened.sheets.front().views.front().use_sheet_scale &&
            reopened.sheets.front().views.front().scale==0.5,"View caption/local scale did not survive reopening");
        require(reopened.sheets.front().views.front().caption_position==state.sheets.front().views.front().caption_position,"Caption position did not survive reopening");
        require(std::abs(reopened.sheets.front().views.front().camera.depth.x+1)<1e-9,"Custom camera did not survive reopening");
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
        {
            auto model=workspace.open_part(part.document_id)->session.document();
            model.user_parameter_order={"stock","name","drawn_by","standard","revision"};
            model.user_parameter_labels["stock"]["cs"]="Polotovar";
            model.user_parameters["name"]="First component";
            model.user_parameter_values["name"][""]="First component";
            model.user_parameter_labels["name"]["cs"]="Název";
            model.user_parameters["drawn_by"]="Original author";
            model.user_parameter_values["drawn_by"][""]="Original author";
            model.user_parameter_labels["drawn_by"]["cs"]="Kreslil";
            workspace.open_part(part.document_id)->session.commit(model,{cache});
            const auto revision=workspace.open_part(part.document_id)->session.revision();
            const auto library=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/formats/ZE-RAZITKO.tblz";
            window.load_title_block_for_test(library);flush();
            require(!state.sheets.front().title_block_fields.empty(),"Library title block did not insert");
            auto* edit=action("editDrawingTitleBlockAction");
            const auto field_center=window.title_field_center_for_test("DRAWN_BY");require(field_center.has_value(),"Title field has no rendered hit area");
            mouse(canvas,QEvent::MouseMove,*field_center,Qt::NoButton,Qt::NoButton);
            click(canvas,*field_center);
            mouse(canvas,QEvent::MouseButtonDblClick,*field_center,Qt::LeftButton,Qt::LeftButton);flush();
            auto* d=window.findChild<QDialog*>("drawingTitleBlockProperties");require(d,"Title block dialog missing");
            auto* author=d->findChild<QLineEdit*>("titleBlockField:DRAWN_BY");
            require(author && author->text()=="Original author"&&!author->isReadOnly(),"Title block did not read model Parameters");
            const auto editor_y=[](QDialog* props,const char* id){auto* editor=props->findChild<QLineEdit*>(id);require(editor,"Missing ordered title editor");return editor->mapTo(props,QPoint{}).y();};
            require(editor_y(d,"titleBlockField:parameter:Polotovar")<editor_y(d,"titleBlockField:NAME")&&editor_y(d,"titleBlockField:NAME")<editor_y(d,"titleBlockField:DRAWN_BY"),"Title block ignored source Parameters order");
            window.grab().save(QString::fromStdString((directory/"drawing-title-properties.png").string()));
            author->setText("Discard");d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
            require(workspace.open_part(part.document_id)->session.revision()==revision,"Title block Cancel changed model");
            edit->trigger();flush();d=window.findChild<QDialog*>("drawingTitleBlockProperties");
            d->findChild<QLineEdit*>("titleBlockField:DRAWN_BY")->setText("New author");
            d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            require(workspace.open_part(part.document_id)->session.document().user_parameter_values.at("drawn_by").at("")=="New author", "Title block did not write back to model Parameters");
            require(workspace.open_part(part.document_id)->session.revision()==revision+1,"Title block must create one source revision");
            workspace.open_part(part.document_id)->session.undo();
            require(workspace.open_part(part.document_id)->session.document().user_parameters.at("drawn_by")=="Original author","Source Undo failed after title-block edit");
            auto assembly=zima::assembly::AssemblyDocument::create_default();
            assembly.user_parameters=model.user_parameters;assembly.user_parameter_labels=model.user_parameter_labels;assembly.user_parameter_values=model.user_parameter_values;
            auto second=zima::document::PartDocument::create_default();
            second.user_parameter_order={"drawn_by","name","stock"};
            second.user_parameter_labels=model.user_parameter_labels;
            second.user_parameters["name"]="Second component";
            second.user_parameter_values["name"][""]="Second component";
            second.user_parameter_labels["name"]["cs"]="Název";
            workspace.add_part(second,{cache},"second.prtz");
            auto repeated=zima::assembly::AssemblyDocument::create_part_occurrence("First",part.document_id,"source.prtz",cache);
            assembly.components.push_back(repeated);
            repeated.occurrence_id="repeated-hidden";repeated.visible=false;assembly.components.push_back(repeated);
            assembly.components.push_back(zima::assembly::AssemblyDocument::create_part_occurrence("Second",second.document_id,"second.prtz",cache));
            repeated.occurrence_id="suppressed-tool";repeated.suppressed=true;assembly.components.push_back(repeated);
            workspace.add_assembly(assembly,std::filesystem::absolute(directory/"source.asmz"));
            auto assembly_drawing=zima::drawing::DrawingDocument::create_default();
            assembly_drawing.source_path="source.asmz";
            workspace.add_drawing(assembly_drawing,std::filesystem::absolute(directory/"source.drwz"));
            window.edit_workspace_document(assembly_drawing.document_id);
            window.load_title_block_for_test(library);edit->trigger();flush();
            d=window.findChild<QDialog*>("drawingTitleBlockProperties");require(d,"Assembly title block missing");
            require(d->findChild<QLineEdit*>("titleBlockField:DRAWN_BY")->text()=="Original author","Assembly Parameters were not read before view insertion");
            d->findChild<QLineEdit*>("titleBlockField:DRAWN_BY")->setText("Assembly author");
            d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            require(workspace.open_assembly(assembly.document_id)->session.document().user_parameters.at("drawn_by")=="Assembly author","Title block wrote to wrong source");
            action("insertDrawingViewAction")->trigger();click(canvas,canvas->rect().center());flush();
            require(dialog(),"Assembly view placement has no Properties");
            dialog()->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            const auto& bom_sheet=window.document_for_test().sheets.front();
            require(bom_sheet.bom_rows.size()==2 && bom_sheet.bom_rows[0].quantity==2 && bom_sheet.bom_rows[1].quantity==1,
                "Assembly BOM must group repeated/hidden components and omit suppressed components");
            zima::drawing::TitleBlockContext bom_context;
            const auto layout=zima::drawing::title_block_layout(bom_sheet,bom_context);
            const auto first=std::ranges::find(layout.texts,std::string("First component"),&zima::drawing::TemplateText::text);
            const auto second_row=std::ranges::find(layout.texts,std::string("Second component"),&zima::drawing::TemplateText::text);
            require(first!=layout.texts.end() && second_row!=layout.texts.end() &&
                std::abs(std::abs(first->position.y-second_row->position.y)-bom_sheet.repeat_regions.front().step)<1e-6,
                "Purple BOM region did not create separate rows with each Part's Parameters");
            window.grab().save(QString::fromStdString((directory/"assembly-bom-rows.png").string()));
            const auto row_center=window.title_field_center_for_test(second_row->field_id);
            require(row_center.has_value(),"Repeated BOM cell has no edit target");
            click(canvas,*row_center);mouse(canvas,QEvent::MouseButtonDblClick,*row_center,Qt::LeftButton,Qt::LeftButton);flush();
            d=window.findChild<QDialog*>("drawingTitleBlockProperties");require(d,"BOM row editor missing");
            auto* row_name=d->findChild<QLineEdit*>("titleBlockField:NAME");
            require(d->findChild<QLineEdit*>("titleBlockField:DRAWN_BY")&&d->findChild<QLineEdit*>("titleBlockField:parameter:Polotovar"),"BOM cell did not open the complete title-block table");
            require(row_name&&row_name->selectedText()==row_name->text(),"BOM cell did not focus its parameter in the complete table");
            require(row_name&&!row_name->isReadOnly()&&row_name->text()=="Second component","BOM edit did not resolve its exact source Part");
            require(editor_y(d,"titleBlockField:DRAWN_BY")<editor_y(d,"titleBlockField:NAME")&&editor_y(d,"titleBlockField:NAME")<editor_y(d,"titleBlockField:parameter:Polotovar"),"BOM table used parent order instead of its source Part order");
            row_name->setText("Second revised");d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            require(workspace.open_part(second.document_id)->session.document().user_parameters.at("name")=="Second revised","BOM name did not write back to its Part");
            require(workspace.open_part(part.document_id)->session.document().user_parameters.at("name")=="First component"&&workspace.open_assembly(assembly.document_id)->session.document().user_parameters.at("name")=="First component","BOM edit modified another Part or its parent Assembly");
            require(window.document_for_test().sheets.front().bom_rows[1].parameters.at("name")=="Second revised"&&window.document_for_test().sheets.front().bom_rows[0].quantity==2,"BOM metadata or quantity failed to refresh");
        }
        {
        // A tilted foreground face can have a smaller mean depth and must
        // still cover the flat face at the near end, in either input order.
        zima::drawing::DrawingView depth_view;
        zima::drawing::ProjectedTriangle tilted,flat;
        tilted.points=flat.points={zima::drawing::Point2{0,0},{10,0},{0,10}};
        tilted.vertex_depths={10,-10,-10};tilted.light=1;
        flat.vertex_depths={0,0,0};flat.light=0.5;
        depth_view.projected_triangles={tilted,flat};
        const auto depth_image=zima::app::drawing_shaded_fill(depth_view,QRectF(0,0,10,10),10);
        require(qRed(depth_image.pixel(5,94))==185&&qRed(depth_image.pixel(60,89))==92,
            "Shaded fill used triangle mean depth instead of local visibility");
        std::reverse(depth_view.projected_triangles.begin(),depth_view.projected_triangles.end());
        require(zima::app::drawing_shaded_fill(depth_view,QRectF(0,0,10,10),10)==depth_image,
            "Shaded visibility depends on triangle ordering");
        // Export the same persisted view styles to a physical-size multipage PDF.
        auto print_document=zima::drawing::DrawingDocument::create_default();
        auto& print_sheet=print_document.sheets.front();
        print_sheet.thick_line_mm=0.5;print_sheet.thin_line_mm=0.25;
        for(int i=0;i<4;++i) {
            const auto pen=std::array{zima::drawing::DrawingPen::White,zima::drawing::DrawingPen::Red,zima::drawing::DrawingPen::Yellow,zima::drawing::DrawingPen::Green}[i];
            print_sheet.title_block_lines.push_back({{130,78.0-i*6},{80,78.0-i*6},pen});
        }
        zima::kernel::OcctKernel print_kernel;
        const auto bodies=print_kernel.evaluate_history({{"print-cylinder",zima::kernel::CylinderRequest{10,20},zima::kernel::BooleanOperation::Add}});
        const auto& cylinder=bodies.back().mesh;
        auto print_source=zima::document::PartDocument::create_default();
        workspace.add_part(print_source,bodies);
        const auto outline=zima::drawing::project_edges(cylinder,zima::drawing::ViewOrientation::Front);
        int generators=0;
        for(const auto& edge:outline)if(edge.silhouette&&!edge.hidden)for(std::size_t i=1;i<edge.points.size();++i)
            if(std::abs(edge.points[i].y-edge.points[i-1].y)>19.9)++generators;
        require(generators==2,"Calculated cylinder lost its two silhouette generators");
        for(const double y:{0.0,20.0})for(int sample=-99;sample<=99;++sample) {
            const double x=sample/10.0;
            const bool covered=std::ranges::any_of(outline,[&](const auto& edge) {
                if(edge.hidden)return false;
                for(std::size_t i=1;i<edge.points.size();++i)if(std::abs(edge.points[i-1].y-y)<1e-6&&std::abs(edge.points[i].y-y)<1e-6&&
                    x>=std::min(edge.points[i-1].x,edge.points[i].x)-1e-6&&x<=std::max(edge.points[i-1].x,edge.points[i].x)+1e-6)return true;
                return false;
            });
            require(covered,"Calculated cylinder rim contains visible gaps");
        }
        for(int i=0;i<4;++i) {
            auto view=zima::drawing::DrawingDocument::create_view(print_source.document_id, {},cylinder,
                i==0?zima::drawing::ViewOrientation::Front:zima::drawing::ViewOrientation::Isometric);
            view.display_style=static_cast<zima::drawing::DisplayStyle>(i);
            view.x=i%2?60:150;view.y=i<2?220:120;view.scale=2;
            view.show_caption=true;view.name=std::array{"Visible", "Hidden dashed", "Shaded + edges", "Shaded"}[i];
            print_sheet.views.push_back(view);
        }
        const auto box=print_kernel.evaluate_history({{"tangent-box",zima::kernel::BoxRequest{30,25,20},zima::kernel::BooleanOperation::Add}});
        const auto selected_edge=std::ranges::find_if(box.back().mesh.edges,[](const auto& edge){return edge.reference.valid()&&!edge.parameter_seam;});
        require(selected_edge!=box.back().mesh.edges.end(),"Box has no fillet selection");
        const auto rounded=print_kernel.evaluate_history({
            {"tangent-box",zima::kernel::BoxRequest{30,25,20},zima::kernel::BooleanOperation::Add},
            {"tangent-fillet",zima::kernel::FilletRequest{{selected_edge->reference},3},zima::kernel::BooleanOperation::Add}});
        auto rounded_source=zima::document::PartDocument::create_default();workspace.add_part(rounded_source,rounded);
        auto tangent_view=zima::drawing::DrawingDocument::create_view(rounded_source.document_id, {},rounded.back().mesh,zima::drawing::ViewOrientation::Isometric);
        require(std::ranges::any_of(tangent_view.projected_edges,[](const auto& edge){return edge.tangent;}),"Actual OCCT fillet lost tangent boundary classification");
        const auto box_edges=zima::drawing::project_edges(box.back().mesh,zima::drawing::ViewOrientation::Isometric);
        require(std::ranges::none_of(box_edges,[](const auto& edge){return edge.tangent;}),"Sharp box edges were classified as tangent");
        for(int i=0;i<3;++i) {
            auto view=tangent_view;view.id="tangent-example-"+std::to_string(i);view.x=165-65*i;view.y=33;view.scale=0.8;
            view.tangent_edge_style=static_cast<zima::drawing::TangentEdgeStyle>(i);view.display_style=zima::drawing::DisplayStyle::HiddenEdges;
            view.show_caption=true;view.name=std::array{"Tangent thick","Tangent thin","Tangent off"}[i];print_sheet.views.push_back(view);
        }
        auto second_sheet=print_sheet;second_sheet.id="print-second";second_sheet.format=zima::drawing::SheetFormat::A3;
        for(auto& view:second_sheet.views)view.id+="-second-sheet";
        second_sheet.views[1].hidden_edge_style=zima::drawing::HiddenEdgeStyle::Gray;second_sheet.views[1].name="Hidden gray";
        print_document.sheets.push_back(second_sheet);
        workspace.add_drawing(print_document);window.edit_workspace_document(print_document.document_id);flush();
        window.export_pdf(directory/"view-styles.pdf");
        require(std::filesystem::file_size(directory/"view-styles.pdf")>1000,"PDF output is empty");
        print_document.save(directory/"view-styles.drwz");
        const auto reopened=zima::drawing::DrawingDocument::load(directory/"view-styles.drwz");
        require(reopened.sheets.size()==2&&reopened.sheets[1].views[1].hidden_edge_style==zima::drawing::HiddenEdgeStyle::Gray&&
            reopened.sheets[0].views[3].display_style==zima::drawing::DisplayStyle::Shaded&&reopened.sheets[0].thick_line_mm==0.5,
            "Drawing view/print settings did not persist");
        window.grab().save(QString::fromStdString((directory/"view-styles.png").string()));
        }
        {
            auto source=zima::document::PartDocument::create_default();auto box=zima::document::PartDocument::create_box_container();source.history={box};
            zima::document::BodyHistoryGraph graph;const auto body=graph.create_body("Source");graph.insert({zima::document::PartHistoryKind::Feature,box.id});graph.activate({});source.set_body_history(graph);
            zima::kernel::OcctKernel kernel;const auto calculated=kernel.evaluate_history(source.kernel_operations());
            const auto source_path=directory/"linked-source.prtz";source.save(source_path,calculated);workspace.add_part(source,calculated,source_path);
            auto drawing=zima::drawing::DrawingDocument::create_default();drawing.source_document_id=source.document_id;drawing.source_path=source_path;
            for(int i=0;i<2;++i) {
                if(i) {auto sheet=zima::drawing::DrawingDocument::create_default().sheets.front();drawing.sheets.push_back(sheet);}
                for(int j=0;j<2;++j)drawing.sheets[i].views.push_back(zima::drawing::DrawingDocument::create_view(source.document_id,source_path,calculated.back().mesh,static_cast<zima::drawing::ViewOrientation>(j)));
            }
            workspace.add_drawing(drawing);window.edit_workspace_document(drawing.document_id);window.select_view({});flush();
            auto* regenerate=window.findChild<QAction*>("regenerateDrawingViewAction");require(regenerate&&regenerate->isEnabled(),"Drawing regeneration requires a selected view");
            auto* part=workspace.open_part(source.document_id);auto deleted=part->session.document();deleted.erase_history_object(body);part->session.commit(deleted,{});
            regenerate->trigger();flush();
            for(const auto& sheet:window.document_for_test().sheets)for(const auto& view:sheet.views)
                require(view.projected_edges.empty()&&view.projected_triangles.empty(),"Regenerate retained deleted geometry or used the stale saved Part");
            part->session.undo();regenerate->trigger();flush();
            for(const auto& sheet:window.document_for_test().sheets)for(const auto& view:sheet.views)
                require(!view.projected_triangles.empty(),"Undo source followed by Regenerate did not restore all linked views");
        }
        {
            // Regeneration pulls changed cuts from memory, but derives camera
            // orientation only from the view's projection tree, never the cut.
            auto source=zima::document::PartDocument::create_default();auto box=zima::document::PartDocument::create_box_container();source.history={box};
            zima::document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Projection source"));graph.insert({zima::document::PartHistoryKind::Feature,box.id});graph.activate({});source.set_body_history(graph);
            zima::kernel::OcctKernel kernel;const auto calculated=kernel.evaluate_history(source.kernel_operations());
            auto section=zima::document::create_section();static_cast<void>(section.sketch.add_segment(-20,0,20,0.185847));source.sections={section};
            const auto source_path=directory/"projection-source.prtz";source.save(source_path,calculated);workspace.add_part(source,calculated,source_path);auto fixture=zima::drawing::DrawingDocument::create_default();fixture.sheets.clear();
            for(const auto method:{zima::drawing::ProjectionMethod::FirstAngle,zima::drawing::ProjectionMethod::ThirdAngle}) {
                auto sheet=zima::drawing::DrawingDocument::create_default().sheets.front();sheet.projection_method=method;
                auto root=zima::drawing::DrawingDocument::create_view(source.document_id,source_path,calculated.back().mesh,zima::drawing::ViewOrientation::Front);root.section_id=section.id;root.section_snapshot=section;
                auto child=zima::drawing::DrawingDocument::create_view(source.document_id,source_path,calculated.back().mesh,zima::drawing::ViewOrientation::Isometric);child.parent_view_id=root.id;child.projection_direction=zima::drawing::ProjectionDirection::Left;
                auto nested=zima::drawing::DrawingDocument::create_view(source.document_id,source_path,calculated.back().mesh,zima::drawing::ViewOrientation::Isometric);nested.parent_view_id=child.id;nested.projection_direction=zima::drawing::ProjectionDirection::Top;
                sheet.views={nested,child,root};fixture.sheets.push_back(sheet);
            }
            workspace.add_drawing(fixture);window.edit_workspace_document(fixture.document_id);flush();
            const auto stale_camera=fixture.sheets.front().views.front().camera;
            require(window.document_for_test().sheets.front().views.front().camera.depth==stale_camera.depth,"Activation implicitly regenerated a projection camera");
            auto* opened=workspace.open_part(source.document_id);auto changed=opened->session.document();changed.sections.front().sketch.points.back().y=6;opened->session.commit(changed,calculated);
            const auto source_revision=opened->session.revision();action("regenerateDrawingViewAction")->trigger();flush();
            for(const auto& sheet:window.document_for_test().sheets) {
                const auto& root=sheet.views.back();const auto front=zima::drawing::standard_camera(zima::drawing::ViewOrientation::Front);
                require(root.camera.horizontal==front.horizontal&&root.camera.vertical==front.vertical&&root.camera.depth==front.depth,"Changed Section tilted the primary view");
                require(root.section_snapshot->sketch.points.back().y==6,"Regenerate missed the unsaved cut change");
                for(const auto& view:sheet.views)if(!view.parent_view_id.empty()) {
                    const auto* parent=window.document_for_test().find_view(view.parent_view_id);const auto expected=zima::drawing::projected_camera(parent->camera,view.projection_direction,sheet.projection_method);
                    require(view.camera.horizontal==expected.horizontal&&view.camera.vertical==expected.vertical&&view.camera.depth==expected.depth,"Regenerate did not update nested projection cameras parent-first");
                    const auto& a=parent->camera.depth;const auto& b=view.camera.depth;require(std::abs(a.x*b.x+a.y*b.y+a.z*b.z)<1e-12,"Projected view is not perpendicular to its parent");
                    const auto edges=zima::drawing::project_edges(calculated.back().mesh,expected);
                    require(view.projected_edges.size()==edges.size()&&!edges.empty()&&view.projected_edges.front().points==edges.front().points,"Projected geometry uses a stale camera");
                }
            }
            require(opened->session.revision()==source_revision,"Drawing regeneration modified the source Part");
            window.document_for_test().save(directory/"section-independent-camera.drwz");
            const auto reopened=zima::drawing::DrawingDocument::load(directory/"section-independent-camera.drwz");
            require(reopened.sheets.front().views.back().camera.depth==zima::drawing::standard_camera(zima::drawing::ViewOrientation::Front).depth,"Fixed Section camera did not survive reopening");
        }
        {
            // A right-hand projected view must offer its cutting trace even
            // when both endpoints of the Section sketch project to one point.
            zima::kernel::OcctKernel kernel;const auto box=kernel.evaluate_history({{"side-trace-box",zima::kernel::BoxRequest{30,20,40},zima::kernel::BooleanOperation::Add}});
            auto fixture=zima::drawing::DrawingDocument::create_default();auto root=zima::drawing::DrawingDocument::create_view(part.document_id,"source.prtz",box.back().mesh,zima::drawing::ViewOrientation::Front);root.x=150;root.y=150;
            auto side=zima::drawing::DrawingDocument::create_view(part.document_id,"source.prtz",box.back().mesh,zima::drawing::ViewOrientation::Left);side.parent_view_id=root.id;side.projection_direction=zima::drawing::ProjectionDirection::Right;side.x=65;side.y=150;
            side.camera=zima::drawing::projected_camera(root.camera,side.projection_direction,fixture.sheets.front().projection_method);side.projected_edges=zima::drawing::project_edges(box.back().mesh,side.camera);side.projected_triangles=zima::drawing::project_triangles(box.back().mesh,side.camera);
            auto section=zima::document::create_section();static_cast<void>(section.sketch.add_segment(-10,0,10,0));side.section_markers={section};fixture.sheets.front().views={root,side};workspace.add_drawing(fixture);window.edit_workspace_document(fixture.document_id);flush();
            const auto point=window.annotation_handle_for_test(section.id);require(point.has_value(),"Side projected trace has no visible manipulation handle");
            mouse(canvas,QEvent::MouseButtonPress,*point,Qt::LeftButton,Qt::LeftButton);mouse(canvas,QEvent::MouseMove,*point+QPointF(0,20),Qt::NoButton,Qt::LeftButton);mouse(canvas,QEvent::MouseButtonRelease,*point+QPointF(0,20),Qt::LeftButton,Qt::NoButton);
            const auto moved=window.annotation_handle_for_test(section.id);require(moved&&std::abs(moved->y()-point->y()-20)<1e-6,"Side trace handle did not follow a vertical drag");
            window.document_for_test().save(directory/"side-trace.drwz");window.export_pdf(directory/"side-trace.pdf");window.grab().save(QString::fromStdString((directory/"side-trace.png").string()));
            require(zima::drawing::DrawingDocument::load(directory/"side-trace.drwz").find_view(side.id)->section_marker_offsets.contains(section.id),"Side trace position did not persist");
        }
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_SECTION_CAMERA_DRAWING")) {
            // Explicit diagnostic: repair only the selected drawing view's old
            // section-driven camera in a new output copy; never change its Part.
            const auto path=std::filesystem::path(qEnvironmentVariable("ZIMA_VERIFY_SECTION_CAMERA_DRAWING").toStdString());
            auto drawing=zima::drawing::DrawingDocument::load(path);
            std::vector<zima::kernel::BodyResult> calculated;auto source=zima::document::PartDocument::load(drawing.source_path,&calculated);
            require(!calculated.empty(),"Section camera fixture has no calculated Part");
            for(auto& sheet:drawing.sheets)for(auto& view:sheet.views)if(view.parent_view_id.empty()&&!view.section_id.empty())view.camera=zima::drawing::standard_camera(view.orientation);
            workspace.add_part(source,calculated,drawing.source_path);workspace.add_drawing(drawing,path);window.edit_workspace_document(drawing.document_id);window.select_view({});flush();
            action("regenerateDrawingViewAction")->trigger();flush();
            for(const auto& sheet:window.document_for_test().sheets)for(const auto& view:sheet.views) {
                if(view.parent_view_id.empty()&&!view.section_id.empty())require(view.camera.depth==zima::drawing::standard_camera(view.orientation).depth,"Saved Section camera was not restored");
                if(!view.parent_view_id.empty()) {const auto* parent=window.document_for_test().find_view(view.parent_view_id);const auto expected=zima::drawing::projected_camera(parent->camera,view.projection_direction,sheet.projection_method);require(view.camera.horizontal==expected.horizontal&&view.camera.vertical==expected.vertical&&view.camera.depth==expected.depth,"Saved projected view retained its Section tilt");}
            }
            window.document_for_test().save(directory/"section-camera-fixed.drwz");window.export_pdf(directory/"section-camera-fixed.pdf");window.grab().save(QString::fromStdString((directory/"section-camera-fixed.png").string()));
            std::cout<<"Saved Section camera restored in output copy, source Part unchanged\n";
            // Every field opens the same complete table, including cells inside
            // the BOM rectangle and the ordinary NAME field outside it.
            std::set<QString> title_editors;
            const auto complete_table=[&](QDialog* props){
                std::set<QString> names;for(auto* editor:props->findChildren<QLineEdit*>())if(editor->objectName().startsWith("titleBlockField:"))names.insert(editor->objectName());
                require(names.contains("titleBlockField:NAME")&&names.contains("titleBlockField:DRAWN_BY")&&names.contains("titleBlockField:parameter:Polotovar")&&names.contains("titleBlockField:parameter:Norma")&&names.contains("titleBlockField:parameter:Verze"),"Title-block table is incomplete");
                if(title_editors.empty())title_editors=names;else require(names==title_editors,"Clicked field changed the title-block editor contents");
            };
            const auto name_center=window.title_field_center_for_test("NAME");require(name_center.has_value(),"User title block has no ordinary NAME field");
            click(canvas,*name_center);mouse(canvas,QEvent::MouseButtonDblClick,*name_center,Qt::LeftButton,Qt::LeftButton);flush();
            auto* name_props=window.findChild<QDialog*>("drawingTitleBlockProperties");require(name_props,"Cannot open ordinary title-block NAME");complete_table(name_props);
            auto* name_editor=name_props->findChild<QLineEdit*>("titleBlockField:NAME");require(name_editor&&name_editor->selectedText()==name_editor->text(),"Ordinary NAME field was not focused");
            name_props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
            // Exercise the user's exact cells without writing the source file.
            for(const auto& [token,key,value]:std::vector<std::tuple<std::string,std::string,std::string>>{
                    {"Polotovar","stock","40X50-459"},{"Název","name","TEST TŘMEN"},{"Norma","standard","TEST STANDARD"},{"Verze","revision","01"}}){
                zima::drawing::TitleBlockContext context;
                const auto layout=zima::drawing::title_block_layout(window.document_for_test().sheets.front(),context);
                const auto contains_token=[&](const auto& entry){const auto tokens=zima::drawing::title_block_tokens(entry.second.expression);return std::ranges::find(tokens,token)!=tokens.end();};
                auto selected=std::ranges::find_if(layout.edit_targets,[&](const auto& entry){return entry.second.bom_row&&contains_token(entry);});
                if(selected==layout.edit_targets.end())selected=std::ranges::find_if(layout.edit_targets,contains_token);
                require(selected!=layout.edit_targets.end(),"User title-block parameter has no target");
                const auto center=window.title_field_center_for_test(selected->first);require(center.has_value(),"User title-block parameter has no hit region");
                click(canvas,*center);mouse(canvas,QEvent::MouseButtonDblClick,*center,Qt::LeftButton,Qt::LeftButton);flush();
                auto* props=window.findChild<QDialog*>("drawingTitleBlockProperties");require(props,"Cannot edit user's title-block field");
                complete_table(props);
                const auto editor_id=token=="Název"?std::string("titleBlockField:NAME"):"titleBlockField:parameter:"+token;
                auto* field=props->findChild<QLineEdit*>(QString::fromStdString(editor_id));
                require(field&&!field->isReadOnly(),"User's parameter remains read-only");
                require(field->selectedText()==field->text(),"Clicked parameter was not focused in the complete table");
                const auto revision=workspace.open_part(source.document_id)->session.revision();field->setText("DISCARD");props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
                require(workspace.open_part(source.document_id)->session.revision()==revision,"Title field Cancel modified source");
                action("editDrawingTitleBlockAction")->trigger();flush();props=window.findChild<QDialog*>("drawingTitleBlockProperties");field=props->findChild<QLineEdit*>(QString::fromStdString(editor_id));
                require(field,"Field cannot be reopened");field->setText(QString::fromStdString(value));props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
                require(workspace.open_part(source.document_id)->session.document().user_parameter_values.at(key).at("")==value,"User title cell did not update source Parameters");
            }
            require(workspace.open_part(source.document_id)->session.document().user_parameters.at("mass")==source.user_parameters.at("mass"),"Title block edit changed calculated mass");
        }
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_LINKED_DRAWING")) {
            const auto path=std::filesystem::path(qEnvironmentVariable("ZIMA_VERIFY_LINKED_DRAWING").toStdString());
            auto drawing=zima::drawing::DrawingDocument::load(path);
            std::vector<zima::kernel::BodyResult> calculated;auto part=zima::document::PartDocument::load(drawing.source_path,&calculated);
            require(part.body_history.bodies().size()==1&&!calculated.empty(),"Linked fixture must contain one surviving calculated body");
            auto graph=part.body_history;auto body=graph.bodies().front();body.visible=true;graph.update_body(body);graph.activate({});part.set_body_history(graph);
            workspace.add_part(part,calculated,drawing.source_path);workspace.add_drawing(drawing,path);window.edit_workspace_document(drawing.document_id);window.select_view({});flush();
            window.findChild<QAction*>("regenerateDrawingViewAction")->trigger();flush();
            for(const auto& sheet:window.document_for_test().sheets)for(const auto& view:sheet.views)
                require(view.projected_triangles.size()==calculated.back().mesh.triangles.size()/3,"Linked fixture did not refresh every view from its current Part");
            require(!workspace.open_part(part.document_id)->session.body_context_mesh().triangles.empty(),"Surviving fixture body is still invisible");
            part.save(directory/"linked-source-fixed.prtz",calculated);
            window.document_for_test().save(directory/"linked-drawing-fixed.drwz");
            window.export_pdf(directory/"linked-drawing-fixed.pdf");
            window.grab().save(QString::fromStdString((directory/"linked-drawing-fixed.png").string()));
            std::cout<<"Linked fixture refreshed from "<<drawing.source_path<<'\n';
        }
        {
            auto fixture=zima::drawing::DrawingDocument::create_default();auto v=zima::drawing::DrawingDocument::create_view(part.document_id,"source.prtz",cache.mesh,zima::drawing::ViewOrientation::Front);
            v.measurement_geometry=zima::drawing::share_measurement_geometry({{},{{{"profile","vertex:bottom",""},{0,0,-20}},{{"profile","vertex:top",""},{0,0,20}}}});
            auto d=zima::drawing::make_drawing_dimension(v.id);d.id="handle-dimension";
            d.attachments={{zima::drawing::DimensionAttachmentKind::Point,v.measurement_geometry->points[0].source},{zima::drawing::DimensionAttachmentKind::Point,v.measurement_geometry->points[1].source}};
            zima::drawing::place_drawing_dimension(v,d,0,{0,45});
            fixture.sheets.front().views={v};fixture.sheets.front().dimensions={d};workspace.add_drawing(fixture);window.edit_workspace_document(fixture.document_id);flush();
            const auto point=window.annotation_handle_for_test(d.id,0,true);require(point.has_value(),"Dimension has no manipulation point");
            mouse(canvas,QEvent::MouseMove,*point,Qt::NoButton,Qt::NoButton);click(canvas,*point);
            const auto selected_image=canvas->grab().toImage();const auto handle_pixel=(*point*selected_image.devicePixelRatio()).toPoint();
            const auto purple=selected_image.pixelColor(handle_pixel);require(purple.red()>150&&purple.blue()>200&&purple.green()<150,"Dimension handle is not purple");
            // A vertical normal-view dimension has upright text along the vertical support line, to its left.
            bool cyan_dimension=false;for(int y=-80;y<80;++y)for(int x=-40;x<-7;++x){const auto pixel=selected_image.pixelColor(handle_pixel+QPoint(x,y));cyan_dimension|=pixel.red()<60&&pixel.green()>150&&pixel.blue()>200;}
            require(cyan_dimension,"Selected dimension text is not cyan");
            mouse(canvas,QEvent::MouseButtonPress,*point,Qt::LeftButton,Qt::LeftButton);mouse(canvas,QEvent::MouseMove,*point+QPointF(30,20),Qt::NoButton,Qt::LeftButton);mouse(canvas,QEvent::MouseButtonRelease,*point+QPointF(30,20),Qt::LeftButton,Qt::NoButton);
            const auto moved=window.annotation_handle_for_test(d.id,0,true);require(moved&&std::abs(moved->x()-point->x()-30)<1e-6&&std::abs(moved->y()-point->y()-20)<1e-6,"Dimension handle moved opposite to the mouse");
        }
        {
            auto fixture=zima::drawing::DrawingDocument::create_default();auto root=zima::drawing::DrawingDocument::create_view(part.document_id,"source.prtz",cache.mesh,zima::drawing::ViewOrientation::Front);root.x=100;root.y=145;
            auto child=zima::drawing::DrawingDocument::create_view(part.document_id,"source.prtz",cache.mesh);child.parent_view_id=root.id;child.projection_direction=zima::drawing::ProjectionDirection::Right;child.x=40;child.y=145;
            fixture.sheets.front().views={root,child};workspace.add_drawing(fixture);window.edit_workspace_document(fixture.document_id);window.select_view(root.id);action("editDrawingViewAction")->trigger();flush();
            const auto start=*window.view_rectangle_center_for_test(root.id);
            const auto drag=[&](QPointF from,QPointF delta){mouse(canvas,QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);mouse(canvas,QEvent::MouseMove,from+delta,Qt::NoButton,Qt::LeftButton);mouse(canvas,QEvent::MouseButtonRelease,from+delta,Qt::LeftButton,Qt::NoButton);};
            drag(start,{40,-10});auto* props=dialog();require(props,"Dragging preview closed its properties");
            const double x=props->findChild<QDoubleSpinBox*>("drawingViewX")->value(),y=props->findChild<QDoubleSpinBox*>("drawingViewY")->value();
            require(window.document_for_test().find_view(root.id)->x==root.x,"Live preview committed before OK");
            props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            require(window.document_for_test().find_view(root.id)->x==x&&window.document_for_test().find_view(root.id)->y==y,"OK did not save the dragged preview");
            const auto child_before=*window.document_for_test().find_view(child.id);require(std::abs(child_before.x-child.x-(x-root.x))<1e-9&&std::abs(child_before.y-y)<1e-9,"Committed preview left its projected child behind");
            window.select_view(child.id);action("editDrawingViewAction")->trigger();flush();drag(*window.view_rectangle_center_for_test(child.id),{30,25});props=dialog();
            require(std::abs(props->findChild<QDoubleSpinBox*>("drawingViewY")->value()-y)<.001&&props->findChild<QDoubleSpinBox*>("drawingViewX")->value()!=child_before.x,"Projected preview escaped its alignment ray or did not move");
            props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
            require(window.document_for_test().find_view(child.id)->x==child_before.x&&window.document_for_test().find_view(child.id)->y==child_before.y,"Projected preview Cancel changed its saved position");
        }
        {
            auto fixture=zima::drawing::DrawingDocument::create_default();
            zima::drawing::TitleBlockField empty;empty.id="EMPTY";empty.position={105,160};empty.height=5;empty.editable=true;empty.anchor_position=true;
            fixture.sheets.front().title_block_fields={empty};workspace.add_drawing(fixture);window.edit_workspace_document(fixture.document_id);flush();
            const auto center=window.title_field_center_for_test(empty.id);require(center.has_value(),"Empty text has no hit region");
            const auto image=canvas->grab().toImage();const auto pixel=(*center*image.devicePixelRatio()).toPoint();bool dash_visible=false;
            for(int y=-5;y<=5;++y)for(int x=-8;x<=8;++x){const auto c=image.pixelColor(pixel+QPoint(x,y));dash_visible|=c.green()>140&&c.red()<140&&c.blue()<120;}
            require(dash_visible,"Empty text placeholder is not visible");
            click(canvas,*center);mouse(canvas,QEvent::MouseButtonDblClick,*center,Qt::LeftButton,Qt::LeftButton);flush();
            auto* props=window.findChild<QDialog*>("drawingTitleBlockProperties");require(props,"Empty text cannot open its editor");
            auto* field=props->findChild<QLineEdit*>("titleBlockField:EMPTY");require(field&&field->text().isEmpty(),"Placeholder leaked into the text value");
            field->setText("Filled");props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            require(window.document_for_test().sheets.front().title_block_fields.front().value=="Filled","Empty text could not be filled");
            action("editDrawingTitleBlockAction")->trigger();flush();props=window.findChild<QDialog*>("drawingTitleBlockProperties");props->findChild<QLineEdit*>("titleBlockField:EMPTY")->clear();props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            window.document_for_test().save(directory/"empty-text.drwz");window.export_pdf(directory/"empty-text.pdf");
            require(zima::drawing::DrawingDocument::load(directory/"empty-text.drwz").sheets.front().title_block_fields.front().value.empty(),"Empty text did not persist as empty");
            window.grab().save(QString::fromStdString((directory/"empty-text.png").string()));
        }
        require(modal_error.isEmpty(),modal_error.toUtf8().constData());
        std::cout<<"Drawing placement, rectangular selection, projection, Cancel, MMB, persistence and global paths passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
