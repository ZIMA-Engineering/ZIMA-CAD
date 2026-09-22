#include "drawing_dimension_dialog.hpp"
#include "application_settings.hpp"
#include "drawing_window.hpp"
#include <zima/drawing/view_breaks.hpp>
#include <zima/drawing/annotation_guides.hpp>
#include <QAction>
#include <QFile>
#include <QMenu>
#include <zima/drawing_render/chain_dimension_layout.hpp>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTabWidget>
#include <QTemporaryDir>
#include <iostream>
#include <numbers>
#include <zima/kernel/stable_id.hpp>
#include <zima/workspace/workspace.hpp>
#include <zima/workspace/drawing_dimension_operations.hpp>
#include <zima/workspace/drawing_projection.hpp>
#include <zima/drawing_render/pdf_export.hpp>
namespace {
void require(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
void flush() {
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
void mouse(QWidget *w, QEvent::Type type, QPointF p, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent e(type, p, QPointF(w->mapToGlobal(p.toPoint())), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
    flush();
}
void pick(QWidget *w, QPointF p) {
    mouse(w, QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton);
    mouse(w, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
    mouse(w, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
}
} // namespace
int verify_measurement_dimension_ui() {
    using namespace zima;
    using namespace drawing;
    try {
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_FILLET_DRAWING")) {
            const auto path=std::filesystem::path(qEnvironmentVariable("ZIMA_VERIFY_FILLET_DRAWING").toStdString());
            auto document=DrawingDocument::load(path);workspace::DrawingProjection projection(nullptr,path);
            for(auto& sheet:document.sheets)for(auto& view:sheet.views)projection.project(view,{});
            document.save("build/01-fillet-fixed.drwz");
            drawing_render::export_pdf(document,"build/01-fillet-fixed.pdf",path,nullptr,true);
            std::cout<<"Saved exact-curve projection proof without modifying the source document\n";
            return 0;
        }
        workspace::Workspace workspace;
        kernel::ViewerMesh mesh;
        mesh.edges = {{{{0, 0, 0}, {30, 0, 0}}, {"profile", "bottom", {}}},
                      {{{30, 0, 0}, {30, 20, 0}}, {"profile", "right", {}}},
                      {{{30, 20, 0}, {0, 20, 0}}, {"profile", "top", {}}}};
        kernel::ViewerEdge circle;
        circle.reference = {"profile", "circle", {}};
        for (int i = 0; i <= 96; ++i) {
            const double t = 2 * std::numbers::pi * i / 96;
            circle.points.push_back({10 + 5 * std::cos(t), 10 + 5 * std::sin(t), 0});
        }
        mesh.edges.push_back(circle);
        auto drawing = DrawingDocument::create_default();
        auto view = DrawingDocument::create_view("source", "", mesh);
        view.camera = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        view.projected_edges = project_edges(mesh, view.camera);
        view.x = 105;
        view.y = 100;
        view.scale = 2;
        view.show_caption = false;
        ModelAnnotation axis;
        axis.kind=ModelAnnotationKind::Axis;axis.visible=true;
        axis.source={"source","profile","axis",{}};
        axis.model_axis=std::array<kernel::Vec3,2>{{{45,10,-5},{45,10,5}}};
        axis.model_envelope.include({40,5,-5});axis.model_envelope.include({50,15,5});
        ModelAnnotation parameter;
        parameter.source={"source","profile","length",{}};
        parameter.visible=true;
        kernel::ViewerDimension model_dimension;
        model_dimension.value=12.5;model_dimension.unit_suffix="mm";
        model_dimension.witness_first={60,0,0};model_dimension.witness_second={72.5,0,0};
        model_dimension.line_first={60,8,0};model_dimension.line_second={72.5,8,0};
        parameter.model_dimension=model_dimension;
        view.model_annotations={axis,parameter};
        drawing.sheets.front().views = {view};
        workspace.add_drawing(drawing);
        app::DrawingWindow window(&workspace, false);
        window.resize(1300, 900);
        window.edit_workspace_document(drawing.document_id);
        window.show();
        flush();
        auto *canvas = window.findChild<QWidget *>("drawingCanvas");
        require(canvas, "Drawing canvas missing");
        auto *command = window.findChild<QAction *>("drawingDimensionAction");
        require(command && command->text() == QString::fromUtf8("Kóta"),
                "Universal dimension command missing");
        auto dialog = [&]() -> app::DrawingDimensionDialog * {
            for (auto *d : window.findChildren<QDialog *>())
                if (auto *p = dynamic_cast<app::DrawingDimensionDialog *>(d); p && p->isVisible())
                    return p;
            return nullptr;
        };
        auto point = [&](double x, double y) {
            const auto box = window.sheet_rectangle_for_test();
            const auto scale = box.height() / drawing.sheets.front().height_mm();
            return QPointF(box.right() - view.x * scale + x * view.scale * scale,
                           box.bottom() - view.y * scale - y * view.scale * scale);
        };
        auto count = [&] { return window.document_for_test().sheets.front().dimensions.size(); };
        // Use the same two projected points throughout live placement, then
        // commit and reopen the actual native document for each selected mode.
        for (const auto& [cursor, expected, length] : std::vector<std::tuple<Point2, DimensionDirection, double>>{
                 {{15, 30}, DimensionDirection::Horizontal, 30},
                 {{40, 10}, DimensionDirection::Vertical, 20},
                 {{15, 10}, DimensionDirection::Automatic, std::hypot(30., 20.)}}) {
            auto pending = make_drawing_dimension(view.id);
            DrawingDimension committed;
            app::DrawingDimensionDialog placement(pending, true,
                [&](const std::string&) { return &view; },
                [&](auto result) { committed = std::move(result); }, &window);
            placement.setAttribute(Qt::WA_DeleteOnClose, false);
            auto* reference_table=placement.findChild<QTableWidget*>("drawingDimensionReferences");
            require(!reference_table->isRowHidden(0)&&reference_table->isRowHidden(1),"Empty dimension exposes more than its first reference row");
            require(!placement.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled(),"Empty references can be committed");
            MeasurementCandidate a, b;
            a.attachment = {DimensionAttachmentKind::CurvePoint, mesh.edges[0].reference};
            b.attachment = {DimensionAttachmentKind::CurvePoint, mesh.edges[1].reference};
            b.attachment.parameter = 1;
            placement.accept_candidate(view.id, a);
            require(!reference_table->isRowHidden(1),"First reference did not reveal the second row");
            placement.accept_candidate(view.id, b);
            require(reference_table->rowCount()==2,"Complete ordinary dimension offers an extra reference row");
            placement.position({15,30}, false);
            placement.position({40,10}, false);
            placement.position(cursor, true);
            require(placement.value().direction == expected, "Drawing drag chose a different direction than Sketcher");
            const auto evaluated = evaluate_drawing_dimension(view, placement.value());
            require(evaluated.state == MeasurementState::Resolved &&
                    std::abs(evaluated.presentations[0].value-length)<1e-8,
                    "Automatic placement measured the wrong projected distance");
            placement.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            auto saved = drawing;saved.sheets.front().dimensions = {committed};
            QTemporaryDir temporary;const auto path = std::filesystem::path(temporary.path().toStdString())/"automatic.drwz";
            saved.save(path);const auto reopened = DrawingDocument::load(path);
            require(reopened.sheets.front().dimensions[0].direction == expected,
                    "Automatic placement direction did not survive save/reopen");
        }

        command->trigger();
        flush();
        auto *props = dialog();
        require(props && props->windowType() == Qt::SubWindow &&
                    props->windowTitle() == QString::fromUtf8("Vlastnosti kóty"),
                "Dimension did not open unified internal properties");
        auto *table = props->findChild<QTableWidget *>("drawingDimensionReferences");
        require(dynamic_cast<ui::ReferenceCellItem *>(table->item(0, 2))->is_active_input(),
                "First reference not green");
        mouse(props, QEvent::MouseButtonPress, {30, 30}, Qt::MiddleButton, Qt::MiddleButton);
        mouse(props, QEvent::MouseButtonRelease, {30, 30}, Qt::MiddleButton, Qt::NoButton);
        require(!dynamic_cast<ui::ReferenceCellItem *>(table->item(0, 2))->is_active_input() && count() == 0,
                "Short MMB over properties did not end reference entry");
        auto *mode = props->findChild<QComboBox *>("dimensionAttachmentMode0");
        mode->setCurrentIndex(mode->findData(int(DimensionAttachmentKind::Line)));
        flush();
        mouse(canvas,QEvent::MouseMove,point(15,0),Qt::NoButton,Qt::NoButton);
        const auto line_hover=canvas->grab().toImage();
        const auto orange=[](QColor c){return c.red()>220&&c.green()>90&&c.green()<175&&c.blue()<60;};
        const auto pixel=[&](const QImage& image,QPointF p){return image.pixelColor((p*image.devicePixelRatio()).toPoint());};
        require(!orange(pixel(line_hover,point(15,0)+QPointF(0,4))),"Line hover includes a misleading point marker");
        require(orange(pixel(line_hover,point(15,0)+QPointF(12,0))),"Line hover does not highlight the line");
        pick(canvas, point(15, 0));
        require(props->value().attachments[0].reference.valid() &&
                    props->value().attachments[0].kind == DimensionAttachmentKind::Line,
                "First geometric line not bound");
        mode = props->findChild<QComboBox *>("dimensionAttachmentMode1");
        mode->setCurrentIndex(mode->findData(int(DimensionAttachmentKind::Line)));
        flush();
        pick(canvas, point(15, 20));
        require(props->placing() && count() == 0, "References committed before placement/OK");
        pick(canvas, point(-10, 10));
        require(!props->placing() && count() == 0, "Placement committed without OK");
        props->findChild<QLineEdit *>("sketchDimensionPrefix")->setText("2×");
        auto *tolerance = props->findChild<QComboBox *>("sketchDimensionToleranceMode");
        tolerance->setCurrentIndex(tolerance->findData("symmetric"));
        props->findChild<QLineEdit *>("sketchSymmetricTolerance")->setText("0.1");
        flush();
        window.grab().save("build/measurement-properties-proof.png");
        mouse(canvas, QEvent::MouseButtonPress, point(40, 30), Qt::MiddleButton, Qt::MiddleButton);
        mouse(canvas, QEvent::MouseButtonRelease, point(40, 30), Qt::MiddleButton, Qt::NoButton);
        require(count() == 0 && dialog(), "Short MMB committed dimension");
        mouse(canvas, QEvent::MouseButtonDblClick, point(40, 30), Qt::MiddleButton, Qt::MiddleButton);
        require(count() == 1 && !dialog(), "MMB double click did not commit dimension");
        auto linear = window.document_for_test().sheets.front().dimensions.front();
        require(linear.style.prefix == "2×" && linear.style.symmetric_tolerance == "0.1",
                "Unified tolerance fields not committed");
        const auto evaluation =
            evaluate_drawing_dimension(*window.document_for_test().find_view(view.id), linear);
        require(std::abs(evaluation.presentations[0].value - 20) < 1e-9,
                "UI dimension changed projected measurement");

        const auto before_snap=linear;
        // Native Drawing dimensions snap to the same 2D rectangle as model dimensions.
        canvas->grab();
        const auto guides=annotation_guides(*window.document_for_test().find_view(view.id));
        const auto guide=std::ranges::find_if(guides,[](const auto& g){return std::abs(g.first.x-g.second.x)<1e-9;});
        require(guide!=guides.end(),"Drawing dimension has no vertical 2D guide");
        const auto initial_grip=window.annotation_handle_for_test(linear.id,0,true);require(initial_grip.has_value(),"Drawing dimension text grip missing");
        const auto target=point(guide->first.x/view.scale,10);
        mouse(canvas,QEvent::MouseMove,*initial_grip,Qt::NoButton,Qt::NoButton);
        mouse(canvas,QEvent::MouseButtonPress,*initial_grip,Qt::LeftButton,Qt::LeftButton);
        mouse(canvas,QEvent::MouseMove,target+QPointF(1,0),Qt::NoButton,Qt::LeftButton);
        require(canvas->property("annotationSnapActive").toBool(),"Drawing dimension did not snap to its 2D rectangle");
        mouse(canvas,QEvent::MouseButtonRelease,target+QPointF(1,0),Qt::LeftButton,Qt::NoButton);canvas->grab();
        const auto snapped_grip=window.annotation_handle_for_test(linear.id,0,true);
        require(snapped_grip&&QLineF(*snapped_grip,target).length()<2,"Drawing snap did not place the actual grip");
        linear=window.document_for_test().sheets.front().dimensions.front();
        require(std::abs(evaluate_drawing_dimension(*window.document_for_test().find_view(view.id),linear).presentations[0].value-20)<1e-9,"Snapping changed projected length");

        // Drag an already dimensioned view, with both local and source dimensions.
        // Repeat after shortening it: only sheet placement may change.
        for(bool broken:{false,true}) {
            auto* state=workspace.open_drawing(drawing.document_id);
            auto pending=state->document();
            pending.find_view(view.id)->breaks=broken?std::vector<ViewBreak>{{"move-break",false,2,4,2,BreakMark::Zigzag}}:std::vector<ViewBreak>{};
            state->commit(std::move(pending));window.edit_workspace_document(drawing.document_id);flush();canvas->grab();
            const auto before_view=*window.document_for_test().find_view(view.id);
            const auto before_dimension=window.document_for_test().sheets.front().dimensions.front();
            const auto local_grip=window.annotation_handle_for_test(linear.id,0,true);
            const auto model_grip=window.model_annotation_handle_for_test(parameter.source,0,view.id);
            require(local_grip&&model_grip,"Dimensioned view has missing grips");
            const auto box=window.sheet_rectangle_for_test();const double zoom=box.height()/drawing.sheets.front().height_mm();
            const auto local=break_map(before_view,{25,15});
            const QPointF start(box.right()-before_view.x*zoom+local.x*view.scale*zoom,
                                box.bottom()-before_view.y*zoom-local.y*view.scale*zoom);
            const QPointF delta(41,-27);
            mouse(canvas,QEvent::MouseMove,start,Qt::NoButton,Qt::NoButton);
            mouse(canvas,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
            mouse(canvas,QEvent::MouseMove,start+delta,Qt::NoButton,Qt::LeftButton);
            mouse(canvas,QEvent::MouseButtonRelease,start+delta,Qt::LeftButton,Qt::NoButton);canvas->grab();
            const auto& moved=*window.document_for_test().find_view(view.id);
            require(std::abs(moved.x-before_view.x+delta.x()/zoom)<1e-6&&std::abs(moved.y-before_view.y+delta.y()/zoom)<1e-6,"Dragging dimensioned view did not move sheet placement");
            const auto local_after=window.annotation_handle_for_test(linear.id,0,true);
            const auto model_after=window.model_annotation_handle_for_test(parameter.source,0,view.id);
            require(local_after&&model_after&&QLineF(*local_after,*local_grip+delta).length()<.1&&QLineF(*model_after,*model_grip+delta).length()<.1,"Moving view left local or model dimensions behind");
            require(window.document_for_test().sheets.front().dimensions.front()==before_dimension&&moved.model_annotations==before_view.model_annotations&&moved.breaks==before_view.breaks,"Moving view changed references, values, layout or breaks");
            QTemporaryDir saved;const auto path=std::filesystem::path(saved.path().toStdString())/"moved.drwz";
            window.document_for_test().save(path);const auto reloaded=DrawingDocument::load(path);
            require(reloaded.find_view(view.id)->x==moved.x&&reloaded.find_view(view.id)->breaks==moved.breaks&&reloaded.sheets.front().dimensions.front()==before_dimension,"Moved dimensions or breaks did not survive native reopen");
            auto restored=state->document();*restored.find_view(view.id)=before_view;restored.find_view(view.id)->breaks.clear();state->commit(std::move(restored));window.edit_workspace_document(drawing.document_id);flush();canvas->grab();
        }

        // Restore the independent chain-edit fixture after snap/move coverage.
        auto initial_chain=workspace.open_drawing(drawing.document_id)->document();
        initial_chain.sheets.front().dimensions.front()=before_snap;
        workspace.open_drawing(drawing.document_id)->commit(std::move(initial_chain));
        window.edit_workspace_document(drawing.document_id);flush();canvas->grab();linear=before_snap;

        std::string confirmed;
        window.set_selection_handler([&](const auto& ids){confirmed=ids.empty()?std::string{}:ids.front();});
        const auto cyan_pixels=[&] {
            const auto image=canvas->grab().toImage();std::size_t pixels=0;
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
                const auto c=image.pixelColor(x,y);
                if(c.red()<40&&c.green()>110&&c.blue()>150&&std::abs(c.green()*255-c.blue()*209)<1000)++pixels;
            }
            return pixels;
        };
        {
            const auto handle=window.annotation_handle_for_test(linear.id,0,true);
            require(handle.has_value(),"Conversion grip missing");pick(canvas,*handle);
            const auto on_line=*handle+QPointF(0,20);
            QContextMenuEvent context(QContextMenuEvent::Mouse,on_line.toPoint(),canvas->mapToGlobal(on_line.toPoint()));
            QApplication::sendEvent(canvas,&context);flush();
            auto* convert=canvas->findChild<QAction*>("convertDrawingChainAction");
            require(convert,"Linear dimension has no direct chain conversion");
            convert->trigger();for(auto* menu:canvas->findChildren<QMenu*>())menu->close();flush();
            auto* converted=dialog();
            require(converted&&converted->value().kind==DrawingDimensionKind::Chain&&
                converted->value().attachments==linear.attachments&&!converted->entering(),
                "Chain conversion requested another reference");
            converted->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            require(window.document_for_test().sheets.front().dimensions.front().kind==DrawingDimensionKind::Chain&&
                window.document_for_test().sheets.front().dimensions.front().attachments.size()==2,
                "Two-reference chain conversion did not commit");
            require(workspace.open_drawing(drawing.document_id)->undo(),"Chain conversion has no Undo");
            window.edit_workspace_document(drawing.document_id);flush();
            require(window.document_for_test().sheets.front().dimensions.front()==linear,"Conversion Undo did not restore linear dimension");
        }
        // One dialog class also edits and extends an existing dimension.
        auto grip = window.annotation_handle_for_test(linear.id, 0, true);
        require(grip.has_value(), "Committed dimension has no grip");
        pick(canvas, *grip);
        mouse(canvas, QEvent::MouseButtonDblClick, *grip, Qt::LeftButton, Qt::LeftButton);
        props = dialog();
        require(props, "Existing dimension did not open properties");
        require(confirmed=="drawing-dimension:"+linear.id,"Double-click lost confirmed Tree selection");
        require(cyan_pixels()>10,"Double-click removed cyan dimension presentation");
        const auto previous = props->value().segments.front();
        props->extend(true);
        pick(canvas, point(0, 0));
        props->end_entry();
        require(count() == 1 && window.document_for_test().sheets.front().dimensions.front() == linear,
                "Chain preview changed document");
        props->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        flush();
        require(count() == 1 && window.document_for_test().sheets.front().dimensions.front() == linear,
                "Cancel changed original dimension");
        require(confirmed=="drawing-dimension:"+linear.id&&cyan_pixels()>10,"Cancel lost the edited dimension selection");

        grip = window.annotation_handle_for_test(linear.id, 0, true);
        pick(canvas, *grip);
        mouse(canvas, QEvent::MouseButtonDblClick, *grip, Qt::LeftButton, Qt::LeftButton);
        props = dialog();
        require(props, "Cannot reopen linear properties");
        props->extend(false);
        mode = props->findChild<QComboBox *>("dimensionAttachmentMode2");
        mode->setCurrentIndex(mode->findData(int(DimensionAttachmentKind::Center)));
        flush();
        pick(canvas, point(15, 10));
        props->end_entry();
        props->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        flush();
        require(count() == 1 && window.document_for_test().sheets.front().dimensions.front().kind ==
                                    DrawingDimensionKind::Chain,
                "Chain extension did not commit");
        require(confirmed=="drawing-dimension:"+linear.id&&cyan_pixels()>10,"OK lost the edited dimension selection");
        require(window.document_for_test().sheets.front().dimensions.front().segments.front().id == linear.segments.front().id &&
                window.document_for_test().sheets.front().dimensions.front().segments.front().layout == linear.segments.front().layout,
                "Chain changed original segment");
        command->trigger();
        flush();
        props = dialog();
        props->findChild<QComboBox *>("drawingDimensionType")
            ->setCurrentIndex(int(DrawingDimensionKind::Radius));
        flush();
        pick(canvas, point(15, 10));
        require(props->value().attachments[0].reference == circle.reference && props->placing(),
                "Circle not offered for radius");
        pick(canvas, point(24, 10));
        props->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        flush();
        require(count() == 2, "Radius was not committed");
        const auto radius_id = window.document_for_test().sheets.front().dimensions.back().id;
        for (int state = 0; state < 3; ++state) {
            const auto rim = *window.annotation_handle_for_test(radius_id, 1, true);
            pick(canvas, rim);
            mouse(canvas, QEvent::MouseButtonPress, rim, Qt::LeftButton, Qt::LeftButton);
            mouse(canvas, QEvent::MouseMove, rim + QPointF(-8, -12), Qt::NoButton, Qt::LeftButton);
            const auto moved_rim = *window.annotation_handle_for_test(radius_id, 1, true);
            require(QLineF(rim, moved_rim).length() > 3, "Arrow grip cannot move around circle");
            mouse(canvas, QEvent::MouseButtonRelease, moved_rim, Qt::LeftButton, Qt::NoButton);
            const auto before = window.document_for_test().sheets.front().dimensions.back();
            auto text = *window.annotation_handle_for_test(radius_id, 0, true);
            mouse(canvas, QEvent::MouseButtonPress, text, Qt::LeftButton, Qt::LeftButton);
            mouse(canvas, QEvent::MouseMove, text + QPointF(45, -20), Qt::NoButton, Qt::LeftButton);
            const auto moved_text = *window.annotation_handle_for_test(radius_id, 0, true);
            require(QLineF(text, moved_text).length() > 10, "Text grip is locked in radius mode");
            require(QLineF(moved_rim, *window.annotation_handle_for_test(radius_id, 1, true)).length() < .01,
                    "Text grip moved radius arrow");
            mouse(canvas, QEvent::MouseButtonPress, moved_text, Qt::RightButton,
                  Qt::LeftButton | Qt::RightButton);
            mouse(canvas, QEvent::MouseButtonRelease, moved_text, Qt::RightButton, Qt::LeftButton);
            mouse(canvas, QEvent::MouseButtonRelease, moved_text, Qt::LeftButton, Qt::NoButton);
            const auto &now = window.document_for_test().sheets.front().dimensions.back();
            require(now.attachments == before.attachments, "Presentation grips rewrote model reference");
            const auto measured =
                evaluate_drawing_dimension(*window.document_for_test().find_view(view.id), now);
            require(std::abs(measured.presentations[0].value - 5) < 1e-9, "Radius drag changed value");
            if (state == 0)
                require(now.segments[0].layout.arrows_reversed &&
                            !now.segments[0].layout.radius_center_line_hidden,
                        "First RMB did not reverse arrow");
            if (state == 1)
                require(now.segments[0].layout.radius_center_line_hidden,
                        "Second RMB did not shorten radius");
            if (state == 2)
                require(!now.segments[0].layout.radius_center_line_hidden &&
                            !now.segments[0].layout.arrows_reversed,
                        "Third RMB did not restore first mode");
        }
        window.grab().save("build/measurement-grips-proof.png");
        const auto saved = std::filesystem::path("build/measurement-ui-proof.drwz");
        window.document_for_test().save(saved);
        const auto loaded = DrawingDocument::load(saved);
        require(loaded.sheets.front().dimensions == window.document_for_test().sheets.front().dimensions,
                "UI dimensions failed save/open");
        window.export_pdf("build/measurement-ui-proof.pdf");
        window.export_dxf("build/measurement-ui-proof.dxf");
        auto broken = DrawingDocument::create_default();
        auto broken_view = view;
        broken_view.id = kernel::make_stable_id();
        auto damaged_geometry=*broken_view.measurement_geometry;
        std::erase_if(damaged_geometry.curves,
                      [](const auto &c) { return c.source.semantic_key == "top"; });
        broken_view.measurement_geometry=share_measurement_geometry(std::move(damaged_geometry));
        auto damaged = linear;
        damaged.view_id = broken_view.id;
        broken.sheets.front().views = {broken_view};
        broken.sheets.front().dimensions = {damaged};
        workspace.add_drawing(broken);
        window.edit_workspace_document(broken.document_id);
        flush();
        grip = window.annotation_handle_for_test(damaged.id, 0, true);
        require(grip.has_value(), "Broken dimension cannot be selected");
        pick(canvas, *grip);
        mouse(canvas, QEvent::MouseButtonDblClick, *grip, Qt::LeftButton, Qt::LeftButton);
        props = dialog();
        require(props, "Broken dimension has no repair dialog");
        table = props->findChild<QTableWidget *>("drawingDimensionReferences");
        require(dynamic_cast<ui::ReferenceCellItem *>(table->item(1, 2))->is_missing(),
                "Lost reference field not marked");
        mode = props->findChild<QComboBox *>("dimensionAttachmentMode1");
        mode->setCurrentIndex(mode->findData(int(DimensionAttachmentKind::Center)));
        flush();
        pick(canvas, point(15, 10));
        props->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        flush();
        const auto &repaired = window.document_for_test().sheets.front().dimensions.front();
        require(repaired.attachments.front() == damaged.attachments.front() &&
                    repaired.segments.front().layout == damaged.segments.front().layout &&
                    repaired.style == damaged.style,
                "Repair changed unrelated references or presentation");
        require(evaluate_drawing_dimension(*window.document_for_test().find_view(broken_view.id), repaired)
                        .state == MeasurementState::Resolved,
                "Rebound dimension still unresolved");
        // Selection hands keyboard focus to the canvas, and Delete removes the
        // exact manual dimension. Model dimensions are erased only in this view.
        grip=window.annotation_handle_for_test(repaired.id,0,true);require(grip.has_value(),"Repaired grip missing");
        pick(canvas,*grip);
        QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);
        QApplication::sendEvent(canvas,&remove);flush();
        require(count()==0,"Delete did not remove selected manual dimension");
        const auto model_grip=window.model_annotation_handle_for_test(parameter.source,0,broken_view.id);
        require(model_grip.has_value(),"Model dimension grip missing");
        pick(canvas,*model_grip);QApplication::sendEvent(canvas,&remove);flush();
        const auto* after_delete=window.document_for_test().find_view(broken_view.id);
        require(after_delete&&!after_delete->model_annotations.back().visible&&
                after_delete->model_annotations.back().model_dimension==parameter.model_dimension,
                "Delete changed source dimension instead of erasing it");
        command->trigger();flush();props=dialog();require(props,"Dimension command did not reopen after Delete");
        mode=props->findChild<QComboBox*>("dimensionAttachmentMode0");
        mode->setCurrentIndex(mode->findData(int(DimensionAttachmentKind::Point)));flush();
        pick(canvas,point(49,10));
        require(props->value().attachments[0].kind==DimensionAttachmentKind::Center&&
                props->value().attachments[0].reference.semantic_key=="axis",
                "Axis cross did not retain its centre binding through the Point field");
        mode=props->findChild<QComboBox*>("dimensionAttachmentMode1");
        mode->setCurrentIndex(mode->findData(int(DimensionAttachmentKind::Point)));flush();
        pick(canvas,point(30,0));
        require(props->value().attachments[1].kind==DimensionAttachmentKind::CurvePoint,
                "Point field corrupted an endpoint binding");
        require(evaluate_drawing_dimension(*after_delete,props->value()).state==MeasurementState::Resolved,
                "Offered point references cannot produce a dimension");
        props->reject();flush();
        // The same command/dialog selects two nonparallel lines for an angle.
        auto angular_document=DrawingDocument::create_default();auto angular_view=view;angular_view.model_annotations.clear();angular_document.sheets.front().views={angular_view};
        workspace.add_drawing(angular_document);window.edit_workspace_document(angular_document.document_id);flush();
        command->trigger();flush();props=dialog();require(props,"Angular properties did not open");
        props->findChild<QComboBox*>("drawingDimensionType")->setCurrentIndex(int(DrawingDimensionKind::Angular));flush();
        require(props->pick_request().lines_only&&!props->pick_request().parallel_line.valid(),"Angular picker still requires parallel edges");
        pick(canvas,point(15,0));pick(canvas,point(30,10));require(props->placing(),"Two nonparallel lines did not enter angle placement");
        pick(canvas,point(20,10));require(count()==0,"Angular preview committed early");
        props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();require(count()==0,"Angular Cancel committed a dimension");
        command->trigger();flush();props=dialog();props->findChild<QComboBox*>("drawingDimensionType")->setCurrentIndex(int(DrawingDimensionKind::Angular));flush();
        pick(canvas,point(15,0));pick(canvas,point(30,10));pick(canvas,point(20,10));
        mouse(canvas,QEvent::MouseButtonPress,point(40,30),Qt::MiddleButton,Qt::MiddleButton);mouse(canvas,QEvent::MouseButtonRelease,point(40,30),Qt::MiddleButton,Qt::NoButton);
        require(count()==0&&dialog(),"Short middle click committed angular preview");
        mouse(canvas,QEvent::MouseButtonDblClick,point(40,30),Qt::MiddleButton,Qt::MiddleButton);require(count()==1&&!dialog(),"Middle double click did not confirm angle");
        const auto angular=window.document_for_test().sheets.front().dimensions.front();auto measured=evaluate_drawing_dimension(*window.document_for_test().find_view(view.id),angular);
        require(measured.state==MeasurementState::Resolved&&std::abs(measured.presentations[0].value-90)<1e-6&&drawing_dimension_text(angular,measured.presentations[0])=="90°","Angular UI measured the wrong value or unit");
        const auto pixels=[&](QColor color){
            const auto image=canvas->grab().toImage();std::size_t found=0;
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
                const auto pixel=image.pixelColor(x,y);
                // Thin antialiased lines retain their hue on the black canvas
                // without necessarily containing fully covered, exact RGB pixels.
                const double alpha=double(std::max({pixel.red(),pixel.green(),pixel.blue()}))/std::max({color.red(),color.green(),color.blue()});
                if(alpha>.35&&alpha<=1.01&&std::abs(pixel.red()-alpha*color.red())<3&&
                   std::abs(pixel.green()-alpha*color.green())<3&&std::abs(pixel.blue()-alpha*color.blue())<3)++found;
            }
            return found;
        };
        window.grab().save("build/drawing-angle-valid.png");require(pixels(QColor("#FFD400"))>10,"Valid angle is not yellow");
        auto invalid_document=DrawingDocument::create_default();auto invalid_view=angular_view;auto packet=*invalid_view.measurement_geometry;
        std::erase_if(packet.curves,[](const auto& c){return c.source.semantic_key=="right";});invalid_view.measurement_geometry=share_measurement_geometry(std::move(packet));
        invalid_document.sheets.front().views={invalid_view};invalid_document.sheets.front().dimensions={angular};workspace.add_drawing(invalid_document);window.edit_workspace_document(invalid_document.document_id);flush();
        measured=evaluate_drawing_dimension(*window.document_for_test().find_view(view.id),angular);
        require(measured.state==MeasurementState::Unresolved&&drawing_dimension_text(angular,measured.presentations[0],true)=="90°","Invalid angle lost its last numeric value");
        require(pixels(QColor("#C62828"))>10,"Invalid angle is not red");window.grab().save("build/drawing-angle-invalid.png");
        grip=window.annotation_handle_for_test(angular.id,0,true);require(grip.has_value(),"Floating invalid angle cannot be selected");
        pick(canvas,*grip);mouse(canvas,QEvent::MouseButtonDblClick,*grip,Qt::LeftButton,Qt::LeftButton);props=dialog();require(props,"Floating angle cannot open repair properties");
        table=props->findChild<QTableWidget*>("drawingDimensionReferences");require(dynamic_cast<ui::ReferenceCellItem*>(table->item(1,2))->is_missing(),"Invalid angle reference field is not red");
        QMetaObject::invokeMethod(table,"cellClicked",Q_ARG(int,1),Q_ARG(int,2));flush();pick(canvas,point(15,20));
        props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();require(!dialog(),"Angular reference repair did not commit");
        const auto fixed=window.document_for_test().sheets.front().dimensions.front();measured=evaluate_drawing_dimension(*window.document_for_test().find_view(view.id),fixed);
        require(fixed.id==angular.id&&fixed.attachments[0]==angular.attachments[0]&&measured.state==MeasurementState::Resolved&&measured.angular_leaders[0],"Repair lost dimension identity or parallel display");
        pick(canvas,point(-40,-30));require(pixels(QColor("#C62828"))==0&&pixels(QColor("#FFD400"))>10,"Repaired angle did not return from red to yellow");window.grab().save("build/drawing-angle-repaired.png");
        window.document_for_test().save("build/drawing-angle-proof.drwz");const auto angular_saved=DrawingDocument::load("build/drawing-angle-proof.drwz");require(angular_saved.sheets.front().dimensions.front()==fixed,"Repaired parallel angle lost persisted state");
        window.export_pdf("build/drawing-angle-proof.pdf");window.export_dxf("build/drawing-angle-proof.dxf");
        grip=window.annotation_handle_for_test(fixed.id,0,true);require(grip.has_value(),"Angle delete handle missing");pick(canvas,*grip);
        QKeyEvent delete_angle(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(canvas,&delete_angle);flush();
        require(window.document_for_test().sheets.front().dimensions.empty(),"GUI Delete did not remove the measured angle");
        auto* angle_state=workspace.open_drawing(invalid_document.document_id);require(angle_state->undo(),"GUI angle deletion has no Undo");
        window.edit_workspace_document(invalid_document.document_id);flush();require(window.document_for_test().sheets.front().dimensions.front()==fixed,"GUI delete Undo lost angle references or presentation");
        {
            auto chain_document=DrawingDocument::create_default();
            auto chain_view=view;chain_view.model_annotations.clear();chain_view.projected_edges.clear();
            chain_view.projected_edges.push_back({{{0,0},{20,0},{20,50},{0,50},{0,0}}});
            for(double y:{10.,25.,40.}) {
                ProjectedEdge hole;
                for(int i=0;i<=64;++i)hole.points.push_back({10+2*std::cos(i*2*std::numbers::pi/64),y+2*std::sin(i*2*std::numbers::pi/64)});
                chain_view.projected_edges.push_back(hole);
            }
            chain_view.measurement_geometry=share_measurement_geometry({{}, {
                {{"chain","zero",{}},{0,0,0}},{{"chain","ten",{}},{10,10,0}},
                {{"chain","twenty-five",{}},{10,25,0}},{{"chain","forty",{}},{10,40,0}}}});
            auto running=make_drawing_dimension(chain_view.id,DrawingDimensionKind::Chain);
            running.direction=DimensionDirection::Vertical;running.style.suffix="";
            running.attachments.clear();
            for(const auto& p:chain_view.measurement_geometry->points)
                running.attachments.push_back({DimensionAttachmentKind::Point,p.source});
            resize_dimension_segments(running);place_drawing_dimension(chain_view,running,0,{-8,10});
            {
                auto independent=drawing;independent.sheets.front().views={chain_view};independent.sheets.front().dimensions.clear();
                workspace::commit_drawing_chain(independent,independent.sheets.front().id,running,true);
                auto& members=independent.sheets.front().dimensions;
                require(members.size()==running.segments.size(),"Chain command did not create independent dimensions");
                for(const auto& member:members)require(member.attachments.size()==2&&member.segments.size()==1&&!member.chain_group.empty(),"Chain member does not have exactly two references");
                const auto sibling_style=members[1].style;
                auto changed=members[0];changed.style.prefix="T=";changed.style.tolerance_mode="deviations";changed.style.upper_tolerance="0.2";changed.style.lower_tolerance="-0.1";changed.segments[0].layout.line_offset+=5;
                workspace::edit_drawing_dimension(independent,independent.sheets.front().id,changed,false);
                require(members[1].style==sibling_style&&members[1].segments[0].layout.line_offset==changed.segments[0].layout.line_offset,"Independent text leaked or shared spine failed to move");
                const auto stored=deserialize_drawing_dimensions(serialize_drawing_dimensions(members));
                require(stored==members,"Independent chain lost its group on reopen");
                const auto remaining=members[1];workspace::erase_drawing_dimension(independent.sheets.front(),members[0].id);
                require(members.front()==remaining,"Deleting one member changed its neighbor");
            }
            const auto projection=[](kernel::Vec3 p){return QPointF(p.x,-p.y);};
            auto result=evaluate_drawing_dimension(chain_view,running);
            for(const auto& ordinate:result.presentations) {
                auto layout=drawing_render::chain_dimension_layout(ordinate,projection,canvas->font(),QString::number(ordinate.value),1.);
                require(layout.valid&&layout.arrows.size()==1&&layout.text_angle==0,
                        "Running ordinate must have one arrow and upright text");
                require(layout.curves.size()==2,"Branch needs its witness and a connection to zero");
                require(layout.curves.front().back()==projection(ordinate.line_second),"Witness extends underneath the chain value");
            }
            // Both sides of horizontal, vertical and oblique spines: text is
            // perpendicular, readable and outside the witness half-plane.
            for(const auto axis:{QPointF(1,0),QPointF(0,1),QPointF(.6,.8)})for(double side:{-1.,1.}) {
                kernel::ViewerDimension sample;
                const QPointF normal(-axis.y(),axis.x());
                const auto target=axis*40,reference=target+normal*(side*10);
                sample.line_first={0,0,0};sample.line_second={target.x(),target.y(),0};
                sample.witness_second={reference.x(),reference.y(),0};sample.label_position=sample.line_second;
                const auto project=[](kernel::Vec3 p){return QPointF(p.x,p.y);};
                const auto layout=drawing_render::chain_dimension_layout(sample,project,canvas->font(),"40",1.);
                const double angle=layout.text_angle*std::numbers::pi/180;
                const QPointF text_axis(std::cos(angle),std::sin(angle));
                require(std::abs(QPointF::dotProduct(axis,text_axis))<1e-8,"Chain text is not perpendicular to its spine");
                QTransform transform;transform.translate(layout.text_baseline.x(),layout.text_baseline.y());transform.rotate(layout.text_angle);
                const auto box=viewer::dimension_text_box(canvas->font(),"40",.5);
                for(const auto corner:{box.topLeft(),box.topRight(),box.bottomLeft(),box.bottomRight()})
                    require(QPointF::dotProduct(transform.map(corner)-target,reference-target)<0,
                            "Chain label lies on the witness/reference side");
            }
            auto zero=drawing_render::chain_dimension_layout(result.presentations.front(),projection,canvas->font(),"0",1.,true);
            require(zero.valid&&zero.arrows.empty()&&zero.curves.size()==1,
                    "Running datum must have a witness and zero without an arrow");
            for(const auto& ordinate:result.presentations) {
                const auto layout=drawing_render::chain_dimension_layout(ordinate,projection,canvas->font(),QString::number(ordinate.value),1.);
                QTransform transform;transform.translate(layout.text_baseline.x(),layout.text_baseline.y());transform.rotate(layout.text_angle);
                const auto bounds=transform.mapRect(viewer::dimension_text_box(canvas->font(),QString::number(ordinate.value),.5));
                require(bounds.bottom()<projection(ordinate.line_second).y(),"Ordinate text crosses its witness line");
            }
            auto slid=running;
            drag_drawing_dimension(chain_view,slid,1,0,{12,37});
            const auto slid_result=evaluate_drawing_dimension(chain_view,slid);
            require(slid_result.presentations[1].line_second==result.presentations[1].line_second&&
                    slid_result.presentations[1].label_position->y==result.presentations[1].label_position->y,
                    "Text drag moved the spine or left the witness line");
            require(slid_result.presentations[1].label_position->x!=result.presentations[1].label_position->x,
                    "Text cannot slide along its witness");
            chain_document.sheets.front().views={chain_view};chain_document.sheets.front().dimensions={running};
            workspace.add_drawing(chain_document);window.edit_workspace_document(chain_document.document_id);flush();
            window.grab().save("build/drawing-chain-proof.png");
            auto handle=window.annotation_handle_for_test(running.id,2,true);require(handle.has_value(),"Running dimension handle missing");
            mouse(canvas,QEvent::MouseButtonPress,*handle,Qt::LeftButton,Qt::LeftButton);
            mouse(canvas,QEvent::MouseMove,*handle+QPointF(-20,0),Qt::NoButton,Qt::LeftButton);
            mouse(canvas,QEvent::MouseButtonRelease,*handle+QPointF(-20,0),Qt::LeftButton,Qt::NoButton);
            const auto& moved_chain=window.document_for_test().sheets.front().dimensions.front();
            result=evaluate_drawing_dimension(chain_view,moved_chain);
            for(std::size_t i=0;i<3;++i) {
                require(std::abs(result.presentations[i].value-std::array{10.,25.,40.}[i])<1e-8,"Dragging changed an ordinate value");
                require(std::abs(result.presentations[i].line_second.x-result.presentations[0].line_second.x)<1e-8,
                        "Dragging split the running dimension spine");
            }
            require(result.presentations[0].line_second.x < -8,"Running dimension did not move with its grip");
            window.document_for_test().save("build/drawing-chain-proof.drwz");
            const auto reopened=DrawingDocument::load("build/drawing-chain-proof.drwz");
            require(reopened.sheets.front().dimensions.front()==moved_chain,"Running dimension save/reopen changed state");
            window.export_pdf("build/drawing-chain-proof.pdf");window.export_dxf("build/drawing-chain-proof.dxf");
            QFile exported("build/drawing-chain-proof.dxf");require(exported.open(QIODevice::ReadOnly),"Running DXF missing");
            const auto bytes=exported.readAll();
            for(const auto& value:{"0","10","25","40"})
                require(bytes.contains(QByteArray("\n1\n")+value+"\n"),"DXF lost a running ordinate or its literal zero");
            const auto before_delete=moved_chain;
            canvas->grab();
            auto branch_handle=window.annotation_handle_for_test(running.id,3,true);
            require(branch_handle.has_value(),"Second chain branch has no text handle");
            pick(canvas,*branch_handle);
            QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(canvas,&remove);flush();
            const auto& remaining=window.document_for_test().sheets.front().dimensions;
            require(remaining.size()==1&&remaining.front().segments.size()==2,"Delete did not remove exactly one chain branch");
            const auto kept=evaluate_drawing_dimension(chain_view,remaining.front());
            require(kept.presentations[0].value==10&&kept.presentations[1].value==40,"Branch deletion changed another ordinate");
            require(workspace.open_drawing(chain_document.document_id)->undo(),"Branch deletion has no Undo");
            window.edit_workspace_document(chain_document.document_id);flush();
            require(window.document_for_test().sheets.front().dimensions.front()==before_delete,"Undo did not restore the exact chain");
            const auto branch_id="drawing-dimension:"+running.id+":branch:"+before_delete.segments[1].id;
            window.select_tree_entities({branch_id},branch_id);flush();
            QMenu branch_menu;window.populate_selection_menu(branch_menu);
            const auto* select_chain=branch_menu.findChild<QAction*>("drawingSelectChainAction");
            require(select_chain,"Tree branch selection has no Select Parent action");
            auto* branch_properties=branch_menu.findChild<QAction*>("drawingEntityPropertiesAction");
            require(branch_properties,"Tree branch selection has no properties");
            branch_properties->trigger();flush();
            auto* branch_dialog=dialog();require(branch_dialog,"Branch properties did not open");
            require(branch_dialog->findChild<QComboBox*>("drawingDimensionSegment")->currentIndex()==1,"Branch properties selected a different segment");
            branch_dialog->reject();flush();
            for(const auto& segment:before_delete.segments) {
                const auto id="drawing-dimension:"+running.id+":branch:"+segment.id;
                window.select_tree_entities({id},id);
                QKeyEvent remove_branch(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);
                QApplication::sendEvent(canvas,&remove_branch);flush();
            }
            require(window.document_for_test().sheets.front().dimensions.size()==1&&window.document_for_test().sheets.front().dimensions.front().chain_datum_only,
                    "Deleting all branches removed the common zero");
            canvas->grab();
            require(window.annotation_handle_for_test(running.id,1,true).has_value()&&!window.annotation_handle_for_test(running.id,2,true).has_value(),
                    "Standalone zero exposes a nonexistent branch arrow");
            window.grab().save("build/drawing-chain-zero-proof.png");
            auto oblique=before_delete;oblique.direction=DimensionDirection::Automatic;
            const auto original=evaluate_drawing_dimension(chain_view,oblique);
            const auto kept_id=oblique.segments.back().id;
            require(erase_dimension_branch(chain_view,oblique,oblique.segments.front().id),"Cannot remove first branch");
            const auto after=evaluate_drawing_dimension(chain_view,oblique);
            require(std::abs(after.presentations.back().value-original.presentations.back().value)<1e-8&&oblique.segments.back().id==kept_id,
                    "Removing first branch rotated the automatic axis or changed branch identity");
            const auto roundtrip=deserialize_drawing_dimensions(serialize_drawing_dimensions({oblique}));
            require(roundtrip.front()==oblique,"Branch deletion lost its datum/axis on save and reopen");
            {
                for(int direction_choice:{0,1,2}) {
                DrawingDimension zero_value;
                auto* zero_dialog=new app::DrawingDimensionDialog(make_drawing_dimension(chain_view.id),true,
                    [&](const auto&){return &chain_view;},[&](auto value){zero_value=std::move(value);},&window);
                zero_dialog->show();zero_dialog->enable_chain_command();
                zero_dialog->findChild<QComboBox*>("drawingDimensionDirection")->setCurrentIndex(direction_choice);
                zero_dialog->accept_candidate(chain_view.id,{{DimensionAttachmentKind::Point,{"chain","zero",{}}},{0,0}});
                require(zero_dialog->entering(),"Point datum must request a direction point");
                zero_dialog->accept_candidate(chain_view.id,{{DimensionAttachmentKind::Point,{"chain","ten",{}}},{10,10}});
                zero_dialog->position({-8,10},true);
                require(!zero_dialog->value().chain_datum_only,"First chain dimension is only a datum");
                zero_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
                require(evaluate_drawing_dimension(chain_view,zero_value).presentations.front().value>0,"First chain dimension lost its measured value");
                require(zero_value.direction!=DimensionDirection::Automatic,"Point datum lost automatic horizontal/vertical placement");
                if(direction_choice)require(int(zero_value.direction)==direction_choice,"Placing zero overrode the explicit direction");
                require(deserialize_drawing_dimensions(serialize_drawing_dimensions({zero_value})).front()==zero_value,"Standalone zero did not round-trip");
                extend_dimension_chain(zero_value,false,{DimensionAttachmentKind::Point,{"chain","forty",{}}});
                require(!zero_value.chain_datum_only&&zero_value.segments.size()==2,"Next point did not append an independent target");
                }
            }
            {
                auto* edge_dialog=new app::DrawingDimensionDialog(make_drawing_dimension(view.id),true,
                    [&](const auto&){return &view;},[](auto){},&window);
                edge_dialog->show();edge_dialog->enable_chain_command();
                edge_dialog->accept_candidate(view.id,{{DimensionAttachmentKind::Line,{"profile","bottom",{}},{},.5},{15,0}});
                require(edge_dialog->entering()&&!edge_dialog->placing(),"First chain dimension must request its second reference");
                edge_dialog->accept_candidate(view.id,{{DimensionAttachmentKind::Line,{"profile","top",{}},{},.5},{15,20}});
                edge_dialog->position({-8,0},true);
                const auto first=edge_dialog->value();
                require(!first.chain_datum_only&&first.attachments.size()==2&&edge_dialog->entering(),"First dimension did not begin continuous entry");
                auto* table=edge_dialog->findChild<QTableWidget*>("drawingDimensionReferences");
                int visible=0;for(int row=0;row<table->rowCount();++row)visible+=!table->isRowHidden(row);
                require(visible==2&&table->verticalHeaderItem(0)->text()=="0"&&table->cellWidget(0,0),"Chain entry must expose two rows with outside numbers");
                edge_dialog->accept_candidate(view.id,{{DimensionAttachmentKind::CurvePoint,{"profile","right",{}},{},.5},{30,10}});
                require(edge_dialog->value().segments.size()==2&&edge_dialog->entering(),"Consecutive pick failed to append another dimension");
                require(edge_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled(),"Continuous entry leaves an incomplete stored dimension");
                edge_dialog->end_entry();
                auto* draft=table->findChild<QWidget*>("tableRowAction3");require(draft,"Missing continuation arrow");
                mouse(qobject_cast<QWidget*>(draft->property("_arrowWidget").value<QObject*>()),QEvent::MouseButtonRelease,{15,15},Qt::LeftButton,Qt::NoButton);
                edge_dialog->accept_candidate(view.id,{{DimensionAttachmentKind::CurvePoint,{"profile","right",{}},{},.25},{30,5}});
                require(edge_dialog->value().segments.size()==3,"Restarted entry did not append another target");
                window.grab().save("build/drawing-reference-table-proof.png");
                edge_dialog->reject();flush();
                DrawingDimension adopted;
                app::DrawingDimensionDialog continuation(make_drawing_dimension(view.id),true,[&](const auto&){return &view;},[&](auto result){adopted=std::move(result);},&window);
                continuation.setAttribute(Qt::WA_DeleteOnClose,false);continuation.enable_chain_command();
                require(continuation.awaiting_chain_seed(),"Chain command does not offer existing dimensions");
                continuation.adopt_chain_seed(first);
                require(continuation.entering()&&continuation.value().chain_group==first.id&&continuation.value().style==first.style,"Adopting an existing chain lost its shared datum or style");
                continuation.accept_candidate(view.id,{{DimensionAttachmentKind::CurvePoint,{"profile","right",{}},{},.5},{30,10}});
                continuation.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
                require(adopted.segments.size()==1&&adopted.attachments.front()==first.attachments.front(),"Continuation duplicated the seed dimension");
            }
        }
        {
            auto original=make_drawing_dimension(view.id);
            original.attachments={{DimensionAttachmentKind::Line,{"profile","bottom",{}}},{DimensionAttachmentKind::Line,{"profile","top",{}}}};
            bool committed=false;
            app::DrawingDimensionDialog edit(original,false,[&](const auto&){return &view;},[&](auto){committed=true;},&window);
            edit.setAttribute(Qt::WA_DeleteOnClose,false);
            auto* table=edit.findChild<QTableWidget*>("drawingDimensionReferences");
            auto* action=table->findChild<QWidget*>("tableRowAction0");
            require(action,"Required reference has no removal action");
            auto* remove=qobject_cast<QPushButton*>(action->property("_removeWidget").value<QObject*>());
            require(remove,"Required reference has no cross");
            remove->click();
            require(!edit.value().attachments[0].reference.valid()&&edit.value().attachments[1]==original.attachments[1],"Clearing a required reference changed its other endpoint");
            require(!table->isRowHidden(1),"Clearing the first reference hid an existing second reference");
            require(!edit.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled(),"Incomplete edited dimension can be committed");
            edit.accept_candidate(view.id,{{DimensionAttachmentKind::Line,{"profile","bottom",{}}},{15,0}});
            require(edit.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled(),"Replacing a required reference did not restore OK");
            edit.reject();
            require(!committed,"Cancel committed pending reference edits");
            auto radial_value=make_drawing_dimension(view.id,DrawingDimensionKind::Radius);
            radial_value.direction=DimensionDirection::Parallel;
            radial_value.attachments={{DimensionAttachmentKind::Center,{"profile","circle",{}}}};
            app::DrawingDimensionDialog radial_edit(radial_value,false,[&](const auto&){return &view;},[](auto){},&window);
            require(radial_edit.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled(),"Radial dimension requires an irrelevant parallel direction reference");
        }
        {
            auto group_document=drawing;group_document.sheets.front().dimensions.clear();group_document.sheets.front().views={view};
            auto chain=make_drawing_dimension(view.id,DrawingDimensionKind::Chain);
            chain.attachments={{DimensionAttachmentKind::Line,{"profile","bottom",{}}},{DimensionAttachmentKind::Line,{"profile","top",{}}}};
            place_drawing_dimension(view,chain,0,{-8,10});
            extend_dimension_chain(chain,false,{DimensionAttachmentKind::CurvePoint,{"profile","right",{}},{},.5});
            workspace::commit_drawing_chain(group_document,group_document.sheets.front().id,chain,true);
            auto* state=workspace.open_drawing(drawing.document_id);state->commit(group_document);window.edit_workspace_document(drawing.document_id);flush();canvas->grab();
            const auto members=window.document_for_test().sheets.front().dimensions;
            const auto zero=window.annotation_handle_for_test(members[0].id,1,true);require(zero.has_value(),"Shared zero has no grip");
            mouse(canvas,QEvent::MouseMove,*zero,Qt::NoButton,Qt::NoButton);
            mouse(canvas,QEvent::MouseButtonPress,*zero,Qt::LeftButton,Qt::LeftButton);
            require(canvas->property("drawingSelectionCount").toInt()==2,"Picking common zero did not select all independent members");
            mouse(canvas,QEvent::MouseMove,*zero+QPointF(-24,0),Qt::NoButton,Qt::LeftButton);
            mouse(canvas,QEvent::MouseButtonRelease,*zero+QPointF(-24,0),Qt::LeftButton,Qt::NoButton);
            const auto shifted=window.document_for_test().sheets.front().dimensions;
            require(shifted[0].segments[0].layout.line_offset!=members[0].segments[0].layout.line_offset&&shifted[0].segments[0].layout.line_offset==shifted[1].segments[0].layout.line_offset,"Shared zero drag did not move the whole chain");
            require(state->undo()&&state->document().sheets.front().dimensions==members,"Shared zero drag is not one reversible transaction");
            window.edit_workspace_document(drawing.document_id);flush();canvas->grab();
            const auto seed_point=window.annotation_handle_for_test(members[1].id,0,true);require(seed_point.has_value(),"Independent dimension has no selectable value");
            auto* chain_action=window.findChild<QAction*>("drawingChainDimensionAction");require(chain_action&&chain_action->isEnabled(),"Standalone chain command missing");
            chain_action->trigger();flush();canvas->grab();
            auto* continuation=dialog();require(continuation&&continuation->awaiting_chain_seed(),"New chain command cannot adopt an existing dimension");
            pick(canvas,*seed_point);
            require(continuation->value().chain_group==members[1].chain_group&&continuation->value().chain_datum_only,"Canvas picking did not adopt the existing chain");
            continuation->accept_candidate(view.id,{{DimensionAttachmentKind::CurvePoint,{"profile","right",{}},{},.25},{30,5}});
            continuation->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            require(window.document_for_test().sheets.front().dimensions.size()==3,"Continuation duplicated its seed or failed to add a dimension");
            require(state->undo()&&state->document().sheets.front().dimensions==members,"Continuing a chain is not one reversible transaction");
            auto ordinary=group_document;ordinary.sheets.front().dimensions.clear();
            auto a=make_drawing_dimension(view.id);a.attachments=members[0].attachments;place_drawing_dimension(view,a,0,{-8,10});
            auto b=make_drawing_dimension(view.id);b.attachments=members[0].attachments;place_drawing_dimension(view,b,0,{38,10});
            ordinary.sheets.front().dimensions={a,b};state->commit(ordinary);window.edit_workspace_document(drawing.document_id);flush();canvas->grab();
            window.select_tree_entities({"drawing-dimension:"+a.id,"drawing-dimension:"+b.id},"drawing-dimension:"+a.id);
            const auto grip=window.annotation_handle_for_test(a.id,0,true);require(grip.has_value(),"Selected ordinary dimension has no text grip");
            mouse(canvas,QEvent::MouseMove,*grip,Qt::NoButton,Qt::NoButton);
            mouse(canvas,QEvent::MouseButtonPress,*grip,Qt::LeftButton,Qt::LeftButton);
            mouse(canvas,QEvent::MouseMove,*grip+QPointF(-20,-18),Qt::NoButton,Qt::LeftButton);
            mouse(canvas,QEvent::MouseButtonRelease,*grip+QPointF(-20,-18),Qt::LeftButton,Qt::NoButton);
            const auto moved=window.document_for_test().sheets.front().dimensions;
            require(moved[0].segments[0].layout!=a.segments[0].layout&&moved[1].segments[0].layout!=b.segments[0].layout,"Dragging multiple selected dimensions moved only one");
            require(state->undo()&&state->document().sheets.front().dimensions==ordinary.sheets.front().dimensions,"Multi-dimension drag did not undo atomically");
        }
        {
            const auto original_settings=app::ApplicationSettings::load();
            QTemporaryDir language_directory;
            const std::array<std::pair<const char*,const char*>,5> labels{{
                {"cs","Přidat větev"},{"en","Add branch"},{"de","Zweig hinzufügen"},
                {"fr","Ajouter une branche"},{"ru","Добавить ветвь"}}};
            for(const auto& [language,expected]:labels) {
                QFile config(language_directory.filePath("config.ini"));require(config.open(QIODevice::WriteOnly),"Cannot create language fixture");
                config.write(QByteArray("[Application]\nLanguage=")+language+"\n");config.close();
                app::apply_application_translations(*qApp,app::ApplicationSettings::load(language_directory.path()));
                app::DrawingWindow translated_window(&workspace,false);
                const std::map<std::string,QString> command_labels{{"cs",QString::fromUtf8("Řetězová kóta")},{"en","Chain dimension"},{"de",QString::fromUtf8("Kettenbemaßung")},{"fr",QString::fromUtf8("Cotation en chaîne")},{"ru",QString::fromUtf8("Цепочка размеров")}};
                require(translated_window.findChild<QAction*>("drawingChainDimensionAction")->text()==command_labels.at(language),"Chain command is not localized after switching language");
                auto localized_value=make_drawing_dimension(view.id,DrawingDimensionKind::Chain);
                localized_value.attachments={{DimensionAttachmentKind::Line,{"profile","bottom",{}}},{DimensionAttachmentKind::Line,{"profile","top",{}}}};
                app::DrawingDimensionDialog localized(localized_value,false,
                    [&](const auto&){return &view;},[](auto){},&window);
                localized.enable_chain_command();
                const auto* references=localized.findChild<QTableWidget*>("drawingDimensionReferences");
                require(references->item(2,2)->text()==QString::fromUtf8(expected),
                    "Add branch is not localized after switching language");
            }
            app::apply_application_translations(*qApp,original_settings);
        }
        std::cout << "Manual dimension properties, references, preview/Cancel, MMB, measured values and "
                     "radius grips passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
