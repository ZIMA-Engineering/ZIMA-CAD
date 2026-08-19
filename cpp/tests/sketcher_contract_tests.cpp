#include <zima/sketcher/sketch.hpp>
#include <zima/viewer/picking.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        auto sketch = zima::sketcher::Sketch::create_default();
        auto first = zima::sketcher::Sketch::create_point(0.0, 0.0);
        auto second = zima::sketcher::Sketch::create_point(13.0, 4.0);
        first.fixed = true;
        const auto first_id = first.id;
        const auto second_id = second.id;
        sketch.points.push_back(first);
        sketch.points.push_back(second);
        auto initial_segment = zima::sketcher::Sketch::create_segment(first_id, second_id);
        const auto initial_segment_id = initial_segment.id;
        sketch.segments.push_back(std::move(initial_segment));
        sketch.constraints.push_back({"horizontal", zima::sketcher::ConstraintKind::Horizontal,
            first_id, second_id, false, initial_segment_id});
        zima::sketcher::SketchDimension initial_dimension{
            "length", zima::sketcher::DimensionKind::DistanceX,
            first_id, second_id, 20.0, true, false, 10.0, 30.0};
        initial_dimension.geometry_id = initial_segment_id;
        sketch.dimensions.push_back(std::move(initial_dimension));
        const auto solved = sketch.solve();
        require(solved.status == zima::sketcher::SolveStatus::Solved &&
                    std::abs(sketch.points.back().x - 20.0) < 1.0e-7 &&
                    std::abs(sketch.points.back().y) < 1.0e-7,
                "Horizontal dimension did not solve deterministically");
        require(!sketch.set_dimension_value("length", 35.0) &&
                    sketch.set_dimension_value("length", 25.0),
                "Absolute dimension limits did not reject an out-of-range edit");
        require(sketch.solve().status == zima::sketcher::SolveStatus::Solved &&
                    std::abs(sketch.points.back().x - 25.0) < 1.0e-7,
                "Edited dimension did not drive sketch geometry");
        const auto path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-sketch-contract.zcs.json";
        sketch.save(path);
        const auto loaded = zima::sketcher::Sketch::load(path);
        std::filesystem::remove(path);
        require(loaded.id == sketch.id && loaded.points == sketch.points &&
                    loaded.segments == sketch.segments &&
                    loaded.constraints == sketch.constraints &&
                    loaded.dimensions == sketch.dimensions,
                "Sketch did not round-trip its stable graph and limits");
        const auto mesh = sketch.viewer_mesh();
        require(mesh.triangles.empty() && mesh.edges.size() == 1 &&
                    mesh.points.size() == 2 && mesh.axes.size() == 2 &&
                    mesh.dimensions.size() == 1 &&
                    mesh.edges.front().reference.owner_id == sketch.id &&
                    mesh.edges.front().reference.semantic_key.rfind("segment:", 0) == 0 &&
                    mesh.edges.front().overlay,
                "Sketch viewer packet lost stable point/segment ownership");
        const auto candidates = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                mesh, {12.5, 0.0, 10.0}, {0.0, 0.0, -1.0}, 0.2),
            {zima::viewer::CandidateKind::SketchSegment});
        require(candidates.size() == 1 && candidates.front().owner_id == sketch.id,
                "Sketch edge did not use the common viewer candidate list");
        const auto axis_candidates = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                mesh, {12.5, 0.0, 10.0}, {0.0, 0.0, -1.0}, 0.2),
            {zima::viewer::CandidateKind::SketchAxis});
        require(axis_candidates.size() == 1 &&
                    axis_candidates.front().semantic_key == "sketch_axis:x",
                "Sketch base axis did not use its dedicated selection contract");
        const auto dimension_candidates = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                mesh, {12.5, 5.0, 10.0}, {0.0, 0.0, -1.0}, 0.25),
            {zima::viewer::CandidateKind::Dimension});
        require(dimension_candidates.size() == 1 &&
                    dimension_candidates.front().semantic_key == "dimension:length",
                "Sketch dimension did not join the common viewer candidate list");
        const auto xy_hit = sketch.intersect_ray({4.0, 7.0, 10.0}, {0.0, 0.0, -1.0});
        require(xy_hit && std::abs((*xy_hit)[0] - 4.0) < 1.0e-9 &&
                    std::abs((*xy_hit)[1] - 7.0) < 1.0e-9,
                "XY viewer ray did not project into Sketch coordinates");
        sketch.plane = zima::sketcher::SketchPlane::XZ;
        sketch.plane_offset = 3.0;
        const auto xz_hit = sketch.intersect_ray({4.0, 20.0, 7.0}, {0.0, -1.0, 0.0});
        require(xz_hit && std::abs((*xz_hit)[0] - 4.0) < 1.0e-9 &&
                    std::abs((*xz_hit)[1] - 7.0) < 1.0e-9,
                "XZ viewer ray did not project into Sketch coordinates");
        sketch.plane = zima::sketcher::SketchPlane::YZ;
        sketch.plane_offset = -2.0;
        const auto yz_hit = sketch.intersect_ray({10.0, 4.0, 7.0}, {-1.0, 0.0, 0.0});
        require(yz_hit && std::abs((*yz_hit)[0] - 4.0) < 1.0e-9 &&
                    std::abs((*yz_hit)[1] - 7.0) < 1.0e-9 &&
                    !sketch.intersect_ray({10.0, 4.0, 7.0}, {0.0, 1.0, 0.0}),
                "YZ viewer ray projection or parallel-ray rejection is invalid");
        auto conflict = sketch;
        conflict.points.back().fixed = true;
        conflict.points.back().x = 5.0;
        const auto conflict_before = conflict.points;
        require(conflict.solve().status == zima::sketcher::SolveStatus::Conflicting &&
                    conflict.points == conflict_before,
                "Immovable conflicting sketch was accepted or partially moved");
        auto under_constrained = zima::sketcher::Sketch::create_default();
        under_constrained.points.push_back(
            zima::sketcher::Sketch::create_point(1.0, 2.0));
        const auto dof = under_constrained.solve();
        require(dof.status == zima::sketcher::SolveStatus::UnderConstrained &&
                    dof.remaining_degrees_of_freedom == 2,
                "Sketch Jacobian did not report two free point coordinates");
        const auto free_point_id = under_constrained.points.front().id;
        under_constrained.set_point_fixed(free_point_id, true);
        require(under_constrained.points.front().fixed &&
                    under_constrained.solve().status == zima::sketcher::SolveStatus::Solved &&
                    under_constrained.solve().remaining_degrees_of_freedom == 0,
                "Fixed point did not remove both coordinate degrees of freedom");
        under_constrained.set_point_fixed(free_point_id, false);
        require(!under_constrained.points.front().fixed &&
                    under_constrained.solve().remaining_degrees_of_freedom == 2,
                "Released point did not restore both coordinate degrees of freedom");
        auto point_tools = zima::sketcher::Sketch::create_default();
        const auto standalone_point = point_tools.add_point(100.0, 100.0);
        const auto snapped_point = point_tools.add_point(100.0 + 1.0e-8, 100.0);
        require(snapped_point == standalone_point && point_tools.points.size() == 1,
                "Standalone point creation did not use the common snap tolerance");
        const auto construction_segment = point_tools.add_segment(
            0.0, 0.0, 10.0, 0.0, 1.0e-6, true);
        const auto removable_segment = point_tools.add_segment(
            20.0, 0.0, 30.0, 0.0);
        static_cast<void>(point_tools.add_segment_constraint(
            construction_segment, zima::sketcher::ConstraintKind::Horizontal));
        auto construction_dimension =
            point_tools.create_segment_dimension(construction_segment);
        point_tools.apply_dimension(std::move(construction_dimension));
        point_tools.remove_geometry(removable_segment);
        require(point_tools.find_point(standalone_point) != nullptr &&
                    point_tools.points.size() == 3 &&
                    point_tools.segments.size() == 1 &&
                    point_tools.segments.front().construction,
                "Deleting unrelated geometry removed a standalone point or construction flag");
        const auto loaded_point_tools = zima::sketcher::Sketch::from_serialized(
            point_tools.serialized());
        require(loaded_point_tools.find_point(standalone_point) != nullptr &&
                    loaded_point_tools.segments.size() == 1 &&
                    loaded_point_tools.segments.front().construction &&
                    loaded_point_tools.viewer_mesh().edges.front().construction &&
                    loaded_point_tools.viewer_mesh().edges.front().overlay,
                "Standalone point or construction segment did not survive serialization");
        point_tools.remove_point(point_tools.segments.front().first_point_id);
        require(point_tools.find_point(standalone_point) != nullptr &&
                    point_tools.points.size() == 1 && point_tools.segments.empty() &&
                    point_tools.constraints.empty() && point_tools.dimensions.empty(),
                "Deleting a control point did not remove only its attached geometry");
        auto projected = zima::sketcher::Sketch::create_default();
        const auto projected_segment = projected.add_segment(0.0, 0.0, 5.0, 7.0);
        auto projected_x = projected.create_segment_dimension(
            projected_segment, zima::sketcher::DimensionKind::DistanceX);
        projected_x.value = 8.0;
        projected.apply_dimension(projected_x);
        auto projected_y = projected.create_segment_dimension(
            projected_segment, zima::sketcher::DimensionKind::DistanceY);
        projected_y.value = 9.0;
        projected.apply_dimension(projected_y);
        const auto* projected_first = projected.find_point(
            projected.segments.front().first_point_id);
        const auto* projected_second = projected.find_point(
            projected.segments.front().second_point_id);
        require(std::abs(projected_second->x - projected_first->x - 8.0) < 1.0e-7 &&
                    std::abs(projected_second->y - projected_first->y - 9.0) < 1.0e-7,
                "Projected X/Y dimensions did not independently drive the segment");
        const auto projected_mesh = projected.viewer_mesh();
        require(projected_mesh.dimensions.size() == 2 &&
                    projected_mesh.dimensions[0].label_prefix == "X " &&
                    projected_mesh.dimensions[1].label_prefix == "Y " &&
                    std::abs(projected_mesh.dimensions[0].line_first.y -
                        projected_mesh.dimensions[0].line_second.y) < 1.0e-9 &&
                    std::abs(projected_mesh.dimensions[1].line_first.x -
                        projected_mesh.dimensions[1].line_second.x) < 1.0e-9,
                "Projected dimensions did not create axis-aligned viewer geometry");
        auto angled = zima::sketcher::Sketch::create_default();
        const auto angled_segment = angled.add_segment(0.0, 0.0, 10.0, 0.0);
        auto angle_dimension = angled.create_segment_dimension(
            angled_segment, zima::sketcher::DimensionKind::Angle);
        angle_dimension.lower_limit = -45.0;
        angle_dimension.upper_limit = 45.0;
        angle_dimension.value = 30.0;
        angled.apply_dimension(angle_dimension);
        const auto* angle_first = angled.find_point(
            angled.segments.front().first_point_id);
        const auto* angle_second = angled.find_point(
            angled.segments.front().second_point_id);
        require(std::abs(std::atan2(angle_second->y - angle_first->y,
                                    angle_second->x - angle_first->x) *
                         180.0 / 3.14159265358979323846 - 30.0) < 1.0e-7 &&
                    std::abs(std::hypot(angle_second->x - angle_first->x,
                                        angle_second->y - angle_first->y) - 10.0) < 1.0e-7,
                "Angle dimension did not rotate the segment while preserving length");
        const auto angle_mesh = angled.viewer_mesh();
        require(angle_mesh.dimensions.size() == 1 &&
                    angle_mesh.dimensions.front().label_prefix == "∠ " &&
                    angle_mesh.dimensions.front().unit_suffix == "°",
                "Angle dimension did not produce degree viewer data");
        const auto angled_before_limit = angled;
        auto invalid_angle = angled.dimensions.front();
        invalid_angle.value = 60.0;
        bool angle_limit_rejected = false;
        try {
            angled.apply_dimension(invalid_angle);
        } catch (const std::runtime_error&) {
            angle_limit_rejected = true;
        }
        require(angle_limit_rejected && angled.points == angled_before_limit.points &&
                    angled.dimensions == angled_before_limit.dimensions,
                "Out-of-range angle partially changed the Sketch");
        const auto loaded_angle = zima::sketcher::Sketch::from_serialized(
            angled.serialized());
        require(loaded_angle.dimensions == angled.dimensions,
                "Angle dimension did not survive serialization");
        auto dragged = zima::sketcher::Sketch::create_default();
        const auto dragged_segment = dragged.add_segment(0.0, 0.0, 10.0, 0.0);
        auto measured = dragged.create_segment_dimension(dragged_segment);
        measured.driving = false;
        measured.lower_limit = 5.0;
        measured.upper_limit = 15.0;
        dragged.apply_dimension(measured);
        const auto dragged_point_id = dragged.segments.front().second_point_id;
        require(dragged.move_point(dragged_point_id, 12.0, 0.0) &&
                    std::abs(dragged.dimensions.front().value - 12.0) < 1.0e-9,
                "Dragging did not update a non-driving measured dimension");
        const auto dragged_before_limit = dragged;
        require(!dragged.move_point(dragged_point_id, 20.0, 0.0) &&
                    dragged.points == dragged_before_limit.points &&
                    dragged.dimensions == dragged_before_limit.dimensions,
                "Drag outside absolute dimension limits partially changed the Sketch");
        dragged.set_point_fixed(dragged_point_id, true);
        const auto dragged_before_fixed_move = dragged;
        require(!dragged.move_point(dragged_point_id, 8.0, 0.0) &&
                    dragged.points == dragged_before_fixed_move.points,
                "Fixed Sketch point accepted a drag move");
        auto connected = zima::sketcher::Sketch::create_default();
        static_cast<void>(connected.add_segment(0.0, 0.0, 10.0, 0.0));
        static_cast<void>(connected.add_segment(10.0 + 1.0e-8, 0.0, 10.0, 10.0));
        require(connected.points.size() == 3 && connected.segments.size() == 2 &&
                    connected.segments[0].second_point_id ==
                        connected.segments[1].first_point_id,
                "Connected segment creation did not reuse the snapped endpoint");
        static_cast<void>(connected.add_segment_constraint(
            connected.segments.front().id, zima::sketcher::ConstraintKind::Horizontal));
        require(std::abs(connected.points[0].y - connected.points[1].y) < 1.0e-8,
                "Horizontal segment constraint did not solve through the model API");
        bool duplicate_constraint_rejected = false;
        try {
            static_cast<void>(connected.add_segment_constraint(
                connected.segments.front().id,
                zima::sketcher::ConstraintKind::Horizontal));
        } catch (const std::invalid_argument&) {
            duplicate_constraint_rejected = true;
        }
        require(duplicate_constraint_rejected,
                "Duplicate segment constraint was accepted");
        auto coincident = zima::sketcher::Sketch::create_default();
        auto coincident_first = zima::sketcher::Sketch::create_point(0.0, 0.0);
        auto coincident_second = zima::sketcher::Sketch::create_point(8.0, 6.0);
        const auto coincident_first_id = coincident_first.id;
        const auto coincident_second_id = coincident_second.id;
        coincident.points.push_back(std::move(coincident_first));
        coincident.points.push_back(std::move(coincident_second));
        static_cast<void>(coincident.add_coincident_constraint(
            coincident_first_id, coincident_second_id));
        require(coincident.constraints.size() == 1 &&
                    coincident.constraints.front().kind ==
                        zima::sketcher::ConstraintKind::Coincident &&
                    std::hypot(coincident.points[0].x - coincident.points[1].x,
                               coincident.points[0].y - coincident.points[1].y) < 1.0e-8,
                "Coincident point constraint did not solve through the model API");
        const auto coincident_before_duplicate = coincident;
        bool duplicate_coincident_rejected = false;
        try {
            static_cast<void>(coincident.add_coincident_constraint(
                coincident_second_id, coincident_first_id));
        } catch (const std::invalid_argument&) {
            duplicate_coincident_rejected = true;
        }
        require(duplicate_coincident_rejected &&
                    coincident.constraints == coincident_before_duplicate.constraints,
                "Reversed duplicate coincident constraint was accepted");
        auto parallel = zima::sketcher::Sketch::create_default();
        const auto reference_segment = parallel.add_segment(0.0, 0.0, 10.0, 0.0);
        const auto driven_segment = parallel.add_segment(0.0, 5.0, 3.0, 9.0);
        const double driven_length_before = 5.0;
        static_cast<void>(parallel.add_segment_pair_constraint(
            reference_segment, driven_segment,
            zima::sketcher::ConstraintKind::Parallel));
        const auto* parallel_c = parallel.find_point(parallel.segments[1].first_point_id);
        const auto* parallel_d = parallel.find_point(parallel.segments[1].second_point_id);
        require(std::abs(parallel_d->y - parallel_c->y) < 1.0e-8 &&
                    std::abs(std::hypot(parallel_d->x - parallel_c->x,
                                        parallel_d->y - parallel_c->y) -
                             driven_length_before) < 1.0e-8,
                "Parallel constraint changed driven length or missed direction");
        bool reversed_parallel_rejected = false;
        try {
            static_cast<void>(parallel.add_segment_pair_constraint(
                driven_segment, reference_segment,
                zima::sketcher::ConstraintKind::Parallel));
        } catch (const std::invalid_argument&) {
            reversed_parallel_rejected = true;
        }
        require(reversed_parallel_rejected,
                "Reversed duplicate Parallel constraint was accepted");
        const auto loaded_parallel = zima::sketcher::Sketch::from_serialized(
            parallel.serialized());
        require(loaded_parallel.constraints == parallel.constraints,
                "Segment-pair owners did not survive serialization");
        parallel.remove_geometry(reference_segment);
        require(parallel.constraints.empty(),
                "Deleting one segment retained its pair constraint");
        auto perpendicular = zima::sketcher::Sketch::create_default();
        const auto perpendicular_reference =
            perpendicular.add_segment(0.0, 0.0, 10.0, 0.0);
        const auto perpendicular_driven =
            perpendicular.add_segment(4.0, 4.0, 9.0, 6.0);
        static_cast<void>(perpendicular.add_segment_pair_constraint(
            perpendicular_reference, perpendicular_driven,
            zima::sketcher::ConstraintKind::Perpendicular));
        const auto* perpendicular_c = perpendicular.find_point(
            perpendicular.segments[1].first_point_id);
        const auto* perpendicular_d = perpendicular.find_point(
            perpendicular.segments[1].second_point_id);
        require(std::abs(perpendicular_d->x - perpendicular_c->x) < 1.0e-8,
                "Perpendicular constraint did not rotate the driven segment");
        auto equal_length = zima::sketcher::Sketch::create_default();
        const auto length_reference = equal_length.add_segment(0.0, 0.0, 10.0, 0.0);
        const auto length_driven = equal_length.add_segment(0.0, 5.0, 3.0, 9.0);
        static_cast<void>(equal_length.add_segment_pair_constraint(
            length_reference, length_driven,
            zima::sketcher::ConstraintKind::EqualLength));
        const auto* equal_c = equal_length.find_point(
            equal_length.segments[1].first_point_id);
        const auto* equal_d = equal_length.find_point(
            equal_length.segments[1].second_point_id);
        const double equal_dx = equal_d->x - equal_c->x;
        const double equal_dy = equal_d->y - equal_c->y;
        require(std::abs(std::hypot(equal_dx, equal_dy) - 10.0) < 1.0e-8 &&
                    std::abs(equal_dx * 4.0 - equal_dy * 3.0) < 1.0e-8,
                "EqualLength changed driven direction or missed reference length");
        auto fixed_equal = zima::sketcher::Sketch::create_default();
        const auto fixed_reference = fixed_equal.add_segment(0.0, 0.0, 10.0, 0.0);
        const auto fixed_driven = fixed_equal.add_segment(0.0, 5.0, 3.0, 9.0);
        fixed_equal.find_point(fixed_equal.segments[1].first_point_id)->fixed = true;
        fixed_equal.find_point(fixed_equal.segments[1].second_point_id)->fixed = true;
        const auto fixed_equal_before = fixed_equal;
        bool fixed_equal_rejected = false;
        try {
            static_cast<void>(fixed_equal.add_segment_pair_constraint(
                fixed_reference, fixed_driven,
                zima::sketcher::ConstraintKind::EqualLength));
        } catch (const std::runtime_error&) {
            fixed_equal_rejected = true;
        }
        require(fixed_equal_rejected && fixed_equal.points == fixed_equal_before.points &&
                    fixed_equal.constraints == fixed_equal_before.constraints,
                "Conflicting fixed EqualLength relation partially changed the Sketch");
        const auto before_collapsing_constraint = connected;
        bool collapsing_constraint_rejected = false;
        try {
            static_cast<void>(connected.add_segment_constraint(
                connected.segments.front().id,
                zima::sketcher::ConstraintKind::Vertical));
        } catch (const std::runtime_error&) {
            collapsing_constraint_rejected = true;
        }
        require(collapsing_constraint_rejected &&
                    connected.points == before_collapsing_constraint.points &&
                    connected.constraints == before_collapsing_constraint.constraints,
                "Conflicting segment constraint collapsed or partially changed geometry");
        auto segment_dimension = connected.create_segment_dimension(
            connected.segments.front().id);
        segment_dimension.lower_limit = 5.0;
        segment_dimension.upper_limit = 15.0;
        const auto segment_dimension_id = segment_dimension.id;
        connected.apply_dimension(segment_dimension);
        require(connected.dimensions.size() == 1 &&
                    connected.dimensions.front().id == segment_dimension_id,
                "Segment dimension was not inserted with stable identity");
        segment_dimension.value = 12.0;
        connected.apply_dimension(segment_dimension);
        const auto* dimension_first = connected.find_point(
            connected.dimensions.front().first_point_id);
        const auto* dimension_second = connected.find_point(
            connected.dimensions.front().second_point_id);
        require(connected.dimensions.size() == 1 &&
                    std::abs(std::hypot(
                        dimension_second->x - dimension_first->x,
                        dimension_second->y - dimension_first->y) - 12.0) < 1.0e-7,
                "Editing a stable Sketch dimension did not drive segment length");
        const auto before_out_of_range = connected;
        segment_dimension.value = 20.0;
        bool out_of_range_rejected = false;
        try {
            connected.apply_dimension(segment_dimension);
        } catch (const std::runtime_error&) {
            out_of_range_rejected = true;
        }
        require(out_of_range_rejected && connected.points == before_out_of_range.points &&
                    connected.dimensions == before_out_of_range.dimensions,
                "Out-of-range dimension changed the Sketch transaction");
        auto rectangle = zima::sketcher::Sketch::create_default();
        const auto rectangle_ids = rectangle.add_rectangle(0.0, 0.0, 30.0, 20.0);
        require(rectangle_ids.size() == 4 && rectangle.points.size() == 4 &&
                    rectangle.segments.size() == 4 && rectangle.constraints.size() == 4 &&
                    rectangle.segments[0].second_point_id ==
                        rectangle.segments[1].first_point_id &&
                    rectangle.segments[3].second_point_id ==
                        rectangle.segments[0].first_point_id,
                "Rectangle did not create one closed constrained point graph");
        const auto rectangle_before_invalid = rectangle;
        bool flat_rectangle_rejected = false;
        try {
            static_cast<void>(rectangle.add_rectangle(0.0, 0.0, 10.0, 0.0));
        } catch (const std::invalid_argument&) {
            flat_rectangle_rejected = true;
        }
        require(flat_rectangle_rejected && rectangle.points == rectangle_before_invalid.points &&
                    rectangle.segments == rectangle_before_invalid.segments,
                "Degenerate rectangle partially changed the Sketch");
        auto polygon = zima::sketcher::Sketch::create_default();
        const auto polygon_result = polygon.add_regular_polygon(
            0.0, 0.0, 10.0, 0.0, 6);
        const auto point_on_circle_count = std::count_if(
            polygon.constraints.begin(), polygon.constraints.end(), [](const auto& value) {
                return value.kind == zima::sketcher::ConstraintKind::PointOnCircle;
            });
        const auto equal_length_count = std::count_if(
            polygon.constraints.begin(), polygon.constraints.end(), [](const auto& value) {
                return value.kind == zima::sketcher::ConstraintKind::EqualLength;
            });
        require(polygon_result.vertex_ids.size() == 6 &&
                    polygon_result.segment_ids.size() == 6 &&
                    polygon.points.size() == 7 && polygon.segments.size() == 6 &&
                    polygon.circles.size() == 1 &&
                    polygon.circles.front().construction &&
                    point_on_circle_count == 6 && equal_length_count == 5,
                "Regular polygon lost its support circle or parametric constraints");
        const auto polygon_center_id = polygon.circles.front().center_point_id;
        require(polygon.move_point(polygon_center_id, 5.0, -3.0),
                "Regular polygon centre could not be moved transactionally");
        auto polygon_radius = polygon.create_circle_radius_dimension(
            polygon_result.support_circle_id);
        polygon_radius.value = 20.0;
        polygon.apply_dimension(std::move(polygon_radius));
        const auto* polygon_center = polygon.find_point(polygon_center_id);
        for (std::size_t index = 0; index < polygon_result.vertex_ids.size(); ++index) {
            const auto* vertex = polygon.find_point(polygon_result.vertex_ids[index]);
            const auto* next_vertex = polygon.find_point(
                polygon_result.vertex_ids[(index + 1) % polygon_result.vertex_ids.size()]);
            require(std::abs(std::hypot(
                        vertex->x - polygon_center->x,
                        vertex->y - polygon_center->y) - 20.0) < 1.0e-7 &&
                        std::abs(std::hypot(
                            next_vertex->x - vertex->x,
                            next_vertex->y - vertex->y) - 20.0) < 1.0e-7,
                    "Regular hexagon did not regenerate from its support radius");
        }
        const auto loaded_polygon = zima::sketcher::Sketch::from_serialized(
            polygon.serialized());
        require(loaded_polygon.constraints == polygon.constraints &&
                    loaded_polygon.circles == polygon.circles &&
                    loaded_polygon.segments == polygon.segments,
                "Regular polygon constraints did not survive serialization");
        const auto polygon_before_invalid = polygon;
        bool invalid_polygon_rejected = false;
        try {
            static_cast<void>(polygon.add_regular_polygon(
                0.0, 0.0, 10.0, 0.0, 5));
        } catch (const std::invalid_argument&) {
            invalid_polygon_rejected = true;
        }
        require(invalid_polygon_rejected && polygon.points == polygon_before_invalid.points &&
                    polygon.constraints == polygon_before_invalid.constraints,
                "Unsupported regular polygon partially changed the Sketch");
        auto mirrored = zima::sketcher::Sketch::create_default();
        const auto mirror_axis = mirrored.add_segment(
            0.0, -20.0, 0.0, 20.0, 1.0e-6, true);
        const auto mirror_first = mirrored.add_segment(2.0, 1.0, 4.0, 1.0);
        const auto mirror_second = mirrored.add_segment(4.0, 1.0, 4.0, 3.0);
        static_cast<void>(mirrored.add_segment_constraint(
            mirror_first, zima::sketcher::ConstraintKind::Horizontal));
        static_cast<void>(mirrored.add_segment_constraint(
            mirror_second, zima::sketcher::ConstraintKind::Vertical));
        static_cast<void>(mirrored.add_segment_pair_constraint(
            mirror_first, mirror_second,
            zima::sketcher::ConstraintKind::EqualLength));
        const auto mirrored_pair = mirrored.mirror_geometry(
            {mirror_first, mirror_second}, mirror_axis);
        require(mirrored_pair.geometry_ids.size() == 2,
                "Sketch mirror did not return two new segment identities");
        const auto mirrored_first = std::find_if(
            mirrored.segments.begin(), mirrored.segments.end(), [&](const auto& value) {
                return value.id == mirrored_pair.geometry_ids[0];
            });
        const auto mirrored_second = std::find_if(
            mirrored.segments.begin(), mirrored.segments.end(), [&](const auto& value) {
                return value.id == mirrored_pair.geometry_ids[1];
            });
        require(mirrored_first != mirrored.segments.end() &&
                    mirrored_second != mirrored.segments.end(),
                "Sketch mirror did not return its new segment identities");
        const auto* mirrored_start = mirrored.find_point(
            mirrored_first->first_point_id);
        const auto* mirrored_corner = mirrored.find_point(
            mirrored_first->second_point_id);
        const auto* mirrored_end = mirrored.find_point(
            mirrored_second->second_point_id);
        require(mirrored_first->second_point_id == mirrored_second->first_point_id &&
                    std::abs(mirrored_start->x + 2.0) < 1.0e-9 &&
                    std::abs(mirrored_corner->x + 4.0) < 1.0e-9 &&
                    std::abs(mirrored_end->x + 4.0) < 1.0e-9 &&
                    mirrored.constraints.size() == 6,
                "Sketch mirror lost reflected coordinates, shared topology, or constraints");
        require(std::count_if(
                    mirrored.constraints.begin(), mirrored.constraints.end(),
                    [](const auto& constraint) {
                        return constraint.kind ==
                            zima::sketcher::ConstraintKind::Symmetric;
                    }) == 3 &&
                    std::none_of(
                        mirrored.constraints.begin(), mirrored.constraints.end(),
                        [&](const auto& constraint) {
                            return constraint.geometry_id == mirrored_first->id ||
                                constraint.geometry_id == mirrored_second->id ||
                                constraint.second_geometry_id == mirrored_first->id ||
                                constraint.second_geometry_id == mirrored_second->id;
                        }),
                "Sketch mirror copied source constraints instead of creating symmetry");
        const auto source_circle = mirrored.add_circle(3.0, 2.0, 4.0, true);
        const auto source_arc = mirrored.add_arc(
            3.0, 0.0, 5.0, 0.0, 3.0, 2.0);
        const auto source_ellipse = mirrored.add_ellipse(
            4.0, 0.0, 7.0, 0.0, 4.0, 2.0);
        const auto source_spline = mirrored.add_bspline(
            {{{2.0, 5.0}, {3.0, 6.0}, {4.0, 6.0}, {5.0, 5.0}}});
        const auto mirrored_curves = mirrored.mirror_geometry(
            {source_circle, source_arc, source_ellipse, source_spline}, mirror_axis);
        require(mirrored_curves.geometry_ids.size() == 4,
                "Sketch mirror did not return four new curve identities");
        const auto reflected_circle = std::find_if(
            mirrored.circles.begin(), mirrored.circles.end(), [&](const auto& value) {
                return value.id == mirrored_curves.geometry_ids[0];
            });
        const auto reflected_arc = std::find_if(
            mirrored.arcs.begin(), mirrored.arcs.end(), [&](const auto& value) {
                return value.id == mirrored_curves.geometry_ids[1];
            });
        const auto reflected_ellipse = std::find_if(
            mirrored.ellipses.begin(), mirrored.ellipses.end(), [&](const auto& value) {
                return value.id == mirrored_curves.geometry_ids[2];
            });
        const auto reflected_spline = std::find_if(
            mirrored.bsplines.begin(), mirrored.bsplines.end(), [&](const auto& value) {
                return value.id == mirrored_curves.geometry_ids[3];
            });
        require(reflected_circle != mirrored.circles.end() &&
                    reflected_arc != mirrored.arcs.end() &&
                    reflected_ellipse != mirrored.ellipses.end() &&
                    reflected_spline != mirrored.bsplines.end(),
                "Sketch mirror did not preserve the selected curve kinds");
        const auto* reflected_circle_center = mirrored.find_point(
            reflected_circle->center_point_id);
        const auto* reflected_arc_start = mirrored.find_point(
            reflected_arc->start_point_id);
        const auto* reflected_ellipse_center = mirrored.find_point(
            reflected_ellipse->center_point_id);
        const auto* reflected_spline_first = mirrored.find_point(
            reflected_spline->control_point_ids.front());
        const auto source_ellipse_value = std::find_if(
            mirrored.ellipses.begin(), mirrored.ellipses.end(), [&](const auto& value) {
                return value.id == source_ellipse;
            });
        require(reflected_circle->construction &&
                    std::abs(reflected_circle_center->x + 3.0) < 1.0e-9 &&
                    std::abs(reflected_arc_start->x + 3.0) < 1.0e-9 &&
                    std::abs(reflected_arc_start->y - 2.0) < 1.0e-9 &&
                    std::abs(reflected_ellipse_center->x + 4.0) < 1.0e-9 &&
                    std::abs(reflected_spline_first->x + 2.0) < 1.0e-9 &&
                    source_ellipse_value != mirrored.ellipses.end() &&
                    reflected_ellipse->reversed != source_ellipse_value->reversed,
                "Sketch mirror changed curve type, orientation, or construction state");
        const auto source_circle_value = std::find_if(
            mirrored.circles.begin(), mirrored.circles.end(), [&](const auto& value) {
                return value.id == source_circle;
            });
        const auto source_circle_center_id = source_circle_value->center_point_id;
        const auto reflected_circle_center_id = reflected_circle->center_point_id;
        const auto axis_value = std::find_if(
            mirrored.segments.begin(), mirrored.segments.end(), [&](const auto& value) {
                return value.id == mirror_axis;
            });
        const auto axis_first_id = axis_value->first_point_id;
        const auto axis_second_id = axis_value->second_point_id;
        require(mirrored.move_point(source_circle_center_id, 6.0, 4.0) &&
                    std::abs(mirrored.find_point(reflected_circle_center_id)->x + 6.0) <
                        1.0e-8 &&
                    std::abs(mirrored.find_point(reflected_circle_center_id)->y - 4.0) <
                        1.0e-8,
                "Moving mirror source did not solve its persisted symmetry constraint");
        require(mirrored.move_point(axis_first_id, 2.0, -20.0) &&
                    mirrored.move_point(axis_second_id, 2.0, 20.0) &&
                    std::abs(mirrored.find_point(reflected_circle_center_id)->x + 2.0) <
                        1.0e-8 &&
                    std::abs(mirrored.find_point(reflected_circle_center_id)->y - 4.0) <
                        1.0e-8,
                "Moving mirror axis did not regenerate its driven geometry");
        const auto mirror_source_point = mirrored.add_point(11.0, 13.0);
        const auto mirrored_point = mirrored.mirror_geometry(
            {mirror_source_point}, "sketch_axis:x");
        require(mirrored_point.geometry_ids.empty() &&
                    mirrored_point.point_ids.size() == 1 &&
                    std::abs(mirrored.find_point(mirrored_point.point_ids.front())->x -
                        11.0) < 1.0e-9 &&
                    std::abs(mirrored.find_point(mirrored_point.point_ids.front())->y +
                        13.0) < 1.0e-9 &&
                    mirrored.move_point(mirror_source_point, 12.0, 14.0) &&
                    std::abs(mirrored.find_point(mirrored_point.point_ids.front())->x -
                        12.0) < 1.0e-8 &&
                    std::abs(mirrored.find_point(mirrored_point.point_ids.front())->y +
                        14.0) < 1.0e-8,
                "Point mirror about a persisted Sketch axis is not associative");
        const auto loaded_mirror = zima::sketcher::Sketch::from_serialized(
            mirrored.serialized());
        require(loaded_mirror.constraints == mirrored.constraints &&
                    loaded_mirror.ellipses == mirrored.ellipses &&
                    loaded_mirror.viewer_mesh().axes.size() == 2,
                "Mirror symmetry or reflected ellipse orientation did not round-trip");
        const auto mirror_before_invalid = mirrored;
        bool mirror_axis_source_rejected = false;
        try {
            static_cast<void>(mirrored.mirror_geometry({mirror_axis}, mirror_axis));
        } catch (const std::invalid_argument&) {
            mirror_axis_source_rejected = true;
        }
        require(mirror_axis_source_rejected &&
                    mirrored.points == mirror_before_invalid.points &&
                    mirrored.segments == mirror_before_invalid.segments,
                "Invalid Sketch mirror partially changed its source graph");
        auto removable_rectangle = rectangle;
        auto removable_dimension = removable_rectangle.create_segment_dimension(
            removable_rectangle.segments.front().id);
        removable_rectangle.apply_dimension(removable_dimension);
        const auto removed_segment_id = removable_rectangle.segments.front().id;
        removable_rectangle.remove_geometry(removed_segment_id);
        require(removable_rectangle.segments.size() == 3 &&
                    removable_rectangle.constraints.size() == 3 &&
                    removable_rectangle.dimensions.empty() &&
                    removable_rectangle.points.size() == 4,
                "Segment deletion lost shared corners or retained owned dependencies");
        auto circle_sketch = zima::sketcher::Sketch::create_default();
        const auto circle_id = circle_sketch.add_circle(5.0, 7.0, 10.0);
        auto radius_dimension =
            circle_sketch.create_circle_radius_dimension(circle_id);
        radius_dimension.lower_limit = 2.0;
        radius_dimension.upper_limit = 20.0;
        radius_dimension.value = 15.0;
        circle_sketch.apply_dimension(radius_dimension);
        require(circle_sketch.circles.size() == 1 &&
                    std::abs(circle_sketch.circles.front().radius - 15.0) < 1.0e-9 &&
                    circle_sketch.dimensions.front().geometry_id == circle_id,
                "Radius dimension did not drive its stable circle");
        require(!circle_sketch.set_dimension_value(
                    circle_sketch.dimensions.front().id, -1.0),
                "Negative radius dimension was accepted by the data model");
        const auto circle_packet = circle_sketch.viewer_mesh();
        require(circle_packet.edges.size() == 1 &&
                    circle_packet.edges.front().points.size() == 97 &&
                    circle_packet.edges.front().reference.semantic_key ==
                        "circle:" + circle_id &&
                    circle_packet.dimensions.size() == 1 &&
                    circle_packet.dimensions.front().label_prefix == "R",
                "Circle or radius dimension did not produce stable viewer data");
        const auto loaded_circle = zima::sketcher::Sketch::from_serialized(
            circle_sketch.serialized());
        require(loaded_circle.circles == circle_sketch.circles &&
                    loaded_circle.dimensions == circle_sketch.dimensions,
                "Circle and radius dimension did not survive serialization");
        bool zero_circle_rejected = false;
        try {
            static_cast<void>(circle_sketch.add_circle(0.0, 0.0, 0.0));
        } catch (const std::invalid_argument&) {
            zero_circle_rejected = true;
        }
        require(zero_circle_rejected,
                "Zero-radius circle was accepted");
        auto diameter_sketch = zima::sketcher::Sketch::create_default();
        const auto diameter_circle_id = diameter_sketch.add_circle(0.0, 0.0, 10.0);
        auto diameter_dimension = diameter_sketch.create_circle_diameter_dimension(
            diameter_circle_id);
        diameter_dimension.lower_limit = 10.0;
        diameter_dimension.upper_limit = 40.0;
        diameter_dimension.value = 30.0;
        diameter_sketch.apply_dimension(diameter_dimension);
        require(std::abs(diameter_sketch.circles.front().radius - 15.0) < 1.0e-9 &&
                    diameter_sketch.dimensions.front().kind ==
                        zima::sketcher::DimensionKind::Diameter,
                "Diameter dimension did not drive half its value as circle radius");
        const auto diameter_packet = diameter_sketch.viewer_mesh();
        require(diameter_packet.dimensions.size() == 1 &&
                    diameter_packet.dimensions.front().label_prefix == "Ø" &&
                    std::abs(diameter_packet.dimensions.front().value - 30.0) < 1.0e-9,
                "Diameter dimension did not produce stable viewer data");
        bool second_radial_driver_rejected = false;
        try {
            diameter_sketch.apply_dimension(
                diameter_sketch.create_circle_radius_dimension(diameter_circle_id));
        } catch (const std::invalid_argument&) {
            second_radial_driver_rejected = true;
        }
        require(second_radial_driver_rejected,
                "Circle accepted simultaneous driving radius and diameter dimensions");
        const auto diameter_center_id = diameter_sketch.circles.front().center_point_id;
        require(diameter_sketch.move_point(diameter_center_id, 7.0, -3.0) &&
                    std::abs(diameter_sketch.circles.front().radius - 15.0) < 1.0e-9,
                "Moving a Circle center changed its radius");
        const auto loaded_diameter = zima::sketcher::Sketch::from_serialized(
            diameter_sketch.serialized());
        require(loaded_diameter.circles == diameter_sketch.circles &&
                    loaded_diameter.dimensions == diameter_sketch.dimensions,
                "Diameter dimension did not survive serialization");
        auto removable_circle = zima::sketcher::Sketch::create_default();
        const auto removable_circle_id = removable_circle.add_circle(2.0, 3.0, 5.0);
        removable_circle.apply_dimension(
            removable_circle.create_circle_radius_dimension(removable_circle_id));
        removable_circle.remove_geometry(removable_circle_id);
        require(removable_circle.circles.empty() && removable_circle.dimensions.empty() &&
                    removable_circle.points.empty(),
                "Circle deletion retained its owned dimension or orphan centre");
        auto shared_centre = zima::sketcher::Sketch::create_default();
        const auto shared_circle_id = shared_centre.add_circle(0.0, 0.0, 4.0);
        static_cast<void>(shared_centre.add_arc(0.0, 0.0, 4.0, 0.0, 0.0, 4.0));
        shared_centre.remove_geometry(shared_circle_id);
        require(shared_centre.circles.empty() && shared_centre.arcs.size() == 1 &&
                    shared_centre.points.size() == 3,
                "Deleting Circle removed a centre still shared by an Arc");
        auto arc_sketch = zima::sketcher::Sketch::create_default();
        const auto arc_id = arc_sketch.add_arc(0.0, 0.0, 10.0, 0.0, 0.0, 10.0);
        require(arc_sketch.arcs.size() == 1 &&
                    arc_sketch.points.size() == 3 &&
                    !arc_sketch.arcs.front().start_point_id.empty() &&
                    !arc_sketch.arcs.front().end_point_id.empty() &&
                    std::abs(arc_sketch.arcs.front().radius - 10.0) < 1.0e-9 &&
                    std::abs(arc_sketch.arcs.front().start_angle) < 1.0e-9 &&
                    std::abs(arc_sketch.arcs.front().end_angle -
                        3.14159265358979323846 / 2.0) < 1.0e-9,
                "Three-point arc parameters are incorrect");
        auto joined_arc_sketch = arc_sketch;
        const auto shared_arc_endpoint = joined_arc_sketch.arcs.front().end_point_id;
        static_cast<void>(joined_arc_sketch.add_segment(0.0, 10.0, -5.0, 10.0));
        require(joined_arc_sketch.segments.back().first_point_id == shared_arc_endpoint,
                "Segment did not reuse the Arc's stable endpoint reference");
        const auto arc_packet = arc_sketch.viewer_mesh();
        require(arc_packet.edges.size() == 1 &&
                    arc_packet.edges.front().points.size() == 25 &&
                    arc_packet.edges.front().reference.semantic_key == "arc:" + arc_id,
                "Arc did not produce an adaptive stable viewer curve");
        auto arc_radius = arc_sketch.create_arc_radius_dimension(arc_id);
        arc_radius.lower_limit = 5.0;
        arc_radius.upper_limit = 25.0;
        arc_radius.value = 18.0;
        arc_sketch.apply_dimension(arc_radius);
        require(std::abs(arc_sketch.arcs.front().radius - 18.0) < 1.0e-9 &&
                    arc_sketch.dimensions.front().geometry_id == arc_id,
                "Radius dimension did not drive its stable arc");
        const auto dimensioned_arc_packet = arc_sketch.viewer_mesh();
        require(dimensioned_arc_packet.dimensions.size() == 1 &&
                    dimensioned_arc_packet.dimensions.front().label_prefix == "R" &&
                    std::abs(dimensioned_arc_packet.dimensions.front().value - 18.0) < 1.0e-9,
                "Arc radius dimension did not produce stable viewer data");
        auto moved_arc = zima::sketcher::Sketch::create_default();
        static_cast<void>(moved_arc.add_arc(
            0.0, 0.0, 10.0, 0.0, 0.0, 10.0));
        const auto moved_arc_center = moved_arc.arcs.front().center_point_id;
        const auto moved_arc_start = moved_arc.arcs.front().start_point_id;
        const auto moved_arc_end = moved_arc.arcs.front().end_point_id;
        require(moved_arc.move_point(moved_arc_center, 5.0, 3.0) &&
                    std::abs(moved_arc.find_point(moved_arc_start)->x - 15.0) < 1.0e-9 &&
                    std::abs(moved_arc.find_point(moved_arc_end)->y - 13.0) < 1.0e-9 &&
                    std::abs(moved_arc.arcs.front().radius - 10.0) < 1.0e-9,
                "Moving an Arc center changed its radius or failed to translate endpoints");
        require(moved_arc.move_point(moved_arc_end, 5.0, 23.0) &&
                    std::abs(moved_arc.arcs.front().radius - 20.0) < 1.0e-9 &&
                    std::abs(moved_arc.find_point(moved_arc_start)->x - 25.0) < 1.0e-9,
                "Moving an undimensioned Arc endpoint did not update its radius");
        auto driven_arc = zima::sketcher::Sketch::create_default();
        const auto driven_arc_id = driven_arc.add_arc(
            0.0, 0.0, 10.0, 0.0, 0.0, 10.0);
        driven_arc.apply_dimension(
            driven_arc.create_arc_radius_dimension(driven_arc_id));
        const auto driven_arc_end = driven_arc.arcs.front().end_point_id;
        require(driven_arc.move_point(driven_arc_end, -20.0, 0.0) &&
                    std::abs(driven_arc.find_point(driven_arc_end)->x + 10.0) < 1.0e-9 &&
                    std::abs(driven_arc.arcs.front().radius - 10.0) < 1.0e-9,
                "Dragging a dimensioned Arc endpoint did not preserve its radius");
        moved_arc.set_point_fixed(moved_arc_start, true);
        const auto fixed_arc_before_move = moved_arc;
        require(!moved_arc.move_point(moved_arc_end, 5.0, 33.0) &&
                    moved_arc.points == fixed_arc_before_move.points &&
                    moved_arc.arcs == fixed_arc_before_move.arcs,
                "Arc drag moved a fixed dependent endpoint");
        const auto arc_before_limit_error = arc_sketch;
        auto invalid_arc_radius = arc_sketch.dimensions.front();
        invalid_arc_radius.value = 30.0;
        bool arc_limit_rejected = false;
        try {
            arc_sketch.apply_dimension(invalid_arc_radius);
        } catch (const std::runtime_error&) {
            arc_limit_rejected = true;
        }
        require(arc_limit_rejected && arc_sketch.arcs == arc_before_limit_error.arcs &&
                    arc_sketch.dimensions == arc_before_limit_error.dimensions,
                "Out-of-range arc radius partially changed the Sketch");
        const auto loaded_arc = zima::sketcher::Sketch::from_serialized(
            arc_sketch.serialized());
        require(loaded_arc.arcs == arc_sketch.arcs &&
                    loaded_arc.dimensions == arc_sketch.dimensions,
                "Arc and radius dimension did not survive serialization");
        const auto arc_before_invalid = arc_sketch;
        bool invalid_arc_rejected = false;
        try {
            static_cast<void>(arc_sketch.add_arc(0.0, 0.0, 0.0, 0.0, 1.0, 0.0));
        } catch (const std::invalid_argument&) {
            invalid_arc_rejected = true;
        }
        require(invalid_arc_rejected && arc_sketch.arcs == arc_before_invalid.arcs &&
                    arc_sketch.points == arc_before_invalid.points,
                "Degenerate arc partially changed the Sketch");
        auto ellipse_sketch = zima::sketcher::Sketch::create_default();
        const auto ellipse_id = ellipse_sketch.add_ellipse(
            0.0, 0.0, 20.0, 0.0, 0.0, 8.0);
        require(ellipse_sketch.ellipses.size() == 1 &&
                    ellipse_sketch.points.size() == 3 &&
                    ellipse_sketch.viewer_mesh().edges.front().reference.semantic_key ==
                        "ellipse:" + ellipse_id,
                "Ellipse did not persist stable axis references or viewer identity");
        const auto ellipse_center = ellipse_sketch.ellipses.front().center_point_id;
        const auto ellipse_major = ellipse_sketch.ellipses.front().major_point_id;
        const auto ellipse_minor = ellipse_sketch.ellipses.front().minor_point_id;
        require(ellipse_sketch.move_point(ellipse_center, 5.0, 4.0) &&
                    std::abs(ellipse_sketch.ellipses.front().major_radius - 20.0) < 1.0e-9 &&
                    std::abs(ellipse_sketch.ellipses.front().minor_radius - 8.0) < 1.0e-9 &&
                    std::abs(ellipse_sketch.find_point(ellipse_major)->x - 25.0) < 1.0e-9 &&
                    std::abs(ellipse_sketch.find_point(ellipse_minor)->y - 12.0) < 1.0e-9,
                "Moving an Ellipse center changed its size or failed to translate axes");
        require(ellipse_sketch.move_point(ellipse_major, 5.0, 34.0) &&
                    std::abs(ellipse_sketch.ellipses.front().major_radius - 30.0) < 1.0e-9 &&
                    std::abs(ellipse_sketch.find_point(ellipse_minor)->x + 3.0) < 1.0e-9,
                "Moving the Ellipse major axis did not rotate its minor axis");
        const auto loaded_ellipse = zima::sketcher::Sketch::from_serialized(
            ellipse_sketch.serialized());
        require(loaded_ellipse.ellipses == ellipse_sketch.ellipses &&
                    loaded_ellipse.points == ellipse_sketch.points,
                "Ellipse did not survive current Sketch serialization");
        auto ellipse_major_dimension =
            ellipse_sketch.create_ellipse_radius_dimension(ellipse_id, true);
        ellipse_major_dimension.value = 25.0;
        ellipse_sketch.apply_dimension(ellipse_major_dimension);
        auto ellipse_minor_dimension =
            ellipse_sketch.create_ellipse_radius_dimension(ellipse_id, false);
        ellipse_minor_dimension.value = 6.0;
        ellipse_sketch.apply_dimension(ellipse_minor_dimension);
        auto ellipse_rotation_dimension =
            ellipse_sketch.create_ellipse_rotation_dimension(ellipse_id);
        ellipse_rotation_dimension.value = 30.0;
        ellipse_rotation_dimension.lower_limit = -45.0;
        ellipse_rotation_dimension.upper_limit = 45.0;
        ellipse_sketch.apply_dimension(ellipse_rotation_dimension);
        const auto dimensioned_ellipse_packet = ellipse_sketch.viewer_mesh();
        require(std::abs(ellipse_sketch.ellipses.front().major_radius - 25.0) < 1.0e-9 &&
                    std::abs(ellipse_sketch.ellipses.front().minor_radius - 6.0) < 1.0e-9 &&
                    std::abs(ellipse_sketch.ellipses.front().rotation -
                        3.14159265358979323846 / 6.0) < 1.0e-9 &&
                    dimensioned_ellipse_packet.dimensions.size() == 3 &&
                    dimensioned_ellipse_packet.dimensions[0].label_prefix == "a=" &&
                    dimensioned_ellipse_packet.dimensions[1].label_prefix == "b=" &&
                    dimensioned_ellipse_packet.dimensions[2].unit_suffix == " °",
                "Ellipse dimensions did not drive geometry or viewer data");
        const auto ellipse_before_rotation_limit = ellipse_sketch;
        auto invalid_ellipse_rotation = ellipse_rotation_dimension;
        invalid_ellipse_rotation.value = 60.0;
        bool ellipse_rotation_limit_rejected = false;
        try {
            ellipse_sketch.apply_dimension(invalid_ellipse_rotation);
        } catch (const std::runtime_error&) {
            ellipse_rotation_limit_rejected = true;
        }
        require(ellipse_rotation_limit_rejected &&
                    ellipse_sketch.ellipses == ellipse_before_rotation_limit.ellipses &&
                    ellipse_sketch.points == ellipse_before_rotation_limit.points,
                "Out-of-range Ellipse rotation partially changed geometry");
        const bool ellipse_rotation_set = ellipse_sketch.set_dimension_value(
            ellipse_rotation_dimension.id, -30.0);
        require(ellipse_rotation_set &&
                    std::abs(ellipse_sketch.ellipses.front().rotation +
                        3.14159265358979323846 / 6.0) < 1.0e-9,
                "Generic dimension API did not drive Ellipse rotation");
        const auto loaded_dimensioned_ellipse = zima::sketcher::Sketch::from_serialized(
            ellipse_sketch.serialized());
        require(loaded_dimensioned_ellipse.dimensions == ellipse_sketch.dimensions,
                "Ellipse semiaxis dimensions did not survive serialization");
        ellipse_sketch.remove_geometry(ellipse_id);
        require(ellipse_sketch.ellipses.empty() && ellipse_sketch.points.empty() &&
                    ellipse_sketch.dimensions.empty(),
                "Ellipse deletion retained dimensions or orphan axis points");
        auto spline_sketch = zima::sketcher::Sketch::create_default();
        const auto spline_id = spline_sketch.add_bspline({
            {0.0, 0.0}, {10.0, 20.0}, {20.0, -10.0}, {30.0, 0.0}});
        const auto spline_packet = spline_sketch.viewer_mesh();
        require(spline_sketch.bsplines.size() == 1 &&
                    spline_packet.edges.size() == 1 &&
                    spline_packet.edges.front().points.size() == 129 &&
                    spline_packet.edges.front().reference.semantic_key ==
                        "bspline:" + spline_id &&
                    std::abs(spline_packet.edges.front().points.front().x) < 1.0e-9 &&
                    std::abs(spline_packet.edges.front().points.back().x - 30.0) < 1.0e-9,
                "Cubic B-spline did not preserve stable identity or clamped endpoints");
        const auto moved_spline_point =
            spline_sketch.bsplines.front().control_point_ids[1];
        require(spline_sketch.move_point(moved_spline_point, 10.0, 30.0) &&
                    spline_sketch.viewer_mesh().edges.front().points[64].y >
                        spline_packet.edges.front().points[64].y,
                "Dragging a B-spline control point did not update the viewer curve");
        const auto loaded_spline = zima::sketcher::Sketch::from_serialized(
            spline_sketch.serialized());
        require(loaded_spline.bsplines == spline_sketch.bsplines &&
                    loaded_spline.points == spline_sketch.points,
                "B-spline did not survive current Sketch serialization");
        auto periodic_spline = zima::sketcher::Sketch::create_default();
        const auto periodic_id = periodic_spline.add_bspline({
            {-20.0, 0.0}, {-15.0, 15.0}, {0.0, 22.0}, {15.0, 15.0},
            {20.0, 0.0}, {10.0, -18.0}, {-10.0, -18.0}}, 3, true);
        const auto periodic_packet = periodic_spline.viewer_mesh();
        require(periodic_spline.bsplines.front().closed &&
                    periodic_packet.edges.front().reference.semantic_key ==
                        "bspline:" + periodic_id &&
                    periodic_packet.edges.front().points.front().x ==
                        periodic_packet.edges.front().points.back().x &&
                    periodic_packet.edges.front().points.front().y ==
                        periodic_packet.edges.front().points.back().y &&
                    zima::sketcher::Sketch::from_serialized(
                        periodic_spline.serialized()).bsplines == periodic_spline.bsplines,
                "Closed periodic B-spline did not close or survive serialization");
        spline_sketch.remove_geometry(spline_id);
        require(spline_sketch.bsplines.empty() && spline_sketch.points.empty(),
                "B-spline deletion retained orphan control points");
        std::cout << "C++ Sketcher contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
