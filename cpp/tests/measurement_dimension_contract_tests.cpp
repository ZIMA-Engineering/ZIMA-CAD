#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <zima/document/dimension_layout_json.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/drawing/model_annotations.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/document/part_document.hpp>
using namespace zima;
using namespace zima::drawing;
namespace {
void require(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
void near(double a, double b) {
    if (std::abs(a - b) > 1e-6)
        throw std::runtime_error("Measurement mismatch: " + std::to_string(a) + " / " + std::to_string(b));
}
kernel::EdgeReference ref(std::string name) { return {"profile", std::move(name), {}}; }
kernel::ViewerEdge line(std::string name, kernel::Vec3 a, kernel::Vec3 b) { return {{a, b}, ref(name)}; }
kernel::ViewerEdge circle(std::string name, double x, double y, double radius,
                          double angle = 2 * std::numbers::pi) {
    kernel::ViewerEdge edge;
    edge.reference = ref(name);
    for (int i = 0; i <= 96; ++i) {
        const double t = angle * i / 96;
        edge.points.push_back({x + radius * std::cos(t), y + radius * std::sin(t), 0});
    }
    return edge;
}
DrawingView view(kernel::ViewerMesh mesh) {
    auto v = DrawingDocument::create_view("source", "source.prtz", mesh, ViewOrientation::Top);
    v.camera = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    v.projected_edges = project_edges(mesh, v.camera);
    return v;
}
} // namespace
int main() {
    try {
        {
            auto part=document::PartDocument::create_default();
            auto box=document::PartDocument::create_box_container();box.box={30,20,10};
            auto cut=document::PartDocument::create_box_container();cut.box={5,5,20};cut.combine_mode=document::CombineMode::Subtract;part.history={box,cut};
            kernel::OcctKernel kernel;auto body=kernel.evaluate_history(part.kernel_operations()).back();
            auto actual=DrawingDocument::create_view(part.document_id,{},body.mesh,ViewOrientation::Top);
            MeasurementPickRequest request;request.mode=int(DimensionAttachmentKind::Line);
            require(!measurement_candidates(actual,{-15,0},.5,request).empty(),
                "Calculated Part edge has no drawing dimension hover");
        }
        {
            auto axes = view({});
            ModelAnnotation first;
            first.kind=ModelAnnotationKind::Axis;first.visible=true;
            first.source={"part","hole","axis","first"};
            first.model_axis=std::array<kernel::Vec3,2>{{{10,10,-5},{10,10,5}}};
            first.model_envelope.include({5,5,-5});first.model_envelope.include({15,15,5});
            auto second=first;second.source.instance_path="second";
            second.model_axis=std::array<kernel::Vec3,2>{{{30,10,-5},{30,10,5}}};
            second.model_envelope={};second.model_envelope.include({25,5,-5});second.model_envelope.include({35,15,5});
            axes.model_annotations={first,second};
            MeasurementPickRequest request;
            auto picked=measurement_candidates(axes,{15,10},.6,request);
            require(!picked.empty()&&picked[0].attachment.kind==DimensionAttachmentKind::Center&&
                    picked[0].attachment.reference.instance_path=="first","End-on axis arm did not offer its centre");
            near(picked[0].position.x,10);
            request.mode=int(DimensionAttachmentKind::Center);
            auto other=measurement_candidates(axes,{30,10},.6,request);
            require(!other.empty()&&other[0].attachment.reference.instance_path=="second","Repeated axes share a reference");
            auto dimension=make_drawing_dimension(axes.id);
            dimension.attachments={picked[0].attachment,other[0].attachment};
            near(evaluate_drawing_dimension(axes,dimension).presentations[0].value,20);
            axes.model_annotations[0].visible=false;
            require(measurement_candidates(axes,{10,10},.6,request).empty(),"Erased axis is offered");
            near(evaluate_drawing_dimension(axes,dimension).presentations[0].value,20);
            axes.model_annotations=deserialize_model_annotations(serialize_model_annotations(axes.model_annotations));
            dimension=deserialize_drawing_dimensions(serialize_drawing_dimensions({dimension})).front();
            near(evaluate_drawing_dimension(axes,dimension).presentations[0].value,20);
            axes.model_annotations[0].visible=true;
            axes.camera={{1,0,0},{0,0,1},{0,-1,0}};
            request={};request.lines_only=true;
            picked=measurement_candidates(axes,{10,3},.6,request);
            require(!picked.empty()&&picked[0].attachment.kind==DimensionAttachmentKind::Line,"Side-on axis not offered as a line");
            require(resolve_dimension_attachment(axes,picked[0].attachment).has_value(),"Picked axis line does not resolve");
            axes.camera={{1,0,0},{0,1,0},{0,0,1}};
            require(measurement_candidates(axes,{10,10},.6,request).empty(),"End-on axis offered a nonexistent line");
            request={};request.circles_only=true;
            require(measurement_candidates(axes,{10,10},.6,request).empty(),"Axis offered as a measured circle");
            axes.model_annotations[0].unresolved=true;
            require(evaluate_drawing_dimension(axes,dimension).state==MeasurementState::Unresolved,"Lost axis silently rebound");
        }
        kernel::ViewerMesh mesh;
        mesh.edges = {line("bottom", {0, 0, 0}, {40, 0, 0}), line("top", {0, 20, 0}, {40, 20, 0}),
                      line("right", {40, 0, 0}, {40, 20, 0}), circle("hole", 20, 10, 5),
                      circle("neighbor", 28, 10, 5)};
        auto v = view(mesh);
        auto d = make_drawing_dimension(v.id);
        d.attachments = {{DimensionAttachmentKind::Line, ref("bottom"), {}, .5},
                         {DimensionAttachmentKind::Line, ref("top"), {}, .5}};
        auto evaluated = evaluate_drawing_dimension(v, d);
        require(evaluated.state == MeasurementState::Resolved, "Parallel line measurement missing");
        near(evaluated.presentations[0].value, 20);
        d.attachments[1].reference = ref("right");
        require(evaluate_drawing_dimension(v, d).state == MeasurementState::Unresolved,
                "Nonparallel lines accepted");
        d.attachments[1] = {DimensionAttachmentKind::Center, ref("hole")};
        near(evaluate_drawing_dimension(v, d).presentations[0].value, 10);
        d.attachments[1] = {DimensionAttachmentKind::Tangent, ref("hole"), {}, 0, 1};
        near(evaluate_drawing_dimension(v, d).presentations[0].value, 15);
        d.attachments[1].side = -1;
        near(evaluate_drawing_dimension(v, d).presentations[0].value, 5);
        d.direction = DimensionDirection::Parallel;
        d.parallel_reference = ref("right");
        near(evaluate_drawing_dimension(v, d).presentations[0].value, 5);
        d.parallel_reference = ref("missing");
        require(!evaluate_drawing_dimension(v, d).direction_resolved, "Lost direction was silently replaced");
        d.direction = DimensionDirection::Horizontal;
        d.attachments = {{DimensionAttachmentKind::CurvePoint, ref("bottom"), {}, 0},
                         {DimensionAttachmentKind::CurvePoint, ref("bottom"), {}, 1}};
        near(evaluate_drawing_dimension(v, d).presentations[0].value, 40);
        const double angle = std::numbers::pi / 3;
        v.camera = {{std::cos(angle), 0, std::sin(angle)}, {0, 1, 0}, {-std::sin(angle), 0, std::cos(angle)}};
        near(evaluate_drawing_dimension(v, d).presentations[0].value, 20); // 40*cos(60°), not true 3D length.
        v = view(mesh);
        d.view_id = v.id;
        d.style.prefix = "2×";
        d.style.suffix = " mm";
        d.style.decimals = 1;
        d.style.tolerance_mode = "symmetric";
        d.style.symmetric_tolerance = "0.2";
        require(drawing_dimension_text(d, evaluate_drawing_dimension(v, d).presentations[0]) ==
                    "2×40mm ±0,2",
                "Text/tolerance format diverged");
        place_drawing_dimension(v, d, 0, {20, 30});
        refresh_drawing_dimension(v, d);
        const auto original = evaluate_drawing_dimension(v, d).presentations[0];
        const auto segment = d.segments[0];
        extend_dimension_chain(d, true, {DimensionAttachmentKind::CurvePoint, ref("top"), {}, 0});
        require(d.anchor_attachment == 1 && d.segments[1] == segment,
                "Prepending replaced original segment/layout");
        auto chain = evaluate_drawing_dimension(v, d);
        near(chain.presentations[1].line_first.y, original.line_first.y);
        near(chain.presentations[1].value, 40);
        extend_dimension_chain(d, false, {DimensionAttachmentKind::Center, ref("neighbor")});
        chain = evaluate_drawing_dimension(v, d);
        near(chain.presentations[2].value, 12);
        refresh_drawing_dimension(v, d);
        const auto before_missing = d;
        const auto intact = v.measurement_geometry;
        auto damaged_geometry=*intact;
        damaged_geometry.curves.pop_back();
        v.measurement_geometry=share_measurement_geometry(std::move(damaged_geometry));
        const auto missing = evaluate_drawing_dimension(v, d);
        require(missing.state == MeasurementState::Unresolved && !missing.resolved_attachments.back(),
                "Broken reference not marked");
        require(drawing_dimension_text(d, missing.presentations.back(), true) == "?",
                "Broken dimension displayed stale numeric value");
        v.measurement_geometry=intact;
        require(evaluate_drawing_dimension(v, d).state == MeasurementState::Resolved && d == before_missing,
                "Restored binding lost presentation");
        const auto serialized = serialize_drawing_dimensions({d});
        require(deserialize_drawing_dimensions(serialized) == std::vector{d},
                "Dimension persistence lost references/style/layout");
        auto duplicate = nlohmann::json::parse(serialized);
        duplicate.push_back(duplicate[0]);
        bool rejected = false;
        try {
            deserialize_drawing_dimensions(duplicate.dump());
        } catch (...) {
            rejected = true;
        }
        require(rejected, "Duplicate dimension identity accepted");
        const auto geometry = serialize_measurement_geometry(v);
        auto copy = v;
        copy.measurement_geometry=share_measurement_geometry({});
        deserialize_measurement_geometry(copy, geometry);
        require(copy.measurement_geometry->curves == v.measurement_geometry->curves,
                "Measurement source geometry changed on disk");

        // Both branches and exact tangency are deterministic, including intersections
        // beyond the original finite line segment.
        auto curves = projected_measurement_curves(v);
        auto intersections = dimension_intersections(curves[3], curves[4]);
        require(intersections.size() == 2, "Two circle intersections missing");
        for (auto [p, t] : intersections) {
            near(p.x, 24);
            near(std::abs(p.y - 10), 3);
        }
        auto touch_mesh = mesh;
        touch_mesh.edges[4] = circle("neighbor", 30, 10, 5);
        auto touching = view(touch_mesh);
        auto touch_curves = projected_measurement_curves(touching);
        require(dimension_intersections(touch_curves[3], touch_curves[4]).size() == 1,
                "Tangent circles lost intersection");
        touching.camera = {{.5, 0, std::sqrt(.75)}, {0, 1, 0}, {-std::sqrt(.75), 0, .5}};
        touch_curves = projected_measurement_curves(touching);
        auto ellipse_contact = dimension_intersections(touch_curves[3], touch_curves[4]);
        require(ellipse_contact.size() == 1, "Tangent projected ellipses lost intersection");
        near(ellipse_contact[0].first.x, 12.5);
        touching.camera = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        touch_mesh.edges.push_back(line("short", {-5, 10, 0}, {-4, 10, 0}));
        touching = view(touch_mesh);
        touch_curves = projected_measurement_curves(touching);
        intersections = dimension_intersections(touch_curves.back(), touch_curves[3]);
        require(intersections.size() == 2 && intersections[0].second != intersections[1].second,
                "Extrapolated intersection branches collapsed");
        for (auto [point, parameter] : intersections) {
            DimensionAttachment a{DimensionAttachmentKind::Intersection, ref("short"), ref("hole"),
                                  parameter};
            const auto resolved = resolve_dimension_attachment(touching, a);
            require(resolved.has_value(), "Stored intersection missing");
            near(resolved->x, point.x);
        }
        {
            ProjectedMeasurementCurve first, second;
            first.source = ref("ellipse-a");
            second.source = ref("ellipse-b");
            first.center = second.center = Point2{};
            first.cosine_axis = {5, 0};
            first.sine_axis = {0, 2};
            second.cosine_axis = {2, 0};
            second.sine_axis = {0, 5};
            for (int i = 0; i <= 96; ++i) {
                const double t = 2 * std::numbers::pi * i / 96;
                first.points.push_back({5 * std::cos(t), 2 * std::sin(t)});
                second.points.push_back({2 * std::cos(t), 5 * std::sin(t)});
            }
            const auto crossings = dimension_intersections(first, second);
            require(crossings.size() == 4, "Quartic conic intersection lost a branch");
            for (auto [p, t] : crossings) {
                near(std::abs(p.x), 10 / std::sqrt(29.));
                near(std::abs(p.x), std::abs(p.y));
            }
            second = first;
            second.source = ref("coincident-ellipse");
            require(dimension_intersections(first, second).empty(),
                    "Coincident curves offered arbitrary intersection");
        }
        MeasurementPickRequest request;
        request.mode = int(DimensionAttachmentKind::Center);
        auto offered = measurement_candidates(v, {25, 10}, .6, request);
        require(!offered.empty() && offered[0].attachment.reference == ref("hole"),
                "Centre not offered on circular outline");
        near(offered[0].position.x, 20);
        request = {};
        request.parallel_line = ref("bottom");
        offered = measurement_candidates(v, {40, 10}, .6, request);
        require(std::ranges::none_of(offered,
                                     [](const auto &p) {
                                         return p.attachment.kind == DimensionAttachmentKind::Line &&
                                                p.attachment.reference == ref("right");
                                     }),
                "Picker offered nonparallel second line");
        request = {};
        offered = measurement_candidates(v, {0, 0}, .6, request);
        require(!offered.empty() && offered[0].attachment.kind == DimensionAttachmentKind::CurvePoint,
                "Endpoint did not take precedence over whole line");
        request = {};
        request.mode = int(DimensionAttachmentKind::Intersection);
        request.intersection_first = ref("hole");
        offered = measurement_candidates(v, {24, 13}, .6, request);
        require(!offered.empty() && offered[0].attachment.other_reference.valid(),
                "Intersection picker discarded its second reference");

        for (auto kind : {DrawingDimensionKind::Radius, DrawingDimensionKind::Diameter}) {
            auto radius = make_drawing_dimension(v.id, kind);
            radius.attachments = {{DimensionAttachmentKind::CurvePoint, ref("hole"), {}, .125}};
            auto value = evaluate_drawing_dimension(v, radius);
            near(value.presentations[0].value, kind == DrawingDimensionKind::Radius ? 5 : 10);
            const auto original_radius = radius;
            v.camera = {{1, 0, 0}, {0, .5, std::sqrt(.75)}, {0, -std::sqrt(.75), .5}};
            require(evaluate_drawing_dimension(v, radius).state == MeasurementState::Hidden,
                    "R/diameter shown on ellipse");
            v.camera = {{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}};
            require(evaluate_drawing_dimension(v, radius).state == MeasurementState::Resolved &&
                        radius == original_radius,
                    "In-plane view rotation hid or mutated radius");
            v.camera = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            for (int mode = 0; mode < 3; ++mode) {
                auto source = evaluate_drawing_dimension(v, radius).presentations[0];
                const auto rim = source.witness_second;
                drag_drawing_dimension(v, radius, 0, 0, {-12, 3});
                source = evaluate_drawing_dimension(v, radius).presentations[0];
                require(source.witness_second == rim, "Text grip moved measured radius arrow");
                near(source.label_position->z, 0);
                drag_drawing_dimension(v, radius, 0, 1, {-1, 2});
                source = evaluate_drawing_dimension(v, radius).presentations[0];
                require(source.witness_second != rim, "Arrow grip cannot move around radius");
                near(std::hypot(source.witness_second.x - 20, source.witness_second.y - 10), 5);
                near(source.value, kind == DrawingDimensionKind::Radius ? 5 : 10);
                kernel::cycle_dimension_presentation(radius.segments[0].layout, source.kind);
            }
        }
        std::cout << "Projected measurements, C/T/intersections, chain ends, lost bindings, persistence, "
                     "candidates and radius grips passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
