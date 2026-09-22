#include <zima/symbols/definition.hpp>
#include <zima/drawing_render/sheet_renderer.hpp>
#include <QDate>
#include <zima/drawing_render/pdf_export.hpp>
#include <zima/drawing_render/dxf_export.hpp>
#include "drawing_break_editor.hpp"
#include "application_settings.hpp"
#include <QSettings>
#include "drawing_annotation_layout.hpp"
#include "sketch_text_properties_dialog.hpp"
#include <QToolButton>
#include <QPlainTextEdit>
#include <QKeyEvent>
#include <QToolBar>
#include <fstream>
#include <zima/workspace/document_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/command_host/host.hpp>
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
#include <QSpinBox>
#include <QFileDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QTableWidget>
#include <QToolButton>
#include <QTemporaryDir>
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

int verify_drawing_source_picker() {
    try {
        QTemporaryDir temporary;require(temporary.isValid(),"Cannot create drawing source fixtures");
        const auto directory=std::filesystem::u8path(temporary.path().toStdString());
        auto part=zima::document::PartDocument::create_default();
        part.history.push_back(zima::document::PartDocument::create_box_container());part.resolve_constructions();
        zima::kernel::OcctKernel kernel;const auto calculated=kernel.evaluate_history(part.kernel_operations());
        const auto part_path=directory/"source.prtz";part.save(part_path,calculated);
        auto assembly=zima::assembly::AssemblyDocument::create_default();
        assembly.components.push_back(zima::assembly::AssemblyDocument::create_part_occurrence("Source",part.document_id,part_path,calculated.back()));
        const auto assembly_path=directory/"source.asmz";assembly.save(assembly_path);
        for(const auto& source_path:{part_path,assembly_path}) {
            zima::workspace::Workspace workspace;auto drawing=zima::drawing::DrawingDocument::create_default();
            workspace.add_drawing(drawing);workspace.activate(drawing.document_id);workspace.display_top_level(drawing.document_id);
            zima::app::DrawingWindow window(&workspace,false);window.edit_workspace_document(drawing.document_id);window.resize(1200,850);window.show();flush();
            auto* insert=window.findChild<QAction*>("insertDrawingViewAction");require(insert,"Insert View missing");
            auto* canvas=window.findChild<QWidget*>("drawingCanvas");require(canvas,"Drawing canvas missing");
            bool unexpected_picker=false;QTimer guard;
            QObject::connect(&guard,&QTimer::timeout,[&] {
                if(auto* picker=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {unexpected_picker=true;picker->reject();}
            });guard.start(10);
            insert->trigger();flush();
            require(!unexpected_picker&&window.document_for_test().sheets.front().views.empty(),"Unlinked Drawing must safely reject insertion without a file picker");
            drawing.source_path=source_path;
            drawing.source_document_id=source_path==part_path?part.document_id:assembly.document_id;
            workspace.open_drawing(drawing.document_id)->commit(drawing);window.edit_workspace_document(drawing.document_id);flush();
            for(const bool accept:{false,true}) {
                insert->trigger();flush();
                require(!unexpected_picker,"Insert View must directly start placement for a linked Drawing");
                click(canvas,canvas->rect().center());
                auto* properties=window.findChild<QDialog*>("drawingViewProperties");
                require(properties&&properties->isVisible(),"Insert View did not continue from placement to properties");
                properties->findChild<QDialogButtonBox*>()->button(accept?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();flush();
                require(window.document_for_test().sheets.front().views.size()==(accept?1u:0u),"View OK/Cancel did not preserve the insertion transaction");
            }
            insert->trigger();flush();
            click(canvas,canvas->rect().center()+QPoint(180,0));
            auto* front=window.findChild<QDialog*>("drawingViewProperties");
            require(front&&front->isVisible(),"Second independent Drawing view has no properties");
            front->findChild<QComboBox*>("drawingViewOrientation")->setCurrentIndex(0);
            front->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            const auto& views=window.document_for_test().sheets.front().views;
            require(views.size()==2&&views.front().orientation==zima::drawing::ViewOrientation::Isometric&&
                views.back().orientation==zima::drawing::ViewOrientation::Front,
                "Front view was rejected after an isometric view");
            require(workspace.documents().size()==1,"Drawing insertion opened extra model tabs");
        }
        {
            zima::workspace::Workspace workspace;
            auto family_part=part;
            family_part.name="Generic model";
            family_part.user_parameters["name"]="Generic parameter";
            family_part.user_parameter_values["name"][""]="Generic parameter";
            family_part.user_parameter_labels["name"]["cs"]="nazev";
            workspace.add_part(family_part,calculated,part_path);
            zima::document::FamilyTable table;table.instances.push_back({"Variant",{}});
            static_cast<void>(zima::workspace::set_family_table(workspace,family_part.document_id,table));
            const auto variant=zima::workspace::open_family_instance(
                workspace,kernel,family_part.document_id,"Variant",false);
            auto drawing=zima::drawing::DrawingDocument::create_default();
            drawing.source_document_id=family_part.document_id;drawing.source_path=part_path;
            drawing.sheets.front().bom_source_document_id=family_part.document_id;
            drawing.sheets.front().title_block_fields.push_back({
                .id="NAME",.expression="&nazev",.value="-"});
            drawing.sheets.front().views.push_back(zima::drawing::DrawingDocument::create_view(
                family_part.document_id,part_path,calculated.back().mesh,zima::drawing::ViewOrientation::Isometric));
            workspace.add_drawing(drawing,directory/"variants.drwz");
            workspace.activate(drawing.document_id);workspace.display_top_level(drawing.document_id);
            zima::app::DrawingWindow window(&workspace,false);window.edit_workspace_document(drawing.document_id);
            window.resize(1200,850);window.show();flush();
            auto* choices=window.findChild<QComboBox*>("drawingSourceVariant");
            auto* tabs=window.findChild<QTabBar*>("drawingSheetTabs");
            auto* add=window.findChild<QAction*>("addDrawingSheetAction");
            require(choices&&tabs&&add&&choices->count()==2,"Per-sheet Variant controls are unavailable");
            require(window.title_field_text_for_test("NAME")==std::optional<std::string>{"Generic parameter"},
                "Native Drawing title did not resolve the source Parameters name");
            const auto variant_index=choices->findData(QString::fromStdString(variant));
            require(variant_index>=0,"Drawing Variant list does not contain the Family Table row");
            choices->setCurrentIndex(variant_index);choices->activated(variant_index);flush();
            require(window.document_for_test().sheets.front().selected_source_document_id==variant&&
                window.document_for_test().sheets.front().views.front().source_document_id==family_part.document_id,
                "Sheet Variant selection changed an independent Drawing view");
            require(window.title_field_text_for_test("NAME")==std::optional<std::string>{"Generic parameter"},
                "Changing the chooser rebound the title block");
            window.load_title_block_for_test(std::filesystem::absolute("config/formats/ZE-TITLE-BLOCK-CS.tblz"));flush();
            require(window.document_for_test().sheets.front().bom_source_document_id==variant&&
                window.title_field_text_for_test("NAME")==std::optional<std::string>{"Variant"},"Inserted title did not capture the selected variant");
            const int native_index=choices->findData(QString::fromStdString(family_part.document_id));
            choices->setCurrentIndex(native_index);choices->activated(native_index);flush();
            require(window.title_field_text_for_test("NAME")==std::optional<std::string>{"Variant"}&&
                window.document_for_test().sheets.front().bom_source_document_id==variant,"Changing Source rebound an inserted title");
            window.document_for_test().save(directory/"bound-title.drwz");
            const auto bound=zima::drawing::DrawingDocument::load(directory/"bound-title.drwz");
            require(bound.sheets.front().bom_source_document_id==variant&&bound.sheets.front().selected_source_document_id==family_part.document_id,
                "Reopening collapsed the independent title and chooser identities");
            choices->setCurrentIndex(variant_index);choices->activated(variant_index);flush();
            add->trigger();flush();
            require(window.document_for_test().sheets.size()==2&&
                window.document_for_test().sheets.back().selected_source_document_id==variant,
                "New Drawing sheet did not inherit the active variant");
            const auto generic_index=choices->findData(QString::fromStdString(family_part.document_id));
            require(generic_index>=0,"Drawing Variant list does not contain the native model");
            choices->setCurrentIndex(generic_index);choices->activated(generic_index);flush();
            require(window.document_for_test().sheets.front().selected_source_document_id==variant&&
                window.document_for_test().sheets.back().selected_source_document_id==family_part.document_id,
                "Changing one Drawing sheet changed another sheet's variant");
            tabs->setCurrentIndex(0);flush();
            require(choices->currentData().toString().toStdString()==variant,
                "Drawing Variant dropdown did not restore the first sheet selection");
            tabs->setCurrentIndex(1);flush();
            require(choices->currentData().toString().toStdString()==family_part.document_id,
                "Drawing Variant dropdown did not restore the second sheet selection");
        }
        {
            zima::workspace::Workspace workspace;
            auto drawing=zima::drawing::DrawingDocument::create_default();
            drawing.add_data_source({part.document_id,part_path,"Part"});
            drawing.add_data_source({assembly.document_id,assembly_path,"Assembly"});
            auto first=zima::drawing::DrawingDocument::create_view(part.document_id,part_path,calculated.back().mesh);
            auto child=first;child.id+="-child";child.parent_view_id=first.id;
            auto independent=zima::drawing::DrawingDocument::create_view(assembly.document_id,assembly_path,calculated.back().mesh);
            drawing.sheets.front().views={first,child,independent};
            drawing.sheets.front().bom_source_document_id=part.document_id;
            drawing.sheets.front().selected_source_document_id=assembly.document_id;
            const auto path=directory/"settings.drwz";
            workspace.add_drawing(drawing,path);workspace.activate(drawing.document_id);workspace.display_top_level(drawing.document_id);
            zima::app::DrawingWindow window(&workspace,false);window.resize(1200,850);
            window.edit_workspace_document(drawing.document_id);window.show();flush();
            auto* settings=window.findChild<QToolButton*>("drawingSettingsButton");
            auto* choices=window.findChild<QComboBox*>("drawingSourceVariant");
            auto* canvas=window.findChild<QWidget*>("drawingCanvas");
            require(settings&&!settings->icon().isNull()&&choices->count()==2,"Drawing Settings controls missing");
            auto* state=workspace.open_drawing(drawing.document_id);const auto revision=state->revision();
            const auto open=[&] {
                settings->click();flush();auto* dialog=window.findChild<QDialog*>("drawingSettingsDialog");
                require(dialog&&dialog->isVisible()&&(dialog->windowFlags()&Qt::SubWindow),"Settings are not an internal properties window");
                return dialog;
            };
            const auto remove=[&](QDialog* dialog,int row,bool accept) {
                auto* table=dialog->findChild<QTableWidget*>("drawingDataSources");require(table,"Source table missing");
                auto* cross=table->cellWidget(row,0)->findChild<QPushButton*>();require(cross,"Source remove cross missing");
                cross->click();flush();auto* confirmation=window.findChild<QDialog*>("drawingSourceRemovalConfirmation");
                require(confirmation&&confirmation->isVisible()&&!dialog->isEnabled(),"Source removal bypassed confirmation");
                confirmation->findChild<QDialogButtonBox*>()->button(accept?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();flush();
                require(dialog->isEnabled(),"Confirmation left Settings disabled");
            };
            auto* dialog=open();window.grab().save("build/drawing-settings-preview.png");remove(dialog,0,false);
            require(dialog->findChild<QTableWidget*>()->rowCount()==3&&state->revision()==revision,"Cancelled removal changed sources");
            remove(dialog,0,true);
            require(dialog->findChild<QTableWidget*>()->rowCount()==2&&state->document().sheets.front().views.size()==3,"Pending removal leaked before OK");
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
            require(state->revision()==revision&&state->document().data_sources().size()==2,"Settings Cancel mutated history");
            dialog=open();remove(dialog,0,true);
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            require(state->document().sheets.front().views.size()==1&&state->document().sheets.front().bom_source_document_id.empty()&&
                window.selected_source_id()==assembly.document_id,"Source cascade removed unrelated views or rebound title");
            require(state->undo(),"Source removal has no Undo");window.edit_workspace_document(drawing.document_id);flush();
            require(state->document().sheets.front().views.size()==3&&choices->count()==2,"Undo did not restore sources and views");
            require(state->redo(),"Source removal has no Redo");window.edit_workspace_document(drawing.document_id);flush();
            dialog=open();remove(dialog,0,true);
            // The shared middle-double-click confirmation must also work over the sheet.
            mouse(canvas,QEvent::MouseButtonDblClick,canvas->rect().center(),Qt::MiddleButton,Qt::MiddleButton);
            require(state->document().data_sources().empty()&&state->document().sheets.front().views.empty()&&
                window.selected_source_id().empty()&&!choices->isEnabled()&&choices->currentData().toString().isEmpty(),"Last source left stale state");
            state->document().save(path);const auto empty=zima::drawing::DrawingDocument::load(path);
            require(empty.data_sources().empty()&&empty.source_document_id.empty(),"Empty sources did not survive reopen");
            dialog=open();auto* table=dialog->findChild<QTableWidget*>();
            QTimer pick;QObject::connect(&pick,&QTimer::timeout,[&] {
                if(auto* file=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                    file->selectFile(QString::fromStdWString(part_path.wstring()));QMetaObject::invokeMethod(file,"accept",Qt::DirectConnection);pick.stop();
                }
            });pick.start(20);
            table->cellClicked(0,1);flush();
            require(table->rowCount()==2,"Cannot add a source to an empty Drawing");
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            require(window.selected_source_id()==part.document_id&&state->document().sheets.front().views.empty()&&
                state->document().sheets.front().bom_source_document_id.empty(),"Adding a source recreated removed views or rebound title");
            state->document().save(path);require(zima::drawing::DrawingDocument::load(path).data_sources().size()==1,"Unused registered source was not persisted");
            auto missing=zima::drawing::DrawingDocument::create_default();
            missing.add_data_source({"missing-source",directory/"missing.asmz","Missing assembly"});
            missing.sheets.front().bom_source_document_id="missing-source";
            workspace.add_drawing(missing,directory/"missing-source.drwz");window.edit_workspace_document(missing.document_id);flush();
            require(choices->currentText().contains("nedostupný")&&window.selected_source_id()=="missing-source",
                "Missing file lost its registered source or prevented opening Drawing");
            dialog=open();remove(dialog,0,true);dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            require(window.document_for_test().data_sources().empty(),"A missing source cannot be removed through Settings");

        }
        std::cout<<"Drawing Insert View: source safety, Part/Assembly placement, OK/Cancel and isometric plus Front passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
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
        require(variants && variants->count()==1 && variants->currentText().startsWith("Drawing source"),
            "Source variant does not show the model name");
        window.grab();flush();
        const QPointF center=window.sheet_rectangle_for_test().center();
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
        require(properties->findChild<QLineEdit*>("drawingViewName")->text()==QString::fromUtf8("Pohled 1"),"First view has no numbered default name");
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
        require(canvas->property("annotationSnapActive").toBool(),"Caption guide snap feedback missing");
        mouse(canvas,QEvent::MouseButtonRelease,*caption+QPointF(60,-25),Qt::LeftButton,Qt::NoButton);
        require(state.sheets.front().views.front().caption_position.has_value(),"Caption drag was not saved");
        require(QLineF(*window.view_label_center_for_test(original.id),*caption+QPointF(60,-25)).length()<=6.01,"Caption does not follow the mouse within guide snap tolerance");
        require(state.sheets.front().views.front().x==original.x&&state.sheets.front().views.front().y==original.y,"Caption drag moved the model");
        {
            zima::kernel::OcctKernel label_kernel;auto label_directory=std::filesystem::current_path();zima::command_host::Host host(workspace,label_kernel,label_directory);
            const auto run=[&](const char* name,zima::commands::Json args=zima::commands::Json::object()) {
                auto result=host.execute({{"command",name},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result.data;
            };
            const auto moved_position=*state.sheets.front().views.front().caption_position;const auto moved_handle=*window.view_label_center_for_test(original.id);
            const auto query=run("drawing.view.labels.get",{{"view",original.id}});
            require(query.at("caption_position_mm")==zima::commands::Json::array({moved_position.x,moved_position.y}),"CLI query did not see the actual GUI label drag");
            run("drawing.view.labels.set",{{"view",original.id},{"values",{{"caption_position_mm",nullptr}}}});window.edit_workspace_document(drawing.document_id);flush();
            require(!state.sheets.front().views.front().caption_position&&QLineF(*window.view_label_center_for_test(original.id),*caption).length()<1e-6,"CLI automatic label reset did not restore its visible handle");
            run("undo");window.edit_workspace_document(drawing.document_id);flush();
            require(QLineF(*window.view_label_center_for_test(original.id),moved_handle).length()<1e-6,"CLI Undo did not restore the GUI label drag");
            run("redo");window.edit_workspace_document(drawing.document_id);flush();require(!state.sheets.front().views.front().caption_position,"CLI label reset Redo failed");
            run("undo");window.edit_workspace_document(drawing.document_id);flush();
        }
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
        require(child.name=="Pohled 2","Projected view has no numbered default name");
        require(child.parent_view_id==original.id && child.projection_direction==zima::drawing::ProjectionDirection::Right &&
            std::abs(child.y-original.y)<1e-6,"Projected view did not keep parent/ray placement");
        window.select_view_for_test(original.id); action("editDrawingViewAction")->trigger(); flush();
        properties=dialog(); properties->findChild<QDoubleSpinBox*>("drawingViewX")->setValue(original.x+10);
        auto* horizontal=properties->findChild<QDoubleSpinBox*>("drawingRotationHorizontal");
        auto* vertical=properties->findChild<QDoubleSpinBox*>("drawingRotationVertical");
        auto* roll=properties->findChild<QDoubleSpinBox*>("drawingRotationRoll");require(roll&&roll->singleStep()==1,"Missing in-plane rotation");
        require(horizontal&&vertical&&horizontal->singleStep()==1&&vertical->singleStep()==1,"View rotation has no degree inputs");
        horizontal->findChild<QLineEdit*>()->setText("17");horizontal->interpretText();
        vertical->findChild<QLineEdit*>()->setText("-23");vertical->interpretText();flush();
        require(horizontal->value()==17&&vertical->value()==-23,"Rotation input cannot accept typed degrees");
        require(std::abs(state.sheets.front().views.front().camera.depth.x-original.camera.depth.x)+std::abs(state.sheets.front().views.front().camera.depth.y-original.camera.depth.y)+std::abs(state.sheets.front().views.front().camera.depth.z-original.camera.depth.z)<1e-12,"Typed rotation committed before OK");
        roll->setValue(31);horizontal->setValue(0);vertical->setValue(0);roll->setValue(0);
        require(properties->findChild<QDoubleSpinBox*>("drawingGuideSpacing")->decimals()==3,"Guide spacing precision differs from offset");
        require(properties->findChild<QSpinBox*>("drawingGuideCount")!=nullptr,"Guide count control missing");
        properties->findChild<QSpinBox*>("drawingGuideCount")->setValue(7);
        properties->findChild<QDoubleSpinBox*>("drawingGuideSpacing")->setValue(1.234);
        require(properties->findChild<QDoubleSpinBox*>("drawingGuideOffset")->minimum()>0,"Snap offset permits nonpositive values");
        auto* turn=properties->findChild<QPushButton*>("drawingRotateRight");require(turn&&turn->isEnabled(),"Root view has no relative rotation");turn->click();flush();
        require(state.sheets.front().views.front().camera.depth.y==original.camera.depth.y,"Rotation preview committed before OK");
        properties->findChild<QComboBox*>("drawingViewScaleMode")->setCurrentIndex(1);
        properties->findChild<QDoubleSpinBox*>("drawingViewScale")->setValue(0.5);
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); flush();
        require(std::abs(state.sheets.front().views.back().x-child.x-10)<1e-6,"Parent position edit left the projected view behind");
        require(std::abs(state.sheets.front().views.front().camera.depth.x+1)<1e-9&&std::abs(state.sheets.front().views.front().camera.depth.y)<1e-9,"Relative right rotation is not 90 degrees");
        require(std::abs(state.sheets.front().views.back().camera.depth.y+1)<1e-9&&std::abs(state.sheets.front().views.back().camera.depth.x)<1e-9,"Projected child did not follow the rotated parent camera");
        require(state.sheets.front().views.front().dimension_guide_count==7&&state.sheets.front().views.front().dimension_guide_spacing==1.234,"Guide settings did not commit");
        // Four quarter turns from an oblique view restore all axes. Cancel keeps the saved view.
        window.select_view_for_test(original.id);action("editDrawingViewAction")->trigger();flush();properties=dialog();
        require(properties->findChild<QSpinBox*>("drawingGuideCount")->value()==7,"Guide count lost on dialog reopen");
        properties->findChild<QSpinBox*>("drawingGuideCount")->setValue(2);
        properties->findChild<QComboBox*>("drawingViewOrientation")->setCurrentIndex(6);
        for(int i=0;i<4;++i)properties->findChild<QPushButton*>("drawingRotateRight")->click();
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        require(state.sheets.front().views.front().dimension_guide_count==7,"Cancel committed guide count");
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_VIEW_CONTROLS_ONLY")) {
            auto seeded=workspace.open_drawing(drawing.document_id)->document();
            for(const auto& v:seeded.sheets.front().views) {
                auto dimension=zima::drawing::make_drawing_dimension(v.id);
                const auto& curve=v.measurement_geometry->curves.front();
                dimension.attachments={{zima::drawing::DimensionAttachmentKind::CurvePoint,curve.source,{},0},
                                       {zima::drawing::DimensionAttachmentKind::CurvePoint,curve.source,{},1}};
                zima::drawing::refresh_drawing_dimension(v,dimension);seeded.sheets.front().dimensions.push_back(dimension);
            }
            workspace.open_drawing(drawing.document_id)->commit(seeded);window.edit_workspace_document(drawing.document_id);flush();
            const auto original_dimensions=state.sheets.front().dimensions;
            window.select_view_for_test(original.id);action("editDrawingViewAction")->trigger();flush();properties=dialog();
            properties->findChild<QDoubleSpinBox*>("drawingRotationRoll")->setValue(23);
            require(state.sheets.front().dimensions==original_dimensions,"Rotation preview removed committed dimensions");
            properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
            require(state.sheets.front().dimensions==original_dimensions,"Rotation Cancel removed dimensions");
            const auto saved_camera=state.sheets.front().views.front().camera;
            window.select_view_for_test(original.id);action("editDrawingViewAction")->trigger();flush();properties=dialog();
            properties->findChild<QDoubleSpinBox*>("drawingRotationHorizontal")->setValue(17);
            properties->findChild<QDoubleSpinBox*>("drawingRotationVertical")->setValue(-23);
            properties->findChild<QDoubleSpinBox*>("drawingRotationRoll")->setValue(31);
            properties->grab().save("build/drawing-view-properties.png");
            properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            require(state.sheets.front().dimensions.empty(),"Committed rotation retained Drawing dimensions in parent or projected child");
            zima::kernel::OcctKernel history_kernel;auto history_path=std::filesystem::current_path();zima::command_host::Host history(workspace,history_kernel,history_path);
            require(history.execute({{"command","undo"}}).ok,"Rotation Undo failed");window.edit_workspace_document(drawing.document_id);flush();
            require(state.sheets.front().dimensions==original_dimensions,"Rotation Undo did not restore dimensions");
            require(history.execute({{"command","redo"}}).ok,"Rotation Redo failed");window.edit_workspace_document(drawing.document_id);flush();
            require(state.sheets.front().dimensions.empty(),"Rotation Redo retained dimensions");
            const auto expected=zima::drawing::rotated_camera(saved_camera,17,-23,31);
            const auto actual=state.sheets.front().views.front().camera;
            require(std::abs(actual.horizontal.x-expected.horizontal.x)+std::abs(actual.horizontal.y-expected.horizontal.y)+std::abs(actual.horizontal.z-expected.horizontal.z)<1e-9,"In-plane angle did not commit");
            require(std::abs(actual.depth.x-expected.depth.x)+std::abs(actual.depth.y-expected.depth.y)+std::abs(actual.depth.z-expected.depth.z)<1e-9,"Typed non-quarter rotation did not commit");
            window.select_view_for_test(original.id);action("editDrawingViewAction")->trigger();flush();properties=dialog();
            properties->findChild<QPushButton*>("drawingEditBreaks")->click();flush();
            auto* break_editor=dynamic_cast<zima::app::DrawingBreakEditor*>(window.findChild<QDialog*>("drawingBreakEditor"));require(break_editor&&break_editor->isVisible()&&!properties->isVisible(),"View Properties did not open isolated break editor");
            auto* break_canvas=dynamic_cast<zima::app::BreakEditorCanvas*>(break_editor->findChild<QWidget*>("drawingBreakCanvas"));break_canvas->grab();
            auto* break_table=break_editor->findChild<QTableWidget*>("drawingBreakTable");
            click(break_table->viewport(),break_table->visualItemRect(break_table->item(0,2)).center());click(break_canvas,{break_canvas->width()*.4,break_canvas->height()*.5});
            click(break_table->viewport(),break_table->visualItemRect(break_table->item(0,3)).center());click(break_canvas,{break_canvas->width()*.6,break_canvas->height()*.5});
            break_editor->buttons()->button(QDialogButtonBox::Ok)->click();flush();require(properties->isVisible()&&state.find_view(original.id)->breaks.empty(),"Editor committed outer View Properties transaction");
            properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();require(state.find_view(original.id)->breaks.size()==1,"View Properties did not commit break");
            std::cout<<"Drawing rotation and guide inputs, isolated break editor integration, preview, Cancel, OK and projected children passed\n";return 0;
        }

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
            model.user_parameter_labels["stock"]["cs"]="polotovar";
            model.user_parameters["name"]="First component";
            model.user_parameter_values["name"][""]="First component";
            model.user_parameter_labels["name"]["cs"]="nazev";
            model.user_parameters["drawn_by"]="Original author";
            model.user_parameter_values["drawn_by"][""]="Original author";
            model.user_parameter_labels["drawn_by"]["cs"]="kreslil";
            workspace.open_part(part.document_id)->session.commit(model,{cache});
            const auto revision=workspace.open_part(part.document_id)->session.revision();
            const auto library=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/formats/ZE-TITLE-BLOCK-CS.tblz";
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
            require(editor_y(d,"titleBlockField:parameter:polotovar")<editor_y(d,"titleBlockField:NAME")&&editor_y(d,"titleBlockField:NAME")<editor_y(d,"titleBlockField:DRAWN_BY"),"Title block ignored source Parameters order");
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
            assembly.user_parameter_order=model.user_parameter_order;assembly.user_parameters=model.user_parameters;assembly.user_parameter_labels=model.user_parameter_labels;assembly.user_parameter_values=model.user_parameter_values;
            auto second=zima::document::PartDocument::create_default();
            second.user_parameter_order={"drawn_by","name","stock"};
            second.user_parameter_labels=model.user_parameter_labels;
            second.user_parameters["name"]="Second component";
            second.user_parameter_values["name"][""]="Second component";
            second.user_parameter_labels["name"]["cs"]="nazev";
            workspace.add_part(second,{cache},"second.prtz");
            auto repeated=zima::assembly::AssemblyDocument::create_part_occurrence("First",part.document_id,"source.prtz",cache);
            assembly.components.push_back(repeated);
            repeated.occurrence_id="repeated-hidden";repeated.visible=false;assembly.components.push_back(repeated);
            assembly.components.push_back(zima::assembly::AssemblyDocument::create_part_occurrence("Second",second.document_id,"second.prtz",cache));
            repeated.occurrence_id="suppressed-tool";repeated.suppressed=true;assembly.components.push_back(repeated);
            workspace.add_assembly(assembly,std::filesystem::absolute(directory/"source.asmz"));
            auto assembly_drawing=zima::drawing::DrawingDocument::create_default();
            assembly_drawing.add_data_source({assembly.document_id,"source.asmz",assembly.name});
            workspace.add_drawing(assembly_drawing,std::filesystem::absolute(directory/"source.drwz"));
            window.edit_workspace_document(assembly_drawing.document_id);
            window.load_title_block_for_test(library);edit->trigger();flush();
            d=window.findChild<QDialog*>("drawingTitleBlockProperties");require(d,"Assembly title block missing");
            require(d->findChild<QLineEdit*>("titleBlockField:DRAWN_BY")->text()=="Original author","Assembly Parameters were not read before view insertion");
            d->findChild<QLineEdit*>("titleBlockField:DRAWN_BY")->setText("Assembly author");
            d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            if(auto* pending=window.findChild<QDialog*>("drawingTitleBlockProperties"))throw std::runtime_error(pending->findChild<QLabel*>("titleBlockError")->text().toStdString());
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
            require(d->findChild<QLineEdit*>("titleBlockField:DRAWN_BY")&&d->findChild<QLineEdit*>("titleBlockField:parameter:polotovar"),"BOM cell did not open the complete title-block table");
            require(row_name&&row_name->selectedText()==row_name->text(),"BOM cell did not focus its parameter in the complete table");
            require(row_name&&!row_name->isReadOnly()&&row_name->text()=="Second component","BOM edit did not resolve its exact source Part");
            require(editor_y(d,"titleBlockField:DRAWN_BY")<editor_y(d,"titleBlockField:NAME")&&editor_y(d,"titleBlockField:NAME")<editor_y(d,"titleBlockField:parameter:polotovar"),"BOM table used parent order instead of its source Part order");
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
                auto root=zima::drawing::DrawingDocument::create_view(source.document_id,source_path,calculated.back().mesh,zima::drawing::ViewOrientation::Front);root.section_id=section.id;root.section_snapshot=section;root.crop=zima::drawing::ViewCrop{zima::drawing::ViewCropShape::Ellipse,{0,0},{{10,6}}};
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
                require(root.crop&&root.crop->points.front().x==10&&root.crop->points.front().y==6,"Section regeneration lost its local crop");
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
            mouse(canvas,QEvent::MouseButtonPress,*point,Qt::LeftButton,Qt::LeftButton);mouse(canvas,QEvent::MouseMove,*point+QPointF(0,20),Qt::NoButton,Qt::LeftButton);require(canvas->property("annotationSnapActive").toBool(),"Section end guide snap feedback missing");mouse(canvas,QEvent::MouseButtonRelease,*point+QPointF(0,20),Qt::LeftButton,Qt::NoButton);
            const auto moved=window.annotation_handle_for_test(section.id);require(moved&&std::abs(moved->x()-point->x())<1e-6&&std::abs(moved->y()-point->y()-20)<=6.01,"Side trace snap changed its direction or exceeded tolerance");
            {
                workspace.activate(fixture.document_id);workspace.display_top_level(fixture.document_id);
                auto label_directory=std::filesystem::current_path();zima::command_host::Host host(workspace,kernel,label_directory);
                const auto run=[&](const char* name,zima::commands::Json args=zima::commands::Json::object()) {
                    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result.data;
                };
                const auto offsets=window.document_for_test().find_view(side.id)->section_marker_offsets.at(section.id);
                require(run("drawing.view.labels.get",{{"view",side.id}}).at("markers")[0].at("offsets_mm")==zima::commands::Json(offsets),"CLI query lost the GUI Section drag");
                run("drawing.view.labels.set",{{"view",side.id},{"values",{{"markers",zima::commands::Json::array({{{"section",section.id},{"offsets_mm",{offsets[0]+5,offsets[1]}}}})}}}});
                window.edit_workspace_document(fixture.document_id);flush();const auto changed=window.annotation_handle_for_test(section.id);
                require(changed&&std::abs(QLineF(*changed,*moved).length()-5*window.sheet_rectangle_for_test().height()/fixture.sheets.front().height_mm())<1e-6,"CLI Section offset did not move the visible handle by five paper millimetres");
                run("undo");window.edit_workspace_document(fixture.document_id);flush();require(QLineF(*window.annotation_handle_for_test(section.id),*moved).length()<1e-6,"CLI Section Undo lost the GUI drag");
                run("redo");window.edit_workspace_document(fixture.document_id);flush();require(QLineF(*window.annotation_handle_for_test(section.id),*changed).length()<1e-6,"CLI Section Redo lost its handle");
                run("undo");window.edit_workspace_document(fixture.document_id);flush();
            }
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
            // Keep the label inside the dimension span so the drag tests free
            // movement, without triggering automatic outside-label clearance.
            zima::drawing::place_drawing_dimension(v,d,0,{0,0});
            fixture.sheets.front().views={v};fixture.sheets.front().dimensions={d};workspace.add_drawing(fixture);window.edit_workspace_document(fixture.document_id);flush();
            const auto point=window.annotation_handle_for_test(d.id,0,true);require(point.has_value(),"Dimension has no manipulation point");
            mouse(canvas,QEvent::MouseMove,*point,Qt::NoButton,Qt::NoButton);click(canvas,*point);
            const auto selected_image=canvas->grab().toImage();const auto handle_pixel=(*point*selected_image.devicePixelRatio()).toPoint();
            const auto purple=selected_image.pixelColor(handle_pixel);require(purple.red()>150&&purple.blue()>200&&purple.green()<150,"Dimension handle is not purple");
            // A vertical normal-view dimension has upright text along the vertical support line, to its left.
            // Scan the glyphs immediately left of the handle, excluding the
            // support line. Thin antialiased text need not contain full RGB.
            int cyan_pixels=0;const auto ratio=selected_image.devicePixelRatio();
            for(int y=-int(20*ratio);y<int(20*ratio);++y)
                for(int x=-int(20*ratio);x<-int(3*ratio);++x) {
                    const auto pixel=selected_image.pixelColor(handle_pixel+QPoint(x,y));
                    if(pixel.red()<40&&pixel.green()>110&&pixel.blue()>150&&
                       std::abs(pixel.green()*255-pixel.blue()*209)<1000)++cyan_pixels;
                }
            const bool cyan_dimension=cyan_pixels>2;
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
            const auto library=std::filesystem::current_path()/"config/symbols/general/ZE-GENERAL-EDGES-ISO13715.symz";
            const auto definition=zima::symbols::Definition::load(library);
            zima::sketcher::SymbolInstance symbol;symbol.id="independent-edges";symbol.definition=definition.serialized();symbol.variant=definition.default_variant;symbol.x=100;symbol.y=140;
            auto fixture=zima::drawing::DrawingDocument::create_default();fixture.sheets.front().title_block_symbols={symbol};
            auto copy=symbol;copy.id="second-edges";copy.y=100;fixture.sheets.front().title_block_symbols.push_back(copy);
            workspace.add_drawing(fixture);window.edit_workspace_document(fixture.document_id);flush();
            const auto open_symbol=[&] {
                const auto center=window.title_field_center_for_test("symbol:"+symbol.id);require(center.has_value(),"Symbol has no common drawing hit region");
                click(canvas,*center);mouse(canvas,QEvent::MouseButtonDblClick,*center,Qt::LeftButton,Qt::LeftButton);flush();
                auto* dialog=window.findChild<QDialog*>("symbolPropertiesDialog");require(dialog,"Drawing symbol cannot open shared properties");return dialog;
            };
            for(const QString language:{"cs","en","de","fr","ru"}) {
                QTemporaryDir translated;QSettings config(translated.filePath("config.ini"),QSettings::IniFormat);config.setValue("Application/Language",language);config.sync();
                const auto settings=zima::app::ApplicationSettings::load(translated.path());
                zima::app::apply_application_translations(*qApp,settings);
                auto* dialog=open_symbol();auto* variants=dialog->findChild<QComboBox*>("symbolVariant");
                require(variants->currentText()==settings.qt_translations.value("Vnější a vnitřní hrany"),"Factory symbol variant label is not translated");
                variants->setCurrentIndex(variants->findData("external_exceptions"));flush();
                require(!dialog->findChild<QComboBox*>("symbolField:Internal edges")->isVisible()&&!dialog->findChild<QComboBox*>("symbolField:Exception")->isVisible(),"Hidden symbol fields remained editable");
                dialog->reject();flush();
            }
            zima::app::apply_application_translations(*qApp,zima::app::ApplicationSettings::load());
            auto* props=open_symbol();
            require(!props->findChild<QCheckBox*>("symbolOverride:External edges"),"Symbol field still requires an override checkbox");
            require(props->findChild<QComboBox*>("symbolField:External edges")->isEnabled(),"Symbol field is not immediately editable");
            props->findChild<QComboBox*>("symbolField:External edges")->setCurrentText("-0.8");
            props->reject();flush();require(window.document_for_test().sheets.front().title_block_symbols.front()==symbol,"Symbol Cancel changed drawing");
            props=open_symbol();
            props->findChild<QComboBox*>("symbolField:External edges")->setCurrentText("-0.8");
            window.grab().save(QString::fromStdString((std::filesystem::current_path()/"build/drawing-symbol-properties.png").string()));
            props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            const auto& edited=window.document_for_test().sheets.front().title_block_symbols;
            require(edited.front().text_values.at("External edges")=="-0.8"&&edited.back()==copy,"Editing one symbol changed another instance");
            require(zima::symbols::Definition::load(library).serialized()==definition.serialized(),"Drawing symbol edit modified library");
            const auto path=directory/"independent-symbols.drwz";window.document_for_test().save(path);
            require(zima::drawing::DrawingDocument::load(path).sheets.front().title_block_symbols==edited,"Edited symbol values did not survive reopen");
            require(workspace.open_drawing(fixture.document_id)->undo(),"Symbol edit has no Undo");
            window.edit_workspace_document(fixture.document_id);flush();
            require(window.document_for_test().sheets.front().title_block_symbols.front()==symbol,"Symbol Undo did not restore original values");
            require(workspace.open_drawing(fixture.document_id)->redo(),"Symbol edit has no Redo");
            window.edit_workspace_document(fixture.document_id);flush();
            require(window.document_for_test().sheets.front().title_block_symbols.front().text_values.at("External edges")=="-0.8","Symbol Redo lost edited value");
            action("removeDrawingTitleBlockAction")->trigger();flush();
            require(window.document_for_test().sheets.front().title_block_symbols.empty(),"Removing title block left its symbols behind");
        }
        {
            auto fixture=zima::drawing::DrawingDocument::create_default();
            // Printed text must honor the configured physical pen widths.
            auto& weight_sheet=fixture.sheets.front();
            weight_sheet.thick_line_mm=.5;weight_sheet.thin_line_mm=.25;
            zima::drawing::TemplateText weight_text;weight_text.text="I";weight_text.height=5;
            weight_text.font=zima::drawing_render::drawing_font_family().toStdString();weight_text.flipped=true;
            weight_text.position={160,97};weight_text.pen=zima::drawing::DrawingPen::White;
            weight_sheet.title_block_texts={weight_text};weight_text.position.x=150;weight_text.pen=zima::drawing::DrawingPen::Green;weight_sheet.title_block_texts.push_back(weight_text);
            zima::drawing_render::SheetRenderer weight_renderer;weight_renderer.set_render_sheet(&weight_sheet);
            QImage weights(1000,500,QImage::Format_ARGB32);weights.fill(Qt::white);
            {QPainter p(&weights);weight_renderer.paint_sheet(p,40,{-1800,-7600},true);}
            const auto saved_ui_font=qApp->font();auto heavier_ui_font=saved_ui_font;heavier_ui_font.setWeight(QFont::DemiBold);qApp->setFont(heavier_ui_font);
            QImage heavier_ui_output(weights.size(),weights.format());heavier_ui_output.fill(Qt::white);
            {QPainter p(&heavier_ui_output);weight_renderer.paint_sheet(p,40,{-1800,-7600},true);}
            qApp->setFont(saved_ui_font);
            require(heavier_ui_output==weights,"Application font weight changed Drawing output");
            const auto pixels=[&](int first,int last){int count=0;for(int x=first;x<last;++x)if(weights.pixelColor(x,300).red()<128)++count;return count;};
            require(std::abs(pixels(100,400)-20)<=2,"White PDF text does not follow 0.5 mm pen");
            require(std::abs(pixels(500,800)-10)<=2,"Green PDF text does not follow 0.25 mm pen");
            weights.save("build/pdf-text-weights.png");
            zima::drawing_render::export_pdf(fixture,directory/"text-pen-widths.pdf",{},nullptr,true);
            weight_sheet.title_block_texts.clear();
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
        {
            auto fixture=zima::drawing::DrawingDocument::create_default();workspace.add_drawing(fixture);window.edit_workspace_document(fixture.document_id);flush();
            auto* toolbar=window.findChild<QToolBar*>("drawingToolbar");require(toolbar->actions().indexOf(action("drawingTextAction"))==toolbar->actions().indexOf(action("drawingChainDimensionAction"))+1,"Text is not below Chain dimension");
            const auto text_dialog=[&](){for(auto* d:window.findChildren<QDialog*>("drawingTextProperties"))if(d->isVisible())return d;return static_cast<QDialog*>(nullptr);};
            const auto position=canvas->rect().center();
            action("drawingTextAction")->trigger();flush();auto* props=text_dialog();require(props&&(props->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"Text does not use shared internal properties");
            auto* editor=props->findChild<QPlainTextEdit*>("sketchTextValue");require(editor&&editor->height()>=220,"Multiline editor is too small");editor->setPlainText("Hydraulic manifold\nDeburr all ports");click(canvas,position);
            require(window.document_for_test().sheets.front().texts.empty(),"Text preview committed before OK");props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();require(window.document_for_test().sheets.front().texts.empty(),"Text Cancel persisted text");
            action("drawingTextAction")->trigger();flush();props=text_dialog();props->findChild<QPlainTextEdit*>("sketchTextValue")->setPlainText("Hydraulic manifold\nDeburr all ports");click(canvas,position);
            mouse(canvas,QEvent::MouseButtonPress,position,Qt::MiddleButton,Qt::MiddleButton);mouse(canvas,QEvent::MouseButtonRelease,position,Qt::MiddleButton,Qt::NoButton);require(text_dialog(),"Short middle click committed text");
            mouse(canvas,QEvent::MouseButtonDblClick,position,Qt::MiddleButton,Qt::MiddleButton);mouse(canvas,QEvent::MouseButtonRelease,position,Qt::MiddleButton,Qt::NoButton);flush();
            require(!text_dialog()&&window.document_for_test().sheets.front().texts.size()==1,"Middle double-click did not commit text");
            auto text=window.document_for_test().sheets.front().texts.front();auto center=window.title_field_center_for_test("text:"+text.id);require(center.has_value(),"Multiline text has no common hit region");
            click(canvas,*center);mouse(canvas,QEvent::MouseMove,QPointF(10,10),Qt::NoButton,Qt::NoButton);
            mouse(canvas,QEvent::MouseButtonPress,*center,Qt::LeftButton,Qt::LeftButton);mouse(canvas,QEvent::MouseMove,*center+QPointF(35,20),Qt::NoButton,Qt::LeftButton);mouse(canvas,QEvent::MouseButtonRelease,*center+QPointF(35,20),Qt::LeftButton,Qt::NoButton);
            const auto moved=window.document_for_test().sheets.front().texts.front();require(moved.presentation.position.x<text.presentation.position.x&&moved.presentation.position.y<text.presentation.position.y,"Text drag did not follow the cursor");
            require(zima::workspace::step_document_history(workspace,fixture.document_id,zima::workspace::HistoryDirection::Undo),"Text drag has no Undo");window.edit_workspace_document(fixture.document_id);flush();require(window.document_for_test().sheets.front().texts.front().presentation.position.x==text.presentation.position.x,"Text drag Undo did not restore position");
            center=window.title_field_center_for_test("text:"+text.id);click(canvas,*center);mouse(canvas,QEvent::MouseButtonDblClick,*center,Qt::LeftButton,Qt::LeftButton);flush();props=text_dialog();require(props,"Double-click did not edit text");props->findChild<QPlainTextEdit*>("sketchTextValue")->setPlainText("Hydraulic manifold\nDeburr all ports\nClean before assembly");props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            window.document_for_test().save(directory/"multiline-text.drwz");window.export_pdf(directory/"multiline-text.pdf");window.export_dxf(directory/"multiline-text.dxf");window.export_jpg(directory/"multiline-text.jpg");
            const auto reopened=zima::drawing::DrawingDocument::load(directory/"multiline-text.drwz");require(reopened.sheets.front().texts.front().presentation.text=="Hydraulic manifold\nDeburr all ports\nClean before assembly","Multiline text did not round-trip");
            std::ifstream file(directory/"multiline-text.dxf");const std::string dxf((std::istreambuf_iterator<char>(file)),{});require(dxf.find("Hydraulic manifold")!=std::string::npos&&dxf.find("Deburr all ports")!=std::string::npos&&dxf.find("Clean before assembly")!=std::string::npos,"DXF lost real text lines");
            window.grab().save(QString::fromStdString((directory/"multiline-text.png").string()));
            center=window.title_field_center_for_test("text:"+text.id);click(canvas,*center);QKeyEvent key(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(canvas,&key);flush();require(window.document_for_test().sheets.front().texts.empty(),"Selected text cannot be deleted");
        }
        {
            auto fixture=zima::drawing::DrawingDocument::create_default();auto view=original;
            view.x=65;view.y=60;view.show_caption=true;fixture.sheets.front().views={view};
            zima::drawing::DrawingText first;first.id="multi-first";first.presentation.text="First selection";first.presentation.position={200,160};
            auto second=first;second.id="multi-second";second.presentation.text="Second selection";second.presentation.position={200,130};
            fixture.sheets.front().texts={first,second};workspace.add_drawing(fixture);window.edit_workspace_document(fixture.document_id);flush();
            const auto a=*window.title_field_center_for_test("text:"+first.id),b=*window.title_field_center_for_test("text:"+second.id);
            const auto ctrl_click=[&](QPointF point){
                QMouseEvent press(QEvent::MouseButtonPress,point,QPointF(canvas->mapToGlobal(point.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::ControlModifier);
                QApplication::sendEvent(canvas,&press);mouse(canvas,QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton);flush();
            };
            click(canvas,a);ctrl_click(b);require(canvas->property("drawingSelectionCount").toInt()==2,"Ctrl did not add text to drawing selection");
            ctrl_click(a);require(canvas->property("drawingSelectionCount").toInt()==1,"Ctrl did not toggle selected text off");ctrl_click(a);
            QContextMenuEvent menu_event(QContextMenuEvent::Mouse,b.toPoint(),canvas->mapToGlobal(b.toPoint()));QApplication::sendEvent(canvas,&menu_event);flush();
            auto* menu=canvas->findChild<QMenu*>("drawingSelectionContextMenu");require(menu,"Mixed selection context menu missing");
            auto* remove=menu->findChild<QAction*>("drawingDeleteSelectionAction");require(remove,"Selection Delete action missing");remove->trigger();menu->close();flush();
            require(window.document_for_test().sheets.front().texts.empty()&&window.document_for_test().sheets.front().views.size()==1,"Multi-delete removed wrong entities");
            require(zima::workspace::step_document_history(workspace,fixture.document_id,zima::workspace::HistoryDirection::Undo),"Multi-delete has no Undo");window.edit_workspace_document(fixture.document_id);flush();
            require(window.document_for_test().sheets.front().texts.size()==2,"One Undo did not restore complete selection");
            click(canvas,a);ctrl_click(*window.view_rectangle_center_for_test(view.id));
            require(canvas->property("drawingSelectionCount").toInt()==2,"Mixed text/view selection failed");
            QKeyEvent key(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(canvas,&key);flush();
            require(window.document_for_test().sheets.front().views.size()==1&&window.document_for_test().sheets.front().texts.size()==1,"Mixed Delete must preserve views and remove selected text");
            require(zima::workspace::step_document_history(workspace,fixture.document_id,zima::workspace::HistoryDirection::Undo),"Mixed delete has no Undo");window.edit_workspace_document(fixture.document_id);flush();
            click(canvas,*window.view_rectangle_center_for_test(view.id));
            QApplication::sendEvent(canvas,&key);flush();
            require(window.document_for_test().sheets.front().views.size()==1&&window.document_for_test().sheets.front().texts.size()==2,"Delete must preserve an individually selected view");
            require(canvas->property("drawingSelectionCount").toInt()==1,"Ignored view Delete must preserve selection");
            ctrl_click(a);
            QContextMenuEvent mixed_menu_event(QContextMenuEvent::Mouse,a.toPoint(),canvas->mapToGlobal(a.toPoint()));QApplication::sendEvent(canvas,&mixed_menu_event);flush();
            menu=canvas->findChild<QMenu*>("drawingSelectionContextMenu");require(menu,"Mixed text/view context menu missing");
            remove=menu->findChild<QAction*>("drawingDeleteSelectionAction");require(remove,"Mixed Delete action missing");remove->trigger();menu->close();flush();
            require(window.document_for_test().sheets.front().views.size()==1&&window.document_for_test().sheets.front().texts.size()==1,"Context Delete must preserve views and remove selected text");
            require(zima::workspace::step_document_history(workspace,fixture.document_id,zima::workspace::HistoryDirection::Undo),"Mixed context delete has no Undo");window.edit_workspace_document(fixture.document_id);flush();
            click(canvas,a);click(canvas,QPointF(2,2));QApplication::sendEvent(canvas,&key);flush();require(window.document_for_test().sheets.front().texts.size()==2,"Empty click retained stale drawing selection");
            auto annotations=fixture;annotations.document_id+="-annotations";
            auto& annotated=annotations.sheets.front().views.front();annotated.model_annotations.clear();
            for(int i=0;i<3;++i){zima::drawing::ModelAnnotation item;item.source={part.document_id,"selection-owner","selection-"+std::to_string(i),{}};item.visible=true;
                item.kind=i==0?zima::drawing::ModelAnnotationKind::Axis:i==1?zima::drawing::ModelAnnotationKind::Construction:zima::drawing::ModelAnnotationKind::Dimension;
                item.curves={{{-15.,50.+i*15},{15.,50.+i*15}}};item.text_anchor={0,50.+i*15};annotated.model_annotations.push_back(item);}
            workspace.add_drawing(annotations);window.edit_workspace_document(annotations.document_id);flush();canvas->grab();
            for(int i=0;i<3;++i){const auto handle=window.model_annotation_handle_for_test(annotated.model_annotations[i].source,0,annotated.id);require(handle.has_value(),"Model annotation has no selection hit");if(i==0)click(canvas,*handle);else ctrl_click(*handle);}
            require(canvas->property("drawingSelectionCount").toInt()==3,"Mixed axis/construction/dimension selection failed");
            QApplication::sendEvent(canvas,&key);flush();
            require(std::ranges::none_of(window.document_for_test().sheets.front().views.front().model_annotations,[](const auto& a){return a.visible;}),"Model annotations were not all hidden by Delete");
            require(zima::workspace::step_document_history(workspace,annotations.document_id,zima::workspace::HistoryDirection::Undo),"Annotation deletion has no Undo");window.edit_workspace_document(annotations.document_id);flush();
            require(std::ranges::all_of(window.document_for_test().sheets.front().views.front().model_annotations,[](const auto& a){return a.visible;}),"One Undo did not restore all model annotations");

        }
        {
            auto fixture=zima::drawing::DrawingDocument::create_default();auto view=original;view.x=145;view.y=150;view.show_caption=false;view.model_annotations.clear();fixture.sheets.front().views={view};
            workspace.add_drawing(fixture);window.edit_workspace_document(fixture.document_id);flush();
            const auto curves=zima::drawing::projected_measurement_curves(view);require(!curves.empty()&&!curves.front().points.empty(),"Crop fixture has no selectable geometry");
            const auto point=curves.front().points.front();const auto bounds=window.sheet_rectangle_for_test();const double zoom=bounds.height()/fixture.sheets.front().height_mm();
            const QPointF anchor(bounds.right()-view.x*zoom+point.x*view.scale*zoom,bounds.bottom()-view.y*zoom-point.y*view.scale*zoom);
            const auto open_crop_properties=[&](){window.select_view_for_test(view.id);action("editDrawingViewAction")->trigger();flush();auto* props=window.findChild<QDialog*>("drawingViewProperties");require(props,"Crop properties missing");return props;};
            for(int shape=0;shape<3;++shape){
                auto* props=open_crop_properties();auto* crop_action=props->findChild<QAction*>(QString("drawingCropMode%1").arg(shape));require(crop_action,"Crop shape action missing");crop_action->trigger();flush();require(!props->isVisible(),"Crop opened another properties window instead of using the canvas");
                mouse(canvas,QEvent::MouseMove,anchor,Qt::NoButton,Qt::NoButton);click(canvas,anchor);
                if(shape<2){mouse(canvas,QEvent::MouseMove,anchor+QPointF(40,25),Qt::NoButton,Qt::NoButton);click(canvas,anchor+QPointF(40,25));}
                else {for(const auto& delta:{QPointF(-35,-25),QPointF(40,-25),QPointF(40,30),QPointF(-35,30)})click(canvas,anchor+delta);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(canvas,&enter);flush();}
                require(props->isVisible(),"Crop boundary did not return to the existing properties window");
                const auto before=window.document_for_test().find_view(view.id)->crop;require(!before||int(before->shape)!=shape,"Crop changed document before OK");
                props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
                const auto& committed=window.document_for_test().find_view(view.id)->crop;require(committed&&int(committed->shape)==shape,"Crop shape did not commit");
                window.document_for_test().save(directory/"crop.drwz");auto reopened=zima::drawing::DrawingDocument::load(directory/"crop.drwz");require(reopened.find_view(view.id)->crop&&reopened.find_view(view.id)->crop->points==committed->points,"Crop boundary did not round-trip");
            }
            {
                auto sheet=zima::drawing::DrawingDocument::create_default().sheets.front();sheet.frame_lines.clear();sheet.frame_texts.clear();sheet.frame_circles.clear();sheet.title_block_fields.clear();
                zima::drawing::DrawingView test_view;test_view.id="clip-pixels";test_view.x=sheet.width_mm()-50;test_view.y=sheet.height_mm()-50;
                zima::drawing::ProjectedEdge edge;edge.points={{-20,0},{20,0}};test_view.projected_edges={edge};test_view.crop=zima::drawing::ViewCrop{zima::drawing::ViewCropShape::Circle,{0,0},{{5,5}}};sheet.views={test_view};
                zima::drawing_render::SheetRenderer renderer;renderer.set_render_sheet(&sheet);QImage image(200,200,QImage::Format_ARGB32);image.fill(Qt::white);{QPainter painter(&image);renderer.paint_sheet(painter,2,{},true);}
                require(image.pixelColor(130,100)==QColor(Qt::white)&&image.pixelColor(100,100).red()<128,"Print renderer did not clip the projected edge");
            }
            const auto printed_crop=window.render_sheet_for_test(true);require(!printed_crop.isNull(),"Cropped view did not render for printing");
            auto* props=open_crop_properties();props->findChild<QAction*>("drawingCropMode3")->trigger();flush();
            const auto saved=*window.document_for_test().find_view(view.id)->crop;const auto grip=saved.points.front();
            const QPointF location(bounds.right()-view.x*zoom+grip.x*view.scale*zoom,bounds.bottom()-view.y*zoom-grip.y*view.scale*zoom);
            mouse(canvas,QEvent::MouseButtonPress,location,Qt::LeftButton,Qt::LeftButton);mouse(canvas,QEvent::MouseMove,location+QPointF(12,0),Qt::NoButton,Qt::LeftButton);mouse(canvas,QEvent::MouseButtonRelease,location+QPointF(12,0),Qt::LeftButton,Qt::NoButton);
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(canvas,&enter);flush();props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();require(window.document_for_test().find_view(view.id)->crop->points==saved.points,"Crop edit Cancel changed stored boundary");
            window.export_pdf(directory/"crop.pdf");window.export_dxf(directory/"crop.dxf");window.export_jpg(directory/"crop.jpg");
            props=open_crop_properties();props->findChild<QAction*>("drawingRemoveCrop")->trigger();props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();require(!window.document_for_test().find_view(view.id)->crop,"Removing crop did not restore the full view");
            require(zima::workspace::step_document_history(workspace,fixture.document_id,zima::workspace::HistoryDirection::Undo),"Crop removal has no Undo");window.edit_workspace_document(fixture.document_id);flush();require(window.document_for_test().find_view(view.id)->crop.has_value(),"Undo did not restore crop");
        }
        {
            std::optional<zima::sketcher::SketchText> preview,committed;
            zima::sketcher::SketchText initial;initial.value.clear();initial.height=2.5;initial.color=zima::sketcher::SketchTextColor::Green;
            auto* props=new zima::app::SketchTextPropertiesDialog(initial,{},[&](auto p){preview=p;},[&](auto p){committed=p;},&window,true,true);
            props->show();props->set_preview_anchor(20,30);flush();
            require(props->needs_anchor()&&preview&&preview->value.empty()&&preview->anchor_x==20,"Empty text does not preview at cursor");
            props->set_preview_anchor(25,35);require(preview->anchor_x==25,"Text placement preview does not follow cursor");
            props->set_anchor(25,35);props->set_preview_anchor(80,90);
            require(!props->needs_anchor()&&preview->anchor_x==25&&preview->anchor_y==35,"Placed text still follows cursor");
            auto* editor=props->findChild<QPlainTextEdit*>("sketchTextValue");
            auto* symbols=props->findChild<QToolButton*>("drawingTextSymbols");require(symbols&&symbols->menu(),"Text symbols missing");
            editor->setPlainText("AB");auto cursor=editor->textCursor();cursor.setPosition(1);editor->setTextCursor(cursor);
            symbols->menu()->actions().front()->trigger();require(editor->toPlainText()==QStringLiteral("A⌀B"),"Symbol was not inserted at text cursor");
            editor->clear();props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            require(committed&&committed->value.empty()&&committed->height==2.5,"Empty placed text cannot be confirmed");
            auto fixture=zima::drawing::DrawingDocument::create_default();workspace.add_drawing(fixture);window.edit_workspace_document(fixture.document_id);flush();
            action("drawingTextAction")->trigger();flush();
            props=dynamic_cast<zima::app::SketchTextPropertiesDialog*>(window.findChild<QDialog*>("drawingTextProperties"));
            require(props&&props->findChild<QDoubleSpinBox*>("sketchTextHeight")->value()==2.5&&props->findChild<QComboBox*>("sketchTextColor")->currentData().toInt()==int(zima::sketcher::SketchTextColor::Green),"New drawing text has incorrect defaults");
            click(canvas,canvas->rect().center());mouse(canvas,QEvent::MouseMove,{10,10},Qt::NoButton,Qt::NoButton);
            window.grab().save(QString::fromStdString((directory/"drawing-text-properties.png").string()));
            props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            require(window.document_for_test().sheets.front().texts.size()==1,"Canvas could not place empty drawing text");
            const auto text=window.document_for_test().sheets.front().texts.front();require(text.presentation.text.empty()&&window.title_field_center_for_test("text:"+text.id),"Empty text has no selectable dash");
            window.document_for_test().save(directory/"empty-free-text.drwz");
            require(zima::drawing::DrawingDocument::load(directory/"empty-free-text.drwz").sheets.front().texts.front().presentation.text.empty(),"Empty drawing text did not persist");
        }
        {
            auto fixture=zima::drawing::DrawingDocument::create_default();
            zima::drawing::TitleBlockField date;date.id="DATE";date.editable=true;date.value="old";date.action_settings={{"kind","today"},{"date_format","yyyy-MM-dd"}};
            auto list=date;list.id="LIST";list.value="A";list.action_settings={{"kind","list"},{"choices","[\"A\",\"B\"]"},{"allow_custom","no"}};
            auto custom=list;custom.id="CUSTOM";custom.action_settings["allow_custom"]="yes";
            fixture.sheets.front().title_block_fields={date,list,custom};workspace.add_drawing(fixture);window.edit_workspace_document(fixture.document_id);flush();
            for(bool accept:{false,true}) {
                action("editDrawingTitleBlockAction")->trigger();flush();auto* props=window.findChild<QDialog*>("drawingTitleBlockProperties");require(props,"Action dialog missing");
                auto* today=props->findChild<QPushButton*>("titleBlockToday:DATE");auto* value=props->findChild<QLineEdit*>("titleBlockField:DATE");auto* choices=props->findChild<QComboBox*>("titleBlockChoices:LIST");auto* editable=props->findChild<QComboBox*>("titleBlockChoices:CUSTOM");
                require(today&&value&&choices&&editable&&!choices->isEditable()&&editable->isEditable(),"Field action widgets are incorrect");require(value->text()=="old","Date changed without clicking");
                today->click();require(value->text()==QDate::currentDate().toString("yyyy-MM-dd"),"Today action used wrong date format");choices->setCurrentText("B");editable->setEditText("Custom");
                require(props->width()>=std::min(window.width(),3*props->minimumSizeHint().width()/2),"Title value dialog did not use the reduced width");
                window.grab().save(QString::fromStdString((directory/"title-field-actions.png").string()));
                props->findChild<QDialogButtonBox*>()->button(accept?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();flush();
                const auto& fields=window.document_for_test().sheets.front().title_block_fields;
                require(fields[0].value==(accept?QDate::currentDate().toString("yyyy-MM-dd").toStdString():"old")&&fields[1].value==(accept?"B":"A")&&fields[2].value==(accept?"Custom":"A"),"Field actions broke OK/Cancel");
            }
            window.document_for_test().save(directory/"title-field-actions.drwz");
            const auto reopened=zima::drawing::DrawingDocument::load(directory/"title-field-actions.drwz");require(reopened.sheets.front().title_block_fields[1].action_settings==list.action_settings,"Field action did not persist");
            require(workspace.open_drawing(fixture.document_id)->undo(),"Title action Undo missing");
        }
        {
            const auto saved_settings=zima::app::ApplicationSettings::load();QTemporaryDir language_directory;
            const auto catalogs=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/localization";
            for(const auto* language:{"cs","en","de","fr","ru"}) {
                QSettings config(language_directory.filePath("config.ini"),QSettings::IniFormat);config.setValue("Application/Language",language);config.setValue("Paths/Localization",QString::fromStdString(catalogs.generic_string()));config.sync();
                const auto settings=zima::app::ApplicationSettings::load(language_directory.path());zima::app::apply_application_translations(*qApp,settings);
                auto initial=zima::sketcher::Sketch::create_text();initial.modeling_geometry=false;
                std::map<std::string,std::string> accepted;int commits=0;zima::app::SketchTextPropertiesDialog* props=nullptr;
                props=new zima::app::SketchTextPropertiesDialog(initial,std::array{0.,0.},[](auto){},[&](auto){accepted=props->field_action();++commits;},&window,true,false,std::map<std::string,std::string>{});props->show();flush();
                auto* action=props->findChild<QComboBox*>("textFieldAction");require(action&&action->itemText(1)==settings.qt_translations.value("Dnešní datum")&&action->itemText(2)==settings.qt_translations.value("Výběr ze seznamu"),"Text actions did not follow language switch");
                action->setCurrentIndex(2);auto* list=props->findChild<QPlainTextEdit*>("textFieldChoices");require(list->isVisible()&&!props->findChild<QComboBox*>("textFieldDateFormat")->isVisible(),"Action options show irrelevant settings");
                flush();require(props->findChild<QCheckBox*>("textFieldAllowCustom")->y()>=list->y()+list->height(),"List options overlap the custom-value checkbox");
                list->setPlainText("A\nB");props->findChild<QCheckBox*>("textFieldAllowCustom")->setChecked(false);
                props->grab().save(QString("build/text-field-action-%1.png").arg(language));props->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                require(commits==1&&accepted.at("kind")=="list"&&accepted.at("allow_custom")=="no"&&accepted.at("choices")=="[\"A\",\"B\"]","Text property action was not committed");
            }
            zima::app::apply_application_translations(*qApp,saved_settings);
        }
        {
            auto symbol=zima::drawing::DrawingDocument::create_default();zima::drawing::load_title_block_template(symbol.sheets.front(),"config/formats/ZE-TITLE-BLOCK-CS.tblz");
            require(symbol.sheets.front().dimensions.empty(),"Template Sketch dimensions leaked into Drawing");
            zima::drawing_render::SheetRenderer renderer;renderer.set_render_sheet(&symbol.sheets.front());QImage proof(360,200,QImage::Format_ARGB32_Premultiplied);proof.fill(Qt::white);QPainter painter(&proof);
            renderer.paint_sheet(painter,30,{-4600,-7530},true);painter.end();proof.save("build/projection-symbol-proof.png");
            zima::drawing_render::export_pdf(symbol,directory/"projection-symbol.pdf",{},nullptr,true);
            zima::drawing_render::export_dxf(symbol,symbol.sheets.front().id,directory/"projection-symbol.dxf",{},nullptr,true);
        }
        require(modal_error.isEmpty(),modal_error.toUtf8().constData());
        std::cout<<"Drawing placement, rectangular selection, projection, Cancel, MMB, persistence and global paths passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}

int verify_drawing_breaks_ui() {
    using namespace zima;
    try {
        QMainWindow owner;owner.resize(1200,900);owner.show();flush();
        auto sheet=drawing::DrawingDocument::create_default().sheets.front();drawing::DrawingView view;
        view.id="rod-view";view.source_document_id="rod";view.scale=.2;view.camera={{1,0,0},{0,1,0},{0,0,-1}};
        drawing::ProjectedEdge outline;outline.source={"rod","outline",""};outline.points={{0,0},{1000,0},{1000,80},{0,80},{0,0}};view.projected_edges={outline};
        drawing::ModelAnnotation annotation;annotation.source={"rod","rod","length",""};annotation.visible=true;
        kernel::ViewerDimension dimension;dimension.kind=kernel::ViewerDimensionKind::Linear;dimension.witness_first={0,0,0};dimension.witness_second={1000,0,0};dimension.line_first={0,-40,0};dimension.line_second={1000,-40,0};dimension.plane_normal={0,0,1};dimension.value=1000;dimension.label_position=kernel::Vec3{500,-40,0};annotation.model_dimension=dimension;view.model_annotations={annotation};
        sheet.views={view};std::vector<drawing::ViewBreak> accepted;int commits=0;
        app::DrawingBreakEditor editor(&owner,sheet,view,[&](auto b){accepted=std::move(b);++commits;});editor.show();flush();
        auto* canvas=dynamic_cast<app::BreakEditorCanvas*>(editor.findChild<QWidget*>("drawingBreakCanvas"));require(canvas,"Break canvas missing");canvas->grab();flush();
        auto* table=editor.findChild<QTableWidget*>("drawingBreakTable");
        require(table->rowCount()==1&&!editor.findChild<QPushButton*>("drawingBreakAdd")&&!editor.findChild<QPushButton*>("drawingBreakRemove")&&!editor.findChild<QComboBox*>("drawingBreakPreviewMode"),"Break editor retained obsolete controls");
        const auto arm=[&](int row,int column){click(table->viewport(),table->visualItemRect(table->item(row,column)).center());};
        arm(0,2);require(static_cast<ui::ReferenceCellItem*>(table->item(0,2))->is_active_input()&&!static_cast<ui::ReferenceCellItem*>(table->item(0,3))->is_active_input(),"Break input ownership must belong only to first position");
        const auto pointer=canvas->screen({200,40});mouse(canvas,QEvent::MouseMove,pointer,Qt::NoButton,Qt::NoButton);
        const auto offer=canvas->grab().toImage();const auto pixel=offer.pixelColor((pointer*offer.devicePixelRatio()).toPoint());
        require(pixel.green()>150&&pixel.red()<120&&pixel.blue()<100,"Pending break boundary has no green point under cursor");
        click(canvas,pointer);require(canvas->view().breaks.empty()&&!editor.buttons()->button(QDialogButtonBox::Ok)->isEnabled(),"Partial break was committed or accepted");
        arm(0,3);mouse(canvas,QEvent::MouseButtonPress,canvas->rect().center(),Qt::MiddleButton,Qt::MiddleButton);mouse(canvas,QEvent::MouseButtonRelease,canvas->rect().center(),Qt::MiddleButton,Qt::NoButton);
        require(!canvas->picking&&!static_cast<ui::ReferenceCellItem*>(table->item(0,3))->is_active_input()&&canvas->draft_first.has_value(),"Short MMB must end boundary entry without deleting first position");
        arm(0,3);click(canvas,canvas->screen({200,40}));
        require(canvas->view().breaks.empty()&&canvas->picking&&!editor.findChild<QLabel*>("drawingBreakError")->text().isEmpty(),"Coincident boundaries must remain pending with a validation message");
        click(canvas,canvas->screen({800,40}));
        require(table->rowCount()==2&&sheet.views.front().breaks.empty()&&commits==0,"Completed break must append one empty row without mutating source sheet");
        require(canvas->view().breaks.front().gap==2&&qobject_cast<QDoubleSpinBox*>(table->cellWidget(1,6))->value()==2,"New break paper gap must default to 2 mm");
        require(table->cellWidget(0,0)->findChild<QPushButton*>()->isVisible()&&!table->cellWidget(1,0)->findChild<QPushButton*>()->isVisible(),"Completed row must have remove button and empty row a green arrow");
        canvas->grab();
        const auto anchor=canvas->screen({350,60});const auto anchored_model=canvas->model(anchor);
        QWheelEvent zoom(anchor,canvas->mapToGlobal(anchor.toPoint()),QPoint{},QPoint{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(canvas,&zoom);canvas->grab();
        const auto after_zoom=canvas->model(anchor);
        require(std::hypot(after_zoom.x-anchored_model.x,after_zoom.y-anchored_model.y)<1e-6,"Break zoom did not preserve the point under the cursor");
        canvas->fit();canvas->grab();
        const auto drag=[&](QPointF a,QPointF b){mouse(canvas,QEvent::MouseButtonPress,a,Qt::LeftButton,Qt::LeftButton);mouse(canvas,QEvent::MouseMove,b,Qt::NoButton,Qt::LeftButton);mouse(canvas,QEvent::MouseButtonRelease,b,Qt::LeftButton,Qt::NoButton);canvas->grab();};
        drag(canvas->screen({200,40}),canvas->screen({180,40}));
        require(std::abs(canvas->view().breaks.front().start-180)<1e-6&&std::abs(canvas->view().breaks.front().length-620)<1e-6,"Dragging first break endpoint did not keep second endpoint fixed");
        drag(canvas->screen({800,40}),canvas->screen({820,40}));
        require(std::abs(canvas->view().breaks.front().length-640)<1e-6,"Dragging second break endpoint failed");
        drag(canvas->screen({500,40}),canvas->screen({520,40}));
        require(std::abs(canvas->view().breaks.front().start-200)<1e-6&&std::abs(canvas->view().breaks.front().length-640)<1e-6,"Dragging break segment changed its length");
        auto* start=qobject_cast<QDoubleSpinBox*>(table->cellWidget(0,4));auto* length=qobject_cast<QDoubleSpinBox*>(table->cellWidget(0,5));start->setValue(150);length->setValue(650);flush();
        require(canvas->view().breaks.front().start==150&&canvas->view().breaks.front().length==650,"Numeric break dimensions ignored");
        canvas->grab();const auto length_label=canvas->screen({475,40})+QPointF(0,35);
        mouse(canvas,QEvent::MouseButtonDblClick,length_label,Qt::LeftButton,Qt::LeftButton);flush();
        auto* number=canvas->findChild<QLineEdit*>("inlineDimensionValueEdit");require(number,"Double-click did not edit break dimension");number->setText("600+50");
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(number,&enter);flush();require(commits==0&&editor.isVisible()&&canvas->view().breaks.front().length==650,"Inline break expression committed dialog or changed value");
        canvas->result=true;canvas->fit();flush();editor.grab().save("build/drawing-break-result.png");
        require(sheet.views.front().projected_edges.front().points==outline.points,"Result preview mutated source geometry");
        const auto before=app::model_annotation_layout(view,annotation,{});auto broken=view;broken.breaks=canvas->view().breaks;
        const auto after=app::model_annotation_layout(broken,annotation,{});
        require(!after.curves.empty()&&after.handles.at("arrow_second").x()<before.handles.at("arrow_second").x()-100,"Show/Erase length did not follow shortened geometry");
        require(drawing::project_model_annotation(broken,annotation).value==1000,"Show/Erase value was shortened");
        canvas->result=false;canvas->fit();flush();editor.grab().save("build/drawing-break-editor.png");
        mouse(canvas,QEvent::MouseButtonPress,canvas->rect().center(),Qt::MiddleButton,Qt::MiddleButton);mouse(canvas,QEvent::MouseButtonRelease,canvas->rect().center(),Qt::MiddleButton,Qt::NoButton);
        require(editor.isVisible()&&commits==0,"Short MMB committed break editor");
        mouse(canvas,QEvent::MouseButtonDblClick,canvas->rect().center(),Qt::MiddleButton,Qt::MiddleButton);flush();require(commits==1&&accepted.size()==1,"Break editor MMB OK did not commit once");
        QTemporaryDir exports;auto document=drawing::DrawingDocument::create_default();document.sheets.front()=sheet;document.sheets.front().views.front()=broken;
        const auto folder=std::filesystem::path(exports.path().toStdString());
        workspace::Workspace source_models;auto source=document::PartDocument::create_default();source.document_id="rod";source_models.add_part(source,{},{});document.source_document_id="rod";
        require(drawing_render::export_pdf(document,folder/"break.pdf",{},&source_models)>0,"Broken view PDF failed");
        require(drawing_render::export_dxf(document,document.sheets.front().id,folder/"break.dxf",{},&source_models)>0,"Broken view DXF failed");
        view.breaks=accepted;app::DrawingBreakEditor cancelled(&owner,sheet,view,[&](auto){++commits;});cancelled.show();flush();
        auto* second=cancelled.findChild<QTableWidget*>("drawingBreakTable");qobject_cast<QDoubleSpinBox*>(second->cellWidget(0,5))->setValue(300);
        second->cellWidget(0,0)->findChild<QPushButton*>()->click();flush();require(second->rowCount()==1,"Removing break must retain the empty input row");
        auto* vertical_canvas=dynamic_cast<app::BreakEditorCanvas*>(cancelled.findChild<QWidget*>("drawingBreakCanvas"));vertical_canvas->grab();
        qobject_cast<QComboBox*>(second->cellWidget(0,1))->setCurrentIndex(1);
        click(second->viewport(),second->visualItemRect(second->item(0,3)).center());click(vertical_canvas,vertical_canvas->screen({400,60}));
        click(second->viewport(),second->visualItemRect(second->item(0,2)).center());click(vertical_canvas,vertical_canvas->screen({400,10}));
        require(vertical_canvas->view().breaks.size()==1&&vertical_canvas->view().breaks[0].vertical&&std::abs(vertical_canvas->view().breaks[0].start-10)<1e-6&&std::abs(vertical_canvas->view().breaks[0].length-50)<1e-6,"Vertical boundaries must support entering second position first");
        cancelled.buttons()->button(QDialogButtonBox::Cancel)->click();flush();require(commits==1&&view.breaks.front().length==650,"Cancel changed existing break");
        const auto saved_settings=app::ApplicationSettings::load();
        QTemporaryDir language_directory;
        const auto catalogs=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/localization";
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QSettings config(language_directory.filePath("config.ini"),QSettings::IniFormat);
            config.setValue("Application/Language",language);config.setValue("Paths/Localization",QString::fromStdString(catalogs.generic_string()));config.sync();
            const auto settings=app::ApplicationSettings::load(language_directory.path());app::apply_application_translations(*qApp,settings);
            app::DrawingBreakEditor translated(&owner,sheet,view,[](auto){});translated.show();flush();
            auto* localized=translated.findChild<QTableWidget*>("drawingBreakTable");
            require(localized->horizontalHeaderItem(2)->text()==settings.qt_translations.value("První poloha")&&localized->horizontalHeaderItem(3)->text()==settings.qt_translations.value("Druhá poloha"),"Break position headers did not follow language change");
            translated.grab().save(QString("build/drawing-break-editor-%1.png").arg(language));translated.reject();flush();
        }
        app::apply_application_translations(*qApp,saved_settings);
        std::cout<<"Break editor placement, numeric dimensions, isolated preview, Show/Erase true length, OK and Cancel passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

int verify_drawing_details_ui() {
    using namespace zima;
    try {
        // Local hatch limits have a printable boundary over material only.
        // They must leave ordinary edges intact and affect only their section.
        {
            auto sheet=drawing::DrawingDocument::create_default().sheets.front();
            sheet.frame_lines.clear();sheet.frame_texts.clear();sheet.frame_circles.clear();sheet.title_block_fields.clear();
            drawing::DrawingView view;view.id="hatch-boundary";view.section_id="section";view.show_caption=false;
            view.x=sheet.width_mm()-25;view.y=sheet.height_mm()-25;
            drawing::ProjectedTriangle a,b;a.points={drawing::Point2{-10,-10},{10,-10},{10,10}};b.points={drawing::Point2{-10,-10},{10,10},{-10,10}};view.projected_triangles={a,b};
            drawing::ProjectedEdge edge;edge.points={{-10,8},{10,8}};view.projected_edges.push_back(edge);
            edge.hatch=true;edge.points={{-10,7},{10,7}};view.projected_edges.push_back(edge);
            view.section_hatch_crops["section"]={drawing::ViewCropShape::Ellipse,{0,0},{{15,5}}};sheet.views={view};
            zima::drawing_render::SheetRenderer renderer;renderer.set_render_sheet(&sheet);
            const auto render=[&](bool printing){QImage image(200,200,QImage::Format_ARGB32);image.fill(printing?Qt::white:Qt::black);QPainter painter(&image);renderer.paint_sheet(painter,4,{},printing);return image;};
            const auto has_ink=[](const QImage& image,int x,int y,bool printing){for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const auto value=image.pixelColor(x+dx,y+dy).red();if(printing?value<220:value>35)return true;}return false;};
            for(bool printing:{false,true}){
                const auto image=render(printing);
                require(has_ink(image,100,80,printing),"Local hatch boundary is missing over material");
                require(!has_ink(image,160,100,printing),"Local hatch boundary extends outside material");
                require(has_ink(image,100,68,printing)&&!has_ink(image,100,72,printing),"Hatch limit clipped ordinary geometry or retained outside hatch strokes");
            }
            sheet.views.front().crop=drawing::ViewCrop{drawing::ViewCropShape::Circle,{0,0},{{3,3}}};
            require(!has_ink(render(true),100,80,true),"Local hatch boundary escaped the view crop");
            sheet.views.front().crop.reset();sheet.views.front().section_id="other";
            require(!has_ink(render(true),100,80,true),"Local hatch boundary leaked into another section");
        }
        QTemporaryDir temporary;
        workspace::Workspace live;auto part=document::PartDocument::create_default();
        kernel::BodyResult cache;cache.mesh.edges={{{{-30,0,-20},{30,0,-20}},{"box","bottom",{}}},{{{30,0,-20},{30,0,20}},{"box","right",{}}},{{{30,0,20},{-30,0,20}},{"box","top",{}}},{{{-30,0,20},{-30,0,-20}},{"box","left",{}}}};
        cache.mesh.vertices={{-30,0,-20},{30,0,-20},{30,0,20},{-30,0,20}};cache.mesh.triangles={0,1,2,0,2,3};
        auto section=document::create_section();static_cast<void>(section.sketch.add_segment(-50,0,50,0));part.sections.push_back(section);
        live.add_part(part,{cache});
        auto document=drawing::DrawingDocument::create_default();
        auto source=drawing::DrawingDocument::create_view(part.document_id,{},cache.mesh);source.x=105;source.y=180;
        document.sheets.front().views.push_back(source);live.add_drawing(document);
        app::DrawingWindow window(&live,false);window.edit_workspace_document(document.document_id);window.resize(1200,850);window.show();flush();
        auto* canvas=window.findChild<QWidget*>("drawingCanvas");
        const auto action=[&](const char* name){auto* a=window.findChild<QAction*>(name);require(a,"Detail action missing");return a;};
        const auto dialog=[&]{return window.findChild<QDialog*>("drawingDetailProperties");};
        const auto count=[&]{return window.document_for_test().sheets.front().views.size();};
        const auto source_center=*window.view_rectangle_center_for_test(source.id);
        for(int shape=0;shape<3;++shape) {
            action("insertDrawingDetailAction")->trigger();flush();auto* d=dialog();require(d,"Insert Detail did not open properties");
            require((d->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow,"Detail uses a native floating dialog");
            d->findChild<QComboBox*>("detailShape")->setCurrentIndex(shape);
            d->findChild<QPushButton*>("detailSource")->click();flush();click(canvas,source_center);
            require(!d->isVisible(),"Region input did not own the canvas");
            if(shape==2) {
                for(auto delta:{QPointF(-35,-25),QPointF(35,-25),QPointF(35,25),QPointF(-35,25)})click(canvas,source_center+delta);
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(canvas,&enter);flush();
            } else click(canvas,source_center+QPointF(35,25));
            click(canvas,source_center+QPointF(180,140));flush();
            require(d->isVisible()&&d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled(),"Detail cannot commit after region and placement");
            require(count()==1,"Detail preview committed before OK");
            d->findChild<QCheckBox*>("detailShowBoundary")->setChecked(false);
            if(shape<2){d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();require(count()==1,"Cancel persisted a detail");}
            else {d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();require(count()==2&&!dialog(),"Detail OK failed");}
        }
        const auto detail=window.document_for_test().sheets.front().views.back();
        require(detail.detail_view&&detail.name=="X"&&detail.parent_view_id==source.id&&!detail.show_detail_boundary&&detail.show_detail_label,"Detail metadata is incorrect");
        require(detail.projected_edges.size()==source.projected_edges.size(),"Detail lost parent geometry");
        const auto path=std::filesystem::u8path(temporary.filePath("detail.drwz").toStdString());window.document_for_test().save(path);
        const auto reopened=drawing::DrawingDocument::load(path);
        require(reopened.sheets.front().views.back().detail_view&&reopened.sheets.front().views.back().crop->shape==drawing::ViewCropShape::Spline,"Detail save/reopen failed");
        window.select_view_for_test(detail.id);action("editDrawingViewAction")->trigger();flush();
        require(dialog(),"Edit Detail uses a different or missing properties dialog");
        dialog()->findChild<QLineEdit*>("detailName")->setText("Y");
        dialog()->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        require(window.document_for_test().sheets.front().views.back().name=="X","Cancel changed detail name");
        live.open_drawing(document.document_id)->undo();window.edit_workspace_document(document.document_id);flush();require(count()==1,"Detail creation Undo failed");
        live.open_drawing(document.document_id)->redo();window.edit_workspace_document(document.document_id);flush();require(count()==2,"Detail creation Redo failed");
        canvas->grab();flush();
        auto handle=window.detail_label_handle_for_test(detail.id);require(handle.has_value(),"Detail reference label has no movable grip");
        const auto drag=[&](QPointF from,QPointF to,bool cancel){
            mouse(canvas,QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
            mouse(canvas,QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);
            if(cancel){QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(canvas,&escape);flush();}
            mouse(canvas,QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);canvas->grab();flush();
        };
        drag(*handle,*handle+QPointF(55,-35),true);
        require(!window.document_for_test().find_view(detail.id)->detail_label_position,"Escape committed detail label movement");
        handle=window.detail_label_handle_for_test(detail.id);
        drag(*handle,*handle+QPointF(55,-35),false);
        const auto position=window.document_for_test().find_view(detail.id)->detail_label_position;
        require(position.has_value()&&QLineF(*window.detail_label_handle_for_test(detail.id),*handle).length()>20,"Detail reference label did not move");
        live.open_drawing(document.document_id)->undo();window.edit_workspace_document(document.document_id);flush();
        require(!window.document_for_test().find_view(detail.id)->detail_label_position,"Detail label movement Undo failed");
        live.open_drawing(document.document_id)->redo();window.edit_workspace_document(document.document_id);flush();
        require(window.document_for_test().find_view(detail.id)->detail_label_position==position,"Detail label movement Redo failed");
        window.document_for_test().save(path);
        require(drawing::DrawingDocument::load(path).find_view(detail.id)->detail_label_position==position,"Detail label position failed to persist");
        const auto yellow=[](const QImage& image){int pixels=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto p=image.pixelColor(x,y);pixels+=p.red()>180&&p.green()>140&&p.blue()<100;}return pixels;};
        window.select_view_for_test({});mouse(canvas,QEvent::MouseMove,QPointF(5,5),Qt::NoButton,Qt::NoButton);flush();
        require(yellow(window.render_sheet_for_test(false))>10,"Detail reference lines must be yellow on screen");
        window.grab().save("build/drawing-detail-proof.png");
        // A hatch boundary is owned by this Drawing view; removing it is a
        // pending property edit until OK and never mutates the source Section.
        auto pending=window.document_for_test();pending.find_view(source.id)->section_hatch_crops[section.id]={drawing::ViewCropShape::Circle,{0,0},{{5,5}}};
        live.open_drawing(document.document_id)->commit(pending);window.edit_workspace_document(document.document_id);flush();
        for(bool accept:{false,true}) {
            window.select_view_for_test(source.id);action("editDrawingViewAction")->trigger();flush();
            auto* button=window.findChild<QPushButton*>("drawingHatchCrop_"+QString::fromStdString(section.id));require(button&&button->menu(),"Section table lacks hatch region action");
            QDialog* properties=nullptr;for(auto* owner=button->parentWidget();owner&&!properties;owner=owner->parentWidget())properties=qobject_cast<QDialog*>(owner);
            require(properties,"Hatch action is outside properties");
            button->menu()->actions().back()->trigger();flush();
            require(window.document_for_test().find_view(source.id)->section_hatch_crops.size()==1,"Hatch edit committed before OK");
            properties->findChild<QDialogButtonBox*>()->button(accept?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();flush();
            require(window.document_for_test().find_view(source.id)->section_hatch_crops.size()==(accept?0:1),"Hatch OK/Cancel transaction failed");
            require(live.open_part(part.document_id)->session.document().sections.front().id==section.id,"Drawing hatch edit changed source Section");
        }
        std::cout<<"Detail circle/ellipse/spline input, transient preview, OK/Cancel, editing, persistence and Undo/Redo passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
