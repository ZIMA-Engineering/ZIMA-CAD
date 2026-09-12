#include "drawing_dimension_dialog.hpp"
#include "drawing_window.hpp"
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTabWidget>
#include <iostream>
#include <numbers>
#include <zima/kernel/stable_id.hpp>
#include <zima/workspace/workspace.hpp>
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
        const auto linear = window.document_for_test().sheets.front().dimensions.front();
        require(linear.style.prefix == "2×" && linear.style.symmetric_tolerance == "0.1",
                "Unified tolerance fields not committed");
        const auto evaluation =
            evaluate_drawing_dimension(*window.document_for_test().find_view(view.id), linear);
        require(std::abs(evaluation.presentations[0].value - 20) < 1e-9,
                "UI dimension changed projected measurement");

        // One dialog class also edits and extends an existing dimension.
        auto grip = window.annotation_handle_for_test(linear.id, 0, true);
        require(grip.has_value(), "Committed dimension has no grip");
        pick(canvas, *grip);
        mouse(canvas, QEvent::MouseButtonDblClick, *grip, Qt::LeftButton, Qt::LeftButton);
        props = dialog();
        require(props, "Existing dimension did not open properties");
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
        require(window.document_for_test().sheets.front().dimensions.front().segments.front() ==
                    linear.segments.front(),
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
        const auto pixels=[&](QColor color){const auto image=canvas->grab().toImage();std::size_t found=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(image.pixelColor(x,y).rgb()==color.rgb())++found;return found;};
        require(pixels(QColor("#FFD400"))>10,"Valid angle is not yellow");window.grab().save("build/drawing-angle-valid.png");
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
        std::cout << "Manual dimension properties, references, preview/Cancel, MMB, measured values and "
                     "radius grips passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
