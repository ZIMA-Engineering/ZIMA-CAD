#include <zima/drawing/annotation_guides.hpp>
#include <zima/workspace/drawing_annotation_operations.hpp>
#include <zima/workspace/drawing_projection.hpp>
#include <QKeyEvent>
#include <QTabBar>
#include <QContextMenuEvent>
#include <QMenu>
#include <QLineEdit>
#include <zima/command_host/host.hpp>
#include "drawing_annotation_layout_test_support.hpp"
#include <QBuffer>
#include <zima/viewer/dimension_text_layer.hpp>
#include <QTableWidget>
#include <zima/ui/reference_cell.hpp>
#include <QFile>
#include <QImage>
#include "drawing_annotation_layout.hpp"
#include <zima/workspace/drawing_sources.hpp>
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
bool shown_without_origin(const zima::drawing::ModelAnnotation& item) {
  return item.visible == !(item.kind == zima::drawing::ModelAnnotationKind::Axis &&
                           zima::drawing::origin_annotation(item.source));
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
void verify_driving_value_ui() {
    using namespace zima;
    workspace::Workspace live;kernel::OcctKernel kernel;
    QTemporaryDir linked_save;
    const auto linked_source=std::filesystem::path(linked_save.path().toStdString())/"source.prtz";
    const auto linked_drawing=std::filesystem::path(linked_save.path().toStdString())/"drawing.drwz";
    auto part=document::PartDocument::create_default();
    auto profile=sketcher::Sketch::create_default();const auto lines=profile.add_rectangle(0,0,20,10);
    profile.dimensions={profile.create_segment_dimension(lines.front())};
    auto box=document::PartDocument::create_extrusion_container(profile.id);box.extrusion.length_forward=6;profile.owner_container_id=box.id;part.sketches={profile};part.history={box};
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Body"));graph.insert({document::PartHistoryKind::Feature,box.id});part.set_body_history(graph);part.synchronize_dimension_identifiers();
    auto cache=kernel.evaluate_history(part.kernel_operations());
    part.save(linked_source,cache);live.add_part(part,cache,linked_source);
    auto doc=drawing::DrawingDocument::create_default();
    auto view=drawing::DrawingDocument::create_view(part.document_id,linked_source,cache.back().mesh,drawing::ViewOrientation::Top);
    view.x=100;view.y=160;workspace::DrawingProjection projection(&live,linked_drawing);projection.project(view,{});
    drawing::ModelAnnotationReference ref{part.document_id,profile.id,"dimension:"+profile.dimensions.front().id,{}};
    bool found=false;
    for(auto& a:view.model_annotations)if(a.source==ref){a.visible=true;found=true;}
    require(found,"Sketch driving dimension missing");
    auto second=view;second.id="second";second.x=50;second.y=60;
    doc.sheets[0].views={view,second};live.add_drawing(doc,linked_drawing);live.activate(doc.document_id);live.display_top_level(doc.document_id);
    app::DrawingWindow window(&live,false);window.edit_workspace_document(doc.document_id);window.resize(1400,950);window.show();window.fit_sheet();flush();
    auto* canvas=window.findChild<QWidget*>("drawingCanvas");require(canvas,"Drawing canvas missing");
    const auto open=[&]() {
        canvas->grab();flush();
        const auto point=window.model_annotation_handle_for_test(ref,0,view.id);require(point.has_value(),"Dimension text missing");
        click(canvas,*point);
        mouse(canvas,QEvent::MouseButtonDblClick,*point,Qt::LeftButton,Qt::LeftButton);
        mouse(canvas,QEvent::MouseButtonRelease,*point,Qt::LeftButton,Qt::NoButton);
        auto* edit=window.findChild<QLineEdit*>("inlineDimensionValueEdit");
        require(edit,"Double-click did not open shared dimension editor");return edit;
    };
    auto* edit=open();edit->setText("32");
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(edit,&escape);flush();
    require(live.open_part(part.document_id)->session.document().sketches.front().dimensions.front().value==20,"Escape changed source");
    edit=open();edit->setText("12+18");
    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(edit,&enter);flush();
    require(live.open_part(part.document_id)->session.document().sketches.front().dimensions.front().value==30,"Drawing edit did not update source Sketch");
    require(live.active_document_id()==doc.document_id&&live.displayed_document_id()==doc.document_id,"Dimension edit activated source");
    for(const auto& v:window.document_for_test().sheets[0].views) {
        const auto& a=workspace::drawing_annotation(window.document_for_test(),v.id,ref);
        require(std::abs(a.value-30)<1e-7,"Another view retained old dimension value");
    }
    require(live.drawing_edited_sources[doc.document_id].contains(part.document_id),"Drawing value edit did not register its source for Save");
    auto* save=window.findChild<QAction*>("drawingSaveAction");
    if(!save)for(auto* action:window.findChildren<QAction*>())if(action->shortcut()==QKeySequence::Save){save=action;break;}
    require(save,"Drawing Save action missing");save->trigger();flush();
    require(document::PartDocument::load(linked_source).sketches.front().dimensions.front().value==30,"Drawing Save did not persist its changed source");
    require(workspace::drawing_annotation(drawing::DrawingDocument::load(linked_drawing),view.id,ref).value==30,"Drawing Save did not persist matching dimensions");
    auto draft=window.document_for_test();bool rejected=false;
    try {workspace::set_drawing_model_dimension(live,kernel,draft,{},view.id,ref,std::numeric_limits<double>::infinity());}catch(const std::exception&){rejected=true;}
    require(rejected&&live.open_part(part.document_id)->session.document().sketches.front().dimensions.front().value==30,"Invalid value changed model");
    require(workspace::drawing_annotation(draft,view.id,ref).value==30,"Invalid value changed Drawing");
    const auto calculated=live.open_part(part.document_id)->session.calculated_boundaries();
    const auto envelope=kernel::model_envelope(calculated.back().mesh);
    require(envelope.valid&&std::abs(envelope.maximum.x-envelope.minimum.x-30)<1e-6,"Source solid did not change width");
    auto broken=draft;broken.sheets[0].views[1].source_document_id="missing-source";broken.sheets[0].views[1].source_path="missing-source.prtz";
    rejected=false;try {workspace::set_drawing_model_dimension(live,kernel,broken,{},view.id,ref,35);}catch(const std::exception&){rejected=true;}
    require(rejected&&live.open_part(part.document_id)->session.document().sketches.front().dimensions.front().value==30,"Failed projection committed a source edit");
    QTemporaryDir saved;
    const auto source_file=std::filesystem::path(saved.path().toStdString())/"source.prtz";
    const auto drawing_file=std::filesystem::path(saved.path().toStdString())/"drawing.drwz";
    live.open_part(part.document_id)->session.document().save(source_file,calculated);draft.save(drawing_file);
    auto reopened=document::PartDocument::load(source_file);
    require(reopened.sketches.front().dimensions.front().value==30,"Edited source value did not persist");
    const auto reopened_drawing=drawing::DrawingDocument::load(drawing_file);
    require(workspace::drawing_annotation(reopened_drawing,view.id,ref).value==30,"Edited Drawing value did not persist");
    auto locked=live.open_part(part.document_id)->session.document();locked.sketches.front().dimensions.front().locked=true;
    live.open_part(part.document_id)->session.commit(locked,live.open_part(part.document_id)->session.calculated_boundaries());
    rejected=false;try {workspace::set_drawing_model_dimension(live,kernel,draft,{},view.id,ref,40);}catch(const std::exception&){rejected=true;}
    require(rejected,"Stale cached unlocked annotation bypassed source lock");
    workspace::Workspace closed;
    auto closed_drawing=draft;
    for(auto& v:closed_drawing.sheets[0].views)v.source_path=source_file;
    closed.add_drawing(closed_drawing);closed.activate(closed_drawing.document_id);closed.display_top_level(closed_drawing.document_id);
    workspace::set_drawing_model_dimension(closed,kernel,closed_drawing,{},view.id,ref,32);
    require(closed.open_part(part.document_id)&&closed.open_part(part.document_id)->session.document().sketches.front().dimensions.front().value==32,"Closed native source did not open for edit");
    require(closed.active_document_id()==closed_drawing.document_id,"Opening source activated its tab");
    // Snap the displayed text grip, with visible feedback owned by this view.
    const auto& snap_view=window.document_for_test().sheets[0].views[0];
    const auto guides=drawing::annotation_guides(snap_view);
    const auto guide=std::ranges::find_if(guides,[](const auto& g){return std::abs(g.first.y-g.second.y)<1e-8&&std::abs(g.first.x-g.second.x)>1;});
    require(guide!=guides.end(),"Model dimension has no snap guides");
    const auto sheet_rect=window.sheet_rectangle_for_test();const double zoom=sheet_rect.height()/window.document_for_test().sheets[0].height_mm();
    const drawing::Point2 paper{(guide->first.x+guide->second.x)/2,guide->first.y};
    const QPointF target(sheet_rect.right()-snap_view.x*zoom+paper.x*zoom,sheet_rect.bottom()-snap_view.y*zoom-paper.y*zoom);
    canvas->grab();const auto start=window.model_annotation_handle_for_test(ref,0,view.id);require(start.has_value(),"Model text grip unavailable");
    mouse(canvas,QEvent::MouseMove,*start,Qt::NoButton,Qt::NoButton);
    mouse(canvas,QEvent::MouseButtonPress,*start,Qt::LeftButton,Qt::LeftButton);
    mouse(canvas,QEvent::MouseMove,target+QPointF(0,1),Qt::NoButton,Qt::LeftButton);
    require(canvas->property("annotationSnapActive").toBool()&&canvas->property("annotationSnapView").toString().toStdString()==view.id,"Text grip did not display its own view snap");
    window.grab().save("build/drawing-snap-feedback.png");
    mouse(canvas,QEvent::MouseButtonRelease,target+QPointF(0,1),Qt::LeftButton,Qt::NoButton);canvas->grab();
    require(!canvas->property("annotationSnapActive").toBool(),"Snap marker survived drag completion");
    const auto after=window.model_annotation_handle_for_test(ref,0,view.id);
    require(after&&QLineF(*after,target).length()<2,"Snap marker did not match final dimension text grip");
    window.grab().save("build/drawing-inline-value.png");
    std::cout<<"Drawing driving value: shared GUI, Escape, expression, source update, all views, invalid value and locks passed\n";
}
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
    body.mesh.axes.clear();body.mesh.points.clear();
    body.mesh.edges = {
        {{{-30, -20, 0}, {30, -20, 0}}, {"model", "edge:bottom", {}}},
        {{{30, -20, 0}, {30, 20, 0}}, {"model", "edge:right", {}}},
        {{{30, 20, 0}, {-30, 20, 0}}, {"model", "edge:top", {}}},
        {{{-30, 20, 0}, {-30, -20, 0}}, {"model", "edge:left", {}}}};

    body.mesh.axes.push_back({{0,0,0},{0,1,0},40,{"model","axis:authored",{}}});
    workspace.add_part(part, {body}, "source.prtz");
    auto sources = workspace::drawing_annotation_sources(&workspace, part.document_id,
                                                   "source.prtz");
    require(!sources.empty() && !sources[0].dimensions.empty(),
            "Sketch dimensions not collected");
    // A feature axis derived from a Sketch centerline owns the same persisted
    // reference as that construction segment.  It is one selectable Drawing
    // annotation, represented by the axis rather than two ambiguous entries.
    auto axis_part=part;
    axis_part.document_id="axis-source";
    const auto centerline=axis_part.sketches.front().add_segment(0,-25,0,25,true);
    auto axis_body=body;
    axis_body.mesh.axes.push_back({{0,-25,0},{0,1,0},50,
        {axis_part.sketches.front().id,centerline,{}}});
    workspace.add_part(axis_part,{axis_body},"axis-source.prtz");
    const auto axis_sources=workspace::drawing_annotation_sources(
        &workspace,axis_part.document_id,"axis-source.prtz");
    require(axis_sources.size()==1&&
        std::ranges::count_if(axis_sources.front().axes,[&](const auto& axis) {
          return axis.reference.owner_id==axis_part.sketches.front().id&&
              axis.reference.semantic_key==centerline;
        })==1&&
        std::ranges::none_of(axis_sources.front().construction,[&](const auto& edge) {
          return edge.reference.owner_id==axis_part.sketches.front().id&&
              edge.reference.semantic_key==centerline;
        }),"Feature axis and its source centerline remained ambiguous");
    zima::drawing::DrawingView axis_annotation_view;
    axis_annotation_view.camera={{1,0,0},{0,1,0},{0,0,1}};
    zima::drawing::refresh_model_annotations(axis_annotation_view,axis_sources);
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
    auto repeated = workspace::drawing_annotation_sources(
        &workspace, assembly.document_id, "source.asmz");
    require(repeated.size() == 3 &&
                repeated[0].instance_path != repeated[1].instance_path,
            "Repeated occurrences collapsed");
    require(std::abs(repeated[1].construction.at(0).points.at(0).x -
                     repeated[0].construction.at(0).points.at(0).x - 70) < 1e-8,
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
    const auto assembly_annotations=workspace::drawing_annotation_sources(&workspace,measured.document_id,"measured.asmz");
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
    const auto deep = workspace::drawing_annotation_sources(
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
    const auto tilted_sources = workspace::drawing_annotation_sources(
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
    const auto copies = workspace::drawing_annotation_sources(
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
    require(cross.curves.size()==4 && cross.centers.size()==1 && cross.centers[0]==QPointF(10,20),"End-on hole lost its cross or center");
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
            std::ranges::all_of(window.document_for_test().sheets[0].views[1].model_annotations,shown_without_origin),"Retargeted command changed wrong view");
    for(bool accept:{false,true}) {
      command->trigger();flush();
      auto* multi=window.findChild<QDialog*>("drawingShowEraseDialog");
      auto* field=multi->findChild<QTableWidget*>("showEraseView");
      auto* entry=dynamic_cast<ui::ReferenceCellItem*>(field->item(0,0));
      multi->findChild<QPushButton*>("showEraseErase")->click();
      multi->findChild<QPushButton*>("showEraseAll")->click();
      const auto next=*window.view_rectangle_center_for_test(view.id);
      mouse(canvas,QEvent::MouseButtonPress,next,Qt::MiddleButton,Qt::MiddleButton);
      mouse(canvas,QEvent::MouseButtonRelease,next,Qt::MiddleButton,Qt::NoButton);
      require(entry->is_active_input() && multi->isVisible(),"Short MMB did not request next view");
      click(canvas,next);
      multi->findChild<QPushButton*>("showEraseShow")->click();multi->findChild<QPushButton*>("showEraseAll")->click();
      mouse(canvas,QEvent::MouseButtonPress,next,Qt::MiddleButton,Qt::MiddleButton);
      mouse(canvas,QEvent::MouseButtonRelease,next,Qt::MiddleButton,Qt::NoButton);
      const auto& unchanged=workspace.open_drawing(drawing.document_id)->document().sheets[0].views;
      require(std::ranges::none_of(unchanged[0].model_annotations,[](const auto& a){return a.visible;}) &&
              std::ranges::all_of(unchanged[1].model_annotations,shown_without_origin),"Changing Show/Erase target committed before OK");
      if(accept)mouse(canvas,QEvent::MouseButtonDblClick,next,Qt::MiddleButton,Qt::MiddleButton);
      else multi->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
      flush();
      const auto& result=window.document_for_test().sheets[0].views;
      require(std::ranges::all_of(result[0].model_annotations,[&](const auto& a){return accept?shown_without_origin(a):!a.visible;}) &&
              std::ranges::all_of(result[1].model_annotations,[&](const auto& a){return accept?!a.visible:shown_without_origin(a);}),"Multi-view Show/Erase did not commit/cancel all pending views together");
      window.select_view_for_test(other.id);
    }
    workspace.open_drawing(drawing.document_id)->commit(drawing);
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
                                shown_without_origin),
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
    require(std::ranges::all_of(workspace.open_drawing(drawing.document_id)->document().sheets[0].views[0].model_annotations,
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
    {
        // A console edit and actual context-menu Properties share the same local layout.
        const auto initial=workspace.open_drawing(drawing.document_id)->document();
        kernel::OcctKernel kernel;auto directory=std::filesystem::current_path();command_host::Host host(workspace,kernel,directory);
        const auto reference=commands::Json{{"source_document",dim->source.document_id},{"owner",dim->source.owner_id},{"key",dim->source.semantic_id},{"instance_path",dim->source.instance_path}};
        const auto args=commands::Json{{"view",view.id},{"reference",reference}};
        const auto run=[&](const char* command,commands::Json arguments=commands::Json::object()) {
            const auto result=host.execute({{"command",command},{"arguments",std::move(arguments)}});
            if(!result.ok)throw std::runtime_error(result.code+": "+result.message);return result.data;
        };
        auto edit=args;edit["style"]={{"prefix","CLI "}};run("drawing.annotation.set",edit);
        window.edit_workspace_document(drawing.document_id);flush();
        const auto open_properties=[&]() {
            const auto handle=window.model_annotation_handle_for_test(dim->source,0,view.id);require(handle.has_value(),"Edited annotation has no handle");
            // RMB at the exact grip cycles presentation; Properties is on the text away from the grip.
            const auto text_point=*handle+QPointF(12,0);
            mouse(canvas,QEvent::MouseMove,text_point,Qt::NoButton,Qt::NoButton);click(canvas,text_point);
            QContextMenuEvent event(QContextMenuEvent::Mouse,text_point.toPoint(),canvas->mapToGlobal(text_point.toPoint()));QApplication::sendEvent(canvas,&event);flush();
            auto* action=window.findChild<QAction*>("dimensionLayoutPropertiesAction");require(action,"Model dimension context Properties unavailable");action->trigger();
            for(auto* menu:window.findChildren<QMenu*>())if(menu->isVisible())menu->close();flush();
            auto* dialog=window.findChild<QDialog*>("dimensionPropertiesDialog");require(dialog&&dialog->isVisible(),"Model dimension Properties did not open");return dialog;
        };
        auto* properties=open_properties();auto* prefix=properties->findChild<QLineEdit*>("sketchDimensionPrefix");
        require(prefix&&prefix->text()=="CLI ","GUI ignored command annotation style");prefix->setText("Pending ");
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        require(run("drawing.annotation.get",args).at("style").at("prefix")=="CLI ","Properties Cancel committed annotation style");
        properties=open_properties();properties->findChild<QLineEdit*>("sketchDimensionPrefix")->setText("GUI ");
        properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        const auto gui=run("drawing.annotation.get",args);require(gui.at("style").at("prefix")=="GUI "&&gui.at("value")==dim->value,"Shared Properties changed value or failed to commit style");
        run("undo");require(run("drawing.annotation.get",args).at("style").at("prefix")=="CLI ","GUI style Undo did not restore CLI style");
        run("redo");require(run("drawing.annotation.get",args).at("style").at("prefix")=="GUI ","GUI style Redo failed");
        run("undo");run("undo");window.edit_workspace_document(drawing.document_id);flush();
        require(annotation_layout_test::snapshot(workspace.open_drawing(drawing.document_id)->document())==annotation_layout_test::snapshot(initial),"GUI and CLI style transactions did not restore original drawing");
    }

    QTemporaryDir dir;
    const auto jpg=dir.filePath("current-view.jpg");
    QByteArray expected_jpeg;QBuffer expected_buffer(&expected_jpeg);expected_buffer.open(QIODevice::WriteOnly);
    require(canvas->grab().toImage().save(&expected_buffer,"JPG",95),"Reference viewport JPEG failed");
    window.export_jpg(jpg.toStdString());
    QFile actual_jpeg(jpg);require(actual_jpeg.open(QIODevice::ReadOnly)&&actual_jpeg.readAll()==expected_jpeg,"Shared image writer changed the captured GUI viewport");
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
    workspace.open_drawing(drawing.document_id)->commit(multi_sheet);
    window.edit_workspace_document(drawing.document_id);
    window.findChild<QTabBar*>("drawingSheetTabs")->setCurrentIndex(1);flush();
    const auto second_dxf=dir.filePath("second-sheet.dxf");window.export_dxf(second_dxf.toStdString());
    QFile second_file(second_dxf);require(second_file.open(QIODevice::ReadOnly),"Second sheet DXF missing");
    require(!second_file.readAll().contains("\n1\n60\n"),"DXF included dimensions from another sheet");
    multi_sheet.sheets.pop_back();workspace.open_drawing(drawing.document_id)->commit(multi_sheet);
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
    workspace.open_drawing(drawing.document_id)->commit(replacement);
    window.edit_workspace_document(drawing.document_id);
    auto without = window.render_sheet_for_test(true);
    replacement.sheets[0].views[0].show_dimension_guides = true;
    workspace.open_drawing(drawing.document_id)->commit(replacement);
    window.edit_workspace_document(drawing.document_id);
    auto with = window.render_sheet_for_test(true);
    require(with == without, "Working guides leaked into print/PDF rendering");
    require(replacement.sheets[0].views[0].dimension_guide_offset == 8 &&
                replacement.sheets[0].views[0].dimension_guide_spacing == 8,
            "Guide default must be 8 mm");
    auto tilted_drawing = replacement;
    tilted_drawing.sheets[0].views[0].camera={{.7071067811865476,0,.7071067811865476},{0,1,0},{-.7071067811865476,0,.7071067811865476}};
    workspace.open_drawing(drawing.document_id)->commit(tilted_drawing);
    window.edit_workspace_document(drawing.document_id);
    const auto filtered_print = window.render_sheet_for_test(true);
    const auto filtered_view = window.render_sheet_for_test(false);
    for (auto &annotation : tilted_drawing.sheets[0].views[0].model_annotations)
      if (annotation.kind == drawing::ModelAnnotationKind::Dimension)
        annotation.visible = false;
    workspace.open_drawing(drawing.document_id)->commit(tilted_drawing);
    window.edit_workspace_document(drawing.document_id);
    require(filtered_print != window.render_sheet_for_test(true) &&
                filtered_view != window.render_sheet_for_test(false),
            "Oblique stored dimensions disappeared from View or PDF");
    for (auto &annotation : replacement.sheets[0].views[1].model_annotations)
      annotation.visible = true;
    workspace.open_drawing(drawing.document_id)->commit(replacement);
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
    {
      auto proof_document=drawing;
      auto& proof_view=proof_document.sheets[0].views[0];
      for(auto& annotation:proof_view.model_annotations)annotation.visible=true;
      const auto annotation=std::ranges::find(proof_view.model_annotations,dim->source,&drawing::ModelAnnotation::source);
      QFont font=canvas->font();font.setPixelSize(7);
      const QString text=QString::fromStdString(annotation->text);
      const auto presentation=app::model_annotation_layout(proof_view,*annotation,{},QFontMetricsF(font).horizontalAdvance(text)/2);
      const QPointF baseline(2*(proof_document.sheets[0].width_mm()-proof_view.x+presentation.text.x()),
                             2*(proof_document.sheets[0].height_mm()-proof_view.y-presentation.text.y()));
      require(std::abs(presentation.text_angle)<1e-9,"Hatch masking fixture must be horizontal");
      workspace.open_drawing(drawing.document_id)->commit(proof_document);
      window.edit_workspace_document(drawing.document_id);flush();
      const auto clean=window.render_sheet_for_test(true);
      const auto box=viewer::dimension_text_box(font,text,1).translated(baseline);
      const double hatch_y=presentation.text.y()-QFontMetricsF(font).tightBoundingRect(text).center().y()/2;
      auto hatch=proof_view.projected_edges.front();hatch.hatch=true;hatch.hidden=false;
      hatch.points={{(presentation.text.x()-5)/proof_view.scale,hatch_y/proof_view.scale},
                    {(presentation.text.x()+QFontMetricsF(font).horizontalAdvance(text)/2+5)/proof_view.scale,hatch_y/proof_view.scale}};
      proof_view.projected_edges.push_back(hatch);
      workspace.open_drawing(drawing.document_id)->commit(proof_document);
      window.edit_workspace_document(drawing.document_id);flush();
      const auto hatched=window.render_sheet_for_test(true);
      require(clean!=hatched,"Hatch mask fixture did not draw a hatch");
      const QRect interior=box.adjusted(1,1,-1,-1).toAlignedRect().intersected(clean.rect());
      require(!interior.isEmpty() && clean.copy(interior)==hatched.copy(interior),
              "Actual drawing text mask did not cover hatching in print path");
      hatched.save("build/dimension-hatch-mask-print.png");
    }
    // An XZ sketch belongs to a real calculated feature packet. The cached
    // solid intentionally contains no Sketch edges/points or dimension planes.
    {
      auto sample=document::PartDocument::create_default();
      auto container=document::PartDocument::create_sketch_container();sample.history.push_back(container);
      auto profile=sketcher::Sketch::create_default();profile.owner_container_id=container.id;
      profile.plane=sketcher::SketchPlane::XZ;profile.refresh_default_frame();
      auto rectangle=profile.add_rectangle(-10,-10,30,10);
      auto left=profile.add_circle(0,0,5),right=profile.add_circle(20,0,5);
      profile.dimensions={profile.create_circle_radius_dimension(left),profile.create_circle_diameter_dimension(right),profile.create_segment_dimension(rectangle[0])};
      sample.sketches.push_back(profile);
      kernel::BodyResult solid;
      solid.mesh.edges={{{{-10,-5,-10},{30,5,10}},{container.feature_id,"rim",{}}}};
      solid.mesh.axes={{{0,0,0},{0,1,0},100,{container.feature_id,"axis:primary",{}}},
                       {{20,0,0},{0,1,0},100,{container.feature_id,"axis:profile:2",{}}}};
      workspace.add_part(sample,{solid},"controls.prtz");
      auto packets=workspace::drawing_annotation_sources(&workspace,sample.document_id,"controls.prtz");
      require(packets.size()==1 && packets[0].dimensions.size()==3,"Rotated profile dimensions missing");
      const auto frame=packets[0].object_frames.at({profile.id,{}});
      require(frame.maximum.x-frame.minimum.x<=40.000001 && frame.maximum.y-frame.minimum.y<=20.000001,"Sketch working axes inflated drawing bounds");
      require(packets[0].axis_frames.size()==2,"Hole axes have no individual cylinder bounds");
      for(const auto& d:packets[0].dimensions)require(std::abs(d.plane_normal.y)>0.999,"XZ dimension lost its sketch plane");
      auto occurrences=assembly::AssemblyDocument::create_default();
      for(int i=0;i<2;++i) {
        assembly::PartOccurrence c;c.occurrence_id="holes-"+std::to_string(i);c.source_document_id=sample.document_id;c.source_path="controls.prtz";
        c.placement.x=i*100;c.placement.rotation_z=i*90;occurrences.components.push_back(c);
      }
      workspace.add_assembly(occurrences,"controls.asmz");
      const auto occurrence_packets=workspace::drawing_annotation_sources(&workspace,occurrences.document_id,"controls.asmz");
      const auto transformed=occurrence_packets[1].axis_frames.at({container.feature_id,"axis:profile:2"});
      require(std::abs(transformed.origin.x-100)<1e-6 && std::abs(transformed.origin.y-20)<1e-6 &&
              std::abs(transformed.axes[2].x+packets[0].axis_frames.at({container.feature_id,"axis:profile:2"}).axes[2].y)<1e-6 && transformed.maximum.x==5 &&
              occurrence_packets[0].instance_path!=occurrence_packets[1].instance_path,
              "Individual hole frame lost occurrence orientation or size");
      auto doc=drawing::DrawingDocument::create_default();auto v=view;
      v.id="controls-view";v.camera={{-1,0,0},{0,0,1},{0,1,0}};v.model_annotations.clear();
      drawing::refresh_model_annotations(v,packets);
      for(const auto& a:v.model_annotations)if(a.kind==drawing::ModelAnnotationKind::Axis && a.source.owner_id==container.feature_id) {
        const auto mark=app::model_annotation_layout(v,a,{});
        require(mark.curves.size()==4 && mark.centers.size()==1,"Hole is not a grouped four-arm cross");
        for(const auto& arm:mark.curves)require(std::abs(QLineF(arm[0],arm[1]).length()-7)<1e-6,"Hole cross uses another hole's bounds");
      }
      doc.sheets[0].views={v};workspace.add_drawing(doc);
      for(auto kind:{kernel::ViewerDimensionKind::Radius,kernel::ViewerDimensionKind::Diameter,kernel::ViewerDimensionKind::Linear}) {
        auto current=doc;auto& cv=current.sheets[0].views[0];
        for(auto& item:cv.model_annotations)item.visible=item.model_dimension && item.model_dimension->kind==kind;
        const auto chosen=std::ranges::find_if(cv.model_annotations,[](const auto& a){return a.visible;});
        const auto ref=chosen->source;
        workspace.open_drawing(doc.document_id)->commit(current);window.edit_workspace_document(doc.document_id);window.fit_sheet();flush();
        for(int handle:{0,1}) {
          const auto before=window.model_annotation_handle_for_test(ref,handle,v.id);require(before.has_value(),"Rotated dimension grip missing");
          mouse(canvas,QEvent::MouseButtonPress,*before,Qt::LeftButton,Qt::LeftButton);
          mouse(canvas,QEvent::MouseMove,*before+QPointF(45,-25),Qt::NoButton,Qt::LeftButton);
          if(handle==0)for(int step=0;step<(kind==kernel::ViewerDimensionKind::Radius?2:1);++step) {
            mouse(canvas,QEvent::MouseButtonPress,*before+QPointF(45,-25),Qt::RightButton,Qt::LeftButton|Qt::RightButton);
            mouse(canvas,QEvent::MouseButtonRelease,*before+QPointF(45,-25),Qt::RightButton,Qt::LeftButton);
          }
          mouse(canvas,QEvent::MouseButtonRelease,*before+QPointF(45,-25),Qt::LeftButton,Qt::NoButton);
          const auto& items=window.document_for_test().sheets[0].views[0].model_annotations;
          const auto changed=std::ranges::find(items,ref,&drawing::ModelAnnotation::source);
          require(changed->view_layout && changed->view_layout->arrows_reversed,"Drawing grip failed to drag/cycle");
          require(changed->value==chosen->value,"Drawing grip changed measured value");
          if(kind==kernel::ViewerDimensionKind::Radius)require(changed->view_layout->radius_center_line_hidden,"Drawing radius did not enter shortened mode");
          const auto text_before=*window.model_annotation_handle_for_test(ref,0,v.id);
          mouse(canvas,QEvent::MouseButtonPress,text_before,Qt::LeftButton,Qt::LeftButton);
          mouse(canvas,QEvent::MouseMove,text_before+QPointF(-65,20),Qt::NoButton,Qt::LeftButton);
          const auto text_after=*window.model_annotation_handle_for_test(ref,0,v.id);
          require(QLineF(text_before,text_after).length()>5,"Re-grabbed drawing text grip cannot move");
          mouse(canvas,QEvent::MouseButtonRelease,text_after,Qt::LeftButton,Qt::NoButton);
        }
      }
      // Optional read-only acceptance check against an actual supplied project.
      if(const auto file=qEnvironmentVariable("ZIMA_TEST_ANNOTATION_PART");!file.isEmpty()) {
        const auto actual=document::PartDocument::load(file.toStdString());
        const auto actual_packets=workspace::drawing_annotation_sources(nullptr,actual.document_id,file.toStdString());
        auto check=v;check.model_annotations.clear();drawing::refresh_model_annotations(check,actual_packets);
        int holes=0;
        for(const auto& item:check.model_annotations) {
          if(item.model_dimension)require(std::abs(item.model_dimension->plane_normal.y)>.999,"Actual part dimension normal is not XZ");
          if(item.kind==drawing::ModelAnnotationKind::Axis && item.source.semantic_id.starts_with("axis:")) {
            const auto layout=app::model_annotation_layout(check,item,{});
            if(layout.curves.size()==4) {++holes;for(const auto& arm:layout.curves)require(QLineF(arm[0],arm[1]).length()<=7.00001,"Actual hole axis still oversized");}
          }
        }
        require(holes==2,"Actual Part did not supply both hole crosses");
        auto drawing_file=std::filesystem::path(file.toStdString());drawing_file.replace_extension(".drwz");
        if(std::filesystem::exists(drawing_file)) {
          auto saved_doc=drawing::DrawingDocument::load(drawing_file);
          workspace.add_drawing(saved_doc);
          for(const auto& candidate:saved_doc.sheets[0].views[0].model_annotations)if(candidate.model_dimension) {
            auto pending=saved_doc;const auto view_id=pending.sheets[0].views[0].id;
            for(auto& annotation:pending.sheets[0].views[0].model_annotations)annotation.visible=annotation.source==candidate.source;
            workspace.open_drawing(saved_doc.document_id)->commit(pending);
            window.edit_workspace_document(saved_doc.document_id);window.fit_sheet();flush();
            const auto before=window.model_annotation_handle_for_test(candidate.source,0,view_id);
            require(before.has_value(),"Actual saved Drawing dimension has no grip");
            mouse(canvas,QEvent::MouseButtonPress,*before,Qt::LeftButton,Qt::LeftButton);
            mouse(canvas,QEvent::MouseMove,*before+QPointF(60,-30),Qt::NoButton,Qt::LeftButton);
            const auto after=*window.model_annotation_handle_for_test(candidate.source,0,view_id);
            require(QLineF(*before,after).length()>3,"Actual saved Drawing dimension cannot move");
            mouse(canvas,QEvent::MouseButtonPress,after,Qt::RightButton,Qt::LeftButton|Qt::RightButton);
            mouse(canvas,QEvent::MouseButtonRelease,after,Qt::RightButton,Qt::LeftButton);
            mouse(canvas,QEvent::MouseButtonRelease,after,Qt::LeftButton,Qt::NoButton);
            const auto& items=window.document_for_test().sheets[0].views[0].model_annotations;
            const auto updated=std::ranges::find(items,candidate.source,&drawing::ModelAnnotation::source);
            require(updated->view_layout && updated->view_layout!=candidate.view_layout,"Actual saved Drawing grip did not commit presentation");
          }
          auto regenerated=saved_doc;
          for(auto& drawing_view:regenerated.sheets[0].views) {
            drawing::refresh_model_annotations(drawing_view,actual_packets);
            for(auto& item:drawing_view.model_annotations)item.visible=item.kind==drawing::ModelAnnotationKind::Dimension ||
              (item.kind==drawing::ModelAnnotationKind::Axis && item.source.semantic_id.starts_with("axis:"));
          }
          workspace.open_drawing(saved_doc.document_id)->commit(regenerated);
          window.edit_workspace_document(saved_doc.document_id);window.fit_sheet();flush();
          window.grab().save("build/actual-drawing-annotations.png");
        }
        std::cout<<"Actual project: two independent 10mm hole crosses and XZ dimension planes verified\n";
      }
    }
    verify_driving_value_ui();
    std::cout << "Show/Erase source, occurrence transforms, View selection, "
                 "Cancel, MMB, handles and PDF contracts passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
