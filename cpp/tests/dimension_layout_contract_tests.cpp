#include "../app/dimension_properties_fields.hpp"
#include "../app/drawing_annotation_layout.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTemporaryDir>
#include <QVariantAnimation>
#include <iostream>
#include <source_location>
#include <zima/viewer/dimension_text_layer.hpp>
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
void near(double a, double b, std::source_location where=std::source_location::current()) { if(std::abs(a-b)>=1e-6)throw std::runtime_error("Geometric measure changed at line "+std::to_string(where.line())+": "+std::to_string(a)+" vs "+std::to_string(b)); }
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
        {
            drawing::ModelAnnotationSource source;
            source.document_id="part";
            source.envelope.include({-20,-10,-5});source.envelope.include({20,10,5});
            source.axes.push_back({{0,0,0},{0,0,1},10,{"part:origin","origin:axis:z",{}}});
            drawing::DrawingView view;view.camera={{1,0,0},{0,1,0},{0,0,1}};view.scale=1;
            drawing::refresh_model_annotations(view,std::span(&source,1));
            auto cross=app::model_annotation_layout(view,view.model_annotations[0],{});
            require(cross.curves.size()==4 && cross.centers.size()==1,"End-on origin axis has no cross");
            near(cross.curves[0].back().x(),-22);near(cross.curves[1].back().x(),22);
            near(cross.curves[2].back().y(),-12);near(cross.curves[3].back().y(),12);
            auto corner_axis=view.model_annotations[0];
            corner_axis.model_envelope={};corner_axis.model_envelope.include({0,0,0});corner_axis.model_envelope.include({40,20,10});
            auto corner_cross=app::model_annotation_layout(view,corner_axis,{});
            near(corner_cross.curves[0].back().x(),-2);near(corner_cross.curves[1].back().x(),42);
            near(QLineF(corner_cross.curves[0].back(),corner_cross.curves[1].back()).length(),44);
            const auto saved=drawing::deserialize_model_annotations(drawing::serialize_model_annotations(view.model_annotations));
            view.camera={{0,0,1},{0,1,0},{-1,0,0}};
            auto side=app::model_annotation_layout(view,saved[0],{});
            require(side.curves.size()==1,"Rotating saved axis did not replace cross with line");
            near(QLineF(side.curves[0].front(),side.curves[0].back()).length(),14);
            kernel::ViewerDimension d;d.witness_second={10,0,0};d.line_first={0,4,0};d.line_second={10,4,0};
            const auto end_on=[](kernel::Vec3 p){return QPointF(p.z,p.y);};
            require(!viewer::dimension_presentation(d,end_on,10).valid,"End-on dimension stayed visible");
            const auto edge_on_plane=[](kernel::Vec3 p){return QPointF(p.x,p.z);};
            require(viewer::dimension_presentation(d,edge_on_plane,10).valid,"Visible length hidden by edge-on plane");
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
        // Establish the normal view before sampling grip coordinates. Otherwise
        // the 850 ms camera animation races the synthetic mouse events.
        for(auto* animation:viewer.findChildren<QVariantAnimation*>())
            animation->setCurrentTime(animation->duration());
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
        for(auto kind:{kernel::ViewerDimensionKind::Radius,kernel::ViewerDimensionKind::Diameter}) {
            persisted={};
            auto radial_source=source;radial_source.kind=kind;
            radial_source.label_position=kernel::Vec3{28,0,0};
            mesh.dimensions={radial_source};viewer.set_mesh(mesh);
            viewer.set_active_sketch_owner("feature");
            viewer.confirm_reference("feature","parameter:length",{},viewer::CandidateKind::Dimension);
            const auto selected=*viewer.confirmed_candidate();
            const auto before=*viewer.dimension_handle_position(selected,0);
            int menus=0;viewer.set_context_menu_callback([&](const auto&,const auto&){++menus;});
            mouse(&viewer,QEvent::MouseButtonPress,before,Qt::RightButton,Qt::RightButton);
            mouse(&viewer,QEvent::MouseButtonRelease,before,Qt::RightButton,Qt::NoButton);
            require(menus==0,"Grip opened dimension context menu");
            mouse(&viewer,QEvent::MouseButtonPress,before,Qt::LeftButton,Qt::LeftButton);
            mouse(&viewer,QEvent::MouseMove,before+QPointF(25,-50),Qt::NoButton,Qt::LeftButton);
            const auto after=*viewer.dimension_handle_position(selected,0);
            if(QLineF(before,after).length()<=15)std::cerr<<"Radial kind="<<int(kind)<<" before="<<before.x()<<","<<before.y()<<" after="<<after.x()<<","<<after.y()<<" commits="<<commits<<"\n";
            require(QLineF(before,after).length()>15,"Normal radial text grip did not move");
            mouse(&viewer,QEvent::MouseButtonRelease,after,Qt::LeftButton,Qt::NoButton);
            require(viewer.dimension_source(selected)==radial_source,"Radial drag changed measuring data");
            if(kind==kernel::ViewerDimensionKind::Radius) {
                viewer.confirm_reference("feature","parameter:length",{},viewer::CandidateKind::Dimension);
                mouse(&viewer,QEvent::MouseButtonPress,after,Qt::LeftButton,Qt::LeftButton);
                for(int step=0;step<2;++step) {
                    mouse(&viewer,QEvent::MouseButtonPress,after,Qt::RightButton,Qt::LeftButton|Qt::RightButton);
                    mouse(&viewer,QEvent::MouseButtonRelease,after,Qt::RightButton,Qt::LeftButton);
                }
                mouse(&viewer,QEvent::MouseButtonRelease,after,Qt::LeftButton,Qt::NoButton);
                if(!persisted.radius_center_line_hidden)std::cerr<<"cycle flags="<<persisted.arrows_reversed<<","<<persisted.radius_center_line_hidden<<" commits="<<commits<<" selected="<<viewer.confirmed_candidate().has_value()<<"\n";
                require(persisted.arrows_reversed&&persisted.radius_center_line_hidden,"Radius cycle did not hide center line");
                const auto short_grip=*viewer.dimension_handle_position(selected,0);
                mouse(&viewer,QEvent::MouseButtonPress,short_grip,Qt::LeftButton,Qt::LeftButton);
                mouse(&viewer,QEvent::MouseMove,short_grip+QPointF(-60,0),Qt::NoButton,Qt::LeftButton);
                const auto dragged_short=*viewer.dimension_handle_position(selected,0);
                require(QLineF(short_grip,dragged_short).length()>35,"Re-grabbed shortened radius did not move");
                mouse(&viewer,QEvent::MouseButtonRelease,dragged_short,Qt::LeftButton,Qt::NoButton);
                require(document::dimension_layout_from_json(document::dimension_layout_json(persisted))==persisted,"Radius presentation lost on save");
                auto displayed=kernel::layout_dimension(radial_source,{},persisted);
                const auto front=[](kernel::Vec3 p){return QPointF(p.x*10,-p.y*10);};
                kernel::BodyResult packet;packet.mesh.dimensions={displayed};
                const auto reloaded=document::load_body_result(document::serialize_body_result(packet));
                require(reloaded.mesh.dimensions.front().radius_center_line_hidden &&
                        reloaded.mesh.dimensions.front().arrows_reversed &&
                        reloaded.mesh.dimensions.front().label_position==displayed.label_position,
                        "Viewer packet lost radius presentation");
                const auto presentation=viewer::dimension_presentation(displayed,front,50);
                require(presentation.curves.size()==2 && presentation.curves[0].front()==front(displayed.witness_second),
                        "Shortened radius leader did not start at measured arrow");
                for(const auto delta:{QPointF(-120,20),QPointF(240,-40),QPointF(-450,40)}) {
                    viewer.confirm_reference("feature","parameter:length",{},viewer::CandidateKind::Dimension);
                    const auto candidate=*viewer.confirmed_candidate();
                    const auto text=*viewer.dimension_handle_position(candidate,0);
                    const auto rim=*viewer.dimension_handle_position(candidate,1);
                    mouse(&viewer,QEvent::MouseButtonPress,text,Qt::LeftButton,Qt::LeftButton);
                    mouse(&viewer,QEvent::MouseMove,text+delta,Qt::NoButton,Qt::LeftButton);
                    const auto moved=*viewer.dimension_handle_position(candidate,0);
                    require(QLineF(moved,text+QPointF(delta.x(),0)).length()<.01,"Radius grip left its projected radial line");
                    require(QLineF(*viewer.dimension_handle_position(candidate,1),rim).length()<.01,
                            "Dragging shortened radius text moved measured rim");
                    mouse(&viewer,QEvent::MouseButtonRelease,text+delta,Qt::LeftButton,Qt::NoButton);
                }
                viewer.grab().save("build/radius-free-label-view.png");
                kernel::cycle_dimension_presentation(persisted,kind);
                require(!persisted.arrows_reversed&&!persisted.radius_center_line_hidden,"Radius cycle did not return to full line");
            }
        }
        for(const auto direction:{kernel::Vec3{0,-1,0},kernel::Vec3{0,0,1}}) {
            auto radius=source;radius.kind=kernel::ViewerDimensionKind::Radius;
            radius.witness_second={-8.55,0,5.18};radius.line_first={};radius.line_second=radius.witness_second;
            radius.plane_normal={0,-1,0};radius.label_position=kernel::Vec3{-20,0,12};
            persisted={};persisted.radius_center_line_hidden=true;persisted.arrows_reversed=true;
            mesh.dimensions={radius};viewer.set_mesh(mesh);viewer.set_view_direction(direction);
            for(auto* animation:viewer.findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
            viewer.fit_all();flush();viewer.confirm_reference("feature","parameter:length",{},viewer::CandidateKind::Dimension);
            const auto selected=*viewer.confirmed_candidate();const auto grip=*viewer.dimension_handle_position(selected,0);
            const auto rim=*viewer.dimension_handle_position(selected,1);
            mouse(&viewer,QEvent::MouseButtonPress,grip,Qt::LeftButton,Qt::LeftButton);
            mouse(&viewer,QEvent::MouseMove,grip+QPointF(-60,20),Qt::NoButton,Qt::LeftButton);
            const auto after=*viewer.dimension_handle_position(selected,0);
            require(QLineF(grip,after).length()>15,"XZ/edge-on radius grip cannot move");
            require(QLineF(rim,*viewer.dimension_handle_position(selected,1)).length()<.01,"XZ radius moved measured arrow");
            mouse(&viewer,QEvent::MouseButtonRelease,after,Qt::LeftButton,Qt::NoButton);
        }

        {
            auto radius=source;radius.kind=kernel::ViewerDimensionKind::Radius;
            radius.radius_center_line_hidden=true;radius.arrows_reversed=true;
            radius.witness_first={0,0,0};radius.witness_second={20,0,0};
            radius.line_first=radius.witness_first;radius.line_second=radius.witness_second;
            radius.plane_normal={0,0,1};
            for(bool oblique:{false,true})for(double x:{-30.,-1.,0.,1.,10.,19.,20.,21.,40.}) {
                radius.label_position=kernel::Vec3{x,3,0};
                const auto project=[&](kernel::Vec3 p){return oblique?QPointF(p.x*5+p.y*2,-p.y*4):QPointF(p.x*5,-p.y*5);};
                const auto shown=viewer::dimension_presentation(radius,project,40);
                require(shown.valid,"Shortened radius disappeared while crossing centre or rim");
                const auto radial=project(radius.witness_second)-project(radius.witness_first);
                const auto radial_leader=shown.curves[0].back()-shown.curves[0].front();
                near(radial.x()*radial_leader.y()-radial.y()*radial_leader.x(),0);
                // With this projected horizontal radius, moving x crosses both
                // the centre and rim without changing the selected plane.
                near(shown.handles[0].x(),project(*radius.label_position).x());
                require(shown.arrows.size()==1 && shown.arrows[0].first==project(radius.witness_second) &&
                        shown.curves[0].front()==shown.arrows[0].first,
                        "Moving radius text moved arrow or disconnected leader");
                require(shown.curves[0].back()==shown.curves[1].front() || shown.curves[0].back()==shown.curves[1].back(),
                        "Radius leader does not meet text support");
                const auto leader=shown.curves[0].back()-shown.curves[0].front();
                const auto arrow_direction=shown.arrows[0].second;
                near(leader.x()*arrow_direction.y()-leader.y()*arrow_direction.x(),0);
                near(QLineF(shown.curves[1].front(),shown.curves[1].back()).length(),40);
                if(oblique)near(shown.text_angle,0);
            }
        }
        {
            // A source label may carry an old offset normal to its circle.
            // Switching radius mode must never reveal that offset in the View.
            const auto frame=kernel::annotation_frame({7,11,13},{23,41,17});
            const auto center=frame.origin,normal=frame.axes[2];
            auto radius=source;radius.kind=kernel::ViewerDimensionKind::Radius;
            radius.witness_first=center;radius.witness_second=frame.world({20,0,0});
            radius.line_first=center;radius.line_second=radius.witness_second;radius.plane_normal=normal;
            kernel::DimensionLayout layout;
            for(int mode=0;mode<3;++mode) {
                for(double x:{-30.,0.,10.,20.,40.}) {
                    radius.label_position=frame.world({x,4,9});
                    auto shown=kernel::layout_dimension(radius,{},layout);
                    near(kernel::dimension_dot(kernel::dimension_sub(*shown.label_position,center),normal),0);
                    auto dragged=kernel::dragged_dimension_layout(shown,{},layout,0,-8,6);
                    auto moved=kernel::layout_dimension(radius,{},dragged);
                    near(kernel::dimension_dot(kernel::dimension_sub(*moved.label_position,center),normal),0);
                    const auto project=[](kernel::Vec3 p){return QPointF(p.x*5+p.z*2,-p.y*5+p.z);};
                    auto dirty=radius;dirty.radius_center_line_hidden=layout.radius_center_line_hidden;dirty.arrows_reversed=layout.arrows_reversed;
                    auto planar=dirty;planar.label_position=frame.world({x,4,0});
                    const auto actual=viewer::dimension_presentation(dirty,project,40),expected=viewer::dimension_presentation(planar,project,40);
                    require(actual.valid && expected.valid,"Rotated radius disappeared");
                    near(QLineF(actual.handles[0],expected.handles[0]).length(),0);
                    if(layout.radius_center_line_hidden) {
                        const auto radial=project(radius.witness_second)-project(center);
                        const auto leader=actual.curves[0].back()-actual.curves[0].front();
                        near(radial.x()*leader.y()-radial.y()*leader.x(),0);
                    }
                }
                kernel::cycle_dimension_presentation(layout,kernel::ViewerDimensionKind::Radius);
            }
        }
        {
            QImage proof(400,180,QImage::Format_ARGB32);proof.fill(Qt::white);
            QFont font("Arial");font.setPixelSize(24);
            std::vector<viewer::DimensionTextLabel> labels={
                {"R20.000 mm",{80,100},0,font,Qt::red},
                {"R20.000 mm",{80,100},0,font,Qt::blue}};
            QPainter painter(&proof);
            painter.setPen(QPen(Qt::green,1));
            for(int x=-180;x<400;x+=5)painter.drawLine(x,0,x+180,180);
            viewer::paint_dimension_text_layer(painter,labels,5,
                [](QPainter& p,const QPainterPath& mask){p.fillPath(mask,Qt::white);});
            painter.end();
            const auto box=viewer::dimension_text_box(font,labels[0].text,5).translated(labels[0].baseline);
            require(proof.pixelColor(QPoint(qRound(box.center().x()),qRound(box.top()+2)))==QColor(Qt::white),
                    "Hatching remained inside text clearance");
            int blue=0,red=0,green=0;
            for(int y=box.top()+1;y<box.bottom()-1;++y)for(int x=box.left()+1;x<box.right()-1;++x) {
                const auto c=proof.pixelColor(x,y);
                if(c.blue()>150 && c.red()<100)++blue;
                if(c.red()>150 && c.blue()<100)++red;
                if(c.green()>150 && c.red()<100 && c.blue()<100)++green;
            }
            require(blue>20 && red==0 && green==0,"Dimension text layer lost order or hatch masking");
            const auto tight=QFontMetricsF(font).tightBoundingRect(labels[0].text);
            near(box.top(),labels[0].baseline.y()+tight.top()-5);
            require(proof.save(QCoreApplication::applicationDirPath()+"/dimension-text-mask-proof.png"),"Cannot save text-mask proof");
        }

        {
            // Seven configurations from the user's koty.bmp: two full-radius
            // text positions in either arrow mode, three shortened positions.
            QImage proof(1380,870,QImage::Format_ARGB32);proof.fill(Qt::white);QPainter painter(&proof);painter.setRenderHint(QPainter::Antialiasing);
            kernel::ViewerDimension radius;radius.kind=kernel::ViewerDimensionKind::Radius;radius.value=20;
            radius.witness_first={};radius.witness_second={20,0,0};radius.line_second=radius.witness_second;radius.plane_normal={0,0,1};radius.label_position=kernel::Vec3{35,0,0};
            kernel::ModelEnvelope envelope;envelope.include({-30,-25,0});envelope.include({30,25,0});
            kernel::DimensionLayout attached;attached.envelope_offset=8;
            near(kernel::layout_dimension(radius,envelope,attached).label_position->x,38);
            for(int mode=0;mode<3;++mode)for(int row=0;row<(mode==2?3:2);++row){
                kernel::DimensionLayout placement;placement.radius_rotation_degrees=45;
                for(int i=0;i<mode;++i)kernel::cycle_dimension_presentation(placement,radius.kind);
                placement.text_along=(row==0?40.:row==1?-20.:8.)-35;
                auto shown=kernel::layout_dimension(radius,{},placement);
                const QPointF center(180+mode*460,220+row*(mode==2?265:410));
                const auto project=[&](kernel::Vec3 p){return center+QPointF(5*p.x,-5*p.y+2*p.z);};
                painter.setFont(QFont("Arial",15));const auto presentation=viewer::dimension_presentation(shown,project,QFontMetricsF(painter.font()).horizontalAdvance("R20mm"),11,5);
                require(presentation.valid,"A radius state from koty.bmp disappeared");
                require(presentation.handles[1]==project(shown.witness_second),"Arrow grip detached from radius");
                const auto ray=presentation.handles[1]-center;
                const auto leader=presentation.curves[mode==2?0:1].back()-presentation.curves[mode==2?0:1].front();
                near(ray.x()*leader.y()-ray.y()*leader.x(),0);
                if(mode<2)require(presentation.curves.front().front()==center&&presentation.curves.front().back()==presentation.handles[1],"Full radius lost centre-to-arc line");
                else require(presentation.curves.front().front()==presentation.handles[1],"Short radius leader lost arrow attachment");
                near(presentation.text_angle,0);
                for(int grip:{0,1}){
                    const auto moved=kernel::layout_dimension(radius,{},kernel::dragged_dimension_layout(shown,{},placement,grip,-4,5));
                    near(moved.value,20);near(kernel::dimension_dot(kernel::dimension_sub(*moved.label_position,moved.witness_first),moved.plane_normal),0);
                    if(grip==0)require(moved.witness_second==shown.witness_second,"Text point moved radius arrow");
                    else {require(moved.witness_second!=shown.witness_second,"Arrow point did not move around radius");near(std::sqrt(kernel::dimension_dot(moved.witness_second,moved.witness_second)),20);}
                }
                painter.setPen(QPen(Qt::black,2));painter.setBrush(Qt::NoBrush);painter.drawArc(QRectF(center.x()-100,center.y()-100,200,200),-10*16,115*16);painter.drawEllipse(center,4,4);
                painter.setPen(QPen(QColor("#9B7A00"),2));for(const auto& curve:presentation.curves)painter.drawPolyline(curve);
                painter.setBrush(QColor("#9B7A00"));for(const auto& [tip,direction]:presentation.arrows)painter.drawPolygon(viewer::annotation_arrow(tip,direction,11));
                painter.setFont(QFont("Arial",15));painter.drawText(presentation.text_baseline,"R20mm");
                painter.setPen(Qt::NoPen);painter.setBrush(QColor("#D05CFF"));painter.drawEllipse(presentation.handles[0],4,4);painter.drawEllipse(presentation.handles[1],4,4);
                painter.setPen(Qt::black);painter.setFont(QFont("Arial",12));painter.drawText(QPointF(mode*460+20,row*(mode==2?265:410)+35),QString("Rezim %1 / poloha %2").arg(mode).arg(row+1));
            }
            painter.end();require(proof.save("build/radius-seven-states-proof.png"),"Cannot save seven radius states");
        }
        viewer.set_context_menu_callback({});
        viewer.set_dimension_frame_visible(true);
        viewer.grab().save("build/dimension-layout-view.png");
        int dialog_commits = 0;
        app::DimensionPropertiesDialog dialog(
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
        app::DimensionPropertiesDialog confirm(
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
        app::DimensionPropertiesDialog angular_dialog(angle, {0, 8., 0, 0}, [](auto) {}, &owner);
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
