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
    require(std::abs(repeated[1].dimensions[0].witness_first.x -
                     repeated[0].dimensions[0].witness_first.x - 70) < 1e-8,
            "Occurrence annotations not transformed");
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
    const auto before = repeated[0].dimensions[0].witness_first,
               after = deep[0].dimensions[0].witness_first;
    require(std::abs(after.x - (100 - before.y)) < 1e-8 &&
                std::abs(after.y - before.x) < 1e-8,
            "Nested rotation used the wrong owning frame");
    const auto anchor_before = repeated[0].dimensions[0].label_position;
    const auto anchor_after = deep[0].dimensions[0].label_position;
    require(anchor_before && anchor_after &&
                std::abs(anchor_after->x - (100 - anchor_before->y)) < 1e-8 &&
                std::abs(anchor_after->y - anchor_before->x) < 1e-8,
            "Nested text anchor did not follow its source dimension");
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
    require(std::abs(copies[2].dimensions[0].witness_first.x +
                     copies[0].dimensions[0].witness_first.x) < 1e-8,
            "Mirror annotation was left at source position");
    require(std::abs(copies[3].dimensions[0].witness_first.x -
                     copies[0].dimensions[0].witness_first.x - 40) < 1e-8 &&
                copies[3].instance_path != copies[4].instance_path,
            "Pattern annotation lost copy transform or identity");
    drawing::DrawingView derived_view;
    derived_view.camera = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    drawing::refresh_model_annotations(derived_view, copies);
    require(
        drawing::ShowEraseSession(derived_view)
                .candidates(drawing::ShowEraseMode::Show,
                            {drawing::ModelAnnotationKind::Dimension})
                .size() == 5,
        "Derived copies lost face-on dimensions or introduced carrier datums");
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
    window.select_view_for_test(view.id);
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
    mouse(canvas, QEvent::MouseButtonRelease, *point + QPointF(35, 20),
          Qt::LeftButton, Qt::NoButton);
    const auto &moved = state.sheets[0].views[0].model_annotations;
    const auto found = std::ranges::find(moved, dim->source,
                                         &drawing::ModelAnnotation::source);
    require(found != moved.end() && found->paper_handles.contains("text"),
            "Dimension text handle did not move");
    const auto handles = found->paper_handles;
    QTemporaryDir dir;
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
    for (auto &annotation : tilted_drawing.sheets[0].views[0].model_annotations)
      if (annotation.kind == drawing::ModelAnnotationKind::Dimension)
        annotation.plane_normal = {1, 0, 0};
    workspace.open_drawing(drawing.document_id)->document = tilted_drawing;
    window.edit_workspace_document(drawing.document_id);
    const auto filtered_print = window.render_sheet_for_test(true);
    const auto filtered_view = window.render_sheet_for_test(false);
    for (auto &annotation : tilted_drawing.sheets[0].views[0].model_annotations)
      if (annotation.kind == drawing::ModelAnnotationKind::Dimension)
        annotation.visible = false;
    workspace.open_drawing(drawing.document_id)->document = tilted_drawing;
    window.edit_workspace_document(drawing.document_id);
    require(filtered_print == window.render_sheet_for_test(true) &&
                filtered_view == window.render_sheet_for_test(false),
            "Edge-on stored dimensions leaked into View or PDF");
    for (auto &annotation : replacement.sheets[0].views[1].model_annotations)
      annotation.visible = true;
    workspace.open_drawing(drawing.document_id)->document = replacement;
    window.edit_workspace_document(drawing.document_id);
    window.select_view_for_test(view.id);
    dialog = open();
    dialog->findChild<QComboBox *>("showEraseMode")->setCurrentIndex(1);
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
