#include <QTabBar>
#include <QTableWidget>
#include <zima/ui/reference_cell.hpp>
#include <QFile>
#include <QImage>
#include "drawing_annotation_layout.hpp"
#include "drawing_annotation_source.hpp"
#include "drawing_window.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <iostream>
#include <zima/workspace/workspace.hpp>
namespace {
void require(bool b, const char *message) {
  if (!b)
    throw std::runtime_error(message);
}
void flush() {
  QApplication::processEvents();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
void mouse(QWidget *w, QEvent::Type type, QPointF p, Qt::MouseButton button,
           Qt::MouseButtons buttons) {
  QMouseEvent e(type, p, QPointF(w->mapToGlobal(p.toPoint())), button, buttons,
                Qt::NoModifier);
  QApplication::sendEvent(w, &e);
  flush();
}
void click(QWidget *w, QPointF p) {
  mouse(w, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
  mouse(w, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
}
} // namespace
int verify_show_erase_ui() {
  using namespace zima;
  try {
    QString modal_error;
    QTimer modal_catcher;
    QObject::connect(&modal_catcher, &QTimer::timeout, [&] {
      if (auto *box =
              qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
        modal_error = box->text();
        box->accept();
      }
    });
    modal_catcher.start(25);
    workspace::Workspace workspace;
    auto part = document::PartDocument::create_default();
    auto sketch = sketcher::Sketch::create_default();
    auto lines = sketch.add_rectangle(-30, -20, 30, 20);
    sketch.dimensions.push_back(sketch.create_segment_dimension(lines.front()));
    const auto circle = sketch.add_circle(0, 0, 8, true);
    part.sketches.push_back(sketch);
    kernel::BodyResult body;
    body.mesh = sketch.viewer_mesh();
    body.mesh.dimensions.clear();
    body.mesh.edges = {
        {{{-30, -20, 0}, {30, -20, 0}}, {"model", "edge:bottom", {}}},
        {{{30, -20, 0}, {30, 20, 0}}, {"model", "edge:right", {}}},
        {{{30, 20, 0}, {-30, 20, 0}}, {"model", "edge:top", {}}},
        {{{-30, 20, 0}, {-30, -20, 0}}, {"model", "edge:left", {}}}};

    workspace.add_part(part, {body}, "source.prtz");
    auto sources = app::drawing_annotation_sources(&workspace, part.document_id,
                                                   "source.prtz");
    require(!sources.empty() && !sources[0].dimensions.empty(),
            "Sketch dimensions not collected");
    auto assembly = assembly::AssemblyDocument::create_default();
    for (int i = 0; i < 2; ++i) {
      assembly::PartOccurrence c;
      c.occurrence_id = "part-" + std::to_string(i);
      c.source_document_id = part.document_id;
      c.source_path = "source.prtz";
      c.placement.x = i * 70;
      c.name = "Part";
      assembly.components.push_back(c);
    }
    workspace.add_assembly(assembly, "source.asmz");
    auto repeated = app::drawing_annotation_sources(
        &workspace, assembly.document_id, "source.asmz");
    require(repeated.size() == 3 &&
                repeated[0].instance_path != repeated[1].instance_path,
            "Repeated occurrences collapsed");
    require(std::abs(repeated[1].axes[0].point.x -
                     repeated[0].axes[0].point.x - 70) < 1e-8,
            "Occurrence annotations not transformed");
    auto measured=assembly;
    measured.document_id="measured-assembly";
    measured.components[1].placement.rotation_z=23;
    assembly::ComponentPlacementReference angle;
    angle.mate_type=assembly::MateKind::PlaneAngle;angle.offset=23;
    angle.component_reference={assembly::MateReferenceKind::Face,assembly::InstancePath{}.child("part-1"),part.document_id+":origin","origin:plane:xz"};
    angle.target_reference={assembly::MateReferenceKind::Face,assembly::InstancePath{}.child("part-0"),part.document_id+":origin","origin:plane:xz"};
    measured.components[1].placement_references={angle};
    workspace.add_assembly(measured,"measured.asmz");
    const auto assembly_annotations=app::drawing_annotation_sources(&workspace,measured.document_id,"measured.asmz");
    require(assembly_annotations.back().dimensions.size()==1 && assembly_annotations.back().dimensions[0].kind==kernel::ViewerDimensionKind::Angular && std::abs(assembly_annotations.back().dimensions[0].value-23)<1e-8,"Assembly angle missing from Show/Erase sources");
    auto top = assembly::AssemblyDocument::create_default();
    assembly::PartOccurrence nested;
    nested.occurrence_id = "nested";
    nested.source_document_id = assembly.document_id;
    nested.source_kind = assembly::ComponentSourceKind::Assembly;
    nested.source_path = "source.asmz";
    nested.placement.x = 100;
    nested.placement.rotation_z = 90;
    top.components.push_back(nested);
    workspace.add_assembly(top, "top.asmz");
    const auto deep = app::drawing_annotation_sources(
        &workspace, top.document_id, "top.asmz");
    require(deep.size() == 4 && deep[0].instance_path != deep[1].instance_path,
            "Nested annotation paths collapsed");
    const auto before = repeated[0].axes[0].point,
               after = deep[0].axes[0].point;
    require(std::abs(after.x - (100 - before.y)) < 1e-8 &&
                std::abs(after.y - before.x) < 1e-8,
            "Nested rotation used the wrong owning frame");
    require(repeated[0].dimensions.empty() && repeated[1].dimensions.empty(),
            "Assembly drawing must not inherit Part dimensions");
    auto tilted = top;
    tilted.document_id = "tilted-annotation-test";
    tilted.components[0].placement.rotation_y = 90;
    workspace.add_assembly(tilted, "tilted.asmz");
    const auto tilted_sources = app::drawing_annotation_sources(
        &workspace, tilted.document_id, "tilted.asmz");
    drawing::DrawingView tilted_view;
    tilted_view.camera = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    drawing::refresh_model_annotations(tilted_view, tilted_sources);
    require(drawing::ShowEraseSession(tilted_view)
                .candidates(drawing::ShowEraseMode::Show,
                            {drawing::ModelAnnotationKind::Dimension})
                .empty(),
            "Tilted nested occurrence offered edge-on dimensions");
    auto derived = assembly;
    derived.document_id = "derived-test";
    auto mirror = derived.components[0];
    mirror.occurrence_id = "mirror";
    mirror.derived_copy = document::DerivedCopyParameters{};
    mirror.derived_copy->source_id = derived.components[0].occurrence_id;
    mirror.derived_copy->resolved_plane = {{}, {1, 0, 0}};
    derived.components.push_back(mirror);
    auto pattern = mirror;
    pattern.occurrence_id = "pattern";
    pattern.derived_copy->pattern = kernel::PatternRequest{};
    pattern.derived_copy->pattern->linear[0].local_axis = 0;
    pattern.derived_copy->pattern->linear[0].count = 3;
    pattern.derived_copy->pattern->linear[0].spacing = 40;
    pattern.derived_copy->pattern->linear[0].direction = {1, 0, 0};
    derived.components.push_back(pattern);
    workspace.add_assembly(derived, "derived.asmz");
    const auto copies = app::drawing_annotation_sources(
        &workspace, derived.document_id, "derived.asmz");
    require(copies.size() == 6,
            "Derived occurrence annotation count is incorrect");
    require(std::abs(copies[2].axes[0].point.x +
                     copies[0].axes[0].point.x) < 1e-8,
            "Mirror annotation was left at source position");
    require(std::abs(copies[3].axes[0].point.x -
                     copies[0].axes[0].point.x - 40) < 1e-8 &&
                copies[3].instance_path != copies[4].instance_path,
            "Pattern annotation lost copy transform or identity");
    drawing::DrawingView derived_view;
    derived_view.camera = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    drawing::refresh_model_annotations(derived_view, copies);
    require(
        drawing::ShowEraseSession(derived_view)
                .candidates(drawing::ShowEraseMode::Show,
                            {drawing::ModelAnnotationKind::Dimension})
                .empty(),
        "Derived Parts introduced Part dimensions into Assembly drawing");
    drawing::DrawingView axis_view;axis_view.scale=1;
    drawing::ModelAnnotation axial;axial.kind=drawing::ModelAnnotationKind::Axis;
    axial.curves={{{10,20},{10,20}}};
    auto cross=app::model_annotation_layout(axis_view,axial,{});
    require(cross.curves.size()==2 && cross.centers.size()==1 && cross.centers[0]==QPointF(10,20),"End-on hole lost its cross or center");
    axial.curves={{{30,20},{30,20}}};
    auto second_cross=app::model_annotation_layout(axis_view,axial,{});
    require(second_cross.centers[0]!=cross.centers[0],"Separate holes collapsed into one center mark");
    axial.curves={{{10,20},{10,60}}};
    auto side_axis=app::model_annotation_layout(axis_view,axial,{});
    require(side_axis.curves.size()==1 && side_axis.centers[0]==QPointF(10,40) && side_axis.curves[0][0]==QPointF(10,18) && side_axis.curves[0][1]==QPointF(10,62),"Side-on axis must retain cylinder span and midpoint");
    auto drawing = drawing::DrawingDocument::create_default();
    auto view = drawing::DrawingDocument::create_view(
        part.document_id, "source.prtz", body.mesh,
        drawing::ViewOrientation::Top);
    view.camera = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    drawing::refresh_view_geometry(view, body.mesh);
    require(view.projected_edges.size() == 4,
            "Model outline missing from annotation test");
    view.x = 140;
    view.y = 160;
    drawing::refresh_model_annotations(view, sources);
    for(const auto& annotation:view.model_annotations)
      if(annotation.kind==drawing::ModelAnnotationKind::Axis && annotation.source.owner_id==part.document_id+":origin") {
        const auto layout=app::model_annotation_layout(view,annotation,{});
        for(const auto& curve:layout.curves)
          require(QLineF(curve.front(),curve.back()).length()<=64.000001,"Origin axis inflated geometric Part bounds");
      }
    auto other = view;
    other.id = "other-view";
    other.x = 55;
    other.y = 60;
    drawing.sheets[0].views = {view, other};
    workspace.add_drawing(drawing);
    workspace.activate(drawing.document_id);
    workspace.display_top_level(drawing.document_id);
    app::DrawingWindow window(&workspace, false);
    window.edit_workspace_document(drawing.document_id);
    window.resize(1400, 950);
    window.show();
    flush();
    auto *canvas = window.findChild<QWidget *>("drawingCanvas");
    require(canvas, "Missing drawing canvas");
    auto* command=window.findChild<QAction *>("drawingShowEraseAction");
    require(command && command->isEnabled() && !command->icon().isNull(),"Show/Erase unavailable before view selection or missing icon");
    command->trigger();flush();
    auto* picked_dialog=window.findChild<QDialog *>("drawingShowEraseDialog");
    require(picked_dialog && picked_dialog->isVisible(),"Command must immediately open Show/Erase");
    auto* view_field=picked_dialog->findChild<QTableWidget*>("showEraseView");
    auto* view_item=dynamic_cast<ui::ReferenceCellItem*>(view_field->item(0,0));
    require(view_item && view_item->is_active_input() && !picked_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled(),"Empty view must arm input and disable OK");
    click(canvas,*window.view_rectangle_center_for_test(view.id));
    require(view_item->reference()==QString::fromStdString(view.id) && !view_item->is_active_input(),"Picked view did not populate reference");
    click(view_field->viewport(),view_field->visualItemRect(view_item).center());
    require(view_item->is_active_input(),"Reference field did not arm replacement");
    picked_dialog->findChild<QPushButton*>("showEraseErase")->click();
    require(picked_dialog->findChild<QPushButton*>("showEraseErase")->isChecked() && !picked_dialog->findChild<QPushButton*>("showEraseShow")->isChecked(),"Mode buttons are not exclusive");
    click(canvas,*window.view_rectangle_center_for_test(other.id));
    require(view_item->reference()==QString::fromStdString(other.id),"Reference field did not replace target view");
    click(view_field->viewport(),view_field->visualItemRect(view_item).center());
    const auto cancel_pick=*window.view_rectangle_center_for_test(other.id);
    mouse(canvas,QEvent::MouseButtonPress,cancel_pick,Qt::MiddleButton,Qt::MiddleButton);
    mouse(canvas,QEvent::MouseButtonRelease,cancel_pick,Qt::MiddleButton,Qt::NoButton);
    require(!view_item->is_active_input() && view_item->reference()==QString::fromStdString(other.id) && picked_dialog->isVisible(),"Short MMB did not end reference entry without commit");
    picked_dialog->findChild<QPushButton*>("showEraseShow")->click();
    picked_dialog->findChild<QPushButton*>("showEraseAll")->click();
    picked_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
    require(std::ranges::none_of(window.document_for_test().sheets[0].views[0].model_annotations,[](const auto& a){return a.visible;}) &&
            std::ranges::all_of(window.document_for_test().sheets[0].views[1].model_annotations,[](const auto& a){return a.visible;}),"Retargeted command changed wrong view");
    workspace.open_drawing(drawing.document_id)->document=drawing;
    window.edit_workspace_document(drawing.document_id);flush();
    window.select_view_for_test(view.id);
    window.fit_sheet();flush();
    const auto paper=window.sheet_rectangle_for_test();
    require(std::abs(paper.height()-(canvas->height()-48))<1e-8 && (paper.center()-QPointF(canvas->width()/2.0,canvas->height()/2.0)).manhattanLength()<1e-8,"Fit drawing did not center paper and maximize its height");
    auto *action = window.findChild<QAction *>("drawingShowEraseAction");
    require(action && action->isEnabled(),
            "Show/Erase action disabled for view");
    const auto open = [&]() {
      action->trigger();
      flush();
      for (auto *d : window.findChildren<QDialog *>("drawingShowEraseDialog"))
        if (d->isVisible())
          return d;
      return static_cast<QDialog *>(nullptr);
    };
    const auto &state = window.document_for_test();
    auto *dialog = open();
    require(dialog && (dialog->windowFlags() & Qt::SubWindow),
            "Show/Erase must use shared internal window");
    auto *items = dialog->findChild<QTreeWidget *>("showEraseItems");
    require(items && items->topLevelItemCount() > 1,
            "No source candidates in Show");
    dialog->findChild<QPushButton *>("showEraseAll")->click();
    require(std::ranges::none_of(state.sheets[0].views[0].model_annotations,
                                 [](const auto &a) { return a.visible; }),
            "Preview committed before OK");
    dialog->findChild<QDialogButtonBox *>()
        ->button(QDialogButtonBox::Cancel)
        ->click();
    flush();
    require(std::ranges::none_of(state.sheets[0].views[0].model_annotations,
                                 [](const auto &a) { return a.visible; }),
            "Cancel changed visibility");
    dialog = open();
    items = dialog->findChild<QTreeWidget *>("showEraseItems");
    const auto dim =
        std::ranges::find_if(view.model_annotations, [](const auto &a) {
          return a.kind == drawing::ModelAnnotationKind::Dimension;
        });
    require(dim != view.model_annotations.end(), "No model dimension");
    auto point = window.model_annotation_handle_for_test(dim->source);
    require(point.has_value(), "Offered dimension has no View hit");
    click(canvas, *point);
    items = dialog->findChild<QTreeWidget *>("showEraseItems");
    bool checked = false;
    for (int i = 0; i < items->topLevelItemCount(); ++i)
      checked |= items->topLevelItem(i)->checkState(0) == Qt::Checked;
    require(checked, "View click did not select same command candidate");
    dialog->findChild<QPushButton *>("showEraseAll")->click();
    window.grab().save("build/show-erase-dialog.png");
    mouse(canvas, QEvent::MouseButtonRelease, *point, Qt::MiddleButton,
          Qt::NoButton);
    require(dialog->isVisible(), "Short MMB committed dialog");
    mouse(canvas, QEvent::MouseButtonDblClick, *point, Qt::MiddleButton,
          Qt::MiddleButton);
    flush();
    require(std::ranges::all_of(state.sheets[0].views[0].model_annotations,
                                [](const auto &a) { return a.visible; }),
            "MMB double click did not commit Show");
    require(std::ranges::none_of(state.sheets[0].views[1].model_annotations,
                                 [](const auto &a) { return a.visible; }),
            "Show leaked into second view");
    window.grab().save("build/show-erase-visible.png");
    point = window.model_annotation_handle_for_test(dim->source);
    require(point.has_value(), "Confirmed dimension not shown");
    mouse(canvas, QEvent::MouseMove, *point, Qt::NoButton, Qt::NoButton);
    mouse(canvas, QEvent::MouseButtonPress, *point, Qt::LeftButton,
          Qt::LeftButton);
    mouse(canvas, QEvent::MouseMove, *point + QPointF(35, 20), Qt::NoButton,
          Qt::LeftButton);
    mouse(canvas, QEvent::MouseButtonPress, *point + QPointF(35, 20),
          Qt::RightButton, Qt::LeftButton | Qt::RightButton);
    mouse(canvas, QEvent::MouseButtonRelease, *point + QPointF(35, 20),
          Qt::RightButton, Qt::LeftButton);
    require(std::ranges::all_of(workspace.open_drawing(drawing.document_id)->document.sheets[0].views[0].model_annotations,
                [](const auto& item){return !item.view_layout;}),
            "Releasing RMB committed an unfinished LMB drawing grip");
    mouse(canvas, QEvent::MouseButtonRelease, *point + QPointF(35, 20),
          Qt::LeftButton, Qt::NoButton);
    const auto &moved = state.sheets[0].views[0].model_annotations;
    const auto found = std::ranges::find(moved, dim->source,
                                         &drawing::ModelAnnotation::source);
    require(found != moved.end() && (found->paper_handles.contains("text")||found->view_layout.has_value()),
            "Dimension text handle did not move");
    require(found->view_layout && found->view_layout->arrows_reversed,
            "RMB during drawing dimension grip did not reverse arrows");
    require(found->value == dim->value && state.sheets[0].views[1].model_annotations.front().view_layout==std::nullopt,
            "Drawing appearance changed measurement or another view");
    const auto handles = found->paper_handles;
    QTemporaryDir dir;
    const auto jpg=dir.filePath("current-view.jpg");
    window.export_jpg(jpg.toStdString());
    const QImage exported_jpg(jpg);
    require(!exported_jpg.isNull() && exported_jpg.size()==canvas->size()*canvas->devicePixelRatioF(),"JPG did not capture actual drawing viewport");
    const auto dxf=dir.filePath("current-sheet.dxf");
    window.export_dxf(dxf.toStdString());
    QFile dxf_file(dxf);require(dxf_file.open(QIODevice::ReadOnly),"DXF missing");
    const auto dxf_bytes=dxf_file.readAll();
    require(dxf_bytes.contains("$INSUNITS\n70\n4") && dxf_bytes.contains("\nLINE\n") && dxf_bytes.contains("\nTEXT\n"),"DXF missing millimetres, outlines or text");
    if(const auto output=qEnvironmentVariable("ZIMA_TEST_DXF_OUTPUT");!output.isEmpty()) {
        window.export_dxf(output.toStdString());
        window.render_sheet_for_test(true).save(output+".png");
        window.export_jpg((output+".jpg").toStdString());
    }

    auto multi_sheet=state;
    auto second_sheet=multi_sheet.sheets[0];second_sheet.id="second-sheet";
    second_sheet.views.clear();second_sheet.dimensions.clear();
    multi_sheet.sheets.push_back(second_sheet);
    workspace.open_drawing(drawing.document_id)->document=multi_sheet;
    window.edit_workspace_document(drawing.document_id);
    window.findChild<QTabBar*>("drawingSheetTabs")->setCurrentIndex(1);flush();
    const auto second_dxf=dir.filePath("second-sheet.dxf");window.export_dxf(second_dxf.toStdString());
    QFile second_file(second_dxf);require(second_file.open(QIODevice::ReadOnly),"Second sheet DXF missing");
    require(!second_file.readAll().contains("60 mm"),"DXF included dimensions from another sheet");
    multi_sheet.sheets.pop_back();workspace.open_drawing(drawing.document_id)->document=multi_sheet;
    window.edit_workspace_document(drawing.document_id);flush();
    state.save((dir.path() + "/show-erase.drwz").toStdString());
    auto loaded = drawing::DrawingDocument::load(
        (dir.path() + "/show-erase.drwz").toStdString());
    require(loaded.sheets[0].views[0].model_annotations ==
                state.sheets[0].views[0].model_annotations,
            "Annotations did not survive reopen");
    // Toggling view-only guide state must leave the print/PDF paint path
    // identical.
    auto replacement = state;
    replacement.sheets[0].views[0].show_dimension_guides = false;
    workspace.open_drawing(drawing.document_id)->document = replacement;
    window.edit_workspace_document(drawing.document_id);
    auto without = window.render_sheet_for_test(true);
    replacement.sheets[0].views[0].show_dimension_guides = true;
    workspace.open_drawing(drawing.document_id)->document = replacement;
    window.edit_workspace_document(drawing.document_id);
    auto with = window.render_sheet_for_test(true);
    require(with == without, "Working guides leaked into print/PDF rendering");
    require(replacement.sheets[0].views[0].dimension_guide_offset == 8 &&
                replacement.sheets[0].views[0].dimension_guide_spacing == 8,
            "Guide default must be 8 mm");
    auto tilted_drawing = replacement;
    tilted_drawing.sheets[0].views[0].camera={{.7071067811865476,0,.7071067811865476},{0,1,0},{-.7071067811865476,0,.7071067811865476}};
    workspace.open_drawing(drawing.document_id)->document = tilted_drawing;
    window.edit_workspace_document(drawing.document_id);
    const auto filtered_print = window.render_sheet_for_test(true);
    const auto filtered_view = window.render_sheet_for_test(false);
    for (auto &annotation : tilted_drawing.sheets[0].views[0].model_annotations)
      if (annotation.kind == drawing::ModelAnnotationKind::Dimension)
        annotation.visible = false;
    workspace.open_drawing(drawing.document_id)->document = tilted_drawing;
    window.edit_workspace_document(drawing.document_id);
    require(filtered_print != window.render_sheet_for_test(true) &&
                filtered_view != window.render_sheet_for_test(false),
            "Oblique stored dimensions disappeared from View or PDF");
    for (auto &annotation : replacement.sheets[0].views[1].model_annotations)
      annotation.visible = true;
    workspace.open_drawing(drawing.document_id)->document = replacement;
    window.edit_workspace_document(drawing.document_id);
    window.select_view_for_test(view.id);
    dialog = open();
    dialog->findChild<QPushButton *>("showEraseErase")->click();
    flush();
    auto other_point =
        window.model_annotation_handle_for_test(dim->source, 0, other.id);
    require(other_point.has_value(), "Second view dimension is missing");
    click(canvas, *other_point);
    items = dialog->findChild<QTreeWidget *>("showEraseItems");
    for (int i = 0; i < items->topLevelItemCount(); ++i)
      require(items->topLevelItem(i)->checkState(0) == Qt::Unchecked,
              "Click in another view selected an active Show/Erase candidate");
    dialog->findChild<QPushButton *>("showEraseAll")->click();
    dialog->findChild<QDialogButtonBox *>()
        ->button(QDialogButtonBox::Ok)
        ->click();
    flush();
    require(std::ranges::none_of(state.sheets[0].views[0].model_annotations,
                                 [](const auto &a) { return a.visible; }),
            "Erase did not remove selected visible items");
    require(modal_error.isEmpty(), modal_error.toUtf8().constData());
    window.grab().save("build/show-erase-window.png");
    std::cout << "Show/Erase source, occurrence transforms, View selection, "
                 "Cancel, MMB, handles and PDF contracts passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
