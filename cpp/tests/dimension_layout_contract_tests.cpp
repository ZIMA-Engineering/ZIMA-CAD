#include "../app/dimension_layout_dialog.hpp"
#include "../app/drawing_annotation_layout.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTemporaryDir>
#include <iostream>
#include <zima/assembly/assembly_document.hpp>
#include <zima/document/dimension_layout_json.hpp>
#include <zima/document/object_annotation_frames.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/drawing/model_annotations.hpp>
#include <zima/kernel/mirror_geometry.hpp>
#include <zima/viewer/annotation_arrow.hpp>
#include <zima/viewer/dimension_presentation.hpp>
#include <zima/viewer/mesh_view.hpp>
using namespace zima;
void require(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
void near(double a, double b) { require(std::abs(a - b) < 1e-6, "Geometric measure changed"); }
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Invalid presentation accepted");
}
void flush() { QApplication::processEvents(); }
void mouse(QWidget *w, QEvent::Type type, QPointF p, Qt::MouseButton button,
           Qt::MouseButtons buttons) {
    QMouseEvent event(type, p, w->mapToGlobal(p.toPoint()), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(w, &event);
    flush();
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        {
            kernel::ViewerDimension d;
            d.witness_second = {20, 0, 0};
            d.line_first = {0, 8, 0};
            d.line_second = {20, 8, 0};
            d.value = 20;
            const auto front = [](kernel::Vec3 p) { return QPointF(p.x * 10, -p.y * 10); };
            const auto iso = [](kernel::Vec3 p) {
                return QPointF((p.x - p.y) * 7, (-p.x - p.y) * 3.5 - p.z * 7);
            };
            auto centered = viewer::dimension_presentation(d, front, 50);
            require(centered.valid && !centered.oblique && !centered.outside,
                    "Normal dimension did not center its label");
            near(centered.handles[0].x(), 100);
            near(centered.text_baseline.y(), centered.handles[0].y() - 3);
            auto slanted = viewer::dimension_presentation(d, iso, 50);
            require(slanted.oblique && slanted.outside && slanted.text_angle == 0,
                    "Oblique dimension did not use horizontal outside text");
            require(slanted.handles[0].x() >
                        std::max(slanted.handles[1].x(), slanted.handles[2].x()),
                    "Oblique value stayed between arrows");
            d.label_position = kernel::Vec3{-20, 8, 0};
            auto left = viewer::dimension_presentation(d, iso, 50);
            require(left.handles[0].x() < std::min(left.handles[1].x(), left.handles[2].x()),
                    "Left outside label switched sides");
            // Regress the red/green parallel marks in the user's screenshot:
            // both the automatic and dragged leader continue the measured line.
            for (auto kind : {kernel::ViewerDimensionKind::Linear,
                              kernel::ViewerDimensionKind::Radius,
                              kernel::ViewerDimensionKind::Diameter}) {
                auto sample = d;
                sample.kind = kind;
                for (double side : {-1., 1.}) {
                    sample.label_position = kernel::Vec3{side * 40, 23, 0};
                    for (bool oblique : {false, true}) {
                        const auto shown = viewer::dimension_presentation(
                            sample, [&](kernel::Vec3 p) { return oblique ? iso(p) : front(p); }, 50);
                        const auto index = kind == kernel::ViewerDimensionKind::Linear ? 3 : 1;
                        const auto leader = shown.curves.at(index);
                        const auto measured = shown.curves.at(index - 1);
                        const auto u = measured.back() - measured.front();
                        const auto v = leader.back() - leader.front();
                        near(u.x() * v.y() - u.y() * v.x(), 0);
                        require(QLineF(leader.front(), leader.back()).length() >= 16.9,
                                "Leader collapsed at outside arrow");
                        if (oblique)
                            near(shown.text_angle, 0);
                    }
                }
            }
            for (double x : {2., 8., 10., 12., 18.}) {
                auto inside = d;
                inside.label_position = kernel::Vec3{x, 8, 0};
                const auto shown = viewer::dimension_presentation(inside, iso, 50);
                const double lo = std::min(shown.handles[1].x(), shown.handles[2].x());
                const double hi = std::max(shown.handles[1].x(), shown.handles[2].x());
                require(shown.outside && (shown.handles[0].x() + 25 < lo ||
                                         shown.handles[0].x() - 25 > hi),
                        "Dragging between witnesses placed isometric text inside");
            }
            d.arrows_reversed = true;
            auto reversed = viewer::dimension_presentation(d, iso, 50);
            require(reversed.arrows[0].second == -left.arrows[0].second &&
                        reversed.handles == left.handles,
                    "Arrow reversal moved grips or failed");
            drawing::DrawingView drawing_view;
            drawing_view.camera.horizontal = {1, 0, 0};
            drawing_view.camera.vertical = {0, 1, 0};
            drawing_view.scale = 1;
            drawing::ModelAnnotation annotation;
            annotation.kind = drawing::ModelAnnotationKind::Dimension;
            annotation.model_dimension = d;
            annotation.model_layout.arrows_reversed = true;
            const auto paper = app::model_annotation_layout(drawing_view, annotation, {}, 12.5);
            const auto expected = viewer::dimension_presentation(
                d, [](kernel::Vec3 p) { return QPointF(p.x, -p.y); }, 12.5, 2.5, .75);
            near(paper.handles.at("text").x(), expected.handles[0].x());
            near(paper.handles.at("text").y(), -expected.handles[0].y());
            require(paper.text_angle == expected.text_angle &&
                        paper.arrows.size() == expected.arrows.size(),
                    "Drawing and model presentation diverged");
            kernel::DimensionLayout layout;
            layout.arrows_reversed = true;
            layout.line_offset = 4;
            require(document::dimension_layout_from_json(document::dimension_layout_json(layout)) ==
                        layout,
                    "Presentation state did not round-trip");
            auto moved = kernel::layout_dimension(d, {}, layout);
            require(moved.value == d.value && moved.witness_first == d.witness_first &&
                        moved.witness_second == d.witness_second,
                    "Appearance changed measuring geometry");
            QImage proof(1000, 600, QImage::Format_ARGB32);
            proof.fill(Qt::white);
            QPainter painter(&proof);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setFont(QFont("Arial", 12));
            for (int panel = 0; panel < 6; ++panel) {
                auto sample = d;
                sample.arrows_reversed = false;
                sample.label_position.reset();
                if (panel == 2)
                    sample.label_position = kernel::Vec3{-20, 8, 0};
                if (panel >= 3) {
                    sample.kind = panel == 3   ? kernel::ViewerDimensionKind::Radius
                                  : panel == 4 ? kernel::ViewerDimensionKind::Diameter
                                               : kernel::ViewerDimensionKind::Angular;
                    if (panel == 5) {
                        sample.line_first = {15, 0, 0};
                        sample.line_second = {0, 15, 0};
                        sample.sweep_degrees = 90;
                    }
                }
                const QString label = panel == 3   ? "R20"
                                      : panel == 4 ? QString::fromUtf8("⌀40")
                                      : panel == 5 ? QString::fromUtf8("90°")
                                                   : "20 mm";
                const auto project = [&](kernel::Vec3 p) { return panel == 0 ? front(p) : iso(p); };
                const auto result = viewer::dimension_presentation(
                    sample, project, painter.fontMetrics().horizontalAdvance(label));
                require(result.valid, "Presentation sample invalid");
                if (panel >= 3)
                    require(result.oblique && result.text_angle == 0,
                            "Radius/diameter/angle text rotated in isometry");
                painter.save();
                painter.translate(140 + (panel % 3) * 330, 210 + (panel / 3) * 285);
                painter.setPen(QPen(Qt::black, 1));
                painter.setBrush(Qt::black);
                for (const auto &curve : result.curves)
                    painter.drawPolyline(curve);
                for (const auto &[tip, direction] : result.arrows)
                    painter.drawPolygon(viewer::annotation_arrow(tip, direction, 10));
                painter.save();
                painter.translate(result.text_baseline);
                painter.rotate(result.text_angle);
                painter.drawText(QPointF{}, label);
                painter.restore();
                painter.setBrush(QColor("#D05CFF"));
                for (auto grip : result.handles)
                    painter.drawEllipse(grip, 3, 3);
                painter.restore();
            }
            painter.end();
            require(proof.save(QCoreApplication::applicationDirPath() +
                               "/dimension-presentation-proof.png"),
                    "Cannot save visual proof");
        }
        kernel::ModelEnvelope bounds;
        bounds.include({0, 0, 0});
        bounds.include({20, 10, 5});
        kernel::ViewerDimension source;
        source.reference = {"feature", "parameter:length", {}};
        source.witness_second = {20, 0, 0};
        source.line_first = {0, 4, 0};
        source.line_second = {20, 4, 0};
        source.plane_normal = {0, 0, 1};
        source.value = 20;
        for (int plane = 0; plane < 4; ++plane) {
            const auto shown = kernel::layout_dimension(source, bounds, {plane, 8., 0, 0});
            require(shown.reference == source.reference &&
                        shown.witness_first == source.witness_first &&
                        shown.witness_second == source.witness_second && shown.value == 20,
                    "Presentation changed source references");
            const auto side =
                kernel::dimension_unit(kernel::dimension_cross(shown.plane_normal, {1, 0, 0}));
            double support = -1e99;
            for (auto c : bounds.corners())
                support = std::max(support, kernel::dimension_dot(c, side));
            near(kernel::dimension_dot(shown.line_first, side) - support, 8);
            near(kernel::dimension_dot(kernel::dimension_sub(shown.line_second, shown.line_first),
                                       {1, 0, 0}),
                 20);
        }
        const auto shown = kernel::layout_dimension(source, bounds, {0, 8., 0, 0});
        auto drag = kernel::dragged_dimension_layout(shown, bounds, {0, 8., 0, 0}, 1, 0, 12);
        near(*drag.envelope_offset, 20);
        auto moved = kernel::layout_dimension(source, bounds, drag);
        near(moved.line_first.y, 30);
        kernel::ViewerDimension angle = source;
        angle.kind = kernel::ViewerDimensionKind::Angular;
        angle.line_first = {4, 0, 0};
        angle.line_second = {0, 4, 0};
        angle.sweep_degrees = 90;
        angle.value = 90;
        rejects([&] { kernel::layout_dimension(angle, bounds, {1, 8., 0, 0}); });
        auto arc = kernel::layout_dimension(angle, bounds, {0, 8., 0, 0});
        near(arc.value, 90);
        near(arc.line_first.x, std::hypot(20., 10.) + 8);
        auto frame = kernel::annotation_frame({4, 7, 2}, {0, 0, 45});
        frame.include(frame.world({0, 0, 0}));
        frame.include(frame.world({20, 10, 5}));
        near(frame.maximum.x - frame.minimum.x, 20);
        near(frame.maximum.y - frame.minimum.y, 10);
        kernel::ViewerMesh mesh;
        mesh.vertices = {frame.world({0, 0, 0}), frame.world({20, 0, 0}), frame.world({20, 10, 5})};
        mesh.triangles = {0, 1, 2};
        mesh.triangle_references = {kernel::FaceReference{"feature", "face", {}}};
        mesh.annotation_frames[{}] = frame;
        mesh.annotation_frames[{"feature", {}}] = frame;
        mesh.dimensions = {source};
        mesh.dimensions[0].line_second = {1e6, 1e6, 1e6};
        auto geometric = kernel::model_envelope(mesh);
        require(geometric.maximum.x < 100 && geometric.maximum.y < 100,
                "Dimension inflated geometry bounds");
        kernel::ViewerPoint point;
        point.reference = {"point", "point", {}};
        point.position = {2, 3, 4};
        mesh.points.push_back(point);
        const auto frames = kernel::object_envelopes(mesh);
        require(frames.at({"point", {}}).minimum == frames.at({"point", {}}).maximum,
                "Point frame acquired volume");
        auto packet = kernel::BodyResult{};
        packet.mesh = mesh;
        const auto loaded_packet =
            document::load_body_result(document::serialize_body_result(packet));
        require(loaded_packet.mesh.annotation_frames == mesh.annotation_frames,
                "Snapshot lost local oriented frames");
        auto assembly = assembly::AssemblyDocument::create_default();
        auto occurrence =
            assembly::AssemblyDocument::create_part_occurrence("Part", "part", {}, packet);
        occurrence.placement.rotation_z = 90;
        assembly.components.push_back(occurrence);
        auto second = occurrence;
        second.occurrence_id = "second";
        second.placement.x = 100;
        assembly.components.push_back(second);
        auto scene = assembly.build_scene();
        const auto first_path = assembly::InstancePath{}.child(occurrence.occurrence_id).encoded(),
                   second_path = assembly::InstancePath{}.child(second.occurrence_id).encoded();
        const auto a = scene.annotation_frames.at({"feature", first_path}),
                   b = scene.annotation_frames.at({"feature", second_path});
        near(b.origin.x - a.origin.x, 100);
        near(a.axes[0].x, -frame.axes[0].y);
        near(a.maximum.x - a.minimum.x, 20);
        const auto reflected = kernel::mirrored_viewer_mesh(mesh, {{0, 0, 0}, {1, 0, 0}});
        near(reflected.annotation_frames.at({}).origin.x, -frame.origin.x);
        near(reflected.annotation_frames.at({}).axes[0].x, -frame.axes[0].x);
        QTemporaryDir dir;
        auto part = document::PartDocument::create_default();
        kernel::store_dimension_layout(part.dimension_layouts, source.reference, {2, 14., 3, 4});
        part.save((dir.path() + "/part.prtz").toStdString());
        require(document::PartDocument::load((dir.path() + "/part.prtz").toStdString())
                        .dimension_layouts == part.dimension_layouts,
                "Part lost presentation on reopen");
        kernel::store_dimension_layout(assembly.dimension_layouts,
                                       {assembly.document_id, "mate:angle", {}}, {0, 12., 2, 1});
        assembly.save((dir.path() + "/assembly.asmz").toStdString());
        require(assembly::AssemblyDocument::load((dir.path() + "/assembly.asmz").toStdString())
                        .dimension_layouts == assembly.dimension_layouts,
                "Assembly lost presentation on reopen");
        drawing::ModelAnnotationSource annotation_source;
        annotation_source.document_id = part.document_id;
        annotation_source.dimensions = {source};
        annotation_source.envelope = bounds;
        annotation_source.layouts = part.dimension_layouts;
        drawing::DrawingView view;
        view.camera = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        drawing::refresh_model_annotations(view, std::span(&annotation_source, 1));
        auto other = view;
        view.model_annotations[0].view_layout = kernel::DimensionLayout{1, 20., 5, 6};
        auto projected = drawing::project_model_annotation(view, view.model_annotations[0]);
        require(other.model_annotations[0].view_layout == std::nullopt &&
                    part.dimension_layouts[0].layout.plane_quarter_turns == 2,
                "Drawing layout leaked into model or another view");
        require(projected.value == 20, "Drawing changed measured value");
        require(drawing::deserialize_model_annotations(drawing::serialize_model_annotations(
                    view.model_annotations)) == view.model_annotations,
                "Drawing lost local override");
        annotation_source.dimensions[0].value = 30;
        drawing::refresh_model_annotations(view, std::span(&annotation_source, 1));
        require(view.model_annotations[0].value == 30 &&
                    view.model_annotations[0].view_layout->plane_quarter_turns == 1,
                "Refresh discarded drawing override or live value");
        QWidget owner;
        owner.resize(900, 700);
        viewer::MeshView viewer(&owner);
        viewer.setGeometry(0, 0, 900, 700);
        owner.show();
        viewer.show();
        flush();
        kernel::DimensionLayout persisted{0, 8., 0, 0};
        int commits = 0;
        mesh = {};
        mesh.vertices = {{0, 0, 0}, {20, 0, 0}, {20, 10, 5}};
        mesh.triangles = {0, 1, 2};
        mesh.triangle_references = {kernel::FaceReference{}};
        mesh.dimensions = {source};
        viewer.set_dimension_layout_resolver(
            [&](const auto &) -> std::optional<kernel::DimensionLayout> { return persisted; });
        viewer.set_dimension_layout_commit([&](const auto &reference, auto value) {
            require(reference == source.reference, "Drag committed wrong owner");
            persisted = value;
            ++commits;
        });
        viewer.set_mesh(mesh);
        viewer.set_view_direction({0, 0, 1});
        viewer.fit_all();
        flush();
        viewer.confirm_reference("feature", "parameter:length", {},
                                 viewer::CandidateKind::Dimension);
        auto candidate = viewer.confirmed_candidate();
        require(candidate.has_value(), "Dimension confirmation failed");
        auto handle = viewer.dimension_handle_position(*candidate, 0);
        require(handle.has_value(), "Dimension text grip missing");
        mouse(&viewer, QEvent::MouseButtonPress, *handle, Qt::LeftButton, Qt::LeftButton);
        mouse(&viewer, QEvent::MouseMove, *handle + QPointF(35, -20), Qt::NoButton, Qt::LeftButton);
        require(commits == 0, "Dragging committed before release");
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&viewer, &escape);
        mouse(&viewer, QEvent::MouseButtonRelease, *handle + QPointF(35, -20), Qt::LeftButton,
              Qt::NoButton);
        require(commits == 0, "Escape committed drag");
        mouse(&viewer, QEvent::MouseButtonPress, *handle, Qt::LeftButton, Qt::LeftButton);
        mouse(&viewer, QEvent::MouseMove, *handle + QPointF(35, -20), Qt::NoButton, Qt::LeftButton);
        mouse(&viewer, QEvent::MouseButtonRelease, *handle + QPointF(35, -20), Qt::LeftButton,
              Qt::NoButton);
        if (commits != 1)
            std::cerr << "Grip commits=" << commits
                      << " confirmed=" << viewer.confirmed_candidate().has_value()
                      << " grip=" << handle->x() << "," << handle->y() << "\n";
        require(commits == 1 && (persisted.text_along != 0 || persisted.text_outward != 0),
                "3D grip did not persist model-space text movement");
        require(viewer.dimension_source(*candidate) == source, "Dragging mutated source geometry");
        viewer.confirm_reference("feature", "parameter:length", {},
                                 viewer::CandidateKind::Dimension);
        handle = viewer.dimension_handle_position(*viewer.confirmed_candidate(), 0);
        mouse(&viewer, QEvent::MouseButtonPress, *handle, Qt::LeftButton, Qt::LeftButton);
        mouse(&viewer, QEvent::MouseButtonPress, *handle, Qt::RightButton,
              Qt::LeftButton | Qt::RightButton);
        mouse(&viewer, QEvent::MouseButtonRelease, *handle, Qt::RightButton, Qt::LeftButton);
        mouse(&viewer, QEvent::MouseButtonRelease, *handle, Qt::LeftButton, Qt::NoButton);
        require(persisted.arrows_reversed && commits == 2,
                "RMB during LMB grip did not persist reversed arrows");
        viewer.set_dimension_frame_visible(true);
        viewer.grab().save("build/dimension-layout-view.png");
        int dialog_commits = 0;
        app::DimensionLayoutDialog dialog(
            source, persisted, [&](auto) { ++dialog_commits; }, &owner);
        dialog.show();
        flush();
        owner.grab().save("build/dimension-layout-properties.png");
        require(dialog.windowType() == Qt::SubWindow && dialog.parentWidget() == &owner,
                "Presentation dialog escaped owning application");
        require(dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply) == nullptr,
                "Presentation dialog exposes Apply");
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        require(dialog_commits == 0, "Cancel committed presentation");
        app::DimensionLayoutDialog confirm(
            source, persisted, [&](auto value) {
                require(value.arrows_reversed==persisted.arrows_reversed &&
                            value.text_outward==persisted.text_outward,
                        "Properties discarded arrow direction or text attachment");
                ++dialog_commits;
            }, &owner);
        confirm.show();
        flush();
        mouse(&viewer, QEvent::MouseButtonRelease, {850, 650}, Qt::MiddleButton, Qt::NoButton);
        require(dialog_commits == 0, "Short MMB confirmed presentation");
        mouse(&viewer, QEvent::MouseButtonDblClick, {850, 650}, Qt::MiddleButton, Qt::MiddleButton);
        require(dialog_commits == 1, "MMB over View did not confirm presentation");
        app::DimensionLayoutDialog angular_dialog(angle, {0, 8., 0, 0}, [](auto) {}, &owner);
        require(!angular_dialog.findChild<QComboBox *>("dimensionProjectionPlane")->isEnabled(),
                "Angular presentation offers an invalid plane");
        std::cout << "Oriented frames, occurrences, immutable measurements, persistence, per-view "
                     "overrides and native grip transactions passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
