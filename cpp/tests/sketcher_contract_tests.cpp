#include <zima/sketcher/sketch.hpp>
#include <zima/sketcher/sketch_trim.hpp>
#include <zima/viewer/picking.hpp>
#include <zima/document/part_document.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <set>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        using zima::sketcher::DimensionKind;
        std::set<std::string> generated_ids;
        for (int index = 0; index < 1024; ++index) {
            generated_ids.insert(
                zima::sketcher::Sketch::create_point(index, index).id);
        }
        require(generated_ids.size() == 1024,
                "Rapid Sketch ID generation produced a collision");
        for (const auto plane : {zima::sketcher::SketchPlane::XY,
                                 zima::sketcher::SketchPlane::XZ,
                                 zima::sketcher::SketchPlane::YZ}) {
            for (const double normal_displacement : {-7.25, 4.5}) {
                auto original_plane = zima::sketcher::Sketch::create_default();
                original_plane.plane = plane;
                original_plane.plane_offset = 3.0;
                original_plane.refresh_default_frame();
                auto moved_plane = original_plane;
                moved_plane.plane_offset +=
                    zima::sketcher::plane_offset_delta_for_normal_displacement(
                        plane, normal_displacement);
                moved_plane.refresh_default_frame();
                const zima::kernel::Vec3 movement{
                    moved_plane.resolved_origin.x - original_plane.resolved_origin.x,
                    moved_plane.resolved_origin.y - original_plane.resolved_origin.y,
                    moved_plane.resolved_origin.z - original_plane.resolved_origin.z};
                const double movement_along_normal =
                    movement.x * original_plane.resolved_normal.x +
                    movement.y * original_plane.resolved_normal.y +
                    movement.z * original_plane.resolved_normal.z;
                require(std::abs(movement_along_normal - normal_displacement) < 1.0e-9,
                        "Plane-offset drag moved opposite to the Sketch normal");
            }
        }
        require(zima::sketcher::classify_linear_dimension(
                    {0.0, 0.0}, {10.0, 6.0}, {5.0, 3.0}) ==
                    DimensionKind::Distance,
                "cursor inside point bounds did not select aligned distance");
        require(zima::sketcher::classify_linear_dimension(
                    {0.0, 0.0}, {10.0, 6.0}, {5.0, 12.0}) ==
                    DimensionKind::DistanceX,
                "cursor above point bounds did not select horizontal distance");
        require(zima::sketcher::classify_linear_dimension(
                    {0.0, 0.0}, {10.0, 6.0}, {18.0, 3.0}) ==
                    DimensionKind::DistanceY,
                "cursor beside point bounds did not select vertical distance");
        require(zima::sketcher::classify_linear_dimension(
                    {0.0, 0.0}, {10.0, 6.0}, {10.2, 12.0}) ==
                    DimensionKind::DistanceX &&
                zima::sketcher::classify_linear_dimension(
                    {0.0, 0.0}, {10.0, 6.0}, {18.0, 6.2}) ==
                    DimensionKind::DistanceY,
                "linear dimension boundary tolerance did not stabilize axis zones");
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
        const auto solved_serialization = sketch.serialized();
        const auto repeated_solve = sketch.solve();
        require(repeated_solve.status == solved.status &&
                    repeated_solve.remaining_degrees_of_freedom ==
                        solved.remaining_degrees_of_freedom &&
                    sketch.serialized() == solved_serialization,
                "Read-only repeated DOF solve changed the exact Sketch state");
        require(!sketch.set_dimension_value("length", 35.0) &&
                    sketch.set_dimension_value("length", 25.0),
                "Absolute dimension limits did not reject an out-of-range edit");
        require(sketch.solve().status == zima::sketcher::SolveStatus::Solved &&
                    std::abs(sketch.points.back().x - 25.0) < 1.0e-7,
                "Edited dimension did not drive sketch geometry");
        sketch.dimensions.front().locked = true;
        require(sketch.set_dimension_value("length", 20.0) &&
                    std::abs(sketch.points.back().x - 20.0) < 1.0e-7 &&
                    sketch.solve().status == zima::sketcher::SolveStatus::Solved,
                "Locked driving dimension rejected an intentional numeric edit");
        auto bypass_attempt = sketch.dimensions.front();
        bypass_attempt.value = 22.0;
        sketch.apply_dimension(std::move(bypass_attempt));
        require(std::abs(sketch.points.back().x - 22.0) < 1.0e-7,
                "Locked driving dimension Properties edit did not drive geometry");
        require(sketch.set_dimension_value("length", 25.0),
                "Locked dimension could not be restored by a numeric edit");
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
        auto unlocked = loaded;
        auto unlocked_dimension = unlocked.dimensions.front();
        unlocked_dimension.locked = false;
        unlocked.apply_dimension(std::move(unlocked_dimension));
        require(unlocked.set_dimension_value("length", 20.0) &&
                    std::abs(unlocked.points.back().x - 20.0) < 1.0e-7,
                "Explicitly unlocked driving dimension remained immutable");
        const auto mesh = sketch.viewer_mesh();
        require(mesh.triangles.empty() && mesh.edges.size() == 1 &&
                    mesh.points.size() >= 4 && mesh.axes.size() == 2 &&
                    mesh.dimensions.size() == 1 &&
                    std::any_of(mesh.points.begin(), mesh.points.end(),
                        [&](const auto& point) {
                            return point.reference.owner_id == sketch.id &&
                                point.reference.semantic_key ==
                                    "external_point:sketch_origin" &&
                                point.always_visible;
                        }) &&
                    mesh.edges.front().reference.owner_id == sketch.id &&
                    mesh.edges.front().reference.semantic_key.rfind("segment:", 0) == 0 &&
                    mesh.edges.front().overlay,
                "Sketch viewer packet lost stable point/segment ownership");
        const auto origin_candidates = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                mesh, {0.0, 0.0, 10.0}, {0.0, 0.0, -1.0}, 0.2),
            {zima::viewer::CandidateKind::SketchExternalReference});
        require(std::any_of(origin_candidates.begin(), origin_candidates.end(),
                    [](const auto& candidate) {
                        return candidate.semantic_key ==
                            "external_point:sketch_origin";
                    }),
                "Sketch origin was not offered through the common candidate list");
        const auto distant_axis_candidates = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                mesh, {5000.0, 0.0, 10.0}, {0.0, 0.0, -1.0}, 0.2),
            {zima::viewer::CandidateKind::SketchAxis});
        require(std::any_of(distant_axis_candidates.begin(),
                    distant_axis_candidates.end(), [](const auto& candidate) {
                        return candidate.semantic_key == "sketch_axis:x";
                    }),
                "Sketch base axis was drawn as infinite but remained finitely pickable");
        require(std::any_of(origin_candidates.begin(), origin_candidates.end(),
                    [](const auto& candidate) {
                        return candidate.semantic_key ==
                            "sketch_intersection:sketch_axis:x||sketch_axis:y";
                    }),
                "Sketch axis crossing was not offered as an intersection candidate");
        const auto midpoint_candidates = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                mesh, {12.5, 0.0, 10.0}, {0.0, 0.0, -1.0}, 0.2),
            {zima::viewer::CandidateKind::SketchExternalReference});
        require(std::any_of(midpoint_candidates.begin(), midpoint_candidates.end(),
                    [&](const auto& candidate) {
                        return candidate.semantic_key ==
                            "sketch_midpoint:" + initial_segment_id;
                    }),
                "Segment midpoint was not offered as a stable placement candidate");
        auto origin_bound = zima::sketcher::Sketch::create_default();
        const auto origin_bound_point = origin_bound.add_point(3.0, 4.0);
        static_cast<void>(origin_bound.add_point_reference_constraint(
            origin_bound_point, "sketch_origin"));
        require(std::abs(origin_bound.find_point(origin_bound_point)->x) < 1.0e-9 &&
                    std::abs(origin_bound.find_point(origin_bound_point)->y) < 1.0e-9,
                "Coincident did not bind a native point to the local Sketch origin");
        auto crossing_bound = zima::sketcher::Sketch::create_default();
        const auto diagonal_a = crossing_bound.add_segment(-5.0, -5.0, 5.0, 5.0);
        const auto diagonal_b = crossing_bound.add_segment(-5.0, 5.0, 5.0, -5.0);
        const auto crossing_point = crossing_bound.add_point(1.0, 2.0);
        static_cast<void>(crossing_bound.add_point_on_line_constraint(
            crossing_point, diagonal_a));
        static_cast<void>(crossing_bound.add_point_on_line_constraint(
            crossing_point, diagonal_b));
        require(std::abs(crossing_bound.find_point(crossing_point)->x) < 1.0e-8 &&
                    std::abs(crossing_bound.find_point(crossing_point)->y) < 1.0e-8,
                "Two persisted PointOnLine relations did not bind an intersection");
        auto asymmetric_crossing = zima::sketcher::Sketch::create_default();
        const auto asymmetric_first =
            asymmetric_crossing.add_segment(2.0, 1.0, 8.0, 7.0);
        const auto asymmetric_second =
            asymmetric_crossing.add_segment(1.0, 6.0, 9.0, 2.0);
        const auto asymmetric_mesh = asymmetric_crossing.viewer_mesh();
        const auto asymmetric_key = "sketch_intersection:" + asymmetric_first +
            "||" + asymmetric_second;
        const auto asymmetric_marker = std::find_if(
            asymmetric_mesh.points.begin(), asymmetric_mesh.points.end(),
            [&](const auto& point) {
                return point.reference.semantic_key == asymmetric_key;
            });
        require(asymmetric_marker != asymmetric_mesh.points.end() &&
                    std::abs(asymmetric_marker->position.x - 5.0) < 1.0e-9 &&
                    std::abs(asymmetric_marker->position.y - 4.0) < 1.0e-9,
                "Asymmetric finite-line intersection used an invalid parameter");
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

        auto text_sketch = zima::sketcher::Sketch::create_default();
        auto text = zima::sketcher::Sketch::create_text();
        text.value = "ZIMA";
        text.anchor_x = 2.0;
        text.anchor_y = 3.0;
        text.height = 5.0;
        text.horizontal = zima::sketcher::TextHorizontalAlignment::Center;
        text.vertical = zima::sketcher::TextVerticalAlignment::Middle;
        text.angle_degrees = 15.0;
        text.flipped = true;
        text.color = zima::sketcher::SketchTextColor::Yellow;
        text.contours = {{{2.0, 3.0}, {7.0, 3.0}, {7.0, 8.0}, {2.0, 8.0}}};
        const auto text_id = text.id;
        text_sketch.add_text(text);
        const auto loaded_text = zima::sketcher::Sketch::from_serialized(
            text_sketch.serialized());
        require(loaded_text.texts == text_sketch.texts,
                "Semantic Sketch text and its persisted outline did not round-trip");
        const auto text_mesh = text_sketch.viewer_mesh();
        require(text_mesh.edges.size() == 1 &&
                    text_mesh.edges.front().points.size() == 5 &&
                    text_mesh.edges.front().points.front().x ==
                        text_mesh.edges.front().points.back().x &&
                    text_mesh.edges.front().reference.semantic_key ==
                        "text:" + text_id + ":yellow",
                "Sketch text did not expose its closed persisted viewer outline");
        const auto text_candidates = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                text_mesh, {4.0, 3.0, 10.0}, {0.0, 0.0, -1.0}, 0.2),
            {zima::viewer::CandidateKind::SketchText});
        require(text_candidates.size() == 1 &&
                    text_candidates.front().owner_id == text_sketch.id &&
                    text_candidates.front().semantic_key ==
                        "text:" + text_id + ":yellow",
                "Sketch text did not use the common ordered viewer candidate list");
        auto edited_text = text_sketch.texts.front();
        edited_text.value = "CAD";
        edited_text.contours = {{{2.0, 3.0}, {8.0, 3.0}, {8.0, 8.0}, {2.0, 8.0}}};
        text_sketch.update_text(edited_text);
        const auto before_invalid_text = text_sketch.serialized();
        bool invalid_text_rejected = false;
        try {
            edited_text.contours.clear();
            text_sketch.update_text(std::move(edited_text));
        } catch (const std::runtime_error&) {
            invalid_text_rejected = true;
        }
        require(invalid_text_rejected &&
                    text_sketch.serialized() == before_invalid_text,
                "Invalid Sketch text edit partially changed the persisted model");
        text_sketch.remove_geometry(text_id);
        require(text_sketch.texts.empty(),
                "Sketch text was not removed through the common geometry operation");

        auto external_edge = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Edge);
        external_edge.source_document_id = "part-source";
        external_edge.source_owner_id = "container-source";
        external_edge.source_semantic_key = "edge:stable-source";
        external_edge.cached_points = {{2.0, 3.0}, {7.0, 3.0}};
        const auto external_edge_id = external_edge.id;
        text_sketch.add_external_reference(external_edge);
        auto external_point = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Point);
        external_point.source_document_id = "part-source";
        external_point.source_owner_id = "container-source";
        external_point.source_semantic_key = "vertex:stable-source";
        external_point.cached_points = {{2.0, 3.0}};
        const auto external_point_id = external_point.id;
        text_sketch.add_external_reference(external_point);
        auto external_snap_sketch = zima::sketcher::Sketch::create_default();
        external_snap_sketch.add_external_reference(external_point);
        const auto snapped_segment = external_snap_sketch.add_segment(
            2.0005, 3.0005, 8.0, 3.0, 1.0e-3);
        const auto& snapped_geometry = external_snap_sketch.segments.front();
        require(snapped_geometry.id == snapped_segment &&
                    external_snap_sketch.find_point(
                        snapped_geometry.first_point_id)->x == 2.0 &&
                    external_snap_sketch.find_point(
                        snapped_geometry.first_point_id)->y == 3.0 &&
                    std::any_of(external_snap_sketch.constraints.begin(),
                        external_snap_sketch.constraints.end(), [&](const auto& value) {
                            return value.kind ==
                                    zima::sketcher::ConstraintKind::PointReference &&
                                value.first_point_id ==
                                    snapped_geometry.first_point_id &&
                                value.second_point_id == external_point_id;
                        }),
                "Segment endpoint did not snap and bind to persisted external point");
        const auto snapped_circle = external_snap_sketch.add_circle(
            2.0005, 3.0005, 4.0, false, 1.0e-3);
        const auto snapped_circle_geometry = std::find_if(
            external_snap_sketch.circles.begin(),
            external_snap_sketch.circles.end(), [&](const auto& value) {
                return value.id == snapped_circle;
            });
        require(snapped_circle_geometry != external_snap_sketch.circles.end() &&
                    external_snap_sketch.find_point(
                        snapped_circle_geometry->center_point_id)->x == 2.0 &&
                    std::any_of(external_snap_sketch.constraints.begin(),
                        external_snap_sketch.constraints.end(), [&](const auto& value) {
                            return value.kind ==
                                    zima::sketcher::ConstraintKind::PointReference &&
                                value.first_point_id ==
                                    snapped_circle_geometry->center_point_id &&
                                value.second_point_id == external_point_id;
                        }),
                "Curve control point did not snap and bind to external point");
        const auto externally_coincident_point = text_sketch.add_point(20.0, 20.0);
        static_cast<void>(text_sketch.add_point_reference_constraint(
            externally_coincident_point, external_point_id));
        require(text_sketch.find_point(externally_coincident_point)->x == 2.0 &&
                    text_sketch.find_point(externally_coincident_point)->y == 3.0,
                "Coincident constraint did not treat external point as fixed input");
        const auto externally_dimensioned_point = text_sketch.add_point(12.0, 3.0);
        auto external_dimension = text_sketch.create_point_dimension(
            externally_dimensioned_point, external_point_id);
        external_dimension.value = 5.0;
        text_sketch.apply_dimension(external_dimension);
        require(std::abs(std::hypot(
                    text_sketch.find_point(externally_dimensioned_point)->x - 2.0,
                    text_sketch.find_point(externally_dimensioned_point)->y - 3.0) - 5.0) <
                    1.0e-8,
                "Distance dimension did not use external point as read-only datum");
        auto external_axis = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Axis);
        external_axis.source_document_id = "part-source";
        external_axis.source_owner_id = "container-source";
        external_axis.source_semantic_key = "axis:stable-source";
        external_axis.cached_points = {{-10.0, 8.0}, {10.0, 8.0}};
        const auto external_axis_id = external_axis.id;
        text_sketch.add_external_reference(external_axis);
        auto external_face = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Face);
        external_face.source_document_id = "part-source";
        external_face.source_owner_id = "container-source";
        external_face.source_semantic_key = "face:stable-source";
        external_face.cached_points = {{2.0, -1.0}, {2.0, 1.0}};
        external_face.infinite = true;
        const auto external_face_id = external_face.id;
        text_sketch.add_external_reference(external_face);
        const auto external_face_boundary_point = text_sketch.add_point(1.0, 1.0);
        static_cast<void>(text_sketch.add_point_on_line_constraint(
            external_face_boundary_point, external_face_id));
        const auto* face_boundary = text_sketch.find_point(
            external_face_boundary_point);
        require(face_boundary != nullptr &&
                    std::abs(face_boundary->x - 2.0) < 1.0e-8,
                "Point-on-line did not use the external Face intersection line");
        auto external_roundtrip = zima::sketcher::Sketch::from_serialized(
            text_sketch.serialized());
        require(external_roundtrip.external_references ==
                    text_sketch.external_references &&
                    external_roundtrip.constraints == text_sketch.constraints &&
                    external_roundtrip.dimensions == text_sketch.dimensions &&
                    external_roundtrip.points == text_sketch.points &&
                    external_roundtrip.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
                "Persisted external references, constraints, or dimensions did not round-trip");
        auto removed_external_relations = external_roundtrip;
        const auto removed_constraint_id =
            removed_external_relations.constraints.front().id;
        const auto removed_dimension_id =
            removed_external_relations.dimensions.front().id;
        removed_external_relations.remove_constraint(removed_constraint_id);
        removed_external_relations.remove_dimension(removed_dimension_id);
        require(std::none_of(removed_external_relations.constraints.begin(),
                    removed_external_relations.constraints.end(), [&](const auto& value) {
                        return value.id == removed_constraint_id;
                    }) &&
                    std::none_of(removed_external_relations.dimensions.begin(),
                        removed_external_relations.dimensions.end(), [&](const auto& value) {
                            return value.id == removed_dimension_id;
                        }) &&
                    removed_external_relations.solve().status !=
                        zima::sketcher::SolveStatus::Invalid,
                "Independent external constraint or dimension removal failed");
        auto contextual_sketch = zima::sketcher::Sketch::create_default();
        auto contextual_reference = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Edge);
        contextual_reference.source_document_id = "context-source-part";
        contextual_reference.source_owner_id = "context-source-owner";
        contextual_reference.source_semantic_key = "edge:context-source";
        contextual_reference.source_instance_path = "3:src";
        contextual_reference.context_assembly_document_id = "context-assembly";
        contextual_reference.context_instance_path = "3:dep";
        contextual_reference.cached_points = {{1.0, 2.0}, {4.0, 2.0}};
        contextual_sketch.add_external_reference(contextual_reference);
        require(zima::sketcher::Sketch::from_serialized(
                    contextual_sketch.serialized()).external_references ==
                    contextual_sketch.external_references,
                "Contextual occurrence identity did not survive Sketch save/load");
        auto external_direction_sketch = zima::sketcher::Sketch::create_default();
        auto external_direction = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Edge);
        external_direction.source_document_id = "direction-source";
        external_direction.source_owner_id = "direction-owner";
        external_direction.source_semantic_key = "edge:direction";
        external_direction.cached_points = {{-5.0, 0.0}, {5.0, 0.0}};
        const auto external_direction_id = external_direction.id;
        external_direction_sketch.add_external_reference(external_direction);
        const auto external_profile_segment =
            external_direction_sketch.add_external_profile_geometry(
                external_direction_id);
        require(external_direction_sketch.segments.size() == 1 &&
                    external_direction_sketch.segments.front().id ==
                        external_profile_segment &&
                    external_direction_sketch.import_blocks.size() == 1,
                "Projected external line did not create linked profile geometry");
        const auto linked_first_point =
            external_direction_sketch.segments.front().first_point_id;
        require(!external_direction_sketch.move_point(
                    linked_first_point, 100.0, 100.0),
                "Externally linked profile point was not read-only");
        auto removed_linked_reference = external_direction_sketch;
        removed_linked_reference.remove_geometry(external_direction_id);
        require(removed_linked_reference.external_references.empty() &&
                    removed_linked_reference.import_blocks.empty() &&
                    removed_linked_reference.segments.empty() &&
                    removed_linked_reference.points.empty(),
                "Deleting an external reference left linked profile geometry behind");
        const auto perpendicular_segment = external_direction_sketch.add_segment(
            0.0, 0.0, 2.0, 5.0);
        static_cast<void>(external_direction_sketch.add_segment_pair_constraint(
            external_direction_id, perpendicular_segment,
            zima::sketcher::ConstraintKind::Perpendicular));
        const auto point_on_external_line = external_direction_sketch.add_point(
            3.0, 4.0);
        static_cast<void>(external_direction_sketch.add_point_on_line_constraint(
            point_on_external_line, external_direction_id));
        const auto perpendicular_geometry = std::find_if(
            external_direction_sketch.segments.begin(),
            external_direction_sketch.segments.end(), [&](const auto& value) {
                return value.id == perpendicular_segment;
            });
        const auto perpendicular_first = external_direction_sketch.find_point(
            perpendicular_geometry->first_point_id);
        const auto perpendicular_second = external_direction_sketch.find_point(
            perpendicular_geometry->second_point_id);
        require(std::abs(perpendicular_second->x - perpendicular_first->x) < 1.0e-8,
                "External edge did not act as fixed direction for Perpendicular");
        auto origin_axis_direction = zima::sketcher::Sketch::create_default();
        const auto axis_driven = origin_axis_direction.add_segment(
            2.0, 3.0, 8.0, 7.0);
        static_cast<void>(origin_axis_direction.add_segment_pair_constraint(
            "sketch_axis:x", axis_driven,
            zima::sketcher::ConstraintKind::Perpendicular));
        const auto* axis_driven_first = origin_axis_direction.find_point(
            origin_axis_direction.segments.front().first_point_id);
        const auto* axis_driven_second = origin_axis_direction.find_point(
            origin_axis_direction.segments.front().second_point_id);
        require(std::abs(axis_driven_second->x - axis_driven_first->x) < 1.0e-8 &&
                    zima::sketcher::Sketch::from_serialized(
                        origin_axis_direction.serialized()).constraints ==
                        origin_axis_direction.constraints,
                "Persisted local Sketch axis was not a direction reference");
        require(std::abs(external_direction_sketch.find_point(
                    point_on_external_line)->y) < 1.0e-8,
                "Point-on-line did not project a native point to external edge");
        auto curved_external = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Edge);
        curved_external.source_document_id = "curved-source";
        curved_external.source_owner_id = "curved-owner";
        curved_external.source_semantic_key = "edge:curved";
        curved_external.cached_points = {{0.0, 0.0}, {5.0, 5.0}, {10.0, 0.0}};
        const auto curved_external_id = curved_external.id;
        external_direction_sketch.add_external_reference(curved_external);
        const auto point_on_external_curve =
            external_direction_sketch.add_point(5.0, 7.0);
        static_cast<void>(external_direction_sketch.add_point_on_line_constraint(
            point_on_external_curve, curved_external_id));
        require(std::abs(external_direction_sketch.find_point(
                    point_on_external_curve)->x - 5.0) < 1.0e-8 &&
                    std::abs(external_direction_sketch.find_point(
                    point_on_external_curve)->y - 5.0) < 1.0e-8 &&
                    external_direction_sketch.move_point(
                        point_on_external_curve, 2.0, 4.0) &&
                    std::abs(external_direction_sketch.find_point(
                        point_on_external_curve)->x - 3.0) < 1.0e-8 &&
                    std::abs(external_direction_sketch.find_point(
                        point_on_external_curve)->y - 3.0) < 1.0e-8,
                "Point-on-line collapsed a curved external edge to its end chord");
        auto native_line_sketch = zima::sketcher::Sketch::create_default();
        const auto native_line = native_line_sketch.add_segment(
            -5.0, 2.0, 5.0, 2.0);
        const auto point_on_native_line = native_line_sketch.add_point(1.0, 7.0);
        static_cast<void>(native_line_sketch.add_point_on_line_constraint(
            point_on_native_line, native_line));
        require(std::abs(native_line_sketch.find_point(
                    point_on_native_line)->y - 2.0) < 1.0e-8,
                "Point-on-line did not project a point to a native Sketch segment");
        const auto point_on_origin_axis = native_line_sketch.add_point(4.0, 9.0);
        static_cast<void>(native_line_sketch.add_point_on_line_constraint(
            point_on_origin_axis, "sketch_axis:x"));
        require(std::abs(native_line_sketch.find_point(
                    point_on_origin_axis)->y) < 1.0e-8,
                "Point-on-line did not support the persisted local Sketch axis");
        require(zima::sketcher::Sketch::from_serialized(
                    native_line_sketch.serialized()).solve().status !=
                    zima::sketcher::SolveStatus::Invalid,
                "Native point-on-line constraint did not survive serialization");
        auto arc_support_sketch = zima::sketcher::Sketch::create_default();
        const auto arc_support = arc_support_sketch.add_arc(
            0.0, 0.0, 10.0, 0.0, 0.0, 10.0);
        const auto point_on_arc = arc_support_sketch.add_point(-5.0, 5.0);
        static_cast<void>(arc_support_sketch.add_point_on_circle_constraint(
            point_on_arc, arc_support));
        require(std::abs(arc_support_sketch.find_point(point_on_arc)->x) < 1.0e-8 &&
                    std::abs(arc_support_sketch.find_point(point_on_arc)->y - 10.0) <
                        1.0e-8,
                "Point-on-arc did not clamp to the persisted arc domain");
        require(arc_support_sketch.move_point(point_on_arc, 4.0, 4.0) &&
                    std::abs(arc_support_sketch.find_point(point_on_arc)->x -
                        std::sqrt(50.0)) < 1.0e-8 &&
                    std::abs(arc_support_sketch.find_point(point_on_arc)->y -
                        std::sqrt(50.0)) < 1.0e-8,
                "Point-on-arc could not slide inside its angular domain");
        const auto support_arc_it = std::find_if(
            arc_support_sketch.arcs.begin(), arc_support_sketch.arcs.end(),
            [&](const auto& value) { return value.id == arc_support; });
        require(support_arc_it != arc_support_sketch.arcs.end() &&
                    arc_support_sketch.move_point(
                        support_arc_it->center_point_id, 2.0, 3.0) &&
                    std::abs(arc_support_sketch.find_point(point_on_arc)->x -
                        (2.0 + std::sqrt(50.0))) < 1.0e-8 &&
                    std::abs(arc_support_sketch.find_point(point_on_arc)->y -
                        (3.0 + std::sqrt(50.0))) < 1.0e-8,
                "Moving an arc center did not carry its point-on-arc dependants");
        auto constrained_curve_centers =
            zima::sketcher::Sketch::create_default();
        const auto centered_arc = constrained_curve_centers.add_arc(
            0.0, 5.0, 5.0, 5.0, 0.0, 10.0);
        const auto centered_arc_it = std::ranges::find_if(
            constrained_curve_centers.arcs, [&](const auto& value) {
                return value.id == centered_arc;
            });
        const auto centered_arc_center = centered_arc_it->center_point_id;
        const auto centered_arc_start = centered_arc_it->start_point_id;
        const auto centered_arc_end = centered_arc_it->end_point_id;
        static_cast<void>(constrained_curve_centers
            .add_point_on_line_constraint(
                centered_arc_center, "sketch_axis:x"));
        require(std::abs(constrained_curve_centers.find_point(
                    centered_arc_center)->y) < 1.0e-8 &&
                    std::abs(constrained_curve_centers.find_point(
                        centered_arc_start)->x - 5.0) < 1.0e-8 &&
                    std::abs(constrained_curve_centers.find_point(
                        centered_arc_start)->y) < 1.0e-8 &&
                    std::abs(constrained_curve_centers.find_point(
                        centered_arc_end)->x) < 1.0e-8 &&
                    std::abs(constrained_curve_centers.find_point(
                        centered_arc_end)->y - 5.0) < 1.0e-8 &&
                    std::abs(constrained_curve_centers.arcs.front().radius -
                        5.0) < 1.0e-8,
                "Constraining an Arc centre to an axis distorted the Arc");
        auto merged_curve_centers = zima::sketcher::Sketch::create_default();
        const auto merged_circle = merged_curve_centers.add_circle(
            10.0, 5.0, 2.0);
        const auto merged_arc = merged_curve_centers.add_arc(
            20.0, 5.0, 25.0, 5.0, 20.0, 10.0);
        const auto center_target_segment = merged_curve_centers.add_segment(
            0.0, 0.0, 5.0, 0.0);
        const auto center_target = std::ranges::find_if(
            merged_curve_centers.segments, [&](const auto& value) {
                return value.id == center_target_segment;
            });
        const auto first_target = center_target->first_point_id;
        const auto second_target = center_target->second_point_id;
        const auto circle_before = std::ranges::find_if(
            merged_curve_centers.circles, [&](const auto& value) {
                return value.id == merged_circle;
            });
        const auto arc_before = std::ranges::find_if(
            merged_curve_centers.arcs, [&](const auto& value) {
                return value.id == merged_arc;
            });
        const auto circle_center_before = circle_before->center_point_id;
        const auto arc_center_before = arc_before->center_point_id;
        static_cast<void>(merged_curve_centers.merge_points(
            first_target, circle_center_before));
        static_cast<void>(merged_curve_centers.merge_points(
            second_target, arc_center_before));
        const auto merged_arc_after = std::ranges::find_if(
            merged_curve_centers.arcs, [&](const auto& value) {
                return value.id == merged_arc;
            });
        require(merged_curve_centers.circles.front().center_point_id ==
                    first_target &&
                    merged_arc_after->center_point_id == second_target &&
                    std::abs(merged_curve_centers.find_point(
                        merged_arc_after->start_point_id)->x - 10.0) < 1.0e-8 &&
                    std::abs(merged_curve_centers.find_point(
                        merged_arc_after->start_point_id)->y) < 1.0e-8 &&
                    std::abs(merged_curve_centers.find_point(
                        merged_arc_after->end_point_id)->x - 5.0) < 1.0e-8 &&
                    std::abs(merged_curve_centers.find_point(
                        merged_arc_after->end_point_id)->y - 5.0) < 1.0e-8 &&
                    merged_curve_centers.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
                "Merging a curve centre with a Segment endpoint distorted its curve");
        auto loaded_arc_support = zima::sketcher::Sketch::from_serialized(
            arc_support_sketch.serialized());
        require(loaded_arc_support.constraints == arc_support_sketch.constraints &&
                    loaded_arc_support.solve().status !=
                        zima::sketcher::SolveStatus::Invalid,
                "Point-on-arc relation did not survive persistence");
        auto loaded_external_direction = zima::sketcher::Sketch::from_serialized(
            external_direction_sketch.serialized());
        require(loaded_external_direction.constraints ==
                    external_direction_sketch.constraints &&
                    loaded_external_direction.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
                "External direction constraint did not survive serialization");
        zima::kernel::ViewerReferenceGeometry changed_direction;
        changed_direction.edges.push_back({
            {{{0.0, -5.0, 0.0}, {0.0, 5.0, 0.0}}},
            {"direction-owner", "edge:direction", {}}, false, false});
        require(loaded_external_direction.refresh_external_references(
                    "direction-source", changed_direction),
                "Explicit refresh did not update external direction constraint");
        const auto regenerated_geometry = std::find_if(
            loaded_external_direction.segments.begin(),
            loaded_external_direction.segments.end(), [&](const auto& value) {
                return value.id == perpendicular_segment;
            });
        const auto regenerated_first = loaded_external_direction.find_point(
            regenerated_geometry->first_point_id);
        const auto regenerated_second = loaded_external_direction.find_point(
            regenerated_geometry->second_point_id);
        require(std::abs(regenerated_second->y - regenerated_first->y) < 1.0e-8,
                "Explicit refresh did not solve Perpendicular against new direction");
        const auto refreshed_profile = std::find_if(
            loaded_external_direction.segments.begin(),
            loaded_external_direction.segments.end(), [&](const auto& value) {
                return value.id == external_profile_segment;
            });
        require(std::abs(loaded_external_direction.find_point(
                    refreshed_profile->first_point_id)->x) < 1.0e-8 &&
                    std::abs(loaded_external_direction.find_point(
                    refreshed_profile->second_point_id)->x) < 1.0e-8,
                "Linked external profile geometry did not follow reference refresh");
        require(std::abs(loaded_external_direction.find_point(
                    point_on_external_line)->x) < 1.0e-8,
                "Explicit refresh did not regenerate Point-on-line constraint");
        auto fixed_external_conflict = zima::sketcher::Sketch::create_default();
        fixed_external_conflict.add_external_reference(external_point);
        const auto fixed_external_point = fixed_external_conflict.add_point(
            20.0, 20.0);
        fixed_external_conflict.find_point(fixed_external_point)->fixed = true;
        const auto fixed_external_before = fixed_external_conflict;
        bool fixed_external_rejected = false;
        try {
            static_cast<void>(fixed_external_conflict.add_point_reference_constraint(
                fixed_external_point, external_point_id));
        } catch (const std::runtime_error&) {
            fixed_external_rejected = true;
        }
        require(fixed_external_rejected &&
                    fixed_external_conflict.points == fixed_external_before.points &&
                    fixed_external_conflict.constraints ==
                        fixed_external_before.constraints,
                "Conflicting fixed external relation partially changed the Sketch");
        zima::kernel::ViewerReferenceGeometry refreshed_sources;
        refreshed_sources.edges.push_back({
            {{4.0, 5.0, 0.0}, {9.0, 5.0, 0.0}},
            {"container-source", "edge:stable-source", {}}, false, false});
        refreshed_sources.points.push_back({
            {6.0, 7.0, 0.0},
            {"container-source", "vertex:stable-source", {}}});
        refreshed_sources.axes.push_back({
            {3.0, 8.0, 0.0}, {2.0, 0.0, 0.0}, 20.0,
            {"container-source", "axis:stable-source", {}}});
        zima::kernel::ViewerReferenceGeometry face_with_hole;
        face_with_hole.vertices = {
            {-2.0, -2.0, 0.0}, {2.0, -2.0, 0.0},
            {2.0, 2.0, 0.0}, {-2.0, 2.0, 0.0},
            {-1.0, -1.0, 0.0}, {1.0, -1.0, 0.0},
            {1.0, 1.0, 0.0}, {-1.0, 1.0, 0.0}};
        face_with_hole.triangles = {
            0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5,
            2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7};
        face_with_hole.triangle_references.assign(8,
            {"container-source", "face:stable-source", {}});
        const auto projected_face_with_hole = text_sketch.project_external_face(
            face_with_hole, {"container-source", "face:stable-source", {}});
        require(projected_face_with_hole &&
                    projected_face_with_hole->size() == 2 &&
                    projected_face_with_hole->front().front() ==
                        projected_face_with_hole->front().back() &&
                    projected_face_with_hole->back().front() ==
                        projected_face_with_hole->back().back(),
                "External face projection lost a deterministic inner boundary");
        auto ambiguous_face_geometry = face_with_hole;
        ambiguous_face_geometry.vertices.insert(
            ambiguous_face_geometry.vertices.end(), {
                {5.0, 0.0, 0.0}, {7.0, 0.0, 0.0},
                {7.0, 2.0, 0.0}, {5.0, 2.0, 0.0}});
        ambiguous_face_geometry.triangles.insert(
            ambiguous_face_geometry.triangles.end(), {8, 9, 10, 8, 10, 11});
        ambiguous_face_geometry.triangle_references.insert(
            ambiguous_face_geometry.triangle_references.end(), 2,
            {"container-source", "face:stable-source", {}});
        require(!text_sketch.project_external_face(
                    ambiguous_face_geometry,
                    {"container-source", "face:stable-source", {}}),
                "Disconnected faces with one identity were guessed as one reference");
        refreshed_sources.vertices = {
            {2.0, -3.0, -2.0}, {2.0, 3.0, -2.0},
            {2.0, 3.0, 2.0}, {2.0, -3.0, 2.0}};
        refreshed_sources.triangles = {0, 1, 2, 0, 2, 3};
        refreshed_sources.triangle_references.assign(2,
            {"container-source", "face:stable-source", {}});
        require(text_sketch.refresh_external_references(
                    "part-source", refreshed_sources) &&
                    text_sketch.external_references[0].cached_points ==
                        std::vector<std::array<double, 2>>{{4.0, 5.0}, {9.0, 5.0}} &&
                    text_sketch.external_references[1].cached_points ==
                        std::vector<std::array<double, 2>>{{6.0, 7.0}} &&
                    text_sketch.external_references[2].cached_points ==
                        std::vector<std::array<double, 2>>{{-7.0, 8.0}, {13.0, 8.0}} &&
                    text_sketch.external_references[3].cached_points.size() == 2 &&
                    text_sketch.external_references[3].infinite &&
                    !text_sketch.external_references[0].broken &&
                    !text_sketch.external_references[1].broken &&
                    !text_sketch.external_references[2].broken &&
                    !text_sketch.external_references[3].broken,
                "Explicit Sketch external reference refresh lost exact source identity");
        require(text_sketch.find_point(externally_coincident_point)->x == 6.0 &&
                    text_sketch.find_point(externally_coincident_point)->y == 7.0 &&
                    std::abs(std::hypot(
                        text_sketch.find_point(externally_dimensioned_point)->x - 6.0,
                        text_sketch.find_point(externally_dimensioned_point)->y - 7.0) - 5.0) <
                        1.0e-8,
                "Explicit refresh did not regenerate external constraints and dimensions");
        const auto valid_external_cache = text_sketch.external_references;
        const zima::kernel::ViewerReferenceGeometry missing_sources;
        require(text_sketch.refresh_external_references(
                    "part-source", missing_sources) &&
                    text_sketch.external_references[0].broken &&
                    text_sketch.external_references[1].broken &&
                    text_sketch.external_references[2].broken &&
                    text_sketch.external_references[3].broken &&
                    text_sketch.external_references[0].cached_points ==
                        valid_external_cache[0].cached_points &&
                    text_sketch.external_references[1].cached_points ==
                        valid_external_cache[1].cached_points &&
                    text_sketch.external_references[2].cached_points ==
                        valid_external_cache[2].cached_points &&
                    text_sketch.external_references[3].cached_points ==
                        valid_external_cache[3].cached_points &&
                    !text_sketch.refresh_external_references(
                        "part-source", missing_sources),
                "Broken external references did not preserve their last valid cache");
        require(text_sketch.refresh_external_references(
                    "part-source", refreshed_sources) &&
                    !text_sketch.external_references[0].broken &&
                    !text_sketch.external_references[1].broken &&
                    !text_sketch.external_references[2].broken &&
                    !text_sketch.external_references[3].broken,
                "Restored external references did not recover deterministically");
        auto ambiguous_sources = refreshed_sources;
        ambiguous_sources.edges.push_back(refreshed_sources.edges.front());
        require(text_sketch.refresh_external_references(
                    "part-source", ambiguous_sources) &&
                    text_sketch.external_references[0].broken &&
                    !text_sketch.external_references[1].broken &&
                    !text_sketch.external_references[2].broken &&
                    !text_sketch.external_references[3].broken &&
                    text_sketch.external_references[0].cached_points ==
                        valid_external_cache[0].cached_points,
                "Ambiguous external edge identity was guessed instead of broken");
        require(text_sketch.refresh_external_references(
                    "part-source", refreshed_sources),
                "External edge did not recover after ambiguity was removed");
        auto perpendicular_axis_sources = refreshed_sources;
        perpendicular_axis_sources.axes.front().direction = {0.0, 0.0, 1.0};
        require(text_sketch.refresh_external_references(
                    "part-source", perpendicular_axis_sources) &&
                    text_sketch.external_references[2].broken &&
                    text_sketch.external_references[2].cached_points ==
                        valid_external_cache[2].cached_points,
                "Degenerate external axis projection did not preserve its cache");
        require(text_sketch.refresh_external_references(
                    "part-source", refreshed_sources) &&
                    !text_sketch.external_references[2].broken,
                "External axis did not recover after its projection became valid");
        auto edge_on_face_sources = refreshed_sources;
        edge_on_face_sources.vertices = {
            {-2.0, -2.0, 0.0}, {2.0, -2.0, 0.0},
            {2.0, 2.0, 0.0}, {-2.0, 2.0, 0.0}};
        require(text_sketch.refresh_external_references(
                    "part-source", edge_on_face_sources) &&
                    text_sketch.external_references[3].broken &&
                    text_sketch.external_references[3].cached_points ==
                        valid_external_cache[3].cached_points,
                "Degenerate external face projection did not preserve its cache");
        require(text_sketch.refresh_external_references(
                    "part-source", refreshed_sources) &&
                    !text_sketch.external_references[3].broken,
                "External face did not recover after its projection became valid");
        const auto external_candidates = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                text_sketch.viewer_mesh(), {4.0, 5.0, 10.0},
                {0.0, 0.0, -1.0}, 0.2),
            {zima::viewer::CandidateKind::SketchExternalReference});
        require(external_candidates.size() == 1 &&
                    external_candidates.front().semantic_key ==
                        "external_edge:" + external_edge_id,
                "Sketch external edge did not use the common viewer candidate list");
        const auto external_axis_candidates = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                text_sketch.viewer_mesh(), {3.0, 8.0, 10.0},
                {0.0, 0.0, -1.0}, 0.2),
            {zima::viewer::CandidateKind::SketchExternalReference});
        require(external_axis_candidates.size() == 1 &&
                    external_axis_candidates.front().semantic_key ==
                        "external_axis:" + external_axis_id,
                "Sketch external axis did not use the common viewer candidate list");
        const auto external_face_candidates = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                text_sketch.viewer_mesh(), {2.0, 0.0, 10.0},
                {0.0, 0.0, -1.0}, 0.2),
            {zima::viewer::CandidateKind::SketchExternalReference});
        require(external_face_candidates.size() == 1 &&
                    external_face_candidates.front().semantic_key ==
                        "external_face:" + external_face_id,
                "Sketch external face did not use the common viewer candidate list");
        const auto before_invalid_face = text_sketch.serialized();
        bool invalid_face_rejected = false;
        try {
            external_face.id.clear();
            external_face.source_semantic_key = "face:open-path";
            external_face.cached_points.pop_back();
            text_sketch.add_external_reference(std::move(external_face));
        } catch (const std::runtime_error&) {
            invalid_face_rejected = true;
        }
        require(invalid_face_rejected &&
                    text_sketch.serialized() == before_invalid_face,
                "Invalid external face path partially changed the Sketch");
        const auto before_duplicate_reference = text_sketch.serialized();
        bool duplicate_reference_rejected = false;
        try {
            external_edge.id.clear();
            text_sketch.add_external_reference(std::move(external_edge));
        } catch (const std::invalid_argument&) {
            duplicate_reference_rejected = true;
        }
        require(duplicate_reference_rejected &&
                    text_sketch.serialized() == before_duplicate_reference,
                "Duplicate external reference partially changed the Sketch");
        text_sketch.remove_geometry(external_edge_id);
        require(text_sketch.external_references.size() == 3,
                "External reference was not removed as one Sketch entity");

        const auto xy_hit = sketch.intersect_ray({4.0, 7.0, 10.0}, {0.0, 0.0, -1.0});
        require(xy_hit && std::abs((*xy_hit)[0] - 4.0) < 1.0e-9 &&
                    std::abs((*xy_hit)[1] - 7.0) < 1.0e-9,
                "XY viewer ray did not project into Sketch coordinates");
        sketch.plane = zima::sketcher::SketchPlane::XZ;
        sketch.plane_offset = 3.0;
        const auto xz_hit = sketch.intersect_ray({4.0, 20.0, -7.0}, {0.0, -1.0, 0.0});
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
        auto placed_sketch = zima::sketcher::Sketch::create_default();
        placed_sketch.owner_container_id = "placed-sketch";
        placed_sketch.resolved_origin = {2.0, 3.0, 4.0};
        placed_sketch.resolved_x_axis = {1.0, 0.0, 0.0};
        placed_sketch.resolved_y_axis = {0.0, 0.0, 1.0};
        placed_sketch.resolved_normal = {0.0, -1.0, 0.0};
        const auto [snap_origin, snap_direction] =
            placed_sketch.normal_ray(4.0, 7.0);
        const auto placed_snap = placed_sketch.intersect_ray(
            snap_origin, snap_direction);
        require(placed_snap && std::abs((*placed_snap)[0] - 4.0) < 1.0e-9 &&
                    std::abs((*placed_snap)[1] - 7.0) < 1.0e-9,
                "Sketch snap ray ignored the resolved placed-plane normal");
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
        auto solver_stress = zima::sketcher::Sketch::create_default();
        for (int index = 0; index < 40; ++index) {
            auto anchor = zima::sketcher::Sketch::create_point(
                index * 3.0, index * 2.0);
            anchor.fixed = true;
            auto moving = zima::sketcher::Sketch::create_point(
                index * 3.0 + 3.0, index * 2.0 + 0.75);
            const auto anchor_id = anchor.id;
            const auto moving_id = moving.id;
            solver_stress.points.push_back(std::move(anchor));
            solver_stress.points.push_back(std::move(moving));
            auto segment = zima::sketcher::Sketch::create_segment(
                anchor_id, moving_id);
            const auto segment_id = segment.id;
            solver_stress.segments.push_back(std::move(segment));
            solver_stress.constraints.push_back({
                "stress-h:" + std::to_string(index),
                zima::sketcher::ConstraintKind::Horizontal,
                anchor_id, moving_id, false, segment_id});
            solver_stress.dimensions.push_back({
                "stress-d:" + std::to_string(index), DimensionKind::Distance,
                anchor_id, moving_id, 5.0});
        }
        const auto stress_started = std::chrono::steady_clock::now();
        const auto stress_result = solver_stress.solve();
        const auto stress_elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - stress_started);
        require(stress_result.status == zima::sketcher::SolveStatus::Solved &&
                    stress_result.remaining_degrees_of_freedom == 0 &&
                    stress_result.maximum_residual < 1.0e-7 &&
                    stress_elapsed < std::chrono::seconds(2),
                "Solver stress matrix was slow, inaccurate, or reported wrong DOF");
        auto point_tools = zima::sketcher::Sketch::create_default();
        const auto standalone_point = point_tools.add_point(100.0, 100.0);
        const auto snapped_point = point_tools.add_point(100.0 + 1.0e-8, 100.0);
        require(snapped_point == standalone_point && point_tools.points.size() == 1,
                "Standalone point creation did not use the common snap tolerance");
        point_tools.set_geometry_construction(standalone_point, true);
        require(point_tools.find_point(standalone_point)->construction,
                "Existing Sketch point could not be converted to construction geometry");
        const auto construction_segment = point_tools.add_segment(
            0.0, 0.0, 10.0, 0.0, 1.0e-6, true);
        point_tools.set_geometry_construction(construction_segment, false);
        require(!std::find_if(point_tools.segments.begin(), point_tools.segments.end(),
                    [&](const auto& value) {
                        return value.id == construction_segment;
                    })->construction,
                "Existing construction segment could not return to profile geometry");
        point_tools.set_geometry_construction(construction_segment, true);
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
        const auto constrained_packet = loaded_point_tools.viewer_mesh();
        const auto construction_point_packet = std::find_if(
            constrained_packet.points.begin(), constrained_packet.points.end(),
            [&](const auto& point) {
                return point.reference.semantic_key == "point:" + standalone_point;
            });
        require(loaded_point_tools.find_point(standalone_point) != nullptr &&
                    loaded_point_tools.find_point(standalone_point)->construction &&
                    construction_point_packet != constrained_packet.points.end() &&
                    construction_point_packet->construction &&
                    loaded_point_tools.segments.size() == 1 &&
                    loaded_point_tools.segments.front().construction &&
                    constrained_packet.edges.front().construction &&
                    constrained_packet.edges.front().overlay,
                "Standalone point or construction segment did not survive serialization");
        require(constrained_packet.constraint_markers.size() == 1 &&
                    constrained_packet.constraint_markers.front().label == "H" &&
                    constrained_packet.constraint_markers.front().reference.semantic_key ==
                        "constraint:" + loaded_point_tools.constraints.front().id &&
                    constrained_packet.constraint_markers.front()
                        .participant_semantic_keys ==
                        std::vector<std::string>{
                            "point:" + loaded_point_tools.segments.front().first_point_id,
                            "point:" + loaded_point_tools.segments.front().second_point_id},
                "Sketch constraint did not publish its persistent View marker");
        require(zima::sketcher::constraint_marker_label(
                    zima::sketcher::ConstraintKind::Horizontal) == "H" &&
                    zima::sketcher::constraint_marker_label(
                        zima::sketcher::ConstraintKind::PointReference) == "K" &&
                    zima::sketcher::constraint_marker_label(
                        zima::sketcher::ConstraintKind::PointOnCircle) == "C" &&
                    zima::sketcher::constraint_marker_label(
                        zima::sketcher::ConstraintKind::Coincident).empty(),
                "Sketch relation list and View no longer share one marker vocabulary");
        auto coincident_marker_sketch = zima::sketcher::Sketch::create_default();
        const auto coincident_first_segment =
            coincident_marker_sketch.add_segment(0.0, 0.0, 5.0, 0.0);
        const auto coincident_second_segment =
            coincident_marker_sketch.add_segment(7.0, 2.0, 12.0, 2.0);
        const auto coincident_marker_first = coincident_marker_sketch.segments[0];
        const auto coincident_marker_second = coincident_marker_sketch.segments[1];
        const auto merged_marker_point = coincident_marker_sketch.merge_points(
            coincident_marker_first.second_point_id,
            coincident_marker_second.first_point_id);
        require(coincident_marker_sketch.points.size() == 3 &&
                    coincident_marker_sketch.constraints.empty() &&
                    coincident_marker_sketch.segments[0].second_point_id ==
                        merged_marker_point &&
                    coincident_marker_sketch.segments[1].first_point_id ==
                        merged_marker_point &&
                    coincident_marker_sketch.viewer_mesh()
                        .constraint_markers.empty(),
                "Point merge did not create one unmarked topology vertex");
        auto origin_marker_sketch = zima::sketcher::Sketch::create_default();
        origin_marker_sketch.owner_container_id = "origin-marker-extrusion";
        origin_marker_sketch.resolved_origin = {11.0, -7.0, 3.0};
        const auto marker_origin_bound_point =
            origin_marker_sketch.add_point(4.0, 5.0);
        const auto origin_constraint =
            origin_marker_sketch.add_point_reference_constraint(
            marker_origin_bound_point, "sketch_origin");
        const auto origin_marker_packet = origin_marker_sketch.viewer_mesh();
        const auto origin_marker = std::find_if(
            origin_marker_packet.constraint_markers.begin(),
            origin_marker_packet.constraint_markers.end(), [&](const auto& marker) {
                return marker.reference.semantic_key ==
                    "constraint:" + origin_constraint;
            });
        require(origin_marker == origin_marker_packet.constraint_markers.end() &&
                    origin_marker_sketch.constraints.size() == 1 &&
                    origin_marker_sketch.constraints.front().kind ==
                        zima::sketcher::ConstraintKind::PointReference &&
                    std::abs(origin_marker_sketch.find_point(
                        marker_origin_bound_point)->x) < 1.0e-9 &&
                    std::abs(origin_marker_sketch.find_point(
                        marker_origin_bound_point)->y) < 1.0e-9,
                "Origin point reference was not solved or leaked a C marker");
        auto keypoint_marker_sketch = zima::sketcher::Sketch::create_default();
        const auto keypoint_circle =
            keypoint_marker_sketch.add_circle(0.0, 0.0, 5.0);
        const auto keypoint_segment =
            keypoint_marker_sketch.add_segment(5.0, 0.0, 9.0, 0.0);
        const auto keypoint_contact =
            keypoint_marker_sketch.segments.front().first_point_id;
        const auto keypoint_constraint =
            keypoint_marker_sketch.add_point_reference_constraint(
                keypoint_contact,
                "sketch_keypoint:circle:" + keypoint_circle + ":0");
        const auto keypoint_markers =
            keypoint_marker_sketch.viewer_mesh().constraint_markers;
        require(keypoint_markers.size() == 1 &&
                    keypoint_markers.front().label == "K" &&
                    keypoint_markers.front().reference.semantic_key ==
                        "constraint:" + keypoint_constraint &&
                    std::ranges::find(
                        keypoint_markers.front().participant_semantic_keys,
                        "circle:" + keypoint_circle) !=
                        keypoint_markers.front().participant_semantic_keys.end() &&
                    std::ranges::find(
                        keypoint_markers.front().participant_semantic_keys,
                        "segment:" + keypoint_segment) !=
                        keypoint_markers.front().participant_semantic_keys.end(),
                "Exact curve keypoint reference did not expose one K marker");
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
                    projected_mesh.dimensions[0].label_prefix.empty() &&
                    projected_mesh.dimensions[1].label_prefix.empty() &&
                    std::abs(projected_mesh.dimensions[0].line_first.y -
                        projected_mesh.dimensions[0].line_second.y) < 1.0e-9 &&
                    std::abs(projected_mesh.dimensions[1].line_first.x -
                        projected_mesh.dimensions[1].line_second.x) < 1.0e-9,
                "Projected dimensions did not create axis-aligned viewer geometry");
        require(projected.set_dimension_placement(
                    projected.dimensions.front().id, 4.0, 20.0),
                "Dimension placement edit was rejected");
        const auto placed_dimension_mesh = projected.viewer_mesh();
        require(std::abs(placed_dimension_mesh.dimensions.front().line_first.y -
                    projected.world_point(0.0, 20.0).y) < 1.0e-9,
                "Dragged horizontal dimension did not use persisted placement");
        const auto loaded_placed_dimensions = zima::sketcher::Sketch::from_serialized(
            projected.serialized());
        require(loaded_placed_dimensions.dimensions == projected.dimensions,
                "Dimension placement did not survive persistence");
        auto axis_dimensioned = zima::sketcher::Sketch::create_default();
        const auto axis_point = axis_dimensioned.add_point(6.0, -4.0);
        auto from_y_axis = axis_dimensioned.create_axis_dimension(
            axis_point, "sketch_axis:y");
        from_y_axis.value = 9.0;
        axis_dimensioned.apply_dimension(from_y_axis);
        auto from_x_axis = axis_dimensioned.create_axis_dimension(
            axis_point, "sketch_axis:x");
        from_x_axis.value = -7.0;
        axis_dimensioned.apply_dimension(from_x_axis);
        require(std::abs(axis_dimensioned.find_point(axis_point)->x - 9.0) < 1.0e-7 &&
                    std::abs(axis_dimensioned.find_point(axis_point)->y + 7.0) < 1.0e-7 &&
                    axis_dimensioned.viewer_mesh().dimensions.size() == 2 &&
                    axis_dimensioned.dimensions[0].first_point_id == axis_point &&
                    axis_dimensioned.dimensions[0].second_point_id.empty() &&
                    axis_dimensioned.dimensions[0].geometry_id == "sketch_axis:y" &&
                    axis_dimensioned.dimensions[1].first_point_id == axis_point &&
                    axis_dimensioned.dimensions[1].second_point_id.empty() &&
                    axis_dimensioned.dimensions[1].geometry_id == "sketch_axis:x",
                "Point-to-Sketch-axis dimensions did not preserve their exact axes");
        const auto edited_axis_dimension_id = axis_dimensioned.dimensions.front().id;
        require(axis_dimensioned.set_dimension_value(
                    edited_axis_dimension_id, 14.0) &&
                    axis_dimensioned.dimensions.size() == 2 &&
                    axis_dimensioned.viewer_mesh().dimensions.size() == 2 &&
                    std::abs(axis_dimensioned.find_point(axis_point)->x - 14.0) < 1.0e-7,
                "Editing a point offset from the Sketch origin hid its dimension");
        const auto loaded_axis_dimensions = zima::sketcher::Sketch::from_serialized(
            axis_dimensioned.serialized());
        require(loaded_axis_dimensions.dimensions == axis_dimensioned.dimensions,
                "Point-to-axis dimensions did not survive persistence");
        auto zero_axis_dimension = zima::sketcher::Sketch::create_default();
        const auto zero_axis_point = zero_axis_dimension.add_point(0.0, 0.0);
        auto zero_x_coordinate = zero_axis_dimension.create_axis_dimension(
            zero_axis_point, "sketch_axis:y");
        zero_axis_dimension.apply_dimension(zero_x_coordinate);
        const auto zero_axis_view = zero_axis_dimension.viewer_mesh();
        require(zero_axis_view.dimensions.size() == 1 &&
                    zero_axis_dimension.dimensions.front().geometry_id ==
                        "sketch_axis:y" &&
                    zero_axis_dimension.dimensions.front().second_point_id.empty() &&
                    std::ranges::find(
                        zero_axis_view.dimensions.front().participant_semantic_keys,
                        "sketch_axis:y") !=
                        zero_axis_view.dimensions.front()
                            .participant_semantic_keys.end() &&
                    std::ranges::find(
                        zero_axis_view.dimensions.front().participant_semantic_keys,
                        "origin:point") ==
                        zero_axis_view.dimensions.front()
                            .participant_semantic_keys.end(),
                "Zero coordinate dimension was disguised as an origin-point reference");
        require(zero_axis_dimension.set_dimension_value(
                    zero_x_coordinate.id, 6.0) &&
                    std::abs(zero_axis_dimension.find_point(
                        zero_axis_point)->x - 6.0) < 1.0e-8,
                "Zero coordinate-axis dimension could not drive its referenced point");
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
        auto parallel_dimensioned = zima::sketcher::Sketch::create_default();
        const auto parallel_reference = parallel_dimensioned.add_segment(
            0.0, 0.0, 20.0, 0.0, 1.0e-6, true);
        const auto parallel_driven = parallel_dimensioned.add_segment(
            2.0, 5.0, 12.0, 5.0);
        auto line_distance = parallel_dimensioned.create_line_pair_dimension(
            parallel_reference, parallel_driven,
            zima::sketcher::DimensionKind::DistanceLine);
        line_distance.value = 8.0;
        parallel_dimensioned.apply_dimension(line_distance);
        const auto* parallel_first = parallel_dimensioned.find_point(
            parallel_dimensioned.segments[1].first_point_id);
        const auto* parallel_second = parallel_dimensioned.find_point(
            parallel_dimensioned.segments[1].second_point_id);
        require(std::abs(parallel_first->y - 8.0) < 1.0e-7 &&
                    std::abs(parallel_second->y - 8.0) < 1.0e-7 &&
                    parallel_dimensioned.viewer_mesh().dimensions.size() == 1 &&
                    parallel_dimensioned.dimensions.front().solution_side == 1,
                "Parallel-line distance did not translate the driven line");
        require(parallel_dimensioned.set_dimension_placement(
                    line_distance.id, 4.0, 11.0) &&
                    parallel_dimensioned.dimensions.front().solution_side == 1,
                "Moving a line-distance label changed its solution branch");
        require(parallel_dimensioned.set_dimension_value(
                    line_distance.id, -6.0) &&
                    std::abs(parallel_dimensioned.find_point(
                        parallel_dimensioned.segments[1].first_point_id)->y +
                        6.0) < 1.0e-7 &&
                    std::abs(parallel_dimensioned.find_point(
                        parallel_dimensioned.segments[1].second_point_id)->y +
                        6.0) < 1.0e-7 &&
                    parallel_dimensioned.dimensions.front().value == 6.0 &&
                    parallel_dimensioned.dimensions.front().solution_side == -1 &&
                    parallel_dimensioned.viewer_mesh().dimensions.front().value ==
                        6.0,
                "Negative line-distance input did not flip to the opposite side");
        require(parallel_dimensioned.set_dimension_value(
                    line_distance.id, 4.0) &&
                    std::abs(parallel_dimensioned.find_point(
                        parallel_dimensioned.segments[1].first_point_id)->y +
                        4.0) < 1.0e-7 &&
                    parallel_dimensioned.dimensions.front().solution_side == -1,
                "Positive line-distance edit unexpectedly changed its side");
        require(parallel_dimensioned.set_dimension_value(
                    line_distance.id, -3.0) &&
                    std::abs(parallel_dimensioned.find_point(
                        parallel_dimensioned.segments[1].first_point_id)->y -
                        3.0) < 1.0e-7 &&
                    parallel_dimensioned.dimensions.front().value == 3.0 &&
                    parallel_dimensioned.dimensions.front().solution_side == 1,
                "Repeated negative line-distance input did not flip the branch again");
        const auto loaded_line_distance = zima::sketcher::Sketch::from_serialized(
            parallel_dimensioned.serialized());
        require(loaded_line_distance.dimensions == parallel_dimensioned.dimensions,
                "Line-pair distance ownership did not survive serialization");
        parallel_dimensioned.remove_geometry(parallel_reference);
        require(parallel_dimensioned.dimensions.empty(),
                "Deleting a line did not remove its line-pair dimension");

        auto point_line_dimensioned = zima::sketcher::Sketch::create_default();
        const auto point_line_reference = point_line_dimensioned.add_segment(
            -10.0, 0.0, 10.0, 0.0);
        const auto point_line_point = point_line_dimensioned.add_point(3.0, 5.0);
        const auto reference_before = std::array{
            *point_line_dimensioned.find_point(
                point_line_dimensioned.segments.front().first_point_id),
            *point_line_dimensioned.find_point(
                point_line_dimensioned.segments.front().second_point_id)};
        auto point_line_dimension =
            point_line_dimensioned.create_point_line_dimension(
                point_line_point, point_line_reference);
        point_line_dimension.value = 8.0;
        point_line_dimensioned.apply_dimension(point_line_dimension);
        const auto* point_line_solved =
            point_line_dimensioned.find_point(point_line_point);
        require(point_line_solved != nullptr &&
                    std::abs(point_line_solved->x - 3.0) < 1.0e-8 &&
                    std::abs(point_line_solved->y - 8.0) < 1.0e-8 &&
                    *point_line_dimensioned.find_point(
                        point_line_dimensioned.segments.front().first_point_id) ==
                        reference_before[0] &&
                    *point_line_dimensioned.find_point(
                        point_line_dimensioned.segments.front().second_point_id) ==
                        reference_before[1] &&
                    point_line_dimensioned.viewer_mesh().dimensions.size() == 1 &&
                    point_line_dimensioned.dimensions.front().solution_side == 1,
                "Point-line distance did not move only the driven point normally");
        require(point_line_dimensioned.set_dimension_placement(
                    point_line_dimension.id, 6.0, 10.0) &&
                    point_line_dimensioned.dimensions.front().solution_side == 1,
                "Moving a point-line label changed its solution branch");
        require(point_line_dimensioned.set_dimension_value(
                    point_line_dimension.id, -20.0) &&
                    std::abs(point_line_dimensioned.find_point(
                        point_line_point)->y + 20.0) < 1.0e-7 &&
                    point_line_dimensioned.dimensions.front().value == 20.0 &&
                    point_line_dimensioned.dimensions.front().solution_side == -1 &&
                    point_line_dimensioned.viewer_mesh().dimensions.front().value ==
                        20.0,
                "Negative point-line input did not flip to the opposite side");
        require(point_line_dimensioned.set_dimension_value(
                    point_line_dimension.id, 15.0) &&
                    std::abs(point_line_dimensioned.find_point(
                        point_line_point)->y + 15.0) < 1.0e-7 &&
                    point_line_dimensioned.dimensions.front().solution_side == -1,
                "Positive point-line edit unexpectedly changed its side");
        require(point_line_dimensioned.set_dimension_value(
                    point_line_dimension.id, -12.0) &&
                    std::abs(point_line_dimensioned.find_point(
                        point_line_point)->y - 12.0) < 1.0e-7 &&
                    point_line_dimensioned.dimensions.front().value == 12.0 &&
                    point_line_dimensioned.dimensions.front().solution_side == 1,
                "Repeated negative point-line input did not flip the branch again");
        auto loaded_point_line_dimension =
            zima::sketcher::Sketch::from_serialized(
                point_line_dimensioned.serialized());
        require(loaded_point_line_dimension.dimensions ==
                    point_line_dimensioned.dimensions &&
                    loaded_point_line_dimension.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
                "Point-line distance did not survive persistence and solve");
        auto point_axis_dimensioned = zima::sketcher::Sketch::create_default();
        const auto point_axis_point = point_axis_dimensioned.add_point(-4.0, 3.0);
        auto point_axis_dimension =
            point_axis_dimensioned.create_point_line_dimension(
                point_axis_point, "sketch_axis:y");
        point_axis_dimension.value = 7.0;
        point_axis_dimensioned.apply_dimension(point_axis_dimension);
        require(std::abs(point_axis_dimensioned.find_point(
                    point_axis_point)->x + 7.0) < 1.0e-8,
                "Point-line distance did not support a built-in Sketch axis");
        auto signed_coordinate = zima::sketcher::Sketch::create_default();
        const auto signed_coordinate_point = signed_coordinate.add_point(-4.0, 3.0);
        auto signed_coordinate_dimension = signed_coordinate.create_axis_dimension(
            signed_coordinate_point, "sketch_axis:y");
        require(signed_coordinate_dimension.value == -4.0,
                "Coordinate dimension lost its genuine signed value");
        signed_coordinate.apply_dimension(signed_coordinate_dimension);
        require(signed_coordinate.set_dimension_value(
                    signed_coordinate_dimension.id, -7.0) &&
                    std::abs(signed_coordinate.find_point(
                        signed_coordinate_point)->x + 7.0) < 1.0e-8 &&
                    signed_coordinate.dimensions.front().value == -7.0,
                "Negative coordinate dimension was mistaken for a side flip");
        auto symmetric_dimensioned = zima::sketcher::Sketch::create_default();
        const auto symmetric_first = symmetric_dimensioned.add_point(0.0, 4.0);
        const auto symmetric_second = symmetric_dimensioned.add_point(10.0, 4.0);
        auto symmetric_dimension = symmetric_dimensioned.create_symmetric_dimension(
            symmetric_first, symmetric_second, "sketch_axis:x");
        symmetric_dimension.value = 20.0;
        symmetric_dimensioned.apply_dimension(symmetric_dimension);
        require(std::abs(symmetric_dimensioned.find_point(
                    symmetric_first)->y - 10.0) < 1.0e-8 &&
                    std::abs(symmetric_dimensioned.find_point(
                    symmetric_second)->y - 10.0) < 1.0e-8 &&
                    symmetric_dimensioned.viewer_mesh().dimensions.size() == 1 &&
                    symmetric_dimensioned.viewer_mesh().dimensions.front().label_prefix ==
                        "Ø" &&
                    symmetric_dimensioned.viewer_mesh().dimensions.front()
                        .participant_semantic_keys.size() == 3,
                "Symmetric dimension did not use its full diameter value");
        auto construction_symmetric = zima::sketcher::Sketch::create_default();
        const auto construction_axis = construction_symmetric.add_segment(
            2.0, 0.0, 2.0, 10.0, 1.0e-6, true);
        const auto construction_symmetric_target =
            construction_symmetric.add_point(5.0, 4.0);
        auto construction_diameter = construction_symmetric.create_symmetric_dimension(
            construction_symmetric_target, {}, construction_axis);
        construction_diameter.value = 10.0;
        construction_symmetric.apply_dimension(construction_diameter);
        require(std::abs(construction_symmetric.find_point(
                    construction_symmetric_target)->x - 7.0) < 1.0e-8 &&
                    zima::sketcher::Sketch::from_serialized(
                        construction_symmetric.serialized()).dimensions ==
                        construction_symmetric.dimensions,
                "Symmetric dimension did not support a construction axis or persistence");

        auto three_point_angle = zima::sketcher::Sketch::create_default();
        const auto three_angle_first = three_point_angle.add_point(1.0, 2.0);
        const auto three_angle_vertex = three_point_angle.add_point(0.0, 2.0);
        const auto three_angle_second = three_point_angle.add_point(1.0, 3.0);
        auto three_point_dimension =
            three_point_angle.create_three_point_angle_dimension(
                three_angle_first, three_angle_vertex, three_angle_second);
        three_point_dimension.value = 90.0;
        three_point_angle.apply_dimension(three_point_dimension);
        const auto* solved_angle_second =
            three_point_angle.find_point(three_angle_second);
        require(std::abs(solved_angle_second->x) < 1.0e-8 &&
                    std::abs(solved_angle_second->y -
                        (2.0 + std::sqrt(2.0))) < 1.0e-8 &&
                    three_point_angle.viewer_mesh().dimensions.size() == 1 &&
                    three_point_angle.viewer_mesh().dimensions.front().kind ==
                        zima::kernel::ViewerDimensionKind::Angular &&
                    three_point_angle.viewer_mesh().dimensions.front()
                        .participant_semantic_keys ==
                        std::vector<std::string>{
                            "point:" + three_angle_first,
                            "point:" + three_angle_vertex,
                            "point:" + three_angle_second},
                "Three-point angle dimension did not solve or display");
        for (const double equivalent_angle : {270.0, -90.0, -270.0}) {
            auto equivalent = zima::sketcher::Sketch::create_default();
            const auto first = equivalent.add_point(1.0, 2.0);
            const auto vertex = equivalent.add_point(0.0, 2.0);
            const auto second = equivalent.add_point(0.0, 3.0);
            auto dimension = equivalent.create_three_point_angle_dimension(
                first, vertex, second);
            dimension.value = equivalent_angle;
            equivalent.apply_dimension(dimension);
            require(equivalent.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting &&
                    zima::sketcher::Sketch::from_serialized(
                        equivalent.serialized()).dimensions == equivalent.dimensions,
                "Reflex and negative three-point angles are not geometrically equivalent");
        }
        auto point_directional = zima::sketcher::Sketch::create_default();
        const auto directional_reference = point_directional.add_point(2.0, 3.0);
        const auto horizontal_driven = point_directional.add_point(8.0, 7.0);
        static_cast<void>(point_directional.add_point_pair_constraint(
            directional_reference, horizontal_driven,
            zima::sketcher::ConstraintKind::Horizontal));
        require(std::abs(point_directional.find_point(horizontal_driven)->y - 3.0) <
                    1.0e-8 &&
                std::abs(point_directional.find_point(directional_reference)->y - 3.0) <
                    1.0e-8,
                "Point-pair horizontal constraint did not keep its reference point");
        const auto vertical_driven = point_directional.add_point(9.0, -2.0);
        static_cast<void>(point_directional.add_point_pair_constraint(
            directional_reference, vertical_driven,
            zima::sketcher::ConstraintKind::Vertical));
        require(std::abs(point_directional.find_point(vertical_driven)->x - 2.0) <
                    1.0e-8 &&
                zima::sketcher::Sketch::from_serialized(
                    point_directional.serialized()).constraints ==
                    point_directional.constraints,
                "Point-pair vertical constraint did not solve or persist");
        const auto directional_view = point_directional.viewer_mesh();
        require(directional_view.constraint_markers.size() == 2 &&
                    std::abs(directional_view.constraint_markers[0].position.x - 8.0) <
                        1.0e-8 &&
                    std::abs(directional_view.constraint_markers[1].position.x - 2.0) <
                        1.0e-8 &&
                    directional_view.constraint_markers[0]
                        .participant_semantic_keys ==
                        std::vector<std::string>{
                            "point:" + directional_reference,
                            "point:" + horizontal_driven},
                "Point-pair directional markers are not anchored at driven points");
        auto redundant_direction = zima::sketcher::Sketch::create_default();
        const auto redundant_first = redundant_direction.add_point(2.0, 2.0);
        const auto redundant_second = redundant_direction.add_point(3.0, 3.0);
        static_cast<void>(redundant_direction.merge_points(
            redundant_first, redundant_second));
        bool rejected_redundant_direction{};
        try {
            static_cast<void>(redundant_direction.add_point_pair_constraint(
                redundant_first, redundant_second,
                zima::sketcher::ConstraintKind::Horizontal));
        } catch (const std::invalid_argument&) {
            rejected_redundant_direction = true;
        }
        require(rejected_redundant_direction &&
                    redundant_direction.constraints.empty() &&
                    redundant_direction.points.size() == 1,
                "Redundant point-pair constraint was not rejected transactionally");
        auto combined_axis_snap = zima::sketcher::Sketch::create_default();
        static_cast<void>(combined_axis_snap.add_segment(3.0, 5.0, 3.0, 0.0));
        const auto combined_first =
            combined_axis_snap.segments.front().first_point_id;
        const auto combined_second =
            combined_axis_snap.segments.front().second_point_id;
        static_cast<void>(combined_axis_snap.add_point_on_line_constraint(
            combined_second, "sketch_axis:x"));
        static_cast<void>(combined_axis_snap.add_point_pair_constraint(
            combined_first, combined_second,
            zima::sketcher::ConstraintKind::Vertical));
        require(combined_axis_snap.constraints.size() == 2 &&
                    std::abs(combined_axis_snap.find_point(
                        combined_second)->y) < 1.0e-10,
                "Axis endpoint did not preserve combined C+V inference");
        auto redundant_dimension = zima::sketcher::Sketch::create_default();
        const auto fixed_dimension_point = redundant_dimension.add_point(5.0, 0.0);
        redundant_dimension.set_point_fixed(fixed_dimension_point, true);
        auto already_fixed_distance = redundant_dimension.create_point_dimension(
            "sketch_origin", fixed_dimension_point);
        bool rejected_redundant_dimension{};
        try {
            redundant_dimension.apply_dimension(already_fixed_distance);
        } catch (const std::invalid_argument&) {
            rejected_redundant_dimension = true;
        }
        require(rejected_redundant_dimension &&
                    redundant_dimension.dimensions.empty(),
                "Satisfied redundant driving dimension was not rejected transactionally");

        auto between_angle = zima::sketcher::Sketch::create_default();
        const auto angle_driven = between_angle.add_segment(0.0, 0.0, 0.0, 10.0);
        auto between = between_angle.create_line_pair_dimension(
            "sketch_axis:x", angle_driven,
            zima::sketcher::DimensionKind::AngleBetween);
        between.value = 35.0;
        between_angle.apply_dimension(between);
        const auto* between_first = between_angle.find_point(
            between_angle.segments.front().first_point_id);
        const auto* between_second = between_angle.find_point(
            between_angle.segments.front().second_point_id);
        require(std::abs(std::abs(std::atan2(
                    between_second->y - between_first->y,
                    between_second->x - between_first->x) *
                    180.0 / 3.14159265358979323846) - 35.0) < 1.0e-7 &&
                    between_angle.viewer_mesh().dimensions.size() == 1,
                "Angle between a line and the Sketch axis was not solved or displayed");
        require(between_angle.set_dimension_placement(between.id, 8.0, 8.0),
                "Angular dimension did not accept interactive placement");
        const auto near_sector = between_angle.viewer_mesh().dimensions.front();
        require(between_angle.set_dimension_placement(between.id, -12.0, -12.0),
                "Angular dimension did not update interactive placement");
        const auto opposite_sector = between_angle.viewer_mesh().dimensions.front();
        require(near_sector.kind == zima::kernel::ViewerDimensionKind::Angular &&
                    opposite_sector.kind ==
                        zima::kernel::ViewerDimensionKind::Angular &&
                    near_sector.line_first.x > 0.0 &&
                    opposite_sector.line_first.x < 0.0 &&
                    std::hypot(opposite_sector.line_first.x,
                        opposite_sector.line_first.y) >
                        std::hypot(near_sector.line_first.x,
                            near_sector.line_first.y),
                "Angular dimension preview did not follow cursor radius and opposite sector");
        auto reversed_axis_angle = zima::sketcher::Sketch::create_default();
        const auto reversed_axis_segment = reversed_axis_angle.add_segment(
            0.0, 0.0, 8.0, 8.0);
        auto reversed_axis_dimension = reversed_axis_angle.create_line_pair_dimension(
            "sketch_axis:x", reversed_axis_segment,
            zima::sketcher::DimensionKind::AngleBetween);
        reversed_axis_dimension.angle_presentation_reversed = true;
        reversed_axis_dimension.angle_sector = 0;
        reversed_axis_dimension.placement = std::array{6.0, -4.0};
        reversed_axis_dimension.value = -35.0;
        reversed_axis_angle.apply_dimension(reversed_axis_dimension);
        require(std::abs(reversed_axis_angle.viewer_mesh().dimensions.front().value +
                    35.0) < 1.0e-7 &&
                    reversed_axis_angle.dimensions.front()
                        .angle_presentation_reversed,
                "Reversed line-axis presentation lost its negative angular value");
        require(reversed_axis_angle.set_dimension_value(
                    reversed_axis_dimension.id, -60.0) &&
                    std::abs(reversed_axis_angle.viewer_mesh().dimensions.front().value +
                        60.0) < 1.0e-7 &&
                    reversed_axis_angle.dimensions.front().placement ==
                        reversed_axis_dimension.placement,
                "Negative line-axis angle could not be edited in presentation order");
        auto duplicate_angle = zima::sketcher::Sketch::create_default();
        const auto duplicate_first = duplicate_angle.add_segment(
            0.0, 0.0, 10.0, 0.0);
        const auto duplicate_second = duplicate_angle.add_segment(
            0.0, 0.0, 4.0, 8.0);
        auto first_angle_driver = duplicate_angle.create_line_pair_dimension(
            duplicate_first, duplicate_second,
            zima::sketcher::DimensionKind::AngleBetween);
        duplicate_angle.apply_dimension(first_angle_driver);
        const auto duplicate_before = duplicate_angle;
        bool rejected_reversed_angle{};
        try {
            auto reversed_angle = duplicate_angle.create_line_pair_dimension(
                duplicate_second, duplicate_first,
                zima::sketcher::DimensionKind::AngleBetween);
            duplicate_angle.apply_dimension(reversed_angle);
        } catch (const std::invalid_argument&) {
            rejected_reversed_angle = true;
        }
        require(rejected_reversed_angle &&
                    duplicate_angle.dimensions == duplicate_before.dimensions &&
                    duplicate_angle.points == duplicate_before.points,
                "Reversed duplicate angular driver was not rejected transactionally");

        auto constrained_angle = zima::sketcher::Sketch::create_default();
        const auto constrained_segment = constrained_angle.add_segment(
            0.0, 4.0, 10.0, 4.0);
        const auto constrained_geometry = constrained_angle.segments.front();
        static_cast<void>(constrained_angle.add_point_pair_constraint(
            constrained_geometry.first_point_id,
            constrained_geometry.second_point_id,
            zima::sketcher::ConstraintKind::Horizontal));
        const auto constrained_before = constrained_angle;
        bool rejected_constraint_implied_angle{};
        try {
            auto implied = constrained_angle.create_line_pair_dimension(
                "sketch_axis:x", constrained_segment,
                zima::sketcher::DimensionKind::AngleBetween);
            constrained_angle.apply_dimension(implied);
        } catch (const std::invalid_argument&) {
            rejected_constraint_implied_angle = true;
        }
        require(rejected_constraint_implied_angle &&
                    constrained_angle.dimensions.empty() &&
                    constrained_angle.points == constrained_before.points &&
                    constrained_angle.constraints == constrained_before.constraints,
                "Constraint-implied angular driver was not rejected transactionally");

        // Regression from Projects/part.prtz: two connected segments, the
        // first dimensioned from the fixed origin and the second endpoint on
        // the X axis.  Editing the existing length must let the angular
        // equation rotate the free second branch instead of reporting a
        // spurious solver conflict.
        auto editable_chain = zima::sketcher::Sketch::create_default();
        const auto chain_first = editable_chain.add_segment(
            0.0, 0.0, 261.996337890625, -318.6537628174258);
        const auto chain_second = editable_chain.add_segment(
            261.996337890625, -318.6537628174258, 576.8365734371087, 0.0);
        const auto chain_geometry = editable_chain.segments;
        static_cast<void>(editable_chain.add_point_reference_constraint(
            chain_geometry.front().first_point_id, "sketch_origin"));
        static_cast<void>(editable_chain.add_point_on_line_constraint(
            chain_geometry.back().second_point_id, "sketch_axis:x"));
        auto chain_length = editable_chain.create_point_dimension(
            chain_geometry.front().first_point_id,
            chain_geometry.front().second_point_id);
        try {
            editable_chain.apply_dimension(chain_length);
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string{
                "part.prtz initial length dimension: "} + error.what());
        }
        auto chain_angle = editable_chain.create_line_pair_dimension(
            chain_first, chain_second,
            zima::sketcher::DimensionKind::AngleBetween);
        chain_angle.value = 95.91794857737798;
        chain_angle.angle_sector = 1;
        try {
            editable_chain.apply_dimension(chain_angle);
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string{
                "part.prtz initial angular dimension: "} + error.what());
        }
        chain_length.value = 350.0;
        try {
            editable_chain.apply_dimension(chain_length);
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string{
                "part.prtz edited length dimension: "} + error.what());
        }
        const auto* edited_chain_first = editable_chain.find_point(
            chain_geometry.front().first_point_id);
        const auto* edited_chain_second = editable_chain.find_point(
            chain_geometry.front().second_point_id);
        require(std::abs(std::hypot(
                    edited_chain_second->x - edited_chain_first->x,
                    edited_chain_second->y - edited_chain_first->y) - 350.0) <
                    1.0e-7,
                "Existing length dimension in the part.prtz chain could not be edited");
        auto shared_angles = zima::sketcher::Sketch::create_default();
        const auto shared_reference = shared_angles.add_segment(
            0.0, 0.0, 10.0, 0.0);
        const auto shared_middle = shared_angles.add_segment(
            0.0, 0.0, 7.66044443118978, 6.42787609686539);
        const auto shared_last = shared_angles.add_segment(
            0.0, 0.0, 0.0, 10.0);
        const auto shared_origin = shared_angles.segments.front().first_point_id;
        const auto shared_reference_end =
            shared_angles.segments.front().second_point_id;
        shared_angles.find_point(shared_origin)->fixed = true;
        shared_angles.find_point(shared_reference_end)->fixed = true;
        for (const auto& segment_id : {shared_middle, shared_last}) {
            auto length = shared_angles.create_segment_dimension(segment_id);
            shared_angles.apply_dimension(std::move(length));
            shared_angles.dimensions.back().locked = true;
        }
        auto shared_first_angle = shared_angles.create_line_pair_dimension(
            shared_reference, shared_middle,
            zima::sketcher::DimensionKind::AngleBetween);
        shared_first_angle.value = 40.0;
        shared_angles.apply_dimension(shared_first_angle);
        auto shared_second_angle = shared_angles.create_line_pair_dimension(
            shared_middle, shared_last,
            zima::sketcher::DimensionKind::AngleBetween);
        shared_second_angle.value = 50.0;
        shared_angles.apply_dimension(shared_second_angle);
        const auto shared_before = shared_angles;
        require(shared_angles.set_dimension_value(shared_first_angle.id, 30.0) &&
                    shared_angles.solve().maximum_residual < 1.0e-7,
                "First of two shared angular dimensions could not be edited");
        require(shared_angles.set_dimension_value(shared_second_angle.id, 70.0) &&
                    shared_angles.solve().maximum_residual < 1.0e-7,
                "Second of two shared angular dimensions could not be edited");
        require(shared_angles.set_dimension_value(shared_first_angle.id, 40.0) &&
                    shared_angles.set_dimension_value(shared_second_angle.id, 50.0) &&
                    shared_angles.solve().maximum_residual < 1.0e-7,
                "Shared angular dimensions could not return to their initial values");
        for (const auto& original_point : shared_before.points) {
            const auto* returned = shared_angles.find_point(original_point.id);
            require(returned != nullptr &&
                        std::hypot(returned->x - original_point.x,
                            returned->y - original_point.y) < 1.0e-6,
                    "Two shared angular dimensions accumulated coordinate drift");
        }
        require(zima::sketcher::Sketch::from_serialized(
                    shared_angles.serialized()).dimensions == shared_angles.dimensions,
                "Shared angular dimensions did not survive serialization");
        auto blocked_angle_edit = shared_before;
        for (auto& point : blocked_angle_edit.points) point.fixed = true;
        const auto blocked_angle_before = blocked_angle_edit;
        require(!blocked_angle_edit.set_dimension_value(shared_first_angle.id, 25.0) &&
                    blocked_angle_edit.points == blocked_angle_before.points &&
                    blocked_angle_edit.dimensions == blocked_angle_before.dimensions &&
                    blocked_angle_edit.constraints == blocked_angle_before.constraints,
                "Over-constrained angular edit partially changed fixed geometry");
        auto disconnected_angle = zima::sketcher::Sketch::create_default();
        const auto disconnected_reference = disconnected_angle.add_segment(
            0.0, 0.0, 10.0, 0.0);
        const auto disconnected_driven = disconnected_angle.add_segment(
            20.0, 5.0, 20.0, 15.0);
        auto disconnected = disconnected_angle.create_line_pair_dimension(
            disconnected_reference, disconnected_driven,
            zima::sketcher::DimensionKind::AngleBetween);
        disconnected.placement = std::array{14.0, 8.0};
        disconnected.angle_sector = 0;
        disconnected_angle.apply_dimension(disconnected);
        require(disconnected_angle.dimensions.size() == 1 &&
                    disconnected_angle.viewer_mesh().dimensions.size() == 1 &&
                    disconnected_angle.viewer_mesh().dimensions.front().kind ==
                        zima::kernel::ViewerDimensionKind::Angular,
                "Angular dimension between disconnected segments was not created");
        const auto disconnected_before = disconnected_angle;
        require(disconnected_angle.set_dimension_value(disconnected.id, 55.0),
                "Disconnected angular dimension rejected its value edit");
        require(disconnected_angle.dimensions.front().placement ==
                    disconnected.placement,
                "Disconnected angular dimension edit lost its placement");
        require(std::abs(disconnected_angle.viewer_mesh().dimensions.front().value -
                    55.0) < 1.0e-7,
                "Disconnected angular dimension viewer did not show its edited value");
        require(disconnected_angle.set_dimension_value(disconnected.id, 90.0),
                "Disconnected angular dimension rejected its return value");
        require(disconnected_angle.solve().maximum_residual < 1.0e-7,
                "Disconnected angular dimension retained residual after return");
        for (const auto& original_point : disconnected_before.points) {
            const auto* returned = disconnected_angle.find_point(original_point.id);
            require(returned != nullptr &&
                        std::hypot(returned->x - original_point.x,
                            returned->y - original_point.y) < 1.0e-6,
                    "Disconnected angular dimension accumulated coordinate drift");
        }
        auto supplementary_angle = zima::sketcher::Sketch::create_default();
        const auto supplementary_reference = supplementary_angle.add_segment(
            0.0, 0.0, 10.0, 0.0);
        const auto supplementary_driven = supplementary_angle.add_segment(
            0.0, 0.0, 5.0, 8.660254037844386);
        auto supplementary = supplementary_angle.create_line_pair_dimension(
            supplementary_reference, supplementary_driven,
            zima::sketcher::DimensionKind::AngleBetween);
        supplementary.value = 120.0;
        supplementary.angle_sector = 1;
        supplementary.placement = std::array{-5.0, 8.0};
        supplementary_angle.apply_dimension(supplementary);
        const auto supplementary_view = supplementary_angle.viewer_mesh();
        require(std::abs(supplementary_angle.dimensions.front().value - 120.0) <
                    1.0e-9 &&
                    supplementary_angle.dimensions.front().angle_sector == 1 &&
                    supplementary_view.dimensions.size() == 1 &&
                    std::abs(supplementary_view.dimensions.front().value - 120.0) <
                        1.0e-7,
                "Supplementary angular sector disagreed between model and viewer");
        require(supplementary_angle.set_dimension_placement(
                    supplementary.id, 8.0, 3.0),
                "Supplementary angular dimension could not be dragged");
        const auto dragged_supplementary_view =
            supplementary_angle.viewer_mesh();
        require(dragged_supplementary_view.dimensions.size() == 1 &&
                    std::abs(dragged_supplementary_view.dimensions.front().value -
                        120.0) < 1.0e-7 &&
                    std::abs(std::abs(
                        dragged_supplementary_view.dimensions.front().sweep_degrees) -
                        120.0) < 1.0e-7,
                "Dragging a locked angular sector switched to its supplement");
        auto point_line_angle = zima::sketcher::Sketch::create_default();
        const auto direction_first = point_line_angle.add_point(4.0, 3.0);
        const auto direction_second = point_line_angle.add_point(14.0, 3.0);
        auto mixed_angle = point_line_angle.create_point_line_angle_dimension(
            direction_first, direction_second, "sketch_axis:y");
        mixed_angle.value = 30.0;
        point_line_angle.apply_dimension(mixed_angle);
        const auto mixed_measured = point_line_angle.dimensions.front();
        const auto mixed_view = point_line_angle.viewer_mesh();
        require(mixed_measured.first_point_id == direction_first &&
                    mixed_measured.second_point_id == direction_second &&
                    mixed_measured.geometry_id == "sketch_axis:y" &&
                    mixed_measured.second_geometry_id.empty() &&
                    mixed_view.dimensions.size() == 1 &&
                    std::abs(mixed_view.dimensions.front().value - 30.0) < 1.0e-7,
                "Two-point plus axis angle lost its point anchors or value");
        auto four_point_angle = zima::sketcher::Sketch::create_default();
        const auto a = four_point_angle.add_point(0.0, 0.0);
        const auto b = four_point_angle.add_point(10.0, 0.0);
        const auto c = four_point_angle.add_point(20.0, 5.0);
        const auto d = four_point_angle.add_point(20.0, 15.0);
        auto four = four_point_angle.create_four_point_angle_dimension(a, b, c, d);
        four.value = 40.0;
        four_point_angle.apply_dimension(four);
        require(four_point_angle.dimensions.front().first_point_id == a &&
                    four_point_angle.dimensions.front().second_point_id == b &&
                    four_point_angle.dimensions.front().geometry_id == c &&
                    four_point_angle.dimensions.front().second_geometry_id == d &&
                    std::abs(four_point_angle.viewer_mesh().dimensions.front().value -
                        40.0) < 1.0e-7,
                "Four-point angle did not retain both point-defined directions");

        auto zero_origin_dimension = zima::sketcher::Sketch::create_default();
        const auto zero_origin_point =
            zero_origin_dimension.add_point(10.0, 0.0);
        auto zero_y = zero_origin_dimension.create_point_dimension(
            "sketch_origin", zero_origin_point,
            zima::sketcher::DimensionKind::DistanceY);
        zero_origin_dimension.apply_dimension(zero_y);
        const auto zero_origin_view = zero_origin_dimension.viewer_mesh();
        require(zero_origin_view.dimensions.size() == 1 &&
                    zero_origin_dimension.dimensions.front().first_point_id ==
                        "sketch_origin" &&
                    zero_origin_dimension.dimensions.front().second_point_id ==
                        zero_origin_point &&
                    zero_origin_dimension.dimensions.front().geometry_id.empty() &&
                    std::abs(zero_origin_view.dimensions.front().value) < 1.0e-12 &&
                    zero_origin_view.dimensions.front().label_prefix.empty() &&
                    std::abs(zero_origin_view.dimensions.front().line_first.x -
                        zero_origin_view.dimensions.front().line_second.x) < 1.0e-12 &&
                    std::abs(zero_origin_view.dimensions.front().line_first.y -
                        zero_origin_view.dimensions.front().line_second.y) < 1.0e-12,
                "Zero point-to-origin dimension was not preserved for display");
        require(zero_origin_dimension.set_dimension_value(zero_y.id, 4.0) &&
                    std::abs(zero_origin_dimension.find_point(
                        zero_origin_point)->y - 4.0) < 1.0e-8,
                "Zero point-to-origin driving dimension could not be edited");
        auto referenced_x_dimension = zima::sketcher::Sketch::create_default();
        const auto referenced_x_first = referenced_x_dimension.add_point(2.0, 3.0);
        const auto referenced_x_second = referenced_x_dimension.add_point(2.0, 7.0);
        auto referenced_x = referenced_x_dimension.create_point_dimension(
            referenced_x_first, referenced_x_second,
            zima::sketcher::DimensionKind::DistanceX);
        referenced_x_dimension.apply_dimension(referenced_x);
        const auto referenced_x_view = referenced_x_dimension.viewer_mesh();
        require(referenced_x_dimension.dimensions.front().first_point_id ==
                    referenced_x_first &&
                    referenced_x_dimension.dimensions.front().second_point_id ==
                    referenced_x_second &&
                    referenced_x_dimension.dimensions.front().geometry_id.empty() &&
                    std::ranges::find(
                        referenced_x_view.dimensions.front().participant_semantic_keys,
                        "origin:point") ==
                        referenced_x_view.dimensions.front()
                            .participant_semantic_keys.end(),
                "Projected X form replaced its selected point references with origin");
        require(referenced_x_dimension.set_dimension_value(referenced_x.id, 5.0) &&
                    std::abs(referenced_x_dimension.find_point(
                        referenced_x_second)->x -
                        referenced_x_dimension.find_point(
                            referenced_x_first)->x - 5.0) < 1.0e-8,
                "Projected X point-reference dimension could not be edited from zero");
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
        auto rooted_drag = zima::sketcher::Sketch::create_default();
        const auto rooted_segment = rooted_drag.add_segment(0.0, 0.0, 10.0, 0.0);
        auto rooted_length = rooted_drag.create_segment_dimension(rooted_segment);
        rooted_drag.apply_dimension(std::move(rooted_length));
        rooted_drag.dimensions.front().locked = true;
        const auto rooted_first = rooted_drag.segments.front().first_point_id;
        const auto rooted_second = rooted_drag.segments.front().second_point_id;
        require(rooted_drag.move_point(rooted_second, 15.0, 0.0) &&
                    std::abs(rooted_drag.find_point(rooted_second)->x - 15.0) < 1.0e-8 &&
                    std::abs(rooted_drag.find_point(rooted_first)->x - 5.0) < 1.0e-8,
                "Locked dimension correction pulled the dragged root off the cursor instead of moving its free branch");
        auto axis_priority = zima::sketcher::Sketch::create_default();
        const auto axis_segment = axis_priority.add_segment(-10.0, 0.0, 10.0, 0.0);
        axis_priority.set_segment_centerline(axis_segment, true);
        const auto profile_point = axis_priority.add_point(0.0, 5.0);
        const auto axis_first = axis_priority.segments.front().first_point_id;
        auto axis_distance = axis_priority.create_point_dimension(
            axis_first, profile_point, zima::sketcher::DimensionKind::Distance);
        axis_priority.apply_dimension(axis_distance);
        const auto axis_before = *axis_priority.find_point(axis_first);
        require(axis_priority.set_dimension_value(axis_distance.id, 20.0) &&
                    *axis_priority.find_point(axis_first) == axis_before &&
                    std::abs(std::hypot(
                        axis_priority.find_point(profile_point)->x - axis_before.x,
                        axis_priority.find_point(profile_point)->y - axis_before.y) -
                        20.0) < 1.0e-8,
                "Ordinary profile correction moved a construction centerline instead of the profile branch");
        auto fork_drag = zima::sketcher::Sketch::create_default();
        const auto fork_left_segment = fork_drag.add_segment(-10.0, 0.0, 0.0, 0.0);
        const auto fork_right_segment = fork_drag.add_segment(0.0, 0.0, 10.0, 0.0);
        auto fork_left_length = fork_drag.create_segment_dimension(fork_left_segment);
        auto fork_right_length = fork_drag.create_segment_dimension(fork_right_segment);
        fork_drag.apply_dimension(std::move(fork_left_length));
        fork_drag.apply_dimension(std::move(fork_right_length));
        for (auto& dimension : fork_drag.dimensions) dimension.locked = true;
        const auto fork_root = fork_drag.segments.front().second_point_id;
        const auto fork_left = fork_drag.segments.front().first_point_id;
        const auto fork_right = fork_drag.segments.back().second_point_id;
        require(fork_drag.move_point(fork_root, 5.0, 3.0) &&
                    std::abs(fork_drag.find_point(fork_root)->x - 5.0) < 1.0e-8 &&
                    std::abs(fork_drag.find_point(fork_root)->y - 3.0) < 1.0e-8 &&
                    std::abs(std::hypot(
                        fork_drag.find_point(fork_left)->x - 5.0,
                        fork_drag.find_point(fork_left)->y - 3.0) - 10.0) < 1.0e-8 &&
                    std::abs(std::hypot(
                        fork_drag.find_point(fork_right)->x - 5.0,
                        fork_drag.find_point(fork_right)->y - 3.0) - 10.0) < 1.0e-8,
                "Dragging a fork root did not propagate into both free branches");
        require(fork_drag.move_point(fork_root, 0.0, 0.0) &&
                    fork_drag.move_point(fork_root, 5.0, 3.0) &&
                    std::abs(fork_drag.find_point(fork_root)->x - 5.0) < 1.0e-8 &&
                    std::abs(fork_drag.find_point(fork_root)->y - 3.0) < 1.0e-8 &&
                    std::abs(std::hypot(
                        fork_drag.find_point(fork_left)->x - 5.0,
                        fork_drag.find_point(fork_left)->y - 3.0) - 10.0) < 1.0e-8 &&
                    std::abs(std::hypot(
                        fork_drag.find_point(fork_right)->x - 5.0,
                        fork_drag.find_point(fork_right)->y - 3.0) - 10.0) < 1.0e-8 &&
                    fork_drag.solve().maximum_residual < 1.0e-8,
                "Repeated forward/back fork dragging accumulated constraint drift");
        auto origin_rectangle = zima::sketcher::Sketch::create_default();
        const auto origin_rectangle_segments =
            origin_rectangle.add_rectangle(0.0, 0.0, 10.0, 10.0);
        const auto origin_corner =
            origin_rectangle.segments.front().first_point_id;
        static_cast<void>(origin_rectangle.add_point_reference_constraint(
            origin_corner, "sketch_origin"));
        const auto x_axis_corner =
            origin_rectangle.segments.front().second_point_id;
        const auto opposite_x_corner =
            origin_rectangle.segments[1].second_point_id;
        require(origin_rectangle.move_point(x_axis_corner, 15.0, 0.37) &&
                    std::abs(origin_rectangle.find_point(x_axis_corner)->x - 15.0) < 1.0e-8 &&
                    std::abs(origin_rectangle.find_point(x_axis_corner)->y) < 1.0e-8 &&
                    std::abs(origin_rectangle.find_point(opposite_x_corner)->x - 15.0) < 1.0e-8,
                "Origin-based rectangle corner could not move along the main X axis");
        const auto y_axis_corner =
            origin_rectangle.segments[2].second_point_id;
        const auto opposite_y_corner =
            origin_rectangle.segments[1].second_point_id;
        require(origin_rectangle.move_point(y_axis_corner, -0.42, 14.0) &&
                    std::abs(origin_rectangle.find_point(y_axis_corner)->x) < 1.0e-8 &&
                    std::abs(origin_rectangle.find_point(y_axis_corner)->y - 14.0) < 1.0e-8 &&
                    std::abs(origin_rectangle.find_point(opposite_y_corner)->y - 14.0) < 1.0e-8,
                "Origin-based rectangle corner could not move along the main Y axis");
        auto dimensioned_origin_rectangle = zima::sketcher::Sketch::create_default();
        static_cast<void>(dimensioned_origin_rectangle.add_rectangle(
            0.0, 0.0, 10.0, 10.0));
        static_cast<void>(dimensioned_origin_rectangle.add_point_reference_constraint(
            dimensioned_origin_rectangle.segments.front().first_point_id,
            "sketch_origin"));
        auto rectangle_height = dimensioned_origin_rectangle.create_segment_dimension(
            dimensioned_origin_rectangle.segments[1].id);
        const auto rectangle_height_id = rectangle_height.id;
        dimensioned_origin_rectangle.apply_dimension(std::move(rectangle_height));
        auto rectangle_width = dimensioned_origin_rectangle.create_segment_dimension(
            dimensioned_origin_rectangle.segments[2].id);
        const auto rectangle_width_id = rectangle_width.id;
        dimensioned_origin_rectangle.apply_dimension(std::move(rectangle_width));
        const auto dimensioned_x_corner =
            dimensioned_origin_rectangle.segments.front().second_point_id;
        require(dimensioned_origin_rectangle.move_point(
                    dimensioned_x_corner, 15.0, 0.31) &&
                    std::abs(dimensioned_origin_rectangle.find_point(
                        dimensioned_x_corner)->x - 15.0) < 1.0e-8 &&
                    std::abs(std::find_if(
                        dimensioned_origin_rectangle.dimensions.begin(),
                        dimensioned_origin_rectangle.dimensions.end(),
                        [&](const auto& value) {
                            return value.id == rectangle_width_id;
                        })->value - 15.0) < 1.0e-8 &&
                    std::abs(std::find_if(
                        dimensioned_origin_rectangle.dimensions.begin(),
                        dimensioned_origin_rectangle.dimensions.end(),
                        [&](const auto& value) {
                            return value.id == rectangle_height_id;
                        })->value - 10.0) < 1.0e-8,
                "Unlocked driving rectangle dimensions blocked an axis drag instead of updating their values");
        auto locked_origin_rectangle = dimensioned_origin_rectangle;
        for (auto& dimension : locked_origin_rectangle.dimensions) {
            dimension.locked = true;
        }
        const auto locked_before = locked_origin_rectangle;
        require(!locked_origin_rectangle.move_point(
                    dimensioned_x_corner, 20.0, 0.0) &&
                    locked_origin_rectangle.points == locked_before.points &&
                    locked_origin_rectangle.dimensions == locked_before.dimensions,
                "Locked driving rectangle dimensions allowed a point drag");
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
        const auto merged_point = coincident.merge_points(
            coincident_first_id, coincident_second_id);
        require(merged_point == coincident_first_id &&
                    coincident.points.size() == 1 &&
                    coincident.constraints.empty() &&
                    coincident.find_point(coincident_second_id) == nullptr,
                "Point merge did not replace two points with one topology node");
        const auto coincident_before_duplicate = coincident;
        bool duplicate_coincident_rejected = false;
        try {
            static_cast<void>(coincident.merge_points(
                coincident_second_id, coincident_first_id));
        } catch (const std::invalid_argument&) {
            duplicate_coincident_rejected = true;
        }
        require(duplicate_coincident_rejected &&
                    coincident.serialized() ==
                        coincident_before_duplicate.serialized(),
                "A repeated point merge changed the committed Sketch");
        auto anchored_merge = zima::sketcher::Sketch::create_default();
        const auto externally_anchored = anchored_merge.add_point(2.0, 3.0);
        static_cast<void>(anchored_merge.add_point_reference_constraint(
            externally_anchored, "sketch_origin"));
        const auto unanchored = anchored_merge.add_point(8.0, 6.0);
        const auto anchored_survivor = anchored_merge.merge_points(
            unanchored, externally_anchored);
        require(anchored_survivor == externally_anchored &&
                    anchored_merge.find_point(unanchored) == nullptr &&
                    anchored_merge.find_point(externally_anchored) != nullptr &&
                    std::abs(anchored_merge.find_point(
                        externally_anchored)->x) < 1.0e-9 &&
                    std::abs(anchored_merge.find_point(
                        externally_anchored)->y) < 1.0e-9,
                "Point merge did not preserve the externally anchored topology node");
        auto fixed_merge = zima::sketcher::Sketch::create_default();
        auto free_node = zima::sketcher::Sketch::create_point(1.0, 2.0);
        auto fixed_node = zima::sketcher::Sketch::create_point(7.0, 9.0);
        fixed_node.fixed = true;
        const auto free_node_id = free_node.id;
        const auto fixed_node_id = fixed_node.id;
        fixed_merge.points.push_back(std::move(free_node));
        fixed_merge.points.push_back(std::move(fixed_node));
        const auto fixed_survivor = fixed_merge.merge_points(
            free_node_id, fixed_node_id);
        require(fixed_survivor == fixed_node_id &&
                    fixed_merge.find_point(free_node_id) == nullptr &&
                    fixed_merge.find_point(fixed_node_id)->fixed &&
                    std::abs(fixed_merge.find_point(fixed_node_id)->x - 7.0) <
                        1.0e-9 &&
                    std::abs(fixed_merge.find_point(fixed_node_id)->y - 9.0) <
                        1.0e-9,
                "Point merge did not preserve the fixed topology node");
        auto collapsing_merge = zima::sketcher::Sketch::create_default();
        const auto collapsing_segment =
            collapsing_merge.add_segment(0.0, 0.0, 5.0, 0.0);
        const auto collapsing_first =
            collapsing_merge.segments.front().first_point_id;
        const auto collapsing_second =
            collapsing_merge.segments.front().second_point_id;
        static_cast<void>(collapsing_merge.add_segment_constraint(
            collapsing_segment, zima::sketcher::ConstraintKind::Horizontal));
        auto collapsing_dimension =
            collapsing_merge.create_segment_dimension(collapsing_segment);
        collapsing_merge.apply_dimension(std::move(collapsing_dimension));
        static_cast<void>(collapsing_merge.add_import_block(
            "Collapsing Segment", "memory:collapsing-segment",
            {collapsing_segment}, {collapsing_first, collapsing_second}));
        const auto collapsed_survivor = collapsing_merge.merge_points(
            collapsing_first, collapsing_second);
        require(collapsed_survivor == collapsing_first &&
                    collapsing_merge.points.size() == 1 &&
                    collapsing_merge.find_point(collapsing_second) == nullptr &&
                    collapsing_merge.segments.empty() &&
                    collapsing_merge.constraints.empty() &&
                    collapsing_merge.dimensions.empty() &&
                    collapsing_merge.import_blocks.empty(),
                "Merging one Segment's endpoints did not consume the Segment and retain one point");
        auto dependent_merge = zima::sketcher::Sketch::create_default();
        const auto merge_reference_segment = dependent_merge.add_segment(
            0.0, 0.0, 5.0, 0.0);
        const auto merge_dependent_segment = dependent_merge.add_segment(
            8.0, 2.0, 12.0, 2.0);
        const auto merge_reference_id =
            dependent_merge.segments.front().second_point_id;
        const auto merge_absorbed_id =
            dependent_merge.segments[1].first_point_id;
        const auto merge_other_id =
            dependent_merge.segments[1].second_point_id;
        static_cast<void>(dependent_merge.add_segment_constraint(
            merge_dependent_segment,
            zima::sketcher::ConstraintKind::Horizontal));
        auto merged_length = dependent_merge.create_segment_dimension(
            merge_dependent_segment);
        merged_length.driving = false;
        const auto merged_length_id = merged_length.id;
        dependent_merge.apply_dimension(std::move(merged_length));
        const auto merge_direction_first = dependent_merge.add_point(20.0, 0.0);
        const auto merge_direction_second = dependent_merge.add_point(24.0, 4.0);
        auto merged_angle = dependent_merge.create_four_point_angle_dimension(
            merge_direction_first, merge_direction_second,
            merge_absorbed_id, merge_other_id);
        merged_angle.driving = false;
        const auto merged_angle_id = merged_angle.id;
        dependent_merge.apply_dimension(std::move(merged_angle));
        const auto merge_block = dependent_merge.add_import_block(
            "Merge dependencies", "memory:merge-dependencies",
            {merge_dependent_segment}, {merge_absorbed_id, merge_other_id});
        static_cast<void>(dependent_merge.merge_points(
            merge_reference_id, merge_absorbed_id));
        const auto reference_segment_after_merge = std::ranges::find_if(
            dependent_merge.segments, [&](const auto& value) {
                return value.id == merge_reference_segment;
            });
        const auto dependent_segment = std::ranges::find_if(
            dependent_merge.segments, [&](const auto& value) {
                return value.id == merge_dependent_segment;
            });
        const auto horizontal_after_merge = std::ranges::find_if(
            dependent_merge.constraints, [&](const auto& value) {
                return value.geometry_id == merge_dependent_segment;
            });
        const auto length_after_merge = std::ranges::find_if(
            dependent_merge.dimensions, [&](const auto& value) {
                return value.id == merged_length_id;
            });
        const auto angle_after_merge = std::ranges::find_if(
            dependent_merge.dimensions, [&](const auto& value) {
                return value.id == merged_angle_id;
            });
        const auto block_after_merge = std::ranges::find_if(
            dependent_merge.import_blocks, [&](const auto& value) {
                return value.id == merge_block;
            });
        require(dependent_segment != dependent_merge.segments.end() &&
                    reference_segment_after_merge !=
                        dependent_merge.segments.end() &&
                    dependent_segment->first_point_id == merge_reference_id &&
                    horizontal_after_merge != dependent_merge.constraints.end() &&
                    horizontal_after_merge->first_point_id == merge_reference_id &&
                    length_after_merge != dependent_merge.dimensions.end() &&
                    length_after_merge->first_point_id == merge_reference_id &&
                    angle_after_merge != dependent_merge.dimensions.end() &&
                    angle_after_merge->geometry_id == merge_reference_id &&
                    block_after_merge != dependent_merge.import_blocks.end() &&
                    std::ranges::find(block_after_merge->point_ids,
                        merge_reference_id) != block_after_merge->point_ids.end() &&
                    std::ranges::find(block_after_merge->point_ids,
                        merge_absorbed_id) == block_after_merge->point_ids.end() &&
                    dependent_merge.find_point(merge_absorbed_id) == nullptr,
                "Point merge did not rewire every dependent Sketch identity");
        auto midpoint = zima::sketcher::Sketch::create_default();
        const auto midpoint_segment = midpoint.add_segment(0.0, 0.0, 8.0, 4.0);
        midpoint.find_point(midpoint.segments.front().first_point_id)->fixed = true;
        midpoint.find_point(midpoint.segments.front().second_point_id)->fixed = true;
        const auto midpoint_point = midpoint.add_point(30.0, -10.0);
        static_cast<void>(midpoint.add_midpoint_constraint(
            midpoint_point, midpoint_segment));
        const auto* solved_midpoint = midpoint.find_point(midpoint_point);
        require(midpoint.constraints.size() == 1 &&
                    midpoint.constraints.front().kind ==
                        zima::sketcher::ConstraintKind::Midpoint &&
                    midpoint.constraints.front().first_point_id == midpoint_point &&
                    midpoint.constraints.front().geometry_id == midpoint_segment &&
                    std::abs(solved_midpoint->x - 4.0) < 1.0e-8 &&
                    std::abs(solved_midpoint->y - 2.0) < 1.0e-8,
                "Midpoint constraint did not place a point at the segment midpoint");
        const auto loaded_midpoint = zima::sketcher::Sketch::from_serialized(
            midpoint.serialized());
        require(loaded_midpoint.constraints == midpoint.constraints &&
                    loaded_midpoint.points == midpoint.points,
                "Midpoint constraint did not survive Sketch serialization");
        auto midpoint_on_axis = zima::sketcher::Sketch::create_default();
        const auto midpoint_on_axis_segment =
            midpoint_on_axis.add_segment(0.0, 2.0, 10.0, 4.0);
        static_cast<void>(midpoint_on_axis.add_midpoint_on_line_constraint(
            midpoint_on_axis_segment, "sketch_axis:x"));
        const auto& midpoint_on_axis_geometry = midpoint_on_axis.segments.front();
        const auto* midpoint_on_axis_first = midpoint_on_axis.find_point(
            midpoint_on_axis_geometry.first_point_id);
        const auto* midpoint_on_axis_second = midpoint_on_axis.find_point(
            midpoint_on_axis_geometry.second_point_id);
        require(midpoint_on_axis.constraints.size() == 1 &&
                    midpoint_on_axis.constraints.front().kind ==
                        zima::sketcher::ConstraintKind::MidpointOnLine &&
                    std::abs((midpoint_on_axis_first->y +
                              midpoint_on_axis_second->y) * 0.5) < 1.0e-8,
                "Midpoint-on-line constraint did not place the segment midpoint");
        const auto loaded_midpoint_on_axis =
            zima::sketcher::Sketch::from_serialized(
                midpoint_on_axis.serialized());
        require(loaded_midpoint_on_axis.constraints ==
                    midpoint_on_axis.constraints,
                "Midpoint-on-line constraint did not survive serialization");
        auto centered_rectangle = zima::sketcher::Sketch::create_default();
        const auto centered_rectangle_edges = centered_rectangle.add_rectangle(
            2.0, 3.0, 12.0, -3.0);
        static_cast<void>(centered_rectangle.add_midpoint_on_line_constraint(
            centered_rectangle_edges[1], "sketch_axis:x"));
        const auto& centered_side = centered_rectangle.segments[1];
        const auto* centered_side_first = centered_rectangle.find_point(
            centered_side.first_point_id);
        const auto* centered_side_second = centered_rectangle.find_point(
            centered_side.second_point_id);
        require(centered_rectangle.constraints.back().kind ==
                    zima::sketcher::ConstraintKind::MidpointOnLine &&
                    std::abs((centered_side_first->y +
                        centered_side_second->y) * 0.5) < 1.0e-8,
                "Rectangle side midpoint did not remain attached to the Sketch axis");
        const std::array centered_rectangle_points{
            centered_rectangle.segments[0].first_point_id,
            centered_rectangle.segments[0].second_point_id,
            centered_rectangle.segments[1].second_point_id,
            centered_rectangle.segments[2].second_point_id};
        for (std::size_t handle_index = 0;
             handle_index < centered_rectangle_points.size(); ++handle_index) {
            const auto& point_id = centered_rectangle_points[handle_index];
            auto dragged_rectangle = centered_rectangle;
            const auto original_points = dragged_rectangle.points;
            const auto original_constraints = dragged_rectangle.constraints;
            const auto* before = dragged_rectangle.find_point(point_id);
            const double original_x = before->x;
            const double original_y = before->y;
            const double target_x = before->x + 1.0;
            const double target_y = before->y + (before->y > 0.0 ? 1.0 : -1.0);
            const bool moved = dragged_rectangle.move_point(
                point_id, target_x, target_y);
            require(moved &&
                        std::abs(dragged_rectangle.find_point(point_id)->x -
                            target_x) < 1.0e-8 &&
                        std::abs(dragged_rectangle.find_point(point_id)->y -
                            target_y) < 1.0e-8,
                    "Rectangle midpoint-on-axis blocked one of its corner handles");
            require(dragged_rectangle.move_point(
                        point_id, original_x, original_y) &&
                        dragged_rectangle.constraints == original_constraints &&
                        dragged_rectangle.solve().maximum_residual < 1.0e-8,
                    "Rectangle midpoint-on-axis did not survive a forward/back drag");
            for (const auto& original_point : original_points) {
                const auto* returned = dragged_rectangle.find_point(original_point.id);
                require(returned != nullptr &&
                            std::hypot(returned->x - original_point.x,
                                returned->y - original_point.y) < 1.0e-6,
                        "Rectangle midpoint-on-axis accumulated coordinate drift");
            }
        }
        bool duplicate_midpoint_rejected = false;
        try {
            static_cast<void>(midpoint.add_midpoint_constraint(
                midpoint_point, midpoint_segment));
        } catch (const std::invalid_argument&) {
            duplicate_midpoint_rejected = true;
        }
        bool endpoint_midpoint_rejected = false;
        try {
            static_cast<void>(midpoint.add_midpoint_constraint(
                midpoint.segments.front().first_point_id, midpoint_segment));
        } catch (const std::invalid_argument&) {
            endpoint_midpoint_rejected = true;
        }
        require(duplicate_midpoint_rejected && endpoint_midpoint_rejected &&
                    midpoint.constraints.size() == 1,
                "Duplicate or endpoint Midpoint constraint was accepted");
        auto driven_midpoint = zima::sketcher::Sketch::create_default();
        const auto driven_midpoint_segment =
            driven_midpoint.add_segment(0.0, 0.0, 10.0, 4.0);
        const auto driven_midpoint_point = driven_midpoint.add_point(20.0, 5.0);
        driven_midpoint.find_point(driven_midpoint_point)->fixed = true;
        const double driven_dx_before =
            driven_midpoint.points[1].x - driven_midpoint.points[0].x;
        const double driven_dy_before =
            driven_midpoint.points[1].y - driven_midpoint.points[0].y;
        static_cast<void>(driven_midpoint.add_midpoint_constraint(
            driven_midpoint_point, driven_midpoint_segment));
        const auto* driven_first = driven_midpoint.find_point(
            driven_midpoint.segments.front().first_point_id);
        const auto* driven_second = driven_midpoint.find_point(
            driven_midpoint.segments.front().second_point_id);
        require(std::abs((driven_first->x + driven_second->x) * 0.5 - 20.0) <
                        1.0e-8 &&
                    std::abs((driven_first->y + driven_second->y) * 0.5 - 5.0) <
                        1.0e-8 &&
                    std::abs(driven_second->x - driven_first->x -
                             driven_dx_before) < 1.0e-8 &&
                    std::abs(driven_second->y - driven_first->y -
                             driven_dy_before) < 1.0e-8,
                "Fixed Midpoint point did not translate its free segment rigidly");
        auto fixed_midpoint = zima::sketcher::Sketch::create_default();
        const auto fixed_midpoint_segment =
            fixed_midpoint.add_segment(0.0, 0.0, 10.0, 0.0);
        fixed_midpoint.find_point(
            fixed_midpoint.segments.front().first_point_id)->fixed = true;
        fixed_midpoint.find_point(
            fixed_midpoint.segments.front().second_point_id)->fixed = true;
        const auto fixed_midpoint_point = fixed_midpoint.add_point(2.0, 3.0);
        fixed_midpoint.find_point(fixed_midpoint_point)->fixed = true;
        const auto fixed_midpoint_before = fixed_midpoint;
        bool fixed_midpoint_rejected = false;
        try {
            static_cast<void>(fixed_midpoint.add_midpoint_constraint(
                fixed_midpoint_point, fixed_midpoint_segment));
        } catch (const std::runtime_error&) {
            fixed_midpoint_rejected = true;
        }
        require(fixed_midpoint_rejected &&
                    fixed_midpoint.points == fixed_midpoint_before.points &&
                    fixed_midpoint.constraints == fixed_midpoint_before.constraints,
                "Conflicting fixed Midpoint relation partially changed the Sketch");
        auto midpoint_without_point = loaded_midpoint;
        midpoint_without_point.remove_point(midpoint_point);
        require(midpoint_without_point.constraints.empty() &&
                    midpoint_without_point.segments.size() == 1,
                "Deleting a Midpoint point removed its unrelated segment");
        midpoint.remove_geometry(midpoint_segment);
        require(midpoint.constraints.empty() &&
                    midpoint.find_point(midpoint_point) != nullptr,
                "Deleting a Midpoint segment retained its constraint or deleted its point");
        auto symmetric = zima::sketcher::Sketch::create_default();
        const auto symmetry_axis = symmetric.add_segment(
            0.0, -5.0, 0.0, 5.0, 1.0e-6, true);
        const auto symmetry_source = symmetric.add_point(-3.0, 2.0);
        const auto symmetry_driven = symmetric.add_point(4.0, 1.0);
        static_cast<void>(symmetric.add_symmetric_constraint(
            symmetry_source, symmetry_driven, symmetry_axis));
        const auto* symmetry_result = symmetric.find_point(symmetry_driven);
        require(symmetric.constraints.size() == 1 &&
                    symmetric.constraints.front().kind ==
                        zima::sketcher::ConstraintKind::Symmetric &&
                    std::abs(symmetry_result->x - 3.0) < 1.0e-8 &&
                    std::abs(symmetry_result->y - 2.0) < 1.0e-8,
                "Explicit Symmetric constraint did not reflect its driven point");
        const auto loaded_symmetric = zima::sketcher::Sketch::from_serialized(
            symmetric.serialized());
        require(loaded_symmetric.constraints == symmetric.constraints &&
                    loaded_symmetric.points == symmetric.points,
                "Explicit Symmetric constraint did not survive serialization");
        auto base_axis_symmetric = zima::sketcher::Sketch::create_default();
        const auto base_axis_source = base_axis_symmetric.add_point(2.0, 3.0);
        const auto base_axis_driven = base_axis_symmetric.add_point(-1.0, -4.0);
        static_cast<void>(base_axis_symmetric.add_symmetric_constraint(
            base_axis_source, base_axis_driven, "sketch_axis:x"));
        require(std::abs(base_axis_symmetric.find_point(base_axis_driven)->x - 2.0) <
                    1.0e-8 &&
                    std::abs(base_axis_symmetric.find_point(base_axis_driven)->y + 3.0) <
                    1.0e-8,
                "Sketch X origin axis did not act as a Symmetric reference");
        bool duplicate_symmetric_rejected = false;
        try {
            static_cast<void>(symmetric.add_symmetric_constraint(
                symmetry_driven, symmetry_source, symmetry_axis));
        } catch (const std::invalid_argument&) {
            duplicate_symmetric_rejected = true;
        }
        require(duplicate_symmetric_rejected && symmetric.constraints.size() == 1,
                "Duplicate explicit Symmetric constraint was accepted");
        auto fixed_symmetric = zima::sketcher::Sketch::create_default();
        const auto fixed_symmetry_axis = fixed_symmetric.add_segment(
            0.0, -5.0, 0.0, 5.0, 1.0e-6, true);
        const auto fixed_symmetry_source = fixed_symmetric.add_point(-3.0, 2.0);
        const auto fixed_symmetry_driven = fixed_symmetric.add_point(4.0, 1.0);
        fixed_symmetric.find_point(fixed_symmetry_source)->fixed = true;
        fixed_symmetric.find_point(fixed_symmetry_driven)->fixed = true;
        const auto fixed_symmetric_before = fixed_symmetric;
        bool fixed_symmetric_rejected = false;
        try {
            static_cast<void>(fixed_symmetric.add_symmetric_constraint(
                fixed_symmetry_source, fixed_symmetry_driven,
                fixed_symmetry_axis));
        } catch (const std::runtime_error&) {
            fixed_symmetric_rejected = true;
        }
        require(fixed_symmetric_rejected &&
                    fixed_symmetric.points == fixed_symmetric_before.points &&
                    fixed_symmetric.constraints == fixed_symmetric_before.constraints,
                "Conflicting explicit Symmetric relation partially changed the Sketch");
        symmetric.remove_geometry(symmetry_axis);
        require(symmetric.constraints.empty() &&
                    symmetric.find_point(symmetry_source) != nullptr &&
                    symmetric.find_point(symmetry_driven) != nullptr,
                "Deleting a Symmetric axis retained its constraint or deleted its points");
        auto concentric = zima::sketcher::Sketch::create_default();
        const auto concentric_reference = concentric.add_circle(0.0, 0.0, 3.0);
        concentric.find_point(concentric.circles.front().center_point_id)->fixed = true;
        const auto concentric_arc = concentric.add_arc(
            10.0, 5.0, 14.0, 5.0, 10.0, 9.0);
        const auto concentric_ellipse = concentric.add_ellipse(
            -8.0, 6.0, -2.0, 6.0, -8.0, 8.0);
        const auto concentric_elliptical_arc = concentric.add_elliptical_arc(
            12.0, -10.0, 17.0, -10.0, 12.0, -7.0,
            17.0, -10.0, 12.0, -7.0);
        const auto arc_start_id = concentric.arcs.front().start_point_id;
        const auto arc_end_id = concentric.arcs.front().end_point_id;
        static_cast<void>(concentric.add_concentric_constraint(
            concentric_reference, concentric_arc));
        static_cast<void>(concentric.add_concentric_constraint(
            concentric_reference, concentric_ellipse));
        static_cast<void>(concentric.add_concentric_constraint(
            concentric_reference, concentric_elliptical_arc));
        const auto* concentric_center = concentric.find_point(
            concentric.circles.front().center_point_id);
        const auto* concentric_arc_center = concentric.find_point(
            concentric.arcs.front().center_point_id);
        const auto* concentric_ellipse_center = concentric.find_point(
            concentric.ellipses.front().center_point_id);
        const auto* concentric_elliptical_arc_center = concentric.find_point(
            concentric.elliptical_arcs.front().center_point_id);
        require(concentric.constraints.size() == 3 &&
                    std::hypot(concentric_arc_center->x - concentric_center->x,
                               concentric_arc_center->y - concentric_center->y) < 1.0e-8 &&
                    std::hypot(concentric_ellipse_center->x - concentric_center->x,
                               concentric_ellipse_center->y - concentric_center->y) < 1.0e-8 &&
                    std::hypot(
                        concentric_elliptical_arc_center->x - concentric_center->x,
                        concentric_elliptical_arc_center->y - concentric_center->y) <
                        1.0e-8 &&
                    std::abs(concentric.find_point(arc_start_id)->x - 4.0) < 1.0e-8 &&
                    std::abs(concentric.find_point(arc_start_id)->y) < 1.0e-8 &&
                    std::abs(concentric.find_point(arc_end_id)->x) < 1.0e-8 &&
                    std::abs(concentric.find_point(arc_end_id)->y - 4.0) < 1.0e-8,
                "Concentric did not translate every supported driven curve rigidly");
        const auto loaded_concentric = zima::sketcher::Sketch::from_serialized(
            concentric.serialized());
        require(loaded_concentric.constraints == concentric.constraints &&
                    loaded_concentric.points == concentric.points,
                "Concentric constraints did not survive Sketch serialization");
        bool duplicate_concentric_rejected = false;
        try {
            static_cast<void>(concentric.add_concentric_constraint(
                concentric_arc, concentric_reference));
        } catch (const std::invalid_argument&) {
            duplicate_concentric_rejected = true;
        }
        require(duplicate_concentric_rejected,
                "Reversed duplicate Concentric constraint was accepted");
        auto concentric_after_delete = loaded_concentric;
        concentric_after_delete.remove_geometry(concentric_arc);
        require(concentric_after_delete.constraints.size() == 2,
                "Deleting a concentric curve retained its owned constraint");
        auto fixed_concentric = zima::sketcher::Sketch::create_default();
        const auto fixed_concentric_reference =
            fixed_concentric.add_circle(0.0, 0.0, 2.0);
        const auto fixed_concentric_arc = fixed_concentric.add_arc(
            10.0, 0.0, 13.0, 0.0, 10.0, 3.0);
        fixed_concentric.find_point(
            fixed_concentric.arcs.front().start_point_id)->fixed = true;
        const auto fixed_concentric_before = fixed_concentric;
        bool fixed_concentric_rejected = false;
        try {
            static_cast<void>(fixed_concentric.add_concentric_constraint(
                fixed_concentric_reference, fixed_concentric_arc));
        } catch (const std::runtime_error&) {
            fixed_concentric_rejected = true;
        }
        require(fixed_concentric_rejected &&
                    fixed_concentric.points == fixed_concentric_before.points &&
                    fixed_concentric.constraints == fixed_concentric_before.constraints,
                "Blocked Concentric relation partially changed the Sketch");
        auto concentric_polygon = zima::sketcher::Sketch::create_default();
        const auto polygon_reference = concentric_polygon.add_circle(0.0, 0.0, 2.0);
        const auto driven_polygon = concentric_polygon.add_regular_polygon(
            10.0, 10.0, 14.0, 10.0, 6);
        static_cast<void>(concentric_polygon.add_concentric_constraint(
            polygon_reference, driven_polygon.support_circle_id));
        const auto* translated_polygon_center = concentric_polygon.find_point(
            concentric_polygon.circles[1].center_point_id);
        const auto* translated_polygon_vertex = concentric_polygon.find_point(
            driven_polygon.vertex_ids.front());
        require(std::hypot(translated_polygon_center->x, translated_polygon_center->y) <
                        1.0e-8 &&
                    std::abs(translated_polygon_vertex->x - 4.0) < 1.0e-8 &&
                    std::abs(translated_polygon_vertex->y) < 1.0e-8 &&
                    concentric_polygon.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
                "Concentric did not translate PointOnCircle dependency closure");
        auto tangent = zima::sketcher::Sketch::create_default();
        const auto tangent_circle = tangent.add_circle(0.0, 5.0, 2.0);
        const auto tangent_line = tangent.add_segment(-5.0, 0.0, 5.0, 0.0);
        const auto tangent_line_first_id = tangent.segments.front().first_point_id;
        const auto tangent_line_second_id = tangent.segments.front().second_point_id;
        static_cast<void>(tangent.add_tangent_constraint(
            tangent_circle, tangent_line));
        const auto* tangent_line_first = tangent.find_point(tangent_line_first_id);
        const auto* tangent_line_second = tangent.find_point(tangent_line_second_id);
        require(tangent.constraints.size() == 1 &&
                    tangent.constraints.front().kind ==
                        zima::sketcher::ConstraintKind::Tangent &&
                    std::abs(tangent_line_first->x + 5.0) < 1.0e-8 &&
                    std::abs(tangent_line_first->y - 3.0) < 1.0e-8 &&
                    std::abs(tangent_line_second->x - 5.0) < 1.0e-8 &&
                    std::abs(tangent_line_second->y - 3.0) < 1.0e-8 &&
                    std::abs(tangent.circles.front().radius - 2.0) < 1.0e-8,
                "Tangent did not translate the driven segment rigidly");
        require(tangent.viewer_mesh().constraint_markers.front()
                    .participant_semantic_keys ==
                    std::vector<std::string>{
                        "circle:" + tangent_circle, "segment:" + tangent_line},
                "Tangent marker did not expose only its two geometries");
        const auto tangent_center_before = *tangent.find_point(
            tangent.circles.front().center_point_id);
        const auto tangent_first_before = *tangent.find_point(
            tangent.segments.front().first_point_id);
        require(tangent.translate_selection({}, {tangent_circle, tangent_line},
                    7.0, -2.0) &&
                    std::abs(tangent.find_point(
                        tangent.circles.front().center_point_id)->x -
                        (tangent_center_before.x + 7.0)) < 1.0e-9 &&
                    std::abs(tangent.find_point(
                        tangent.segments.front().first_point_id)->y -
                        (tangent_first_before.y - 2.0)) < 1.0e-9 &&
                    std::abs(tangent.circles.front().radius - 2.0) < 1.0e-9,
                "Rigid multi-selection drag changed shape or lost Tangent");
        const auto loaded_tangent = zima::sketcher::Sketch::from_serialized(
            tangent.serialized());
        require(loaded_tangent.constraints == tangent.constraints &&
                    loaded_tangent.points == tangent.points,
                "Tangent constraint did not survive Sketch serialization");
        auto axis_tangent = zima::sketcher::Sketch::create_default();
        const auto axis_circle = axis_tangent.add_circle(0.0, 2.0, 2.0);
        static_cast<void>(axis_tangent.add_tangent_constraint(
            "sketch_axis:x", axis_circle));
        const auto* axis_center = axis_tangent.find_point(
            axis_tangent.circles.front().center_point_id);
        require(axis_center != nullptr && std::abs(axis_center->y - 2.0) < 1.0e-8 &&
                    axis_tangent.constraints.front().geometry_id == "sketch_axis:x" &&
                    axis_tangent.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
                "Circle tangent did not accept or solve against the base X axis");
        auto contact = zima::sketcher::Sketch::create_point(0.0, 0.0);
        const auto contact_id = contact.id;
        axis_tangent.points.push_back(std::move(contact));
        static_cast<void>(axis_tangent.add_point_on_circle_constraint(
            contact_id, axis_circle));
        static_cast<void>(axis_tangent.add_point_on_line_constraint(
            contact_id, "sketch_axis:x"));
        const auto* persisted_contact = axis_tangent.find_point(contact_id);
        require(persisted_contact != nullptr &&
                    std::abs(persisted_contact->x) < 1.0e-8 &&
                    std::abs(persisted_contact->y) < 1.0e-8 &&
                    std::ranges::count_if(axis_tangent.constraints,
                        [&](const auto& value) {
                            return value.first_point_id == contact_id;
                        }) == 2,
                "Circle C+T contact did not persist its own point on circle and axis");
        auto ui_circle_tangent = zima::sketcher::Sketch::create_default();
        const auto ui_circle = ui_circle_tangent.add_circle(0.0, 2.0, 2.0);
        auto ui_contact = zima::sketcher::Sketch::create_point(0.0, 0.0);
        const auto ui_contact_id = ui_contact.id;
        ui_circle_tangent.points.push_back(std::move(ui_contact));
        static_cast<void>(ui_circle_tangent.add_point_on_circle_constraint(
            ui_contact_id, ui_circle));
        static_cast<void>(ui_circle_tangent.add_point_on_line_constraint(
            ui_contact_id, "sketch_axis:x"));
        static_cast<void>(ui_circle_tangent.add_tangent_constraint(
            "sketch_axis:x", ui_circle, ui_contact_id));
        const auto ui_markers = ui_circle_tangent.viewer_mesh().constraint_markers;
        const auto ui_tangent_marker = std::ranges::find_if(
            ui_markers, [](const auto& marker) { return marker.label == "T"; });
        const auto persisted_tangent = std::ranges::find_if(
            ui_circle_tangent.constraints, [](const auto& value) {
                return value.kind == zima::sketcher::ConstraintKind::Tangent;
            });
        require(persisted_tangent != ui_circle_tangent.constraints.end() &&
                    persisted_tangent->first_point_id == ui_contact_id &&
                    ui_markers.size() == 3 &&
                    std::ranges::count_if(ui_markers, [](const auto& marker) {
                        return marker.label == "C";
                    }) == 2 &&
                    ui_tangent_marker != ui_markers.end() &&
                    std::abs(ui_tangent_marker->position.x) < 1.0e-8 &&
                    std::abs(ui_tangent_marker->position.y) < 1.0e-8,
                "Circle C+T did not expose its individual selectable relations");
        for (const double center_y : {-2.0, 2.0}) {
            auto signed_axis_contact = zima::sketcher::Sketch::create_default();
            const auto signed_circle = signed_axis_contact.add_circle(
                0.0, center_y, 2.0);
            auto signed_point = zima::sketcher::Sketch::create_point(0.0, 0.0);
            const auto signed_point_id = signed_point.id;
            signed_axis_contact.points.push_back(std::move(signed_point));
            // This is the order used by interactive circle creation: the
            // exact preview establishes T, then the persisted contact owns C
            // on the new circle and C on the selected base axis.
            static_cast<void>(signed_axis_contact.add_tangent_constraint(
                "sketch_axis:x", signed_circle, signed_point_id));
            static_cast<void>(signed_axis_contact.add_point_on_circle_constraint(
                signed_point_id, signed_circle));
            static_cast<void>(signed_axis_contact.add_point_on_line_constraint(
                signed_point_id, "sketch_axis:x"));
            require(signed_axis_contact.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting &&
                    std::ranges::count_if(signed_axis_contact.constraints,
                        [&](const auto& value) {
                            return value.first_point_id == signed_point_id;
                        }) == 3,
                "Interactive circle C+T failed on one signed side of the base X axis");
        }
        for (const std::string axis_id : {std::string{"sketch_axis:x"},
                 std::string{"sketch_axis:y"}}) {
            for (const double side : {-1.0, 1.0}) {
                for (const bool reversed_order : {false, true}) {
                    auto radial_axis_contact =
                        zima::sketcher::Sketch::create_default();
                    constexpr double initial_radius = 4.0;
                    const double center_x = axis_id == "sketch_axis:x"
                        ? 3.0 : side * initial_radius;
                    const double center_y = axis_id == "sketch_axis:x"
                        ? side * initial_radius : -3.0;
                    const auto circle_id = radial_axis_contact.add_circle(
                        center_x, center_y, initial_radius);
                    auto contact = zima::sketcher::Sketch::create_point(
                        axis_id == "sketch_axis:x" ? center_x : 0.0,
                        axis_id == "sketch_axis:x" ? 0.0 : center_y);
                    const auto contact_id = contact.id;
                    radial_axis_contact.points.push_back(std::move(contact));
                    static_cast<void>(reversed_order
                        ? radial_axis_contact.add_tangent_constraint(
                              circle_id, axis_id, contact_id)
                        : radial_axis_contact.add_tangent_constraint(
                              axis_id, circle_id, contact_id));
                    static_cast<void>(radial_axis_contact
                        .add_point_on_circle_constraint(contact_id, circle_id));
                    static_cast<void>(radial_axis_contact
                        .add_point_on_line_constraint(contact_id, axis_id));
                    auto radius = radial_axis_contact
                        .create_circle_radius_dimension(circle_id);
                    radial_axis_contact.apply_dimension(radius);
                    const auto radial_before = radial_axis_contact;
                    const bool radius_edited =
                        radial_axis_contact.set_dimension_value(radius.id, 7.0);
                    require(radius_edited &&
                                radial_axis_contact.solve().maximum_residual < 1.0e-7,
                            "Circle C+T axis matrix rejected a radius edit");
                    const auto* edited_center = radial_axis_contact.find_point(
                        radial_axis_contact.circles.front().center_point_id);
                    const auto* edited_contact =
                        radial_axis_contact.find_point(contact_id);
                    require(std::abs(radial_axis_contact.circles.front().radius - 7.0) <
                                1.0e-9 &&
                                std::abs(axis_id == "sketch_axis:x"
                                    ? edited_center->y - side * 7.0
                                    : edited_center->x - side * 7.0) < 1.0e-7 &&
                                std::abs(axis_id == "sketch_axis:x"
                                    ? edited_contact->y : edited_contact->x) < 1.0e-8,
                            "Circle C+T radius edit lost its signed axis contact");
                    require(radial_axis_contact.set_dimension_value(
                                radius.id, initial_radius) &&
                                radial_axis_contact.solve().maximum_residual < 1.0e-7,
                            "Circle C+T axis matrix could not return its radius");
                    for (const auto& original_point : radial_before.points) {
                        const auto* returned =
                            radial_axis_contact.find_point(original_point.id);
                        require(returned != nullptr &&
                                    std::hypot(returned->x - original_point.x,
                                        returned->y - original_point.y) < 1.0e-6,
                                "Circle C+T axis matrix accumulated coordinate drift");
                    }
                    require(radial_axis_contact.constraints ==
                                radial_before.constraints &&
                                zima::sketcher::Sketch::from_serialized(
                                    radial_axis_contact.serialized()).constraints ==
                                    radial_axis_contact.constraints,
                            "Circle C+T axis matrix changed or lost its relations");
                }
            }
        }
        auto blocked_radial_contact = zima::sketcher::Sketch::create_default();
        const auto blocked_radial_circle =
            blocked_radial_contact.add_circle(0.0, 4.0, 4.0);
        auto blocked_contact = zima::sketcher::Sketch::create_point(0.0, 0.0);
        const auto blocked_contact_id = blocked_contact.id;
        blocked_radial_contact.points.push_back(std::move(blocked_contact));
        static_cast<void>(blocked_radial_contact.add_tangent_constraint(
            blocked_radial_circle, "sketch_axis:x", blocked_contact_id));
        static_cast<void>(blocked_radial_contact.add_point_on_circle_constraint(
            blocked_contact_id, blocked_radial_circle));
        static_cast<void>(blocked_radial_contact.add_point_on_line_constraint(
            blocked_contact_id, "sketch_axis:x"));
        auto blocked_radius = blocked_radial_contact.create_circle_radius_dimension(
            blocked_radial_circle);
        blocked_radial_contact.apply_dimension(blocked_radius);
        blocked_radial_contact.find_point(
            blocked_radial_contact.circles.front().center_point_id)->fixed = true;
        const auto blocked_radial_before = blocked_radial_contact;
        require(!blocked_radial_contact.set_dimension_value(
                    blocked_radius.id, 7.0) &&
                    blocked_radial_contact.points == blocked_radial_before.points &&
                    blocked_radial_contact.circles == blocked_radial_before.circles &&
                    blocked_radial_contact.constraints ==
                        blocked_radial_before.constraints &&
                    blocked_radial_contact.dimensions ==
                        blocked_radial_before.dimensions,
                "Blocked Circle C+T radius edit was not rejected atomically");
        for (const std::string axis_id : {std::string{"sketch_axis:x"},
                 std::string{"sketch_axis:y"}}) {
            for (const double side : {-1.0, 1.0}) {
                for (const bool reversed_order : {false, true}) {
                    auto elliptic_axis_contact =
                        zima::sketcher::Sketch::create_default();
                    constexpr double major_radius = 5.0;
                    constexpr double minor_radius = 2.0;
                    const double center_x = axis_id == "sketch_axis:x"
                        ? 3.0 : side * minor_radius;
                    const double center_y = axis_id == "sketch_axis:x"
                        ? side * minor_radius : -3.0;
                    const auto ellipse_id = axis_id == "sketch_axis:x"
                        ? elliptic_axis_contact.add_ellipse(
                              center_x, center_y,
                              center_x + major_radius, center_y,
                              center_x, center_y + minor_radius)
                        : elliptic_axis_contact.add_ellipse(
                              center_x, center_y,
                              center_x, center_y + major_radius,
                              center_x - minor_radius, center_y);
                    auto contact = zima::sketcher::Sketch::create_point(
                        axis_id == "sketch_axis:x" ? center_x : 0.0,
                        axis_id == "sketch_axis:x" ? 0.0 : center_y);
                    const auto contact_id = contact.id;
                    elliptic_axis_contact.points.push_back(std::move(contact));
                    static_cast<void>(reversed_order
                        ? elliptic_axis_contact.add_tangent_constraint(
                              ellipse_id, axis_id, contact_id)
                        : elliptic_axis_contact.add_tangent_constraint(
                              axis_id, ellipse_id, contact_id));
                    static_cast<void>(elliptic_axis_contact
                        .add_point_on_circle_constraint(contact_id, ellipse_id));
                    static_cast<void>(elliptic_axis_contact
                        .add_point_on_line_constraint(contact_id, axis_id));
                    auto minor_dimension = elliptic_axis_contact
                        .create_ellipse_radius_dimension(ellipse_id, false);
                    elliptic_axis_contact.apply_dimension(minor_dimension);
                    const auto elliptic_before = elliptic_axis_contact;
                    require(elliptic_axis_contact.set_dimension_value(
                                minor_dimension.id, 3.5) &&
                                elliptic_axis_contact.solve().maximum_residual < 1.0e-7,
                            "Ellipse C+T axis matrix rejected a minor-radius edit");
                    const auto* edited_center = elliptic_axis_contact.find_point(
                        elliptic_axis_contact.ellipses.front().center_point_id);
                    const auto* edited_contact =
                        elliptic_axis_contact.find_point(contact_id);
                    require(std::abs(elliptic_axis_contact.ellipses.front()
                                    .minor_radius - 3.5) < 1.0e-9 &&
                                std::abs(axis_id == "sketch_axis:x"
                                    ? edited_center->y - side * 3.5
                                    : edited_center->x - side * 3.5) < 1.0e-7 &&
                                std::abs(axis_id == "sketch_axis:x"
                                    ? edited_contact->y : edited_contact->x) < 1.0e-8,
                            "Ellipse C+T minor-radius edit lost its axis contact");
                    require(elliptic_axis_contact.set_dimension_value(
                                minor_dimension.id, minor_radius) &&
                                elliptic_axis_contact.solve().maximum_residual < 1.0e-7,
                            "Ellipse C+T axis matrix could not return its minor radius");
                    for (const auto& original_point : elliptic_before.points) {
                        const auto* returned =
                            elliptic_axis_contact.find_point(original_point.id);
                        require(returned != nullptr &&
                                    std::hypot(returned->x - original_point.x,
                                        returned->y - original_point.y) < 1.0e-6,
                                "Ellipse C+T axis matrix accumulated coordinate drift");
                    }
                }
            }
        }
        auto common_tangent = zima::sketcher::Sketch::create_default();
        const auto common_first = common_tangent.add_circle(0.0, 0.0, 2.0);
        const auto common_second = common_tangent.add_circle(10.0, 0.0, 3.0);
        const auto common_segment = common_tangent.add_common_tangent_segment(
            common_first, {0.0, 2.0}, common_second, {10.0, 3.0});
        const auto created_common = std::find_if(common_tangent.segments.begin(),
            common_tangent.segments.end(), [&](const auto& value) {
                return value.id == common_segment;
            });
        require(created_common != common_tangent.segments.end(),
            "Common tangent did not create its segment");
        require(std::count_if(common_tangent.constraints.begin(),
                    common_tangent.constraints.end(), [&](const auto& constraint) {
                        return constraint.kind ==
                                zima::sketcher::ConstraintKind::Tangent &&
                            (constraint.geometry_id == common_segment ||
                             constraint.second_geometry_id == common_segment);
                    }) == 2,
            "Common tangent does not persist both tangent relations");
        auto lower_common_tangent = zima::sketcher::Sketch::create_default();
        const auto lower_first = lower_common_tangent.add_circle(0.0, 0.0, 2.0);
        const auto lower_second = lower_common_tangent.add_circle(10.0, 0.0, 3.0);
        const auto lower_segment_id =
            lower_common_tangent.add_common_tangent_segment(
                lower_first, {0.0, -2.0}, lower_second, {10.0, -3.0});
        const auto lower_segment = std::ranges::find_if(
            lower_common_tangent.segments, [&](const auto& value) {
                return value.id == lower_segment_id;
            });
        require(lower_segment != lower_common_tangent.segments.end() &&
                lower_common_tangent.find_point(
                    lower_segment->first_point_id)->y < 0.0 &&
                lower_common_tangent.find_point(
                    lower_segment->second_point_id)->y < 0.0,
            "Common tangent ignored the lower click-selected branch");
        auto common_elliptic_tangent =
            zima::sketcher::Sketch::create_default();
        const auto common_ellipse = common_elliptic_tangent.add_ellipse(
            0.0, 0.0, 4.0, 0.0, 0.0, 2.0);
        const auto common_circle = common_elliptic_tangent.add_circle(
            12.0, 0.0, 3.0);
        static_cast<void>(common_elliptic_tangent.add_common_tangent_segment(
            common_ellipse, {0.0, 2.0}, common_circle, {12.0, 3.0}));
        require(common_elliptic_tangent.solve().status !=
                    zima::sketcher::SolveStatus::Conflicting &&
                std::count_if(common_elliptic_tangent.constraints.begin(),
                    common_elliptic_tangent.constraints.end(),
                    [](const auto& constraint) {
                        return constraint.kind ==
                            zima::sketcher::ConstraintKind::Tangent;
                    }) == 2,
            "Common ellipse/circle tangent is not parametrically stable");
        auto common_spline_tangent = zima::sketcher::Sketch::create_default();
        const auto common_first_spline = common_spline_tangent.add_bspline(
            {{-2.0, -2.0}, {2.0, -2.0}, {2.0, 2.0}, {-2.0, 2.0}},
            1, true);
        const auto common_second_spline = common_spline_tangent.add_bspline(
            {{8.0, -2.0}, {12.0, -2.0}, {12.0, 2.0}, {8.0, 2.0}},
            1, true);
        static_cast<void>(common_spline_tangent.add_common_tangent_segment(
            common_first_spline, {0.0, 2.0},
            common_second_spline, {10.0, 2.0}));
        require(common_spline_tangent.solve().status !=
                    zima::sketcher::SolveStatus::Conflicting,
            "Common B-spline tangent is not parametrically stable");
        bool duplicate_tangent_rejected = false;
        try {
            static_cast<void>(tangent.add_tangent_constraint(
                tangent_line, tangent_circle));
        } catch (const std::invalid_argument&) {
            duplicate_tangent_rejected = true;
        }
        require(duplicate_tangent_rejected,
                "Reversed duplicate Tangent constraint was accepted");
        auto tangent_from_contact = zima::sketcher::Sketch::create_default();
        const auto contact_circle = tangent_from_contact.add_circle(0.0, 0.0, 5.0);
        const auto contact_line = tangent_from_contact.add_segment(
            5.0, 0.0, 5.0, 8.0);
        const auto contact_segment = std::find_if(
            tangent_from_contact.segments.begin(),
            tangent_from_contact.segments.end(),
            [&](const auto& value) { return value.id == contact_line; });
        const auto contact_point_id = contact_segment->first_point_id;
        static_cast<void>(tangent_from_contact.add_point_on_circle_constraint(
            contact_point_id, contact_circle));
        static_cast<void>(tangent_from_contact.add_tangent_constraint(
            contact_circle, contact_line, contact_point_id));
        const auto contact_markers =
            tangent_from_contact.viewer_mesh().constraint_markers;
        require(tangent_from_contact.constraints.size() == 2 &&
                    tangent_from_contact.constraints[0].kind ==
                        zima::sketcher::ConstraintKind::PointOnCircle &&
                    tangent_from_contact.constraints[1].kind ==
                        zima::sketcher::ConstraintKind::Tangent &&
                    contact_markers.size() == 2 &&
                    contact_markers[0].label == "C" &&
                    contact_markers[1].label == "T" &&
                    contact_markers[0].reference.semantic_key !=
                        contact_markers[1].reference.semantic_key &&
                    std::abs(contact_markers[0].position.x - 5.0) < 1.0e-9 &&
                    std::abs(contact_markers[0].position.y) < 1.0e-9 &&
                    std::abs(contact_markers[1].position.x - 5.0) < 1.0e-9 &&
                    std::abs(contact_markers[1].position.y) < 1.0e-9 &&
                    tangent_from_contact.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
                "Tangent continuation did not preserve its explicit C + T contact");

        // A point is the shared topological node of the Segment and its C
        // relation to the Circle.  Editing either geometric parameter must
        // drive the remaining dependency graph instead of treating that
        // shared node as two competing endpoints.
        auto dimensioned_tangent_chain =
            zima::sketcher::Sketch::create_default();
        const auto dimensioned_tangent_circle =
            dimensioned_tangent_chain.add_circle(0.0, 0.0, 5.0);
        const auto dimensioned_tangent_segment =
            dimensioned_tangent_chain.add_segment(5.0, 0.0, 5.0, 10.0);
        const auto dimensioned_tangent_contact =
            dimensioned_tangent_chain.segments.front().first_point_id;
        static_cast<void>(dimensioned_tangent_chain
            .add_point_on_circle_constraint(
                dimensioned_tangent_contact, dimensioned_tangent_circle));
        static_cast<void>(dimensioned_tangent_chain.add_tangent_constraint(
            dimensioned_tangent_circle, dimensioned_tangent_segment,
            dimensioned_tangent_contact));
        auto tangent_radius = dimensioned_tangent_chain
            .create_circle_radius_dimension(dimensioned_tangent_circle);
        dimensioned_tangent_chain.apply_dimension(tangent_radius);
        static_cast<void>(dimensioned_tangent_chain
            .add_point_reference_constraint(
                dimensioned_tangent_chain.circles.front().center_point_id,
                "sketch_origin"));
        const bool tangent_radius_edited =
            dimensioned_tangent_chain.set_dimension_value(
                tangent_radius.id, 7.0);
        if (!tangent_radius_edited) {
            auto diagnostic = dimensioned_tangent_chain;
            diagnostic.circles.front().radius = 7.0;
            diagnostic.dimensions.front().value = 7.0;
            const auto diagnostic_result = diagnostic.solve();
            throw std::runtime_error(
                "Circle C+T radius dimension did not drive its Segment contact; "
                "manual solve status=" +
                std::to_string(static_cast<int>(diagnostic_result.status)) +
                ", residual=" +
                std::to_string(diagnostic_result.maximum_residual));
        }
        const auto* radius_center = dimensioned_tangent_chain.find_point(
            dimensioned_tangent_chain.circles.front().center_point_id);
        const auto* radius_contact = dimensioned_tangent_chain.find_point(
            dimensioned_tangent_contact);
        const auto radius_other_id =
            dimensioned_tangent_chain.segments.front().second_point_id;
        const auto* radius_other = dimensioned_tangent_chain.find_point(
            radius_other_id);
        const double radial_x = radius_contact->x - radius_center->x;
        const double radial_y = radius_contact->y - radius_center->y;
        const double tangent_x = radius_other->x - radius_contact->x;
        const double tangent_y = radius_other->y - radius_contact->y;
        require(std::abs(std::hypot(radial_x, radial_y) - 7.0) < 1.0e-7 &&
                    std::abs(radial_x * tangent_x + radial_y * tangent_y) <
                        1.0e-7 &&
                    dimensioned_tangent_chain.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
            "Circle radius edit lost its one-point C+T topology");
        auto tangent_length = dimensioned_tangent_chain
            .create_segment_dimension(dimensioned_tangent_segment);
        dimensioned_tangent_chain.apply_dimension(tangent_length);
        require(dimensioned_tangent_chain.set_dimension_value(
                    tangent_length.id, 14.0),
            "C+T Segment length dimension did not drive the free endpoint");
        const auto* length_contact = dimensioned_tangent_chain.find_point(
            dimensioned_tangent_contact);
        const auto* length_other = dimensioned_tangent_chain.find_point(
            radius_other_id);
        require(std::abs(std::hypot(
                    length_other->x - length_contact->x,
                    length_other->y - length_contact->y) - 14.0) < 1.0e-7 &&
                    dimensioned_tangent_chain.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
            "Edited C+T Segment length did not converge");
        auto dragged_tangent_chain = dimensioned_tangent_chain;
        require(dragged_tangent_chain.move_point(
                    radius_other_id, 12.0, 15.0),
            "Dragging the free C+T Segment endpoint did not move its contact");
        const auto* dragged_center = dragged_tangent_chain.find_point(
            dragged_tangent_chain.circles.front().center_point_id);
        const auto* dragged_contact = dragged_tangent_chain.find_point(
            dimensioned_tangent_contact);
        const auto* dragged_other = dragged_tangent_chain.find_point(
            radius_other_id);
        const double dragged_radial_x =
            dragged_contact->x - dragged_center->x;
        const double dragged_radial_y =
            dragged_contact->y - dragged_center->y;
        const double dragged_line_x = dragged_other->x - dragged_contact->x;
        const double dragged_line_y = dragged_other->y - dragged_contact->y;
        require(std::hypot(dragged_other->x - 12.0,
                    dragged_other->y - 15.0) < 1.0e-7 &&
                    std::abs(std::hypot(
                        dragged_radial_x, dragged_radial_y) - 7.0) < 1.0e-7 &&
                    std::abs(dragged_radial_x * dragged_line_x +
                        dragged_radial_y * dragged_line_y) < 1.0e-7,
            "Dragged C+T chain lost its cursor point, radius, or tangency");

        auto radial_contact_drag = zima::sketcher::Sketch::create_default();
        const auto radial_circle =
            radial_contact_drag.add_circle(0.0, 0.0, 5.0);
        const auto radial_segment =
            radial_contact_drag.add_segment(5.0, 0.0, 5.0, 10.0);
        const auto radial_contact =
            radial_contact_drag.segments.front().first_point_id;
        const auto radial_other =
            radial_contact_drag.segments.front().second_point_id;
        static_cast<void>(radial_contact_drag.add_point_on_circle_constraint(
            radial_contact, radial_circle));
        static_cast<void>(radial_contact_drag.add_tangent_constraint(
            radial_circle, radial_segment, radial_contact));
        require(radial_contact_drag.move_point(radial_contact, 8.0, 0.0),
            "Dragging a C+T contact did not resize its free Circle");
        const auto* radial_center = radial_contact_drag.find_point(
            radial_contact_drag.circles.front().center_point_id);
        const auto* moved_radial_contact =
            radial_contact_drag.find_point(radial_contact);
        const auto* moved_radial_other =
            radial_contact_drag.find_point(radial_other);
        require(std::hypot(radial_center->x, radial_center->y) < 1.0e-7 &&
                    std::abs(radial_contact_drag.circles.front().radius - 8.0) <
                        1.0e-7 &&
                    std::hypot(moved_radial_contact->x - 8.0,
                        moved_radial_contact->y) < 1.0e-7 &&
                    std::hypot(moved_radial_other->x - 8.0,
                        moved_radial_other->y - 10.0) < 1.0e-7,
            "C+T contact drag moved the Circle centre or lost its tangent arm");

        auto angular_tangent = zima::sketcher::Sketch::create_default();
        const auto angular_circle = angular_tangent.add_circle(0.0, 0.0, 5.0);
        const auto angular_segment =
            angular_tangent.add_segment(5.0, 0.0, 5.0, 10.0);
        const auto angular_contact =
            angular_tangent.segments.front().first_point_id;
        static_cast<void>(angular_tangent.add_point_on_circle_constraint(
            angular_contact, angular_circle));
        static_cast<void>(angular_tangent.add_tangent_constraint(
            angular_circle, angular_segment, angular_contact));
        auto angular_dimension = angular_tangent.create_segment_dimension(
            angular_segment, zima::sketcher::DimensionKind::Angle);
        angular_tangent.apply_dimension(angular_dimension);
        require(angular_tangent.set_dimension_value(
                    angular_dimension.id, 45.0),
            "Angular dimension did not drive a Circle C+T Segment");
        const auto* angular_center = angular_tangent.find_point(
            angular_tangent.circles.front().center_point_id);
        const auto* moved_angular_contact =
            angular_tangent.find_point(angular_contact);
        const auto* moved_angular_first = angular_tangent.find_point(
            angular_tangent.segments.front().first_point_id);
        const auto* moved_angular_second = angular_tangent.find_point(
            angular_tangent.segments.front().second_point_id);
        const double angular_dx =
            moved_angular_second->x - moved_angular_first->x;
        const double angular_dy =
            moved_angular_second->y - moved_angular_first->y;
        const double angular_radial_x =
            moved_angular_contact->x - angular_center->x;
        const double angular_radial_y =
            moved_angular_contact->y - angular_center->y;
        require(std::hypot(angular_center->x, angular_center->y) < 1.0e-7 &&
                    std::abs(angular_tangent.circles.front().radius - 5.0) <
                        1.0e-7 &&
                    std::abs(std::atan2(angular_dy, angular_dx) *
                            180.0 / 3.14159265358979323846 - 45.0) < 1.0e-7 &&
                    std::abs(angular_radial_x * angular_dx +
                        angular_radial_y * angular_dy) < 1.0e-7 &&
                    angular_tangent.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
            "Edited C+T angle moved the Circle or lost angle/tangency");
        for (const double side : {-1.0, 1.0}) {
            for (const bool contact_is_first : {false, true}) {
                auto ordered_chain = zima::sketcher::Sketch::create_default();
                const auto ordered_circle =
                    ordered_chain.add_circle(0.0, 0.0, 4.0);
                const std::array contact{side * 4.0, 0.0};
                const std::array other{side * 4.0, 6.0};
                const auto ordered_segment = contact_is_first
                    ? ordered_chain.add_segment(
                          contact[0], contact[1], other[0], other[1])
                    : ordered_chain.add_segment(
                          other[0], other[1], contact[0], contact[1]);
                const auto& ordered_geometry = ordered_chain.segments.front();
                const auto ordered_contact = contact_is_first
                    ? ordered_geometry.first_point_id
                    : ordered_geometry.second_point_id;
                const auto ordered_other = contact_is_first
                    ? ordered_geometry.second_point_id
                    : ordered_geometry.first_point_id;
                static_cast<void>(ordered_chain.add_point_on_circle_constraint(
                    ordered_contact, ordered_circle));
                static_cast<void>(contact_is_first
                    ? ordered_chain.add_tangent_constraint(
                          ordered_circle, ordered_segment, ordered_contact)
                    : ordered_chain.add_tangent_constraint(
                          ordered_segment, ordered_circle, ordered_contact));
                auto ordered_radius =
                    ordered_chain.create_circle_radius_dimension(ordered_circle);
                ordered_chain.apply_dimension(ordered_radius);
                static_cast<void>(ordered_chain.add_point_reference_constraint(
                    ordered_chain.circles.front().center_point_id,
                    "sketch_origin"));
                auto ordered_length =
                    ordered_chain.create_segment_dimension(ordered_segment);
                ordered_chain.apply_dimension(ordered_length);
                require(ordered_chain.set_dimension_value(
                            ordered_radius.id, 6.0) &&
                            ordered_chain.set_dimension_value(
                                ordered_length.id, 9.0),
                    "C+T radius/length edit depends on selection or endpoint order");
                const auto* ordered_center = ordered_chain.find_point(
                    ordered_chain.circles.front().center_point_id);
                const auto* ordered_contact_point =
                    ordered_chain.find_point(ordered_contact);
                const auto* ordered_other_point =
                    ordered_chain.find_point(ordered_other);
                const double ordered_rx =
                    ordered_contact_point->x - ordered_center->x;
                const double ordered_ry =
                    ordered_contact_point->y - ordered_center->y;
                const double ordered_tx =
                    ordered_other_point->x - ordered_contact_point->x;
                const double ordered_ty =
                    ordered_other_point->y - ordered_contact_point->y;
                require(std::abs(std::hypot(ordered_rx, ordered_ry) - 6.0) <
                            1.0e-7 &&
                            std::abs(std::hypot(ordered_tx, ordered_ty) - 9.0) <
                                1.0e-7 &&
                            std::abs(ordered_rx * ordered_tx +
                                ordered_ry * ordered_ty) < 1.0e-7,
                    "Ordered C+T matrix lost radius, length, or tangency");
            }
        }
        auto tangent_after_delete = loaded_tangent;
        tangent_after_delete.remove_geometry(tangent_circle);
        require(tangent_after_delete.constraints.empty() &&
                    tangent_after_delete.segments.size() == 1,
                "Deleting tangent geometry retained its constraint");
        auto tangent_polygon = zima::sketcher::Sketch::create_default();
        const auto polygon_tangent_line = tangent_polygon.add_segment(
            -10.0, 0.0, 10.0, 0.0);
        const auto tangent_polygon_result = tangent_polygon.add_regular_polygon(
            0.0, 5.0, 2.0, 5.0, 6);
        static_cast<void>(tangent_polygon.add_tangent_constraint(
            polygon_tangent_line, tangent_polygon_result.support_circle_id));
        const auto* tangent_polygon_center = tangent_polygon.find_point(
            tangent_polygon.circles.front().center_point_id);
        const auto* tangent_polygon_vertex = tangent_polygon.find_point(
            tangent_polygon_result.vertex_ids.front());
        require(std::abs(tangent_polygon_center->x) < 1.0e-8 &&
                    std::abs(tangent_polygon_center->y - 2.0) < 1.0e-8 &&
                    std::abs(tangent_polygon_vertex->x - 2.0) < 1.0e-8 &&
                    std::abs(tangent_polygon_vertex->y - 2.0) < 1.0e-8,
                "Tangent did not translate the driven circular dependency closure");
        auto tangent_arc = zima::sketcher::Sketch::create_default();
        const auto arc_tangent_line = tangent_arc.add_segment(
            -5.0, 0.0, 5.0, 0.0);
        const auto driven_tangent_arc = tangent_arc.add_arc(
            0.0, 5.0, -2.0, 5.0, 2.0, 5.0);
        static_cast<void>(tangent_arc.add_tangent_constraint(
            arc_tangent_line, driven_tangent_arc));
        require(std::abs(tangent_arc.find_point(
                    tangent_arc.arcs.front().center_point_id)->y - 2.0) < 1.0e-8 &&
                    std::abs(tangent_arc.find_point(
                        tangent_arc.arcs.front().start_point_id)->y - 2.0) < 1.0e-8 &&
                    tangent_arc.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
                "Tangent did not preserve a driven circular arc");
        auto endpoint_tangent_arc = zima::sketcher::Sketch::create_default();
        const auto endpoint_arc = endpoint_tangent_arc.add_arc(
            0.0, 0.0, 0.0, -5.0, 5.0, 0.0);
        const auto endpoint_segment = endpoint_tangent_arc.add_segment(
            5.0, 0.0, 5.0, 5.0);
        const auto endpoint_arc_value = std::ranges::find_if(
            endpoint_tangent_arc.arcs, [&](const auto& value) {
                return value.id == endpoint_arc;
            });
        const auto endpoint_segment_value = std::ranges::find_if(
            endpoint_tangent_arc.segments, [&](const auto& value) {
                return value.id == endpoint_segment;
            });
        require(endpoint_arc_value->end_point_id ==
                    endpoint_segment_value->first_point_id,
                "Endpoint Tangent fixture did not share one topology point");
        const auto tangent_free_endpoint =
            endpoint_segment_value->second_point_id;
        static_cast<void>(endpoint_tangent_arc.add_tangent_constraint(
            endpoint_arc, endpoint_segment));
        require(endpoint_tangent_arc.move_point(
                    tangent_free_endpoint, 7.0, 8.0),
                "Dragging a Segment tangent to an Arc endpoint was rejected");
        const auto* dragged_endpoint_arc_center = endpoint_tangent_arc.find_point(
            endpoint_tangent_arc.arcs.front().center_point_id);
        const auto* dragged_endpoint_arc_contact = endpoint_tangent_arc.find_point(
            endpoint_tangent_arc.arcs.front().end_point_id);
        const auto* dragged_endpoint_tangent_end = endpoint_tangent_arc.find_point(
            tangent_free_endpoint);
        const double moved_radius_x =
            dragged_endpoint_arc_contact->x - dragged_endpoint_arc_center->x;
        const double moved_radius_y =
            dragged_endpoint_arc_contact->y - dragged_endpoint_arc_center->y;
        const double moved_tangent_x =
            dragged_endpoint_tangent_end->x - dragged_endpoint_arc_contact->x;
        const double moved_tangent_y =
            dragged_endpoint_tangent_end->y - dragged_endpoint_arc_contact->y;
        require(std::abs(dragged_endpoint_tangent_end->x - 7.0) < 1.0e-8 &&
                    std::abs(dragged_endpoint_tangent_end->y - 8.0) < 1.0e-8 &&
                    std::abs(std::hypot(moved_radius_x, moved_radius_y) -
                        5.0) < 1.0e-7 &&
                    std::abs(moved_radius_x * moved_tangent_x +
                        moved_radius_y * moved_tangent_y) < 1.0e-7 &&
                    endpoint_tangent_arc.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
                "Dragged Segment endpoint lost its Arc endpoint tangency");
        auto outside_tangent_arc = zima::sketcher::Sketch::create_default();
        const auto outside_tangent_line = outside_tangent_arc.add_segment(
            -5.0, 0.0, 5.0, 0.0);
        const auto outside_arc = outside_tangent_arc.add_arc(
            0.0, 5.0, 2.0, 5.0, -2.0, 5.0);
        const auto outside_tangent_before = outside_tangent_arc;
        bool outside_tangent_rejected = false;
        try {
            static_cast<void>(outside_tangent_arc.add_tangent_constraint(
                outside_tangent_line, outside_arc));
        } catch (const std::invalid_argument&) {
            outside_tangent_rejected = true;
        }
        require(outside_tangent_rejected &&
                    outside_tangent_arc.points == outside_tangent_before.points &&
                    outside_tangent_arc.arcs == outside_tangent_before.arcs &&
                    outside_tangent_arc.constraints ==
                        outside_tangent_before.constraints,
                "Tangent accepted a contact outside the selected arc");
        auto outside_tangent_segment = zima::sketcher::Sketch::create_default();
        const auto short_tangent_line = outside_tangent_segment.add_segment(
            10.0, 0.0, 20.0, 0.0);
        const auto distant_tangent_circle = outside_tangent_segment.add_circle(
            0.0, 5.0, 2.0);
        bool outside_segment_rejected = false;
        try {
            static_cast<void>(outside_tangent_segment.add_tangent_constraint(
                short_tangent_line, distant_tangent_circle));
        } catch (const std::invalid_argument&) {
            outside_segment_rejected = true;
        }
        require(outside_segment_rejected &&
                    outside_tangent_segment.constraints.empty(),
                "Tangent accepted a contact outside the finite segment");
        auto fixed_tangent = zima::sketcher::Sketch::create_default();
        const auto fixed_tangent_circle = fixed_tangent.add_circle(0.0, 5.0, 2.0);
        const auto fixed_tangent_line = fixed_tangent.add_segment(
            -5.0, 0.0, 5.0, 0.0);
        fixed_tangent.find_point(
            fixed_tangent.segments.front().first_point_id)->fixed = true;
        const auto fixed_tangent_before = fixed_tangent;
        bool fixed_tangent_rejected = false;
        try {
            static_cast<void>(fixed_tangent.add_tangent_constraint(
                fixed_tangent_circle, fixed_tangent_line));
        } catch (const std::runtime_error&) {
            fixed_tangent_rejected = true;
        }
        require(fixed_tangent_rejected &&
                    fixed_tangent.points == fixed_tangent_before.points &&
                    fixed_tangent.constraints == fixed_tangent_before.constraints,
                "Blocked Tangent relation partially changed the Sketch");
        auto tangent_ellipse = zima::sketcher::Sketch::create_default();
        const auto ellipse_tangent_line = tangent_ellipse.add_segment(
            -10.0, 0.0, 10.0, 0.0);
        const auto driven_tangent_ellipse = tangent_ellipse.add_ellipse(
            0.0, 5.0, 4.0, 5.0, 0.0, 7.0);
        static_cast<void>(tangent_ellipse.add_tangent_constraint(
            ellipse_tangent_line, driven_tangent_ellipse));
        const auto* driven_ellipse_center = tangent_ellipse.find_point(
            tangent_ellipse.ellipses.front().center_point_id);
        const auto* driven_ellipse_major = tangent_ellipse.find_point(
            tangent_ellipse.ellipses.front().major_point_id);
        const auto* driven_ellipse_minor = tangent_ellipse.find_point(
            tangent_ellipse.ellipses.front().minor_point_id);
        require(std::abs(driven_ellipse_center->y - 2.0) < 1.0e-8 &&
                    std::abs(driven_ellipse_major->y - 2.0) < 1.0e-8 &&
                    std::abs(driven_ellipse_minor->y - 4.0) < 1.0e-8,
                "Tangent did not translate a driven ellipse rigidly");
        const auto loaded_tangent_ellipse =
            zima::sketcher::Sketch::from_serialized(tangent_ellipse.serialized());
        require(loaded_tangent_ellipse.constraints == tangent_ellipse.constraints &&
                    loaded_tangent_ellipse.ellipses == tangent_ellipse.ellipses,
                "Ellipse Tangent constraint did not survive serialization");
        auto rotated_tangent_ellipse = zima::sketcher::Sketch::create_default();
        const auto rotated_tangent_line = rotated_tangent_ellipse.add_segment(
            -10.0, 0.0, 10.0, 0.0);
        constexpr double root_half = 0.70710678118654752440;
        const auto rotated_ellipse = rotated_tangent_ellipse.add_ellipse(
            0.0, 8.0,
            4.0 * root_half, 8.0 + 4.0 * root_half,
            -2.0 * root_half, 8.0 + 2.0 * root_half);
        static_cast<void>(rotated_tangent_ellipse.add_tangent_constraint(
            rotated_tangent_line, rotated_ellipse));
        require(std::abs(rotated_tangent_ellipse.find_point(
                    rotated_tangent_ellipse.ellipses.front().center_point_id)->y -
                    std::sqrt(10.0)) < 1.0e-8,
                "Tangent used an axis-aligned approximation for a rotated ellipse");
        auto tangent_elliptical_arc = zima::sketcher::Sketch::create_default();
        const auto elliptical_arc_tangent_line =
            tangent_elliptical_arc.add_segment(-10.0, 0.0, 10.0, 0.0);
        const auto driven_tangent_elliptical_arc =
            tangent_elliptical_arc.add_elliptical_arc(
                0.0, 5.0, 4.0, 5.0, 0.0, 7.0,
                -4.0, 5.0, 4.0, 5.0);
        static_cast<void>(tangent_elliptical_arc.add_tangent_constraint(
            elliptical_arc_tangent_line, driven_tangent_elliptical_arc));
        require(std::abs(tangent_elliptical_arc.find_point(
                    tangent_elliptical_arc.elliptical_arcs.front().center_point_id)->y -
                    2.0) < 1.0e-8 &&
                    tangent_elliptical_arc.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
                "Tangent did not preserve a driven elliptical arc");
        auto outside_tangent_elliptical_arc =
            zima::sketcher::Sketch::create_default();
        const auto outside_elliptical_arc_line =
            outside_tangent_elliptical_arc.add_segment(-10.0, 0.0, 10.0, 0.0);
        const auto outside_elliptical_arc =
            outside_tangent_elliptical_arc.add_elliptical_arc(
                0.0, 5.0, 4.0, 5.0, 0.0, 7.0,
                4.0, 5.0, -4.0, 5.0);
        bool outside_elliptical_arc_rejected = false;
        try {
            static_cast<void>(outside_tangent_elliptical_arc.add_tangent_constraint(
                outside_elliptical_arc_line, outside_elliptical_arc));
        } catch (const std::invalid_argument&) {
            outside_elliptical_arc_rejected = true;
        }
        require(outside_elliptical_arc_rejected &&
                    outside_tangent_elliptical_arc.constraints.empty(),
                "Tangent accepted contact outside an elliptical arc");
        auto external_circle_tangent = zima::sketcher::Sketch::create_default();
        const auto external_reference_circle =
            external_circle_tangent.add_circle(0.0, 0.0, 2.0);
        const auto external_driven_circle =
            external_circle_tangent.add_circle(8.0, 0.0, 3.0);
        static_cast<void>(external_circle_tangent.add_tangent_constraint(
            external_reference_circle, external_driven_circle));
        require(!external_circle_tangent.constraints.front().tangent_internal &&
                    std::abs(external_circle_tangent.find_point(
                        external_circle_tangent.circles[1].center_point_id)->x -
                        5.0) < 1.0e-8 &&
                    external_circle_tangent.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
                "External circle Tangent did not translate the driven circle");
        auto internal_circle_tangent = zima::sketcher::Sketch::create_default();
        const auto internal_reference_circle =
            internal_circle_tangent.add_circle(0.0, 0.0, 5.0);
        const auto internal_driven_circle =
            internal_circle_tangent.add_circle(2.0, 0.0, 2.0);
        static_cast<void>(internal_circle_tangent.add_tangent_constraint(
            internal_reference_circle, internal_driven_circle));
        require(internal_circle_tangent.constraints.front().tangent_internal &&
                    std::abs(internal_circle_tangent.find_point(
                        internal_circle_tangent.circles[1].center_point_id)->x -
                        3.0) < 1.0e-8,
                "Internal circle Tangent did not select the nearest valid branch");
        const auto loaded_internal_circle_tangent =
            zima::sketcher::Sketch::from_serialized(
                internal_circle_tangent.serialized());
        require(loaded_internal_circle_tangent.constraints ==
                    internal_circle_tangent.constraints &&
                    loaded_internal_circle_tangent.constraints.front().tangent_internal,
                "Internal Tangent mode did not survive Sketch serialization");
        auto external_arc_tangent = zima::sketcher::Sketch::create_default();
        const auto external_reference_arc = external_arc_tangent.add_arc(
            0.0, 0.0, 0.0, -2.0, 0.0, 2.0);
        const auto external_driven_arc = external_arc_tangent.add_arc(
            8.0, 0.0, 8.0, 3.0, 8.0, -3.0);
        static_cast<void>(external_arc_tangent.add_tangent_constraint(
            external_reference_arc, external_driven_arc));
        require(std::abs(external_arc_tangent.find_point(
                    external_arc_tangent.arcs[1].center_point_id)->x - 5.0) <
                        1.0e-8 &&
                    std::abs(external_arc_tangent.find_point(
                        external_arc_tangent.arcs[1].start_point_id)->x - 5.0) <
                        1.0e-8 &&
                    std::abs(external_arc_tangent.find_point(
                        external_arc_tangent.arcs[1].end_point_id)->x - 5.0) <
                        1.0e-8,
                "Arc-pair Tangent did not preserve the driven arc closure");
        auto outside_curve_pair = zima::sketcher::Sketch::create_default();
        const auto outside_reference_arc = outside_curve_pair.add_arc(
            0.0, 0.0, 0.0, -2.0, 0.0, 2.0);
        const auto outside_driven_arc = outside_curve_pair.add_arc(
            8.0, 0.0, 8.0, -3.0, 8.0, 3.0);
        const auto outside_curve_pair_before = outside_curve_pair;
        bool outside_curve_pair_rejected = false;
        try {
            static_cast<void>(outside_curve_pair.add_tangent_constraint(
                outside_reference_arc, outside_driven_arc));
        } catch (const std::invalid_argument&) {
            outside_curve_pair_rejected = true;
        }
        require(outside_curve_pair_rejected &&
                    outside_curve_pair.points == outside_curve_pair_before.points &&
                    outside_curve_pair.constraints.empty(),
                "Curve-pair Tangent accepted contact outside an arc domain");
        auto fixed_curve_pair = zima::sketcher::Sketch::create_default();
        const auto fixed_reference_circle =
            fixed_curve_pair.add_circle(0.0, 0.0, 2.0);
        const auto fixed_driven_circle =
            fixed_curve_pair.add_circle(8.0, 0.0, 3.0);
        fixed_curve_pair.find_point(
            fixed_curve_pair.circles[1].center_point_id)->fixed = true;
        const auto fixed_curve_pair_before = fixed_curve_pair;
        bool fixed_curve_pair_rejected = false;
        try {
            static_cast<void>(fixed_curve_pair.add_tangent_constraint(
                fixed_reference_circle, fixed_driven_circle));
        } catch (const std::runtime_error&) {
            fixed_curve_pair_rejected = true;
        }
        require(fixed_curve_pair_rejected &&
                    fixed_curve_pair.points == fixed_curve_pair_before.points &&
                    fixed_curve_pair.constraints ==
                        fixed_curve_pair_before.constraints,
                "Blocked curve-pair Tangent partially changed the Sketch");
        auto elliptic_curve_pair = zima::sketcher::Sketch::create_default();
        const auto elliptic_curve_circle =
            elliptic_curve_pair.add_circle(0.0, 0.0, 2.0);
        const auto elliptic_curve_ellipse = elliptic_curve_pair.add_ellipse(
            8.0, 0.0, 12.0, 0.0, 8.0, 2.0);
        static_cast<void>(elliptic_curve_pair.add_tangent_constraint(
            elliptic_curve_circle, elliptic_curve_ellipse));
        const auto* translated_ellipse_center = elliptic_curve_pair.find_point(
            elliptic_curve_pair.ellipses.front().center_point_id);
        require(elliptic_curve_pair.constraints.size() == 1 &&
                    translated_ellipse_center != nullptr &&
                    std::abs(translated_ellipse_center->x - 6.0) < 1.0e-5 &&
                    std::abs(translated_ellipse_center->y) < 1.0e-5 &&
                    elliptic_curve_pair.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
                "Circle-to-ellipse Tangent did not create and solve the contact");
        auto loaded_elliptic_tangent =
            zima::sketcher::Sketch::from_serialized(
                elliptic_curve_pair.serialized());
        require(loaded_elliptic_tangent.constraints ==
                    elliptic_curve_pair.constraints &&
                    loaded_elliptic_tangent.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
                "Elliptic curve-pair Tangent did not survive serialization");
        auto spline_tangent = zima::sketcher::Sketch::create_default();
        const auto tangent_spline = spline_tangent.add_bspline({
            {0.0, 0.0}, {10.0, 20.0}, {20.0, -10.0}, {30.0, 0.0}});
        const auto spline_tangent_handle_id =
            spline_tangent.bsplines.front().control_point_ids[1];
        const auto spline_tangent_line = spline_tangent.add_segment(
            0.0, 0.0, 10.0, 0.0);
        const auto spline_tangent_id = spline_tangent.add_tangent_constraint(
            tangent_spline, spline_tangent_line);
        const auto spline_tangent_direction = spline_tangent.curve_tangent_at_point(
            tangent_spline, 0.0, 0.0);
        const auto spline_tangent_segment = std::ranges::find_if(
            spline_tangent.segments, [&](const auto& value) {
                return value.id == spline_tangent_line;
            });
        const auto* spline_tangent_first = spline_tangent.find_point(
            spline_tangent_segment->first_point_id);
        const auto* spline_tangent_second = spline_tangent.find_point(
            spline_tangent_segment->second_point_id);
        const auto spline_tangent_line_end_id =
            spline_tangent_segment->second_point_id;
        const auto* spline_tangent_handle = spline_tangent.find_point(
            spline_tangent_handle_id);
        const double tangent_segment_length = std::hypot(
            spline_tangent_second->x - spline_tangent_first->x,
            spline_tangent_second->y - spline_tangent_first->y);
        const double spline_tangent_cross = spline_tangent_direction
            ? std::abs(
                (spline_tangent_second->x - spline_tangent_first->x) /
                    tangent_segment_length * (*spline_tangent_direction)[1] -
                (spline_tangent_second->y - spline_tangent_first->y) /
                    tangent_segment_length * (*spline_tangent_direction)[0])
            : 1.0;
        const auto loaded_spline_tangent =
            zima::sketcher::Sketch::from_serialized(spline_tangent.serialized());
        require(spline_tangent_direction && spline_tangent_cross < 1.0e-6 &&
                    spline_tangent_first != nullptr &&
                    spline_tangent_second != nullptr &&
                    std::abs(spline_tangent_first->x) < 1.0e-9 &&
                    std::abs(spline_tangent_first->y) < 1.0e-9 &&
                    std::abs(spline_tangent_second->x - 10.0) < 1.0e-9 &&
                    std::abs(spline_tangent_second->y) < 1.0e-9 &&
                    spline_tangent_handle != nullptr &&
                    std::abs(spline_tangent_handle->y) < 1.0e-6 &&
                    spline_tangent.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting &&
                    loaded_spline_tangent.constraints.size() == 1 &&
                    loaded_spline_tangent.constraints.front().id ==
                        spline_tangent_id &&
                    loaded_spline_tangent.constraints.front().kind ==
                        zima::sketcher::ConstraintKind::Tangent,
                "B-spline Tangent did not solve or survive serialization");
        const auto spline_tangent_before_drag = spline_tangent;
        require(spline_tangent.move_point(
                    spline_tangent_line_end_id, 10.0, 10.0) &&
                spline_tangent.solve().status !=
                    zima::sketcher::SolveStatus::Conflicting,
            "Connected line could not drive an open B-spline endpoint tangent");
        const auto* rotated_spline_handle = spline_tangent.find_point(
            spline_tangent_handle_id);
        const auto* rotated_line_end = spline_tangent.find_point(
            spline_tangent_line_end_id);
        require(rotated_spline_handle != nullptr && rotated_line_end != nullptr &&
                    std::abs((rotated_spline_handle->x) * rotated_line_end->y -
                        (rotated_spline_handle->y) * rotated_line_end->x) < 1.0e-6,
            "Line-driven B-spline endpoint handle lost Tangent");
        require(spline_tangent.move_point(
                    spline_tangent_line_end_id, 10.0, 0.0),
            "Connected B-spline tangent could not return to its initial branch");
        double spline_tangent_drift{};
        for (const auto& original : spline_tangent_before_drag.points) {
            const auto* restored = spline_tangent.find_point(original.id);
            spline_tangent_drift = std::max(spline_tangent_drift,
                restored == nullptr
                    ? std::numeric_limits<double>::infinity()
                    : std::hypot(restored->x - original.x,
                                 restored->y - original.y));
        }
        require(spline_tangent_drift < 1.0e-6,
            "Connected B-spline tangent accumulated forward/back branch drift");
        auto fixed_spline_handle = spline_tangent_before_drag;
        fixed_spline_handle.find_point(spline_tangent_handle_id)->fixed = true;
        const auto fixed_spline_before = fixed_spline_handle.serialized();
        require(!fixed_spline_handle.move_point(
                    spline_tangent_line_end_id, 10.0, 10.0) &&
                fixed_spline_handle.serialized() == fixed_spline_before,
            "Fixed B-spline tangent handle did not reject the driven line atomically");
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
        auto equal_radius = zima::sketcher::Sketch::create_default();
        const auto radius_reference = equal_radius.add_circle(0.0, 0.0, 5.0);
        const auto radius_driven = equal_radius.add_circle(12.0, 0.0, 2.0);
        static_cast<void>(equal_radius.add_equal_radius_constraint(
            radius_reference, radius_driven));
        require(equal_radius.constraints.size() == 1 &&
                    equal_radius.constraints.front().kind ==
                        zima::sketcher::ConstraintKind::EqualRadius &&
                    std::abs(equal_radius.circles[1].radius - 5.0) < 1.0e-8 &&
                    std::abs(equal_radius.find_point(
                        equal_radius.circles[1].center_point_id)->x - 12.0) < 1.0e-8,
                "EqualRadius moved the driven centre or missed the reference radius");
        const auto loaded_equal_radius = zima::sketcher::Sketch::from_serialized(
            equal_radius.serialized());
        require(loaded_equal_radius.constraints == equal_radius.constraints &&
                    loaded_equal_radius.circles == equal_radius.circles,
                "EqualRadius did not survive Sketch serialization");
        auto equal_radius_after_delete = loaded_equal_radius;
        equal_radius_after_delete.remove_geometry(radius_driven);
        require(equal_radius_after_delete.constraints.empty(),
                "Deleting an EqualRadius owner retained its constraint");
        bool duplicate_equal_radius_rejected = false;
        try {
            static_cast<void>(equal_radius.add_equal_radius_constraint(
                radius_driven, radius_reference));
        } catch (const std::invalid_argument&) {
            duplicate_equal_radius_rejected = true;
        }
        require(duplicate_equal_radius_rejected,
                "Reversed duplicate EqualRadius constraint was accepted");
        auto equal_arc_radius = zima::sketcher::Sketch::create_default();
        const auto arc_radius_reference = equal_arc_radius.add_circle(
            0.0, 0.0, 4.0);
        const auto arc_radius_driven = equal_arc_radius.add_arc(
            10.0, 0.0, 12.0, 0.0, 10.0, 2.0);
        static_cast<void>(equal_arc_radius.add_equal_radius_constraint(
            arc_radius_reference, arc_radius_driven));
        require(std::abs(equal_arc_radius.arcs.front().radius - 4.0) < 1.0e-8 &&
                    std::abs(equal_arc_radius.find_point(
                        equal_arc_radius.arcs.front().start_point_id)->x - 14.0) <
                        1.0e-8 &&
                    std::abs(equal_arc_radius.find_point(
                        equal_arc_radius.arcs.front().end_point_id)->y - 4.0) <
                        1.0e-8,
                "EqualRadius did not resize the driven arc and its endpoints");
        auto equal_polygon_radius = zima::sketcher::Sketch::create_default();
        const auto polygon_radius_reference =
            equal_polygon_radius.add_circle(-10.0, 0.0, 6.0);
        const auto equal_radius_polygon = equal_polygon_radius.add_regular_polygon(
            10.0, 0.0, 12.0, 0.0, 6);
        static_cast<void>(equal_polygon_radius.add_equal_radius_constraint(
            polygon_radius_reference, equal_radius_polygon.support_circle_id));
        const auto* equal_polygon_center = equal_polygon_radius.find_point(
            equal_polygon_radius.circles[1].center_point_id);
        require(std::all_of(
                    equal_radius_polygon.vertex_ids.begin(),
                    equal_radius_polygon.vertex_ids.end(),
                    [&](const auto& point_id) {
                        const auto* point = equal_polygon_radius.find_point(point_id);
                        return std::abs(std::hypot(
                            point->x - equal_polygon_center->x,
                            point->y - equal_polygon_center->y) - 6.0) < 1.0e-7;
                    }) &&
                    equal_polygon_radius.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
                "EqualRadius did not resize a PointOnCircle dependency closure");
        auto fixed_equal_radius = zima::sketcher::Sketch::create_default();
        const auto fixed_radius_reference =
            fixed_equal_radius.add_circle(0.0, 0.0, 4.0);
        const auto fixed_radius_arc = fixed_equal_radius.add_arc(
            10.0, 0.0, 12.0, 0.0, 10.0, 2.0);
        fixed_equal_radius.find_point(
            fixed_equal_radius.arcs.front().start_point_id)->fixed = true;
        const auto fixed_equal_radius_before = fixed_equal_radius;
        bool fixed_equal_radius_rejected = false;
        try {
            static_cast<void>(fixed_equal_radius.add_equal_radius_constraint(
                fixed_radius_reference, fixed_radius_arc));
        } catch (const std::runtime_error&) {
            fixed_equal_radius_rejected = true;
        }
        require(fixed_equal_radius_rejected &&
                    fixed_equal_radius.points == fixed_equal_radius_before.points &&
                    fixed_equal_radius.arcs == fixed_equal_radius_before.arcs &&
                    fixed_equal_radius.constraints ==
                        fixed_equal_radius_before.constraints,
                "Blocked EqualRadius relation partially changed the Sketch");
        auto dimensioned_equal_radius = zima::sketcher::Sketch::create_default();
        const auto dimensioned_radius_reference =
            dimensioned_equal_radius.add_circle(0.0, 0.0, 5.0);
        const auto dimensioned_radius_driven =
            dimensioned_equal_radius.add_circle(12.0, 0.0, 2.0);
        dimensioned_equal_radius.apply_dimension(
            dimensioned_equal_radius.create_circle_radius_dimension(
                dimensioned_radius_driven));
        const auto dimensioned_equal_radius_before = dimensioned_equal_radius;
        bool dimensioned_equal_radius_rejected = false;
        try {
            static_cast<void>(dimensioned_equal_radius.add_equal_radius_constraint(
                dimensioned_radius_reference, dimensioned_radius_driven));
        } catch (const std::runtime_error&) {
            dimensioned_equal_radius_rejected = true;
        }
        require(dimensioned_equal_radius_rejected &&
                    dimensioned_equal_radius.circles ==
                        dimensioned_equal_radius_before.circles &&
                    dimensioned_equal_radius.constraints ==
                        dimensioned_equal_radius_before.constraints,
                "Conflicting radius dimension did not reject EqualRadius transactionally");
        auto unsupported_equal_radius = zima::sketcher::Sketch::create_default();
        const auto unsupported_equal_circle =
            unsupported_equal_radius.add_circle(0.0, 0.0, 3.0);
        const auto unsupported_equal_ellipse = unsupported_equal_radius.add_ellipse(
            10.0, 0.0, 14.0, 0.0, 10.0, 2.0);
        bool unsupported_equal_radius_rejected = false;
        try {
            static_cast<void>(unsupported_equal_radius.add_equal_radius_constraint(
                unsupported_equal_circle, unsupported_equal_ellipse));
        } catch (const std::invalid_argument&) {
            unsupported_equal_radius_rejected = true;
        }
        require(unsupported_equal_radius_rejected,
                "EqualRadius accepted an unsupported ellipse");
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
        require(std::all_of(rectangle.constraints.begin(),
                            rectangle.constraints.end(), [](const auto& value) {
                    return value.geometry_id.empty() &&
                        (value.kind == zima::sketcher::ConstraintKind::Horizontal ||
                         value.kind == zima::sketcher::ConstraintKind::Vertical);
                }) &&
                    rectangle.constraints[0].first_point_id ==
                        rectangle.segments[0].first_point_id &&
                    rectangle.constraints[0].second_point_id ==
                        rectangle.segments[0].second_point_id &&
                    rectangle.constraints[3].first_point_id ==
                        rectangle.segments[0].first_point_id &&
                    rectangle.constraints[3].second_point_id ==
                        rectangle.segments[2].second_point_id,
                "Rectangle directions were not persisted as ordered point-pair H/V relations");
        auto axis_snapped_rectangle = zima::sketcher::Sketch::create_default();
        const auto axis_snapped_ids = axis_snapped_rectangle.add_rectangle(
            5.0, 0.0, 30.0, 20.0);
        const auto& axis_snapped_first = *std::find_if(
            axis_snapped_rectangle.segments.begin(),
            axis_snapped_rectangle.segments.end(), [&](const auto& segment) {
                return segment.id == axis_snapped_ids.front();
            });
        static_cast<void>(axis_snapped_rectangle.add_point_on_line_constraint(
            axis_snapped_first.first_point_id, "sketch_axis:x"));
        require(std::count_if(axis_snapped_rectangle.constraints.begin(),
                    axis_snapped_rectangle.constraints.end(), [](const auto& value) {
                        return value.kind ==
                                zima::sketcher::ConstraintKind::PointOnLine &&
                            value.geometry_id == "sketch_axis:x";
                    }) == 1,
                "Rectangle first corner did not retain its axis coincidence");
        auto vertically_dimensioned_rectangle =
            zima::sketcher::Sketch::create_default();
        const auto vertically_dimensioned_ids =
            vertically_dimensioned_rectangle.add_rectangle(
                0.0, 0.0, 30.0, -20.0);
        const auto& initially_vertical_segment = *std::find_if(
            vertically_dimensioned_rectangle.segments.begin(),
            vertically_dimensioned_rectangle.segments.end(),
            [&](const auto& segment) {
                return segment.id == vertically_dimensioned_ids.front();
            });
        static_cast<void>(
            vertically_dimensioned_rectangle.add_point_reference_constraint(
                initially_vertical_segment.first_point_id, "sketch_origin"));
        auto vertical_dimension =
            vertically_dimensioned_rectangle.create_segment_dimension(
                vertically_dimensioned_ids[3],
                zima::sketcher::DimensionKind::DistanceY);
        vertically_dimensioned_rectangle.apply_dimension(vertical_dimension);
        const auto vertical_dimension_id = vertical_dimension.id;
        require(vertically_dimensioned_rectangle.set_dimension_value(
                    vertical_dimension_id, 35.0),
                "Rectangle vertical dimension rejected an edited value");
        const auto& resized_vertical_segment = *std::find_if(
            vertically_dimensioned_rectangle.segments.begin(),
            vertically_dimensioned_rectangle.segments.end(),
            [&](const auto& segment) {
                return segment.id == vertically_dimensioned_ids[3];
            });
        const auto* resized_vertical_first =
            vertically_dimensioned_rectangle.find_point(
                resized_vertical_segment.first_point_id);
        const auto* resized_vertical_second =
            vertically_dimensioned_rectangle.find_point(
                resized_vertical_segment.second_point_id);
        require(resized_vertical_first != nullptr &&
                    resized_vertical_second != nullptr &&
                    std::abs((resized_vertical_second->y -
                    resized_vertical_first->y) - 35.0) < 1.0e-7,
                "Editing a rectangle vertical dimension did not resize it");
        auto doubly_dimensioned_rectangle =
            zima::sketcher::Sketch::create_default();
        const auto doubly_dimensioned_ids =
            doubly_dimensioned_rectangle.add_rectangle(
                0.0, 0.0, 121.26486206054688, -70.0);
        auto height_dimension =
            doubly_dimensioned_rectangle.create_segment_dimension(
                doubly_dimensioned_ids[1]);
        height_dimension.value = 70.0;
        doubly_dimensioned_rectangle.apply_dimension(height_dimension);
        auto width_dimension =
            doubly_dimensioned_rectangle.create_segment_dimension(
                doubly_dimensioned_ids[2]);
        doubly_dimensioned_rectangle.apply_dimension(width_dimension);
        require(doubly_dimensioned_rectangle.set_dimension_value(
                    width_dimension.id, 100.0),
                "Rectangle width rejected an edited ordinary length dimension");
        const auto& resized_width_segment = *std::find_if(
            doubly_dimensioned_rectangle.segments.begin(),
            doubly_dimensioned_rectangle.segments.end(),
            [&](const auto& segment) {
                return segment.id == doubly_dimensioned_ids[2];
            });
        const auto* resized_width_first = doubly_dimensioned_rectangle.find_point(
            resized_width_segment.first_point_id);
        const auto* resized_width_second = doubly_dimensioned_rectangle.find_point(
            resized_width_segment.second_point_id);
        require(resized_width_first != nullptr && resized_width_second != nullptr &&
                    std::abs(std::hypot(
                        resized_width_second->x - resized_width_first->x,
                        resized_width_second->y - resized_width_first->y) -
                        100.0) < 1.0e-7,
                "Editing the 121.265 mm rectangle width did not resize it");
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
        auto oriented_rectangle = zima::sketcher::Sketch::create_default();
        const auto rectangle_axis = oriented_rectangle.add_segment(
            -20.0, 0.0, 20.0, 0.0, 1.0e-6, true);
        const auto oriented_ids = oriented_rectangle.add_oriented_rectangle(
            0.0, 4.0, 12.0, 7.0, rectangle_axis);
        require(oriented_ids.size() == 4 &&
                    oriented_rectangle.segments.size() == 5 &&
                    std::count_if(oriented_rectangle.constraints.begin(),
                        oriented_rectangle.constraints.end(), [](const auto& value) {
                            return value.kind ==
                                zima::sketcher::ConstraintKind::Symmetric;
                        }) == 2 &&
                    std::count_if(oriented_rectangle.constraints.begin(),
                        oriented_rectangle.constraints.end(), [](const auto& value) {
                            return value.kind ==
                                zima::sketcher::ConstraintKind::Parallel;
                        }) == 1,
                "Oriented rectangle did not persist its axis symmetry and direction");
        const auto* oriented_a = oriented_rectangle.find_point(
            oriented_rectangle.segments[2].first_point_id);
        const auto* oriented_b = oriented_rectangle.find_point(
            oriented_rectangle.segments[2].second_point_id);
        require(std::abs(oriented_a->y + oriented_b->y) < 1.0e-7 &&
                    std::abs(oriented_a->x - oriented_b->x) < 1.0e-7,
                "Oriented rectangle was not mirrored around its construction axis");
        auto base_axis_rectangle = zima::sketcher::Sketch::create_default();
        const auto base_axis_ids = base_axis_rectangle.add_oriented_rectangle(
            0.0, 4.0, 12.0, 7.0, "sketch_axis:x");
        require(base_axis_ids.size() == 4 &&
                    base_axis_rectangle.segments.size() == 4 &&
                    std::count_if(base_axis_rectangle.constraints.begin(),
                        base_axis_rectangle.constraints.end(), [](const auto& value) {
                            return value.kind ==
                                zima::sketcher::ConstraintKind::Symmetric &&
                                value.geometry_id == "sketch_axis:x";
                        }) == 2,
                "Oriented rectangle did not accept the persisted Sketch X axis");
        const auto loaded_oriented = zima::sketcher::Sketch::from_serialized(
            oriented_rectangle.serialized());
        require(loaded_oriented.segments == oriented_rectangle.segments &&
                    loaded_oriented.constraints == oriented_rectangle.constraints,
                "Oriented rectangle did not survive Sketch persistence");
        auto polyline_arc = zima::sketcher::Sketch::create_default();
        const auto tangent_segment = polyline_arc.add_segment(
            0.0, 0.0, 10.0, 0.0);
        const auto tangent_start = polyline_arc.segments.front().second_point_id;
        const auto tangent_arc_id = polyline_arc.add_tangent_arc(
            tangent_start, 20.0, 10.0, tangent_segment);
        const auto created_arc = std::find_if(
            polyline_arc.arcs.begin(), polyline_arc.arcs.end(),
            [&](const auto& value) { return value.id == tangent_arc_id; });
        const auto* tangent_center = polyline_arc.find_point(
            created_arc->center_point_id);
        const auto* tangent_contact = polyline_arc.find_point(tangent_start);
        require(created_arc != polyline_arc.arcs.end() &&
                    std::abs(tangent_center->x - tangent_contact->x) < 1.0e-7 &&
                    std::count_if(polyline_arc.constraints.begin(),
                        polyline_arc.constraints.end(), [](const auto& value) {
                            return value.kind == zima::sketcher::ConstraintKind::Tangent;
                        }) == 1 &&
                    std::ranges::find_if(polyline_arc.constraints,
                        [](const auto& value) {
                            return value.kind ==
                                zima::sketcher::ConstraintKind::Tangent;
                        })->first_point_id == tangent_start,
                "Polyline arc did not start tangentially from its preceding segment");
        auto tangent_source_length =
            polyline_arc.create_segment_dimension(tangent_segment);
        polyline_arc.apply_dimension(tangent_source_length);
        const bool tangent_source_length_edited =
            polyline_arc.set_dimension_value(
                tangent_source_length.id, 15.0);
        if (!tangent_source_length_edited) {
            auto diagnostic = polyline_arc;
            diagnostic.dimensions.front().value = 15.0;
            const auto diagnostic_result = diagnostic.solve();
            throw std::runtime_error(
                "Segment length at a shared Arc T contact was not editable; "
                "manual solve status=" +
                std::to_string(static_cast<int>(diagnostic_result.status)) +
                ", residual=" +
                std::to_string(diagnostic_result.maximum_residual));
        }
        const auto* resized_tangent_source_first = polyline_arc.find_point(
            polyline_arc.segments.front().first_point_id);
        const auto* resized_tangent_source_contact = polyline_arc.find_point(
            tangent_start);
        require(std::abs(std::hypot(
                    resized_tangent_source_contact->x -
                        resized_tangent_source_first->x,
                    resized_tangent_source_contact->y -
                        resized_tangent_source_first->y) - 15.0) < 1.0e-7 &&
                    polyline_arc.solve().status !=
                        zima::sketcher::SolveStatus::Conflicting,
            "Edited Segment length at the Arc T contact did not converge");
        const auto reversed_arc = polyline_arc.add_tangent_arc(
            polyline_arc.arcs.front().end_point_id,
            25.0, 5.0, tangent_arc_id, true);
        require(!reversed_arc.empty() && polyline_arc.arcs.size() == 2,
                "Reversed tangent polyline arc did not continue an arc chain");

        // Solver-training matrix for the new interactive polyline-arc states.
        // Exercise both horizontal/vertical base axes and both tangent
        // orientations so the result cannot depend on one favorable side.
        for (const bool vertical_axis : {false, true}) {
            for (const double side : {-1.0, 1.0}) {
                auto trained_arc = zima::sketcher::Sketch::create_default();
                const std::array segment_start = vertical_axis
                    ? std::array{10.0 * side, 0.0}
                    : std::array{0.0, 10.0 * side};
                const std::array contact = vertical_axis
                    ? std::array{10.0 * side, 10.0}
                    : std::array{10.0, 10.0 * side};
                const std::array endpoint = vertical_axis
                    ? std::array{0.0, 20.0}
                    : std::array{20.0, 0.0};
                const auto source = trained_arc.add_segment(
                    segment_start[0], segment_start[1],
                    contact[0], contact[1]);
                const auto source_geometry = trained_arc.segments.front();
                const auto arc_id = trained_arc.add_tangent_arc(
                    source_geometry.second_point_id,
                    endpoint[0], endpoint[1], source);
                const auto arc = std::ranges::find_if(
                    trained_arc.arcs, [&](const auto& value) {
                        return value.id == arc_id;
                    });
                require(arc != trained_arc.arcs.end(),
                    "Training tangent arc was not created");
                const auto trained_center_id = arc->center_point_id;
                static_cast<void>(trained_arc.add_point_on_line_constraint(
                    trained_center_id,
                    vertical_axis ? "sketch_axis:y" : "sketch_axis:x"));
                const auto before_round_trip = trained_arc.serialized();
                const auto solved_training_arc = trained_arc.solve();
                const auto* center = trained_arc.find_point(trained_center_id);
                require(solved_training_arc.status !=
                            zima::sketcher::SolveStatus::Conflicting &&
                        center != nullptr &&
                        std::abs(vertical_axis ? center->x : center->y) < 1.0e-7 &&
                        zima::sketcher::Sketch::from_serialized(
                            before_round_trip).solve().status ==
                            solved_training_arc.status,
                    "Tangent polyline arc center-on-axis training state is unstable");
            }
        }

        auto aligned_arc = zima::sketcher::Sketch::create_default();
        const auto aligned_source = aligned_arc.add_segment(0.0, 0.0, 10.0, 0.0);
        const auto aligned_contact =
            aligned_arc.segments.front().second_point_id;
        const auto aligned_reference = aligned_arc.add_point(30.0, 10.0);
        const auto aligned_arc_id = aligned_arc.add_tangent_arc(
            aligned_contact, 20.0, 10.0, aligned_source);
        const auto aligned_geometry = std::ranges::find_if(
            aligned_arc.arcs, [&](const auto& value) {
                return value.id == aligned_arc_id;
            });
        const auto aligned_end_id = aligned_geometry->end_point_id;
        static_cast<void>(aligned_arc.add_point_pair_constraint(
            aligned_reference, aligned_end_id,
            zima::sketcher::ConstraintKind::Horizontal));
        const auto aligned_before_drag = aligned_arc;
        auto free_reference_drag = aligned_arc;
        require(free_reference_drag.move_point(aligned_end_id, 24.0, 13.0) &&
                    std::abs(free_reference_drag.find_point(
                        aligned_reference)->y - 13.0) < 1.0e-7,
            "Free H reference did not follow the dragged tangent arc endpoint");
        const bool aligned_forward = aligned_arc.move_point(
            aligned_end_id, 24.0, 10.0);
        const auto* aligned_after_forward = aligned_arc.find_point(aligned_end_id);
        const double aligned_forward_y = aligned_after_forward
            ? aligned_after_forward->y : 9999.0;
        const auto aligned_forward_status = aligned_arc.solve().status;
        const bool aligned_back = aligned_arc.move_point(
            aligned_end_id, 20.0, 10.0);
        double aligned_round_trip_drift{};
        for (const auto& original : aligned_before_drag.points) {
            const auto* restored = aligned_arc.find_point(original.id);
            aligned_round_trip_drift = std::max(aligned_round_trip_drift,
                restored == nullptr
                    ? std::numeric_limits<double>::infinity()
                    : std::hypot(restored->x - original.x,
                                 restored->y - original.y));
        }
        require(aligned_forward &&
                aligned_after_forward != nullptr &&
                std::abs(aligned_forward_y - 10.0) < 1.0e-7 &&
                aligned_forward_status !=
                    zima::sketcher::SolveStatus::Conflicting &&
                aligned_back &&
                aligned_round_trip_drift < 1.0e-6 &&
                aligned_arc.constraints == aligned_before_drag.constraints,
            "H-aligned tangent arc endpoint did not preserve its solver branch");
        auto fixed_connected_arc = aligned_before_drag;
        fixed_connected_arc.find_point(
            fixed_connected_arc.segments.front().first_point_id)->fixed = true;
        const auto fixed_connected_before = fixed_connected_arc.serialized();
        require(!fixed_connected_arc.move_point(
                    aligned_end_id, 24.0, 10.0) &&
                fixed_connected_arc.serialized() == fixed_connected_before,
            "Fixed connected tangent arm did not reject radius propagation atomically");

        auto connected_elliptical_arc =
            zima::sketcher::Sketch::create_default();
        const auto connected_ellipse_id =
            connected_elliptical_arc.add_elliptical_arc(
                0.0, 0.0, 10.0, 0.0, 0.0, 5.0,
                10.0, 0.0, 0.0, 5.0);
        const auto connected_ellipse_geometry =
            connected_elliptical_arc.elliptical_arcs.front();
        const auto connected_ellipse_line =
            connected_elliptical_arc.add_segment(
                10.0, -10.0, 10.0, 0.0);
        static_cast<void>(connected_elliptical_arc.add_tangent_constraint(
            connected_ellipse_id, connected_ellipse_line));
        const auto connected_ellipse_before = connected_elliptical_arc;
        require(connected_elliptical_arc.move_point(
                    connected_ellipse_geometry.major_point_id, 0.0, 12.0) &&
                connected_elliptical_arc.solve().status !=
                    zima::sketcher::SolveStatus::Conflicting &&
                connected_elliptical_arc.move_point(
                    connected_ellipse_geometry.major_point_id, 10.0, 0.0),
            "Connected elliptical-arc major handle could not rotate and return");
        double connected_ellipse_drift{};
        for (const auto& original : connected_ellipse_before.points) {
            const auto* restored = connected_elliptical_arc.find_point(original.id);
            connected_ellipse_drift = std::max(connected_ellipse_drift,
                restored == nullptr
                    ? std::numeric_limits<double>::infinity()
                    : std::hypot(restored->x - original.x,
                                 restored->y - original.y));
        }
        require(connected_ellipse_drift < 1.0e-6 &&
                    connected_elliptical_arc.constraints ==
                        connected_ellipse_before.constraints,
            "Connected elliptical-arc handle accumulated branch drift");
        auto corner_fillet = zima::sketcher::Sketch::create_default();
        const auto fillet_first = corner_fillet.add_segment(
            10.0, 0.0, 0.0, 0.0);
        const auto fillet_second = corner_fillet.add_segment(
            0.0, 0.0, 0.0, 10.0);
        const auto fillet_corner_id =
            corner_fillet.segments.front().second_point_id;
        require(corner_fillet.points.size() == 3 &&
                    corner_fillet.segments.front().second_point_id ==
                        corner_fillet.segments[1].first_point_id,
                "Connected segments created two points at one Sketch position");
        const auto fillet = corner_fillet.add_corner_fillet(
            fillet_first, fillet_second, 2.0);
        const auto free_corner_dof = corner_fillet.solve();
        require(free_corner_dof.status ==
                    zima::sketcher::SolveStatus::UnderConstrained &&
                    free_corner_dof.remaining_degrees_of_freedom == 7,
                "Unannotated corner radius did not expose its seven source/parameter DOF");
        const auto evaluated_fillet = corner_fillet.evaluated_profile_sketch();
        const auto trimmed_first = std::find_if(
            evaluated_fillet.segments.begin(), evaluated_fillet.segments.end(),
            [&](const auto& segment) { return segment.id == fillet_first; });
        const auto trimmed_second = std::find_if(
            evaluated_fillet.segments.begin(), evaluated_fillet.segments.end(),
            [&](const auto& segment) { return segment.id == fillet_second; });
        const auto first_tangent_id = trimmed_first->first_point_id ==
                corner_fillet.segments[0].first_point_id
            ? trimmed_first->second_point_id : trimmed_first->first_point_id;
        const auto second_tangent_id = trimmed_second->first_point_id ==
                corner_fillet.segments[1].second_point_id
            ? trimmed_second->second_point_id : trimmed_second->first_point_id;
        const auto* fillet_first_point =
            evaluated_fillet.find_point(first_tangent_id);
        const auto* fillet_second_point =
            evaluated_fillet.find_point(second_tangent_id);
        require(!fillet.arc_id.empty() && corner_fillet.arcs.empty() &&
                    corner_fillet.corner_radii.size() == 1 &&
                    corner_fillet.corner_radii.front().id == fillet.arc_id &&
                    corner_fillet.corner_radii.front().vertex_id ==
                        fillet_corner_id &&
                    corner_fillet.segments.front().second_point_id ==
                        fillet_corner_id &&
                    corner_fillet.segments[1].first_point_id ==
                        fillet_corner_id &&
                    evaluated_fillet.arcs.size() == 1 &&
                    evaluated_fillet.arcs.front().id == fillet.arc_id &&
                    fillet_first_point != nullptr && fillet_second_point != nullptr &&
                    trimmed_first != evaluated_fillet.segments.end() &&
                    trimmed_second != evaluated_fillet.segments.end() &&
                    first_tangent_id != fillet_corner_id &&
                    second_tangent_id != fillet_corner_id &&
                    corner_fillet.find_point(fillet_corner_id) != nullptr &&
                    std::abs(std::hypot(fillet_first_point->x, fillet_first_point->y) -
                        2.0) < 1.0e-7 &&
                    std::abs(std::hypot(fillet_second_point->x, fillet_second_point->y) -
                        2.0) < 1.0e-7 &&
                    std::count_if(evaluated_fillet.constraints.begin(),
                        evaluated_fillet.constraints.end(), [](const auto& value) {
                            return value.kind == zima::sketcher::ConstraintKind::Tangent;
                        }) == 2,
                "Sketch corner radius did not preserve its source graph and "
                "evaluate stable trimmed tangent geometry");
        const auto loaded_fillet = zima::sketcher::Sketch::from_serialized(
            corner_fillet.serialized());
        require(loaded_fillet.corner_radii == corner_fillet.corner_radii &&
                    loaded_fillet.points == corner_fillet.points &&
                    loaded_fillet.segments == corner_fillet.segments,
                "Sketch corner radius did not survive persistence");
        auto coincident_corner_fillet = zima::sketcher::Sketch::create_default();
        const auto coincident_fillet_first = coincident_corner_fillet.add_segment(
            10.0, 0.0, 0.0, 0.0);
        const auto coincident_fillet_second = coincident_corner_fillet.add_segment(
            0.01, 0.0, 0.0, 10.0);
        const auto first_corner_point =
            coincident_corner_fillet.segments[0].second_point_id;
        const auto second_corner_point =
            coincident_corner_fillet.segments[1].first_point_id;
        require(first_corner_point != second_corner_point,
                "Coincident-corner fixture unexpectedly reused one point ID");
        static_cast<void>(coincident_corner_fillet.merge_points(
            first_corner_point, second_corner_point));
        static_cast<void>(coincident_corner_fillet.add_corner_fillet(
            coincident_fillet_first, coincident_fillet_second, 2.0));
        require(coincident_corner_fillet.corner_radii.size() == 1 &&
                    coincident_corner_fillet.segments[0].second_point_id ==
                        coincident_corner_fillet.segments[1].first_point_id &&
                    coincident_corner_fillet.find_point(second_corner_point) == nullptr &&
                    std::none_of(coincident_corner_fillet.constraints.begin(),
                        coincident_corner_fillet.constraints.end(),
                        [](const auto& constraint) {
                            return constraint.kind ==
                                zima::sketcher::ConstraintKind::Coincident;
                        }),
                "Corner fillet did not normalize two C-connected segment endpoints");
        const auto fillet_mesh = corner_fillet.viewer_mesh();
        const auto visible_first = corner_fillet.visible_segment_endpoints(
            fillet_first);
        require(std::count_if(fillet_mesh.points.begin(), fillet_mesh.points.end(),
                    [&](const auto& point) {
                        return point.reference.semantic_key ==
                            "point:" + fillet_corner_id;
                    }) == 1 &&
                    std::count_if(fillet_mesh.points.begin(), fillet_mesh.points.end(),
                        [](const auto& point) {
                            return point.reference.semantic_key.starts_with(
                                "corner_radius_handle:");
                        }) == 2 &&
                    std::none_of(fillet_mesh.points.begin(), fillet_mesh.points.end(),
                        [&](const auto& point) {
                            return point.reference.semantic_key.starts_with("point:") &&
                                corner_fillet.find_point(
                                    point.reference.semantic_key.substr(6)) == nullptr;
                        }) &&
                    std::any_of(fillet_mesh.edges.begin(), fillet_mesh.edges.end(),
                        [&](const auto& edge) {
                            return edge.reference.semantic_key ==
                                "corner_radius:" + fillet.arc_id;
                        }) &&
                    visible_first &&
                    std::abs((visible_first->first[0] +
                        visible_first->second[0]) * 0.5 - 6.0) < 1.0e-7 &&
                    std::any_of(fillet_mesh.points.begin(), fillet_mesh.points.end(),
                        [&](const auto& point) {
                            return point.reference.semantic_key ==
                                    "sketch_midpoint:" + fillet_first &&
                                std::abs(point.position.x - 6.0) < 1.0e-7;
                        }),
                "Corner radius View exposed derived points or lost its child arc");
        auto ordinary_segment_sketch =
            zima::sketcher::Sketch::create_default();
        const auto ordinary_segment_id = ordinary_segment_sketch.add_segment(
            10.0, 0.0, 0.0, 0.0);
        const auto ordinary_segment_dimension =
            ordinary_segment_sketch.create_segment_dimension(
                ordinary_segment_id, DimensionKind::Distance);
        auto inactive_corner = corner_fillet;
        inactive_corner.corner_radii.front().suppressed = true;
        const auto inactive_corner_dimension =
            inactive_corner.create_segment_dimension(
                fillet_first, DimensionKind::Distance);
        const auto trimmed_distance = corner_fillet.create_segment_dimension(
            fillet_first, DimensionKind::Distance);
        const auto trimmed_x = corner_fillet.create_segment_dimension(
            fillet_first, DimensionKind::DistanceX);
        const auto trimmed_y = corner_fillet.create_segment_dimension(
            fillet_second, DimensionKind::DistanceY);
        require(ordinary_segment_dimension.geometry_id.empty() &&
                    std::abs(ordinary_segment_dimension.value - 10.0) < 1.0e-9 &&
                    inactive_corner_dimension.geometry_id.empty() &&
                    std::abs(inactive_corner_dimension.value - 10.0) < 1.0e-9 &&
                    trimmed_distance.geometry_id == fillet_first &&
                    std::abs(trimmed_distance.value - 8.0) < 1.0e-9 &&
                    trimmed_x.geometry_id == fillet_first &&
                    std::abs(trimmed_x.value + 8.0) < 1.0e-9 &&
                    trimmed_y.geometry_id == fillet_second &&
                    std::abs(trimmed_y.value - 8.0) < 1.0e-9,
                "Segment dimensions did not distinguish active trimmed length "
                "from ordinary point-to-point geometry");

        auto dimensioned_corner = corner_fillet;
        dimensioned_corner.find_point(fillet_corner_id)->fixed = true;
        auto driving_trimmed = dimensioned_corner.create_segment_dimension(
            fillet_first, DimensionKind::Distance);
        driving_trimmed.value = 7.0;
        driving_trimmed.placement = std::array{5.0, -3.0};
        const auto driving_trimmed_id = driving_trimmed.id;
        dimensioned_corner.apply_dimension(driving_trimmed);
        const auto applied_visible =
            dimensioned_corner.visible_segment_endpoints(fillet_first);
        require(applied_visible &&
                    std::abs(std::hypot(
                        applied_visible->second[0] - applied_visible->first[0],
                        applied_visible->second[1] - applied_visible->first[1]) -
                        7.0) < 1.0e-7 &&
                    dimensioned_corner.set_dimension_value(
                        driving_trimmed_id, 6.0),
                "Visible corner-segment length did not converge on apply/edit");
        const auto edited_visible =
            dimensioned_corner.visible_segment_endpoints(fillet_first);
        require(edited_visible.has_value(),
                "Edited corner segment lost its visible endpoints");
        const auto edited_solve = dimensioned_corner.solve();
        const auto dimensioned_mesh = dimensioned_corner.viewer_mesh();
        const auto rendered_trimmed = std::find_if(
            dimensioned_mesh.dimensions.begin(),
            dimensioned_mesh.dimensions.end(), [&](const auto& dimension) {
                return dimension.reference.semantic_key ==
                    "dimension:" + driving_trimmed_id;
            });
        const auto expected_first = dimensioned_corner.world_point(
            edited_visible->first[0], edited_visible->first[1]);
        const auto expected_second = dimensioned_corner.world_point(
            edited_visible->second[0], edited_visible->second[1]);
        const auto world_distance = [](const auto& first, const auto& second) {
            return std::hypot(
                std::hypot(first.x - second.x, first.y - second.y),
                first.z - second.z);
        };
        require(edited_solve.status !=
                    zima::sketcher::SolveStatus::Conflicting &&
                    edited_solve.maximum_residual < 1.0e-7 &&
                    std::abs(std::hypot(
                        edited_visible->second[0] - edited_visible->first[0],
                        edited_visible->second[1] - edited_visible->first[1]) -
                        6.0) < 1.0e-7 &&
                    rendered_trimmed != dimensioned_mesh.dimensions.end() &&
                    std::abs(rendered_trimmed->value - 6.0) < 1.0e-7 &&
                    world_distance(rendered_trimmed->witness_first,
                        expected_first) < 1.0e-7 &&
                    world_distance(rendered_trimmed->witness_second,
                        expected_second) < 1.0e-7,
                "Edited visible corner-segment dimension disappeared or used "
                "the virtual sharp corner in the Viewer");

        auto signed_x_corner = corner_fillet;
        signed_x_corner.find_point(fillet_corner_id)->fixed = true;
        auto signed_x = signed_x_corner.create_segment_dimension(
            fillet_first, DimensionKind::DistanceX);
        signed_x.value = -7.0;
        const auto signed_x_id = signed_x.id;
        signed_x_corner.apply_dimension(signed_x);
        require(signed_x_corner.set_dimension_value(signed_x_id, -6.0),
                "Negative visible X dimension did not accept an edit");
        const auto signed_x_visible =
            signed_x_corner.visible_segment_endpoints(fillet_first);
        auto signed_y_corner = corner_fillet;
        signed_y_corner.find_point(fillet_corner_id)->fixed = true;
        auto signed_y = signed_y_corner.create_segment_dimension(
            fillet_second, DimensionKind::DistanceY);
        signed_y.value = 7.0;
        const auto signed_y_id = signed_y.id;
        signed_y_corner.apply_dimension(signed_y);
        require(signed_y_corner.set_dimension_value(signed_y_id, 6.0),
                "Positive visible Y dimension did not accept an edit");
        const auto signed_y_visible =
            signed_y_corner.visible_segment_endpoints(fillet_second);
        require(signed_x_visible && signed_y_visible &&
                    std::abs(signed_x_visible->second[0] -
                        signed_x_visible->first[0] + 6.0) < 1.0e-7 &&
                    std::abs(signed_y_visible->second[1] -
                        signed_y_visible->first[1] - 6.0) < 1.0e-7,
                "Visible corner-segment X/Y dimensions lost their signed orientation");

        auto reference_corner = corner_fillet;
        auto reference_trimmed = reference_corner.create_segment_dimension(
            fillet_first, DimensionKind::Distance);
        reference_trimmed.driving = false;
        const auto reference_trimmed_id = reference_trimmed.id;
        reference_corner.apply_dimension(reference_trimmed);
        const auto reference_outer_id =
            reference_corner.segments.front().first_point_id;
        require(reference_corner.move_point(reference_outer_id, 12.0, 0.0),
                "Reference visible corner-segment fixture could not be moved");
        const auto refreshed_reference = std::find_if(
            reference_corner.dimensions.begin(),
            reference_corner.dimensions.end(), [&](const auto& dimension) {
                return dimension.id == reference_trimmed_id;
            });
        require(refreshed_reference != reference_corner.dimensions.end() &&
                    !refreshed_reference->driving &&
                    refreshed_reference->geometry_id == fillet_first &&
                    std::abs(refreshed_reference->value - 10.0) < 1.0e-7,
                "Reference dimension did not refresh from the visible trimmed length");
        const auto handle_candidates = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                fillet_mesh, {2.0, 0.0, 10.0}, {0.0, 0.0, -1.0}, 0.2),
            {zima::viewer::CandidateKind::SketchPoint});
        require(std::any_of(handle_candidates.begin(), handle_candidates.end(),
                    [](const auto& candidate) {
                        return candidate.semantic_key.starts_with(
                            "corner_radius_handle:");
                    }),
                "Corner radius tangent handle was not offered as a Sketch control point");
        corner_fillet.corner_radii.front().dimension_visible = true;
        corner_fillet.corner_radii.front().dimension_placement =
            std::array{3.0, 3.0};
        const auto radius_dimension_mesh = corner_fillet.viewer_mesh();
        const auto driven_corner_dof = corner_fillet.solve();
        require(std::any_of(radius_dimension_mesh.dimensions.begin(),
                    radius_dimension_mesh.dimensions.end(), [&](const auto& dimension) {
                        return dimension.reference.semantic_key ==
                                "corner_dimension:" + fillet.arc_id &&
                            std::abs(dimension.value - 2.0) < 1.0e-9;
                    }) && corner_fillet.dimensions.empty() &&
                    driven_corner_dof.remaining_degrees_of_freedom == 6,
                "Corner radius annotation was mixed into generic Sketch dimensions");
        static_cast<void>(corner_fillet.add_corner_fillet(
            fillet_first, fillet_second, 0.0));
        const auto sharp_mesh = corner_fillet.viewer_mesh();
        require(corner_fillet.corner_radii.size() == 1 &&
                    corner_fillet.corner_radii.front().id == fillet.arc_id &&
                    corner_fillet.corner_radii.front().radius == 0.0 &&
                    corner_fillet.dimensions.empty() &&
                    corner_fillet.corner_radii.front().dimension_visible &&
                    corner_fillet.arcs.empty() &&
                    corner_fillet.points.size() == 3 &&
                    corner_fillet.segments[0].second_point_id == fillet_corner_id &&
                    corner_fillet.segments[1].first_point_id == fillet_corner_id &&
                    std::none_of(sharp_mesh.edges.begin(), sharp_mesh.edges.end(),
                        [&](const auto& edge) {
                            return edge.reference.semantic_key ==
                                "corner_radius:" + fillet.arc_id;
                        }) &&
                    std::none_of(sharp_mesh.points.begin(), sharp_mesh.points.end(),
                        [](const auto& point) {
                            return point.reference.semantic_key.starts_with(
                                "corner_radius_handle:");
                        }) &&
                    std::any_of(sharp_mesh.dimensions.begin(),
                        sharp_mesh.dimensions.end(), [&](const auto& dimension) {
                            return dimension.reference.semantic_key ==
                                    "corner_dimension:" + fillet.arc_id &&
                                dimension.value == 0.0;
                        }),
                "R0 did not preserve its dimension and restore the sharp source corner");
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
        auto rotating_polygon = zima::sketcher::Sketch::create_default();
        const auto rotating_polygon_result = rotating_polygon.add_regular_polygon(
            0.0, 0.0, 10.0, 0.0, 6);
        const auto rotating_center_id =
            rotating_polygon.circles.front().center_point_id;
        rotating_polygon.find_point(rotating_center_id)->fixed = true;
        auto rotating_radius = rotating_polygon.create_circle_radius_dimension(
            rotating_polygon_result.support_circle_id);
        rotating_radius.value = 15.0;
        rotating_polygon.apply_dimension(std::move(rotating_radius));
        rotating_polygon.dimensions.front().locked = true;
        const auto rotating_before = rotating_polygon;
        const auto rotating_vertex_id = rotating_polygon_result.vertex_ids.front();
        require(rotating_polygon.move_point(rotating_vertex_id, 0.0, 15.0) &&
                    std::abs(rotating_polygon.find_point(rotating_vertex_id)->x) <
                        1.0e-7 &&
                    std::abs(rotating_polygon.find_point(rotating_vertex_id)->y -
                        15.0) < 1.0e-7 &&
                    std::abs(rotating_polygon.circles.front().radius - 15.0) <
                        1.0e-9 &&
                    rotating_polygon.solve().maximum_residual < 1.0e-7,
                "Regular polygon could not rotate while preserving its driven size");
        const auto* rotating_center = rotating_polygon.find_point(rotating_center_id);
        for (std::size_t index = 0;
             index < rotating_polygon_result.vertex_ids.size(); ++index) {
            const auto* vertex = rotating_polygon.find_point(
                rotating_polygon_result.vertex_ids[index]);
            const auto* next_vertex = rotating_polygon.find_point(
                rotating_polygon_result.vertex_ids[
                    (index + 1) % rotating_polygon_result.vertex_ids.size()]);
            require(std::abs(std::hypot(vertex->x - rotating_center->x,
                                vertex->y - rotating_center->y) - 15.0) < 1.0e-7 &&
                        std::abs(std::hypot(next_vertex->x - vertex->x,
                                next_vertex->y - vertex->y) - 15.0) < 1.0e-7,
                    "Rotated regular polygon lost radius or equal side length");
        }
        require(rotating_polygon.move_point(rotating_vertex_id, 15.0, 0.0) &&
                    rotating_polygon.solve().maximum_residual < 1.0e-7,
                "Regular polygon could not return from a rotation drag");
        for (const auto& original_point : rotating_before.points) {
            const auto* returned = rotating_polygon.find_point(original_point.id);
            require(returned != nullptr &&
                        std::hypot(returned->x - original_point.x,
                            returned->y - original_point.y) < 1.0e-6,
                    "Regular polygon accumulated drift after rotation and return");
        }
        require(rotating_polygon.constraints == rotating_before.constraints &&
                    rotating_polygon.dimensions == rotating_before.dimensions,
                "Regular polygon rotation changed persisted relations");
        auto blocked_polygon = rotating_before;
        for (const auto& vertex_id : rotating_polygon_result.vertex_ids) {
            blocked_polygon.find_point(vertex_id)->fixed = true;
        }
        const auto blocked_polygon_before = blocked_polygon;
        require(!blocked_polygon.move_point(rotating_vertex_id, 0.0, 15.0) &&
                    blocked_polygon.points == blocked_polygon_before.points &&
                    blocked_polygon.constraints == blocked_polygon_before.constraints &&
                    blocked_polygon.dimensions == blocked_polygon_before.dimensions,
                "Fully anchored polygon drag was not rejected atomically");
        for (const unsigned side_count : {4U, 6U, 8U}) {
            auto polygon_matrix = zima::sketcher::Sketch::create_default();
            constexpr double center_x = 3.0;
            constexpr double center_y = -2.0;
            constexpr double radius = 12.0;
            const auto matrix_result = polygon_matrix.add_regular_polygon(
                center_x, center_y, center_x + radius, center_y, side_count);
            polygon_matrix.find_point(
                polygon_matrix.circles.front().center_point_id)->fixed = true;
            auto matrix_radius = polygon_matrix.create_circle_radius_dimension(
                matrix_result.support_circle_id);
            matrix_radius.value = radius;
            polygon_matrix.apply_dimension(std::move(matrix_radius));
            polygon_matrix.dimensions.front().locked = true;
            const auto matrix_before = polygon_matrix;
            const auto matrix_root = matrix_result.vertex_ids.front();
            require(polygon_matrix.move_point(
                        matrix_root, center_x, center_y - radius) &&
                        polygon_matrix.solve().maximum_residual < 1.0e-7,
                    "Regular polygon orientation matrix rejected a valid rotation");
            const auto* matrix_center = polygon_matrix.find_point(
                polygon_matrix.circles.front().center_point_id);
            for (std::size_t index = 0; index < matrix_result.vertex_ids.size(); ++index) {
                const auto* vertex = polygon_matrix.find_point(
                    matrix_result.vertex_ids[index]);
                const auto* next_vertex = polygon_matrix.find_point(
                    matrix_result.vertex_ids[
                        (index + 1) % matrix_result.vertex_ids.size()]);
                const double expected_side = 2.0 * radius * std::sin(
                    3.14159265358979323846 / static_cast<double>(side_count));
                require(std::abs(std::hypot(vertex->x - matrix_center->x,
                                    vertex->y - matrix_center->y) - radius) < 1.0e-7 &&
                            std::abs(std::hypot(next_vertex->x - vertex->x,
                                    next_vertex->y - vertex->y) - expected_side) <
                                1.0e-7,
                        "Regular polygon orientation matrix changed its shape");
            }
            require(polygon_matrix.move_point(
                        matrix_root, center_x + radius, center_y) &&
                        polygon_matrix.solve().maximum_residual < 1.0e-7,
                    "Regular polygon orientation matrix could not return");
            for (const auto& original_point : matrix_before.points) {
                const auto* returned = polygon_matrix.find_point(original_point.id);
                require(returned != nullptr &&
                            std::hypot(returned->x - original_point.x,
                                returned->y - original_point.y) < 1.0e-6,
                        "Regular polygon orientation matrix accumulated drift");
            }
        }
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
        const auto source_circle = mirrored.add_circle(10.0, 10.0, 4.0, true);
        const auto source_arc = mirrored.add_arc(
            3.0, 0.0, 5.0, 0.0, 3.0, 2.0);
        const auto source_ellipse = mirrored.add_ellipse(
            4.0, 0.0, 7.0, 0.0, 4.0, 2.0);
        const auto source_elliptical_arc = mirrored.add_elliptical_arc(
            6.0, -5.0, 9.0, -5.0, 6.0, -3.0,
            9.0, -5.0, 6.0, -3.0);
        const auto source_spline = mirrored.add_bspline(
            {{{2.0, 5.0}, {3.0, 6.0}, {4.0, 6.0}, {5.0, 5.0}}});
        const auto mirrored_curves = mirrored.mirror_geometry(
            {source_circle, source_arc, source_ellipse,
             source_elliptical_arc, source_spline}, mirror_axis);
        require(mirrored_curves.geometry_ids.size() == 5,
                "Sketch mirror did not return five new curve identities");
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
                return value.id == mirrored_curves.geometry_ids[4];
            });
        const auto reflected_elliptical_arc = std::find_if(
            mirrored.elliptical_arcs.begin(), mirrored.elliptical_arcs.end(),
            [&](const auto& value) {
                return value.id == mirrored_curves.geometry_ids[3];
            });
        require(reflected_circle != mirrored.circles.end() &&
                    reflected_arc != mirrored.arcs.end() &&
                    reflected_ellipse != mirrored.ellipses.end() &&
                    reflected_elliptical_arc != mirrored.elliptical_arcs.end() &&
                    reflected_spline != mirrored.bsplines.end(),
                "Sketch mirror did not preserve the selected curve kinds");
        const auto* reflected_circle_center = mirrored.find_point(
            reflected_circle->center_point_id);
        const auto* reflected_arc_start = mirrored.find_point(
            reflected_arc->start_point_id);
        const auto* reflected_ellipse_center = mirrored.find_point(
            reflected_ellipse->center_point_id);
        const auto* reflected_elliptical_arc_start = mirrored.find_point(
            reflected_elliptical_arc->start_point_id);
        const auto* reflected_spline_first = mirrored.find_point(
            reflected_spline->control_point_ids.front());
        const auto source_ellipse_value = std::find_if(
            mirrored.ellipses.begin(), mirrored.ellipses.end(), [&](const auto& value) {
                return value.id == source_ellipse;
            });
        require(reflected_circle->construction &&
                    std::abs(reflected_circle_center->x + 10.0) < 1.0e-9 &&
                    std::abs(reflected_arc_start->x + 3.0) < 1.0e-9 &&
                    std::abs(reflected_arc_start->y - 2.0) < 1.0e-9 &&
                    std::abs(reflected_ellipse_center->x + 4.0) < 1.0e-9 &&
                    std::abs(reflected_elliptical_arc_start->x + 9.0) < 1.0e-9 &&
                    std::abs(reflected_elliptical_arc_start->y + 5.0) < 1.0e-9 &&
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
                    loaded_mirror.elliptical_arcs == mirrored.elliptical_arcs &&
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
                    removable_rectangle.constraints.size() == 4 &&
                    removable_rectangle.dimensions.size() == 1 &&
                    removable_rectangle.dimensions.front().geometry_id.empty() &&
                    removable_rectangle.dimensions.front().first_point_id ==
                        removable_dimension.first_point_id &&
                    removable_rectangle.dimensions.front().second_point_id ==
                        removable_dimension.second_point_id &&
                    removable_rectangle.points.size() == 4,
                "Segment deletion lost shared corners or its point-owned dimension");
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
        auto reference_radius = circle_sketch.dimensions.front();
        reference_radius.driving = false;
        reference_radius.value = 3.0;
        circle_sketch.apply_dimension(reference_radius);
        require(!circle_sketch.dimensions.front().driving &&
                    std::abs(circle_sketch.dimensions.front().value - 15.0) < 1.0e-9 &&
                    std::abs(circle_sketch.circles.front().radius - 15.0) < 1.0e-9,
                "Converting a driving radius to a reference dimension changed geometry");
        circle_sketch.circles.front().radius = 12.0;
        require(circle_sketch.solve().status !=
                    zima::sketcher::SolveStatus::Conflicting &&
                    std::abs(circle_sketch.dimensions.front().value - 12.0) < 1.0e-9,
                "Reference radius did not refresh after its measured geometry changed");
        reference_radius = circle_sketch.dimensions.front();
        reference_radius.driving = true;
        reference_radius.value = 15.0;
        circle_sketch.apply_dimension(reference_radius);
        require(!circle_sketch.set_dimension_value(
                    circle_sketch.dimensions.front().id, -1.0),
                "Negative radius dimension was accepted by the data model");
        const auto circle_packet = circle_sketch.viewer_mesh();
        const auto circle_axis_crossings = circle_sketch.curve_line_intersections(
            circle_id, {0.0, 7.0}, {1.0, 0.0}, false);
        require(circle_packet.edges.size() == 1 &&
                    circle_packet.edges.front().points.size() == 97 &&
                    circle_packet.edges.front().reference.semantic_key ==
                        "circle:" + circle_id &&
                    std::count_if(circle_packet.points.begin(),
                        circle_packet.points.end(), [&](const auto& point) {
                            return point.reference.semantic_key.starts_with(
                                "sketch_curve_keypoint:circle:" + circle_id + ":");
                        }) == 4 &&
                    circle_packet.dimensions.size() == 1 &&
                    circle_packet.dimensions.front().label_prefix == "R" &&
                    circle_axis_crossings.size() == 2 &&
                    std::abs(circle_axis_crossings.front()[1] - 7.0) < 1.0e-9,
                "Circle or radius dimension did not produce stable viewer data");
        const auto loaded_circle = zima::sketcher::Sketch::from_serialized(
            circle_sketch.serialized());
        require(loaded_circle.circles == circle_sketch.circles &&
                    loaded_circle.dimensions == circle_sketch.dimensions,
                "Circle and radius dimension did not survive serialization");
        auto cardinal_lock = zima::sketcher::Sketch::create_default();
        const auto cardinal_circle = cardinal_lock.add_circle(0.0, 0.0, 10.0);
        const auto cardinal_point = cardinal_lock.add_point(10.0, 0.0);
        static_cast<void>(cardinal_lock.add_point_reference_constraint(
            cardinal_point,
            "sketch_keypoint:circle:" + cardinal_circle + ":0"));
        cardinal_lock.circles.front().radius = 14.0;
        require(cardinal_lock.solve().status !=
                    zima::sketcher::SolveStatus::Conflicting &&
                    std::abs(cardinal_lock.find_point(cardinal_point)->x - 14.0) <
                        1.0e-8 &&
                    std::abs(cardinal_lock.find_point(cardinal_point)->y) < 1.0e-8 &&
                    zima::sketcher::Sketch::from_serialized(
                        cardinal_lock.serialized()).constraints ==
                        cardinal_lock.constraints,
                "Persisted curve keypoint did not retain its circle quadrant");
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
                    diameter_packet.dimensions.front().kind ==
                        zima::kernel::ViewerDimensionKind::Diameter &&
                    diameter_packet.dimensions.front().label_prefix == "Ø" &&
                    std::abs(diameter_packet.dimensions.front().witness_first.x) <
                        1.0e-9 &&
                    std::abs(diameter_packet.dimensions.front().witness_first.y) <
                        1.0e-9 &&
                    std::abs(std::hypot(
                        diameter_packet.dimensions.front().witness_second.x,
                        diameter_packet.dimensions.front().witness_second.y) -
                        15.0) < 1.0e-9 &&
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
        auto sliding_circle = zima::sketcher::Sketch::create_default();
        const auto sliding_circle_id = sliding_circle.add_circle(0.0, 0.0, 10.0);
        const auto sliding_point_id = sliding_circle.add_point(10.0, 0.0);
        static_cast<void>(sliding_circle.add_point_on_circle_constraint(
            sliding_point_id, sliding_circle_id));
        require(sliding_circle.move_point(sliding_point_id, 30.0, 40.0) &&
                    std::abs(sliding_circle.circles.front().radius - 10.0) < 1.0e-9 &&
                    std::abs(std::hypot(
                        sliding_circle.find_point(sliding_point_id)->x,
                        sliding_circle.find_point(sliding_point_id)->y) - 10.0) < 1.0e-8,
                "Dragging a point on an undimensioned Circle changed its radius");
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
                    arc_packet.edges.front().reference.semantic_key == "arc:" + arc_id &&
                    std::count_if(arc_packet.points.begin(),
                        arc_packet.points.end(), [&](const auto& point) {
                            return point.reference.semantic_key.starts_with(
                                "sketch_curve_keypoint:arc:" + arc_id + ":");
                        }) == 2,
                "Arc did not produce an adaptive stable viewer curve");
        auto clockwise_arc = zima::sketcher::Sketch::create_default();
        static_cast<void>(clockwise_arc.add_arc(
            0.0, 0.0, 10.0, 0.0, 0.0, 10.0, false, 1.0e-6, true));
        const auto clockwise_packet = clockwise_arc.viewer_mesh();
        require(clockwise_packet.edges.size() == 1 &&
                    clockwise_packet.edges.front().points.size() >
                        arc_packet.edges.front().points.size() &&
                    std::abs(clockwise_packet.edges.front().points.front().x) < 1.0e-7 &&
                    std::abs(clockwise_packet.edges.front().points.front().y - 10.0) < 1.0e-7,
                "Clockwise arc did not persist the complementary directed sweep");
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
        auto arc_diameter_sketch = zima::sketcher::Sketch::create_default();
        const auto arc_diameter_id = arc_diameter_sketch.add_arc(
            0.0, 0.0, 6.0, 0.0, 0.0, 6.0);
        auto arc_diameter = arc_diameter_sketch.create_arc_diameter_dimension(
            arc_diameter_id);
        arc_diameter.value = 20.0;
        arc_diameter_sketch.apply_dimension(arc_diameter);
        const auto arc_diameter_packet = arc_diameter_sketch.viewer_mesh();
        require(std::abs(arc_diameter_sketch.arcs.front().radius - 10.0) < 1.0e-9 &&
                    arc_diameter_packet.dimensions.size() == 1 &&
                    arc_diameter_packet.dimensions.front().label_prefix == "Ø" &&
                    std::abs(arc_diameter_packet.dimensions.front().value - 20.0) < 1.0e-9,
                "Diameter dimension did not drive or display its stable arc");
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
                    std::abs(moved_arc.find_point(moved_arc_start)->x - 25.0) < 1.0e-9 &&
                    std::abs(moved_arc.find_point(moved_arc_end)->y - 23.0) < 1.0e-9,
                "Moving an Arc endpoint did not edit its radius and sweep");
        auto driven_arc = zima::sketcher::Sketch::create_default();
        const auto driven_arc_id = driven_arc.add_arc(
            0.0, 0.0, 10.0, 0.0, 0.0, 10.0);
        driven_arc.apply_dimension(
            driven_arc.create_arc_radius_dimension(driven_arc_id));
        const auto driven_arc_end = driven_arc.arcs.front().end_point_id;
        require(driven_arc.move_point(driven_arc_end, -20.0, 0.0) &&
                    std::abs(driven_arc.find_point(driven_arc_end)->x + 20.0) < 1.0e-9 &&
                    std::abs(driven_arc.arcs.front().radius - 20.0) < 1.0e-9 &&
                    std::abs(driven_arc.dimensions.front().value - 20.0) < 1.0e-9,
                "Dragging an unlocked dimensioned Arc endpoint did not edit its radius");
        auto locked_arc = zima::sketcher::Sketch::create_default();
        const auto locked_arc_id = locked_arc.add_arc(
            0.0, 0.0, 10.0, 0.0, 0.0, 10.0);
        auto locked_arc_radius = locked_arc.create_arc_radius_dimension(
            locked_arc_id);
        locked_arc_radius.locked = true;
        locked_arc.apply_dimension(locked_arc_radius);
        const auto locked_arc_end = locked_arc.arcs.front().end_point_id;
        require(locked_arc.move_point(locked_arc_end, -20.0, 0.0) &&
                    std::abs(locked_arc.find_point(locked_arc_end)->x + 10.0) <
                        1.0e-9 &&
                    std::abs(locked_arc.arcs.front().radius - 10.0) < 1.0e-9,
                "Dragging a locked Arc radius did not reduce to angular motion");
        moved_arc.set_point_fixed(moved_arc_start, true);
        require(moved_arc.move_point(moved_arc_end, -15.0, 3.0) &&
                    std::abs(moved_arc.find_point(moved_arc_start)->x - 25.0) < 1.0e-9 &&
                    std::abs(moved_arc.arcs.front().radius - 20.0) < 1.0e-9,
                "Arc sweep edit moved a fixed start point or changed radius");
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
        const auto initial_ellipse_packet = ellipse_sketch.viewer_mesh();
        require(ellipse_sketch.ellipses.size() == 1 &&
                    ellipse_sketch.points.size() == 3 &&
                    initial_ellipse_packet.edges.front().reference.semantic_key ==
                        "ellipse:" + ellipse_id &&
                    std::count_if(initial_ellipse_packet.points.begin(),
                        initial_ellipse_packet.points.end(),
                        [&](const auto& point) {
                            return point.reference.semantic_key.starts_with(
                                "sketch_curve_keypoint:ellipse:" + ellipse_id + ":");
                        }) == 4,
                "Ellipse did not persist stable axis references or viewer identity");
        const auto ellipse_center = ellipse_sketch.ellipses.front().center_point_id;
        const auto ellipse_major = ellipse_sketch.ellipses.front().major_point_id;
        const auto ellipse_minor = ellipse_sketch.ellipses.front().minor_point_id;
        const auto ellipse_attached = ellipse_sketch.add_point(15.0, 6.0);
        static_cast<void>(ellipse_sketch.add_point_on_circle_constraint(
            ellipse_attached, ellipse_id));
        const auto* attached_before_translation =
            ellipse_sketch.find_point(ellipse_attached);
        require(std::abs(
                    attached_before_translation->x * attached_before_translation->x /
                        (20.0 * 20.0) +
                    attached_before_translation->y * attached_before_translation->y /
                        (8.0 * 8.0) - 1.0) < 1.0e-7,
                "Point-on-curve did not project onto an Ellipse");
        const auto ellipse_tangent = ellipse_sketch.curve_tangent_at_point(
            ellipse_id, 20.0, 0.0);
        require(ellipse_tangent && std::abs((*ellipse_tangent)[0]) < 1.0e-9 &&
                    std::abs(std::abs((*ellipse_tangent)[1]) - 1.0) < 1.0e-9,
                "Ellipse point did not expose its exact tangent direction");
        const auto attached_x = attached_before_translation->x;
        const auto attached_y = attached_before_translation->y;
        require(ellipse_sketch.move_point(ellipse_center, 5.0, 4.0) &&
                    std::abs(ellipse_sketch.ellipses.front().major_radius - 20.0) < 1.0e-9 &&
                    std::abs(ellipse_sketch.ellipses.front().minor_radius - 8.0) < 1.0e-9 &&
                    std::abs(ellipse_sketch.find_point(ellipse_major)->x - 25.0) < 1.0e-9 &&
                    std::abs(ellipse_sketch.find_point(ellipse_minor)->y - 12.0) < 1.0e-9 &&
                    std::abs(ellipse_sketch.find_point(ellipse_attached)->x -
                        (attached_x + 5.0)) < 1.0e-8 &&
                    std::abs(ellipse_sketch.find_point(ellipse_attached)->y -
                        (attached_y + 4.0)) < 1.0e-8,
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
                    dimensioned_ellipse_packet.dimensions[0].kind ==
                        zima::kernel::ViewerDimensionKind::Radius &&
                    dimensioned_ellipse_packet.dimensions[1].label_prefix == "b=" &&
                    dimensioned_ellipse_packet.dimensions[1].kind ==
                        zima::kernel::ViewerDimensionKind::Radius &&
                    dimensioned_ellipse_packet.dimensions[2].kind ==
                        zima::kernel::ViewerDimensionKind::Angular &&
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
        ellipse_sketch.remove_point(ellipse_attached);
        ellipse_sketch.remove_geometry(ellipse_id);
        require(ellipse_sketch.ellipses.empty() && ellipse_sketch.points.empty() &&
                    ellipse_sketch.dimensions.empty(),
                "Ellipse deletion retained dimensions or orphan axis points");
        auto elliptical_arc_sketch = zima::sketcher::Sketch::create_default();
        const double elliptical_start = 0.3;
        const double elliptical_end = 2.2;
        const auto elliptical_arc_id = elliptical_arc_sketch.add_elliptical_arc(
            0.0, 0.0, 8.0, 0.0, 0.0, 4.0,
            8.0 * std::cos(elliptical_start),
            4.0 * std::sin(elliptical_start),
            8.0 * std::cos(elliptical_end),
            4.0 * std::sin(elliptical_end));
        const auto elliptical_center =
            elliptical_arc_sketch.elliptical_arcs.front().center_point_id;
        const auto elliptical_major =
            elliptical_arc_sketch.elliptical_arcs.front().major_point_id;
        const auto elliptical_minor =
            elliptical_arc_sketch.elliptical_arcs.front().minor_point_id;
        const auto elliptical_start_point =
            elliptical_arc_sketch.elliptical_arcs.front().start_point_id;
        const auto initial_elliptical_arc_packet =
            elliptical_arc_sketch.viewer_mesh();
        require(std::count_if(
                    initial_elliptical_arc_packet.points.begin(),
                    initial_elliptical_arc_packet.points.end(),
                    [&](const auto& point) {
                        return point.reference.semantic_key.starts_with(
                            "sketch_curve_keypoint:elliptical_arc:" +
                            elliptical_arc_id + ":");
                    }) == 1,
                "Elliptical Arc did not expose in-domain cardinal keypoints");
        require(elliptical_arc_sketch.move_point(elliptical_center, 2.0, 3.0) &&
                    std::abs(elliptical_arc_sketch.find_point(
                        elliptical_major)->x - 10.0) < 1.0e-9,
                "Moving an elliptical Arc center did not translate its exact controls");
        require(elliptical_arc_sketch.move_point(elliptical_major, 2.0, 13.0) &&
                    std::abs(elliptical_arc_sketch.find_point(
                        elliptical_minor)->x + 2.0) < 1.0e-9,
                "Moving an elliptical Arc major axis did not rotate its exact controls");
        constexpr double moved_parameter = 0.6;
        require(elliptical_arc_sketch.move_point(
                    elliptical_start_point,
                    2.0 - 4.0 * std::sin(moved_parameter),
                    3.0 + 10.0 * std::cos(moved_parameter)) &&
                    std::abs(
                        elliptical_arc_sketch.elliptical_arcs.front().start_parameter -
                        moved_parameter) < 1.0e-8,
                "Moving an elliptical Arc endpoint did not update its exact parameter");
        require(elliptical_arc_sketch.viewer_mesh().edges.front().reference.semantic_key ==
                    "elliptical_arc:" + elliptical_arc_id,
                "Elliptical Arc did not preserve its viewer identity after editing");
        elliptical_arc_sketch.remove_geometry(elliptical_arc_id);
        require(elliptical_arc_sketch.elliptical_arcs.empty() &&
                    elliptical_arc_sketch.points.empty(),
                "Elliptical Arc deletion retained orphan control points");
        auto reversed_elliptical_arc = zima::sketcher::Sketch::create_default();
        const auto reversed_elliptical_arc_id =
            reversed_elliptical_arc.add_elliptical_arc(
                0.0, 0.0, 8.0, 0.0, 0.0, -4.0,
                8.0, 0.0, 0.0, -4.0, true);
        require(reversed_elliptical_arc.elliptical_arcs.size() == 1 &&
                    reversed_elliptical_arc.elliptical_arcs.front().reversed &&
                    std::abs(
                        reversed_elliptical_arc.elliptical_arcs.front().
                            start_parameter) < 1.0e-9 &&
                    std::abs(
                        reversed_elliptical_arc.elliptical_arcs.front().
                            end_parameter - 0.5 * std::numbers::pi) < 1.0e-9 &&
                    reversed_elliptical_arc.viewer_mesh().edges.front().
                        reference.semantic_key ==
                        "elliptical_arc:" + reversed_elliptical_arc_id,
                "Clockwise five-point Elliptical Arc lost its selected side");
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
        const auto spline_attached = spline_sketch.add_point(15.0, 12.0);
        static_cast<void>(spline_sketch.add_point_on_circle_constraint(
            spline_attached, spline_id));
        const auto projected_spline_point = spline_sketch.project_point_to_curve(
            spline_id, spline_sketch.find_point(spline_attached)->x,
            spline_sketch.find_point(spline_attached)->y);
        require(projected_spline_point &&
                    std::hypot(
                        spline_sketch.find_point(spline_attached)->x -
                            (*projected_spline_point)[0],
                        spline_sketch.find_point(spline_attached)->y -
                            (*projected_spline_point)[1]) < 1.0e-8,
                "Point-on-curve did not bind a point to a B-spline");
        const auto spline_crossings = spline_sketch.curve_line_intersections(
            spline_id, {0.0, 0.0}, {30.0, 0.0}, true);
        require(!spline_crossings.empty(),
                "B-spline did not expose line-curve placement intersections");
        const auto loaded_spline = zima::sketcher::Sketch::from_serialized(
            spline_sketch.serialized());
        require(loaded_spline.bsplines == spline_sketch.bsplines &&
                    loaded_spline.points == spline_sketch.points,
                "B-spline did not survive current Sketch serialization");
        auto interpolating_spline = zima::sketcher::Sketch::create_default();
        const std::vector<std::array<double, 2>> interpolation_points{
            {0.0, 0.0}, {7.0, 11.0}, {15.0, -4.0}, {24.0, 13.0}, {31.0, 2.0}};
        const auto interpolating_id = interpolating_spline.add_bspline(
            interpolation_points, 3, false, false, 1.0e-6, true);
        const auto interpolation_packet = interpolating_spline.viewer_mesh();
        const auto& interpolation_edge = interpolation_packet.edges.front();
        const auto curve_contains = [&](const std::array<double, 2>& expected) {
            return std::ranges::any_of(interpolation_edge.points,
                [&](const auto& sampled) {
                    return std::hypot(sampled.x - expected[0],
                               sampled.y - expected[1]) < 1.0e-9;
                });
        };
        require(interpolating_spline.bsplines.size() == 1 &&
                    interpolating_spline.bsplines.front().interpolating &&
                    std::ranges::all_of(interpolation_points, curve_contains) &&
                    interpolation_edge.reference.semantic_key ==
                        "bspline:" + interpolating_id,
                "Interpolating spline did not pass through every persisted point");
        const auto moved_interpolation_point =
            interpolating_spline.bsplines.front().control_point_ids[2];
        require(interpolating_spline.move_point(
                    moved_interpolation_point, 15.0, 6.0),
                "Interpolating spline point could not be moved");
        const auto moved_interpolation_packet = interpolating_spline.viewer_mesh();
        require(std::ranges::any_of(moved_interpolation_packet.edges.front().points,
                    [](const auto& sampled) {
                        return std::hypot(sampled.x - 15.0, sampled.y - 6.0) < 1.0e-9;
                    }),
                "Moved interpolation point no longer lay on its spline");
        const auto loaded_interpolation = zima::sketcher::Sketch::from_serialized(
            interpolating_spline.serialized());
        require(loaded_interpolation.bsplines == interpolating_spline.bsplines &&
                    loaded_interpolation.points == interpolating_spline.points,
                "Interpolating spline did not survive current Sketch serialization");
        auto three_point_interpolation = zima::sketcher::Sketch::create_default();
        static_cast<void>(three_point_interpolation.add_bspline(
            {{0.0, 0.0}, {5.0, 8.0}, {12.0, 0.0}},
            2, false, false, 1.0e-6, true));
        require(three_point_interpolation.bsplines.size() == 1 &&
                    three_point_interpolation.arcs.empty() &&
                    three_point_interpolation.dimensions.empty(),
                "Three-point interpolation was incorrectly converted to a radius geometry");
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
        spline_sketch.remove_point(spline_attached);
        spline_sketch.remove_geometry(spline_id);
        require(spline_sketch.bsplines.empty() && spline_sketch.points.empty(),
                "B-spline deletion retained orphan control points");

        auto trimmed_cross = zima::sketcher::Sketch::create_default();
        const auto trimmed_target = trimmed_cross.add_segment(-10.0, 4.0, 10.0, 4.0);
        static_cast<void>(trimmed_cross.add_segment(0.0, -10.0, 0.0, 10.0));
        const auto trimmed_target_points = std::array{
            trimmed_cross.segments.front().first_point_id,
            trimmed_cross.segments.front().second_point_id};
        const auto cross_topology = zima::sketcher::sketch_trim_topology(
            trimmed_cross, false);
        const auto target_piece_count = std::count_if(
            cross_topology.begin(), cross_topology.end(), [&](const auto& piece) {
                return piece.geometry_id == trimmed_target;
            });
        const auto right_piece = zima::sketcher::nearest_sketch_trim_piece(
            cross_topology, {7.0, 4.0}, 0.25);
        require(target_piece_count == 2 && right_piece &&
                    right_piece->geometry_id == trimmed_target,
                "Sketch trim topology did not split or pick a crossed segment");
        const auto cross_mapping = zima::sketcher::apply_sketch_trim(
            trimmed_cross, {*right_piece});
        const auto retained_target = std::find_if(
            trimmed_cross.segments.begin(), trimmed_cross.segments.end(),
            [&](const auto& segment) { return segment.id == trimmed_target; });
        require(cross_mapping.geometry_mapping.at(trimmed_target) ==
                    std::vector<std::string>{trimmed_target} &&
                    retained_target != trimmed_cross.segments.end() &&
                    retained_target->first_point_id == trimmed_target_points[0] &&
                    std::abs(trimmed_cross.find_point(
                        retained_target->second_point_id)->x) < 1.0e-6,
                "Segment trim lost stable identity, original endpoint, or exact survivor");

        auto axis_trim = zima::sketcher::Sketch::create_default();
        const auto axis_target = axis_trim.add_segment(-8.0, 3.0, 8.0, 3.0);
        const auto axis_topology = zima::sketcher::sketch_trim_topology(axis_trim, true);
        require(std::count_if(axis_topology.begin(), axis_topology.end(),
                    [&](const auto& piece) { return piece.geometry_id == axis_target; }) == 2,
                "Sketch base Y axis did not create a trim boundary");

        auto construction_trim = zima::sketcher::Sketch::create_default();
        const auto construction_target = construction_trim.add_segment(
            -8.0, 4.0, 8.0, 4.0);
        const auto construction_cutter = construction_trim.add_segment(
            0.0, -1.0, 0.0, 1.0, 1.0e-6, true);
        const auto construction_topology =
            zima::sketcher::sketch_trim_topology(construction_trim, false);
        require(std::count_if(construction_topology.begin(), construction_topology.end(),
                    [&](const auto& piece) {
                        return piece.geometry_id == construction_target;
                    }) == 2 &&
                std::none_of(construction_topology.begin(), construction_topology.end(),
                    [&](const auto& piece) {
                        return piece.geometry_id == construction_cutter;
                    }),
                "Construction line did not cut normal geometry or became trimmable");

        auto whole_trim = zima::sketcher::Sketch::create_default();
        const auto whole_id = whole_trim.add_segment(2.0, 2.0, 7.0, 2.0);
        const auto whole_topology = zima::sketcher::sketch_trim_topology(
            whole_trim, false);
        require(whole_topology.size() == 1 &&
                    whole_topology.front().geometry_id == whole_id,
                "Uncut Sketch geometry did not expose one complete trim piece");
        const auto whole_mapping = zima::sketcher::apply_sketch_trim(
            whole_trim, {whole_topology.front()});
        require(whole_trim.segments.empty() && whole_trim.points.empty() &&
                    whole_mapping.geometry_mapping.at(whole_id).empty(),
                "Trimming an uncut Sketch entity did not remove it completely");

        auto circle_trim = zima::sketcher::Sketch::create_default();
        const auto trim_circle_id = circle_trim.add_circle(0.0, 0.0, 10.0);
        static_cast<void>(circle_trim.add_segment(-15.0, 0.0, 15.0, 0.0));
        const auto trim_circle_center = circle_trim.circles.front().center_point_id;
        const auto circle_topology = zima::sketcher::sketch_trim_topology(
            circle_trim, false);
        const auto upper_circle_piece = zima::sketcher::nearest_sketch_trim_piece(
            circle_topology, {0.0, 10.0}, 0.5);
        require(upper_circle_piece && upper_circle_piece->geometry_id == trim_circle_id,
                "Circle trim topology did not expose the selected semicircle");
        static_cast<void>(zima::sketcher::apply_sketch_trim(
            circle_trim, {*upper_circle_piece}));
        const auto trimmed_circle_arc = std::find_if(
            circle_trim.arcs.begin(), circle_trim.arcs.end(), [&](const auto& arc) {
                return arc.id == trim_circle_id;
            });
        require(circle_trim.circles.empty() &&
                    trimmed_circle_arc != circle_trim.arcs.end() &&
                    trimmed_circle_arc->center_point_id == trim_circle_center &&
                    std::abs(trimmed_circle_arc->radius - 10.0) < 1.0e-7 &&
                    std::abs(trimmed_circle_arc->end_angle -
                        trimmed_circle_arc->start_angle -
                        3.14159265358979323846) < 1.0e-4,
                "Circle trim did not reconstruct one stable exact-radius Arc");

        auto attached_contact_trim = zima::sketcher::Sketch::create_default();
        const auto attached_circle =
            attached_contact_trim.add_circle(0.0, 0.0, 10.0);
        const auto right_contact =
            attached_contact_trim.add_point(10.0, 0.0);
        const auto left_contact =
            attached_contact_trim.add_point(-10.0, 0.0);
        static_cast<void>(attached_contact_trim.add_point_on_circle_constraint(
            right_contact, attached_circle));
        static_cast<void>(attached_contact_trim.add_point_on_circle_constraint(
            left_contact, attached_circle));
        const auto attached_topology = zima::sketcher::sketch_trim_topology(
            attached_contact_trim, false);
        const auto attached_upper = zima::sketcher::nearest_sketch_trim_piece(
            attached_topology, {0.0, 10.0}, 0.25);
        require(attached_upper &&
                    attached_upper->geometry_id == attached_circle &&
                    std::count_if(attached_topology.begin(),
                        attached_topology.end(), [&](const auto& piece) {
                            return piece.geometry_id == attached_circle;
                        }) == 2,
                "Persisted point-on-curve contacts did not split trim topology");
        static_cast<void>(zima::sketcher::apply_sketch_trim(
            attached_contact_trim, {*attached_upper}));
        require(attached_contact_trim.arcs.size() == 1 &&
                    ((attached_contact_trim.arcs.front().start_point_id ==
                          right_contact &&
                      attached_contact_trim.arcs.front().end_point_id ==
                          left_contact) ||
                     (attached_contact_trim.arcs.front().start_point_id ==
                          left_contact &&
                      attached_contact_trim.arcs.front().end_point_id ==
                          right_contact)) &&
                    attached_contact_trim.constraints.empty(),
                "Trim reconstruction lost persisted contact point identities");

        auto tangent_line_trim = zima::sketcher::Sketch::create_default();
        const auto tangent_line_circle =
            tangent_line_trim.add_circle(0.0, 0.0, 10.0);
        const auto trim_tangent_line_id =
            tangent_line_trim.add_segment(-15.0, 10.0, 15.0, 10.0);
        const auto trim_tangent_contact_id =
            tangent_line_trim.add_point(0.0, 10.0);
        static_cast<void>(tangent_line_trim.add_tangent_constraint(
            tangent_line_circle, trim_tangent_line_id,
            trim_tangent_contact_id));
        static_cast<void>(tangent_line_trim.add_point_on_circle_constraint(
            trim_tangent_contact_id, tangent_line_circle));
        const auto tangent_line_topology =
            zima::sketcher::sketch_trim_topology(tangent_line_trim, false);
        const auto tangent_line_left_piece =
            zima::sketcher::nearest_sketch_trim_piece(
                tangent_line_topology, {-8.0, 10.0}, 0.25);
        require(tangent_line_left_piece &&
                    tangent_line_left_piece->geometry_id == trim_tangent_line_id,
                "Tangent C+T contact did not split the line trim topology");
        static_cast<void>(zima::sketcher::apply_sketch_trim(
            tangent_line_trim, {*tangent_line_left_piece}));
        const auto trimmed_tangent_line = std::ranges::find_if(
            tangent_line_trim.segments,
            [&](const auto& value) {
                return value.id == trim_tangent_line_id;
            });
        require(trimmed_tangent_line != tangent_line_trim.segments.end() &&
                    (trimmed_tangent_line->first_point_id ==
                         trim_tangent_contact_id ||
                     trimmed_tangent_line->second_point_id ==
                         trim_tangent_contact_id) &&
                    std::count_if(tangent_line_trim.points.begin(),
                        tangent_line_trim.points.end(), [&](const auto& point) {
                            return std::hypot(point.x, point.y - 10.0) < 1.0e-8;
                        }) == 1 &&
                    std::count_if(tangent_line_trim.constraints.begin(),
                        tangent_line_trim.constraints.end(), [](const auto& value) {
                            return value.kind ==
                                zima::sketcher::ConstraintKind::PointOnCircle;
                        }) == 1 &&
                    std::none_of(tangent_line_trim.constraints.begin(),
                        tangent_line_trim.constraints.end(), [](const auto& value) {
                            return value.kind ==
                                zima::sketcher::ConstraintKind::PointOnLine;
                        }) &&
                    std::count_if(tangent_line_trim.constraints.begin(),
                        tangent_line_trim.constraints.end(), [](const auto& value) {
                            return value.kind ==
                                zima::sketcher::ConstraintKind::Tangent;
                        }) == 1,
                "Trimming a tangent line did not retain one shared endpoint on the circle");

        auto tangent_bridge_trim = zima::sketcher::Sketch::create_default();
        const auto left_bridge_circle =
            tangent_bridge_trim.add_circle(-15.0, 0.0, 5.0);
        const auto right_bridge_circle =
            tangent_bridge_trim.add_circle(15.0, 0.0, 5.0);
        const auto left_bridge_center =
            tangent_bridge_trim.circles[0].center_point_id;
        const auto upper_bridge =
            tangent_bridge_trim.add_segment(-15.0, 5.0, 15.0, 5.0);
        const auto lower_bridge =
            tangent_bridge_trim.add_segment(-15.0, -5.0, 15.0, -5.0);
        const auto upper_left_contact =
            tangent_bridge_trim.segments[0].first_point_id;
        const auto upper_right_contact =
            tangent_bridge_trim.segments[0].second_point_id;
        const auto lower_left_contact =
            tangent_bridge_trim.segments[1].first_point_id;
        const auto lower_right_contact =
            tangent_bridge_trim.segments[1].second_point_id;
        static_cast<void>(tangent_bridge_trim.add_point_on_circle_constraint(
            upper_left_contact, left_bridge_circle));
        static_cast<void>(tangent_bridge_trim.add_point_on_circle_constraint(
            upper_right_contact, right_bridge_circle));
        static_cast<void>(tangent_bridge_trim.add_tangent_constraint(
            left_bridge_circle, upper_bridge));
        static_cast<void>(tangent_bridge_trim.add_tangent_constraint(
            right_bridge_circle, upper_bridge));
        static_cast<void>(tangent_bridge_trim.add_point_on_circle_constraint(
            lower_left_contact, left_bridge_circle));
        static_cast<void>(tangent_bridge_trim.add_point_on_circle_constraint(
            lower_right_contact, right_bridge_circle));
        static_cast<void>(tangent_bridge_trim.add_tangent_constraint(
            left_bridge_circle, lower_bridge));
        static_cast<void>(tangent_bridge_trim.add_tangent_constraint(
            right_bridge_circle, lower_bridge));
        const auto bridge_topology = zima::sketcher::sketch_trim_topology(
            tangent_bridge_trim, false);
        const auto left_outer_bridge_piece =
            zima::sketcher::nearest_sketch_trim_piece(
                bridge_topology, {-20.0, 0.0}, 0.5);
        const auto right_outer_bridge_piece =
            zima::sketcher::nearest_sketch_trim_piece(
                bridge_topology, {20.0, 0.0}, 0.5);
        require(left_outer_bridge_piece && right_outer_bridge_piece &&
                    left_outer_bridge_piece->geometry_id == left_bridge_circle &&
                    right_outer_bridge_piece->geometry_id == right_bridge_circle,
                "Two-circle tangent bridge did not expose both Trim pieces");
        static_cast<void>(zima::sketcher::apply_sketch_trim(
            tangent_bridge_trim,
            {*left_outer_bridge_piece, *right_outer_bridge_piece}));
        const std::array bridge_contact_ids{
            upper_left_contact, upper_right_contact,
            lower_left_contact, lower_right_contact};
        require(tangent_bridge_trim.arcs.size() == 2 &&
                    std::count_if(tangent_bridge_trim.constraints.begin(),
                        tangent_bridge_trim.constraints.end(), [](const auto& value) {
                            return value.kind ==
                                zima::sketcher::ConstraintKind::PointOnCircle;
                        }) == 0 &&
                    std::count_if(tangent_bridge_trim.constraints.begin(),
                        tangent_bridge_trim.constraints.end(), [](const auto& value) {
                            return value.kind ==
                                zima::sketcher::ConstraintKind::Tangent;
                        }) == 4 &&
                    tangent_bridge_trim.points.size() == 6 &&
                    std::ranges::all_of(tangent_bridge_trim.arcs,
                        [&](const auto& arc) {
                            return std::ranges::find(
                                    bridge_contact_ids, arc.start_point_id) !=
                                    bridge_contact_ids.end() &&
                                std::ranges::find(
                                    bridge_contact_ids, arc.end_point_id) !=
                                    bridge_contact_ids.end();
                        }),
                "Two-circle Trim did not preserve four single shared C+T topology points");
        tangent_bridge_trim.validate();
        const auto unrelated_drag_point = tangent_bridge_trim.add_point(42.0, 17.0);
        require(tangent_bridge_trim.move_point(
                    unrelated_drag_point, 44.0, 19.0),
                "Fresh Trim blocked a point in an unrelated Sketch branch");
        require(tangent_bridge_trim.move_point(
                    left_bridge_center, -16.0, 1.0),
                "Freshly trimmed C+T bridge made the whole Sketch immovable");
        // Reproduce the interactive command exactly: unequal circles and two
        // segments created by Common tangent, followed by trimming both outer
        // circle pieces into one closed C+T loop.
        auto commanded_bridge_trim = zima::sketcher::Sketch::create_default();
        const auto commanded_left =
            commanded_bridge_trim.add_circle(-15.0, 0.0, 7.0);
        const auto commanded_right =
            commanded_bridge_trim.add_circle(15.0, 0.0, 5.0);
        const auto commanded_left_center =
            commanded_bridge_trim.circles[0].center_point_id;
        const auto commanded_right_center =
            commanded_bridge_trim.circles[1].center_point_id;
        const auto commanded_center_distance =
            commanded_bridge_trim.create_point_dimension(
                commanded_left_center, commanded_right_center,
                zima::sketcher::DimensionKind::DistanceX);
        commanded_bridge_trim.apply_dimension(commanded_center_distance);
        static_cast<void>(commanded_bridge_trim.add_common_tangent_segment(
            commanded_left, {-15.0, 7.0}, commanded_right, {15.0, 5.0}));
        static_cast<void>(commanded_bridge_trim.add_common_tangent_segment(
            commanded_left, {-15.0, -7.0}, commanded_right, {15.0, -5.0}));
        const auto commanded_topology =
            zima::sketcher::sketch_trim_topology(commanded_bridge_trim, false);
        const auto commanded_left_outer =
            zima::sketcher::nearest_sketch_trim_piece(
                commanded_topology, {-22.0, 0.0}, 1.0);
        const auto commanded_right_outer =
            zima::sketcher::nearest_sketch_trim_piece(
                commanded_topology, {20.0, 0.0}, 1.0);
        require(commanded_left_outer && commanded_right_outer,
            "Command-created tangent bridge did not expose outer Trim pieces");
        static_cast<void>(zima::sketcher::apply_sketch_trim(
            commanded_bridge_trim,
            {*commanded_left_outer, *commanded_right_outer}));
        const auto commanded_left_radius =
            commanded_bridge_trim.create_arc_radius_dimension(commanded_left);
        const auto commanded_right_radius =
            commanded_bridge_trim.create_arc_radius_dimension(commanded_right);
        commanded_bridge_trim.apply_dimension(commanded_left_radius);
        commanded_bridge_trim.apply_dimension(commanded_right_radius);
        const auto commanded_free = commanded_bridge_trim.add_point(40.0, 20.0);
        require(commanded_bridge_trim.move_point(commanded_free, 42.0, 21.0),
            "Command-created trimmed C+T loop blocked an unrelated point");
        require(commanded_bridge_trim.set_dimension_value(
                    commanded_left_radius.id, 8.0),
            "Trimmed left Arc radius dimension was not editable");
        require(commanded_bridge_trim.set_dimension_value(
                    commanded_right_radius.id, 6.0),
            "Trimmed right Arc radius dimension was not editable");
        require(commanded_bridge_trim.set_dimension_value(
                    commanded_center_distance.id, 34.0),
            "Trimmed tangent-loop center distance dimension was not editable");

        auto split_trim = zima::sketcher::Sketch::create_default();
        const auto split_target = split_trim.add_segment(-10.0, 2.0, 10.0, 2.0);
        static_cast<void>(split_trim.add_segment(
            -3.0, -1.0, -3.0, 1.0, 1.0e-6, true));
        static_cast<void>(split_trim.add_segment(
            3.0, -1.0, 3.0, 1.0, 1.0e-6, true));
        static_cast<void>(split_trim.add_segment_constraint(
            split_target, zima::sketcher::ConstraintKind::Horizontal));
        const auto split_topology = zima::sketcher::sketch_trim_topology(
            split_trim, false);
        const auto middle_piece = zima::sketcher::nearest_sketch_trim_piece(
            split_topology, {0.0, 2.0}, 0.2);
        require(middle_piece && middle_piece->geometry_id == split_target,
                "Two trim boundaries did not expose the middle segment piece");
        const auto split_mapping = zima::sketcher::apply_sketch_trim(
            split_trim, {*middle_piece});
        const auto& split_survivors = split_mapping.geometry_mapping.at(split_target);
        require(split_survivors.size() == 2 &&
                    split_survivors.front() == split_target &&
                    split_survivors[0] != split_survivors[1] &&
                    std::count_if(split_trim.constraints.begin(),
                        split_trim.constraints.end(), [](const auto& constraint) {
                            return constraint.kind ==
                                zima::sketcher::ConstraintKind::Horizontal;
                        }) == 2,
                "Split trim lost unique stable IDs or reusable H/V constraints");

        auto path_trim = zima::sketcher::Sketch::create_default();
        static_cast<void>(path_trim.add_segment(-8.0, -3.0, 8.0, -3.0));
        static_cast<void>(path_trim.add_segment(-8.0, 3.0, 8.0, 3.0));
        const auto path_topology = zima::sketcher::sketch_trim_topology(
            path_trim, false);
        const auto crossed = zima::sketcher::sketch_trim_pieces_crossed_by_path(
            path_topology, {{{0.0, -5.0}, {0.0, 5.0}}}, 0.1);
        require(crossed.size() == 2 &&
                    crossed[0].geometry_id != crossed[1].geometry_id,
                "Sketch trim drag path did not select every crossed piece once");

        auto spline_trim = zima::sketcher::Sketch::create_default();
        const auto trim_spline_id = spline_trim.add_bspline({
            {-12.0, 0.0}, {-4.0, 0.0}, {4.0, 0.0}, {12.0, 0.0}});
        static_cast<void>(spline_trim.add_segment(
            0.0, -2.0, 0.0, 2.0, 1.0e-6, true));
        const auto spline_trim_topology = zima::sketcher::sketch_trim_topology(
            spline_trim, false);
        const auto spline_piece = zima::sketcher::nearest_sketch_trim_piece(
            spline_trim_topology, {7.0, 0.0}, 0.25);
        require(spline_piece && spline_piece->geometry_id == trim_spline_id,
                "B-spline did not participate in persisted trim topology");
        static_cast<void>(zima::sketcher::apply_sketch_trim(
            spline_trim, {*spline_piece}));
        require(spline_trim.bsplines.size() == 1 &&
                    spline_trim.bsplines.front().id == trim_spline_id &&
                    !spline_trim.bsplines.front().closed,
                "B-spline trim did not reconstruct a valid open survivor");
        spline_trim.validate();

        auto ellipse_cutter = zima::sketcher::Sketch::create_default();
        const auto trim_ellipse_id = ellipse_cutter.add_ellipse(
            0.0, 0.0, 8.0, 0.0, 0.0, 4.0);
        const auto ellipse_cut_target = ellipse_cutter.add_segment(
            -12.0, 0.0, 12.0, 0.0);
        const auto ellipse_cut_topology = zima::sketcher::sketch_trim_topology(
            ellipse_cutter, false);
        const auto upper_ellipse_piece =
            zima::sketcher::nearest_sketch_trim_piece(
                ellipse_cut_topology, {0.0, 4.0}, 0.25);
        require(upper_ellipse_piece &&
                    upper_ellipse_piece->geometry_id == trim_ellipse_id &&
                std::count_if(ellipse_cut_topology.begin(), ellipse_cut_topology.end(),
                    [&](const auto& piece) {
                        return piece.geometry_id == trim_ellipse_id;
                    }) == 2 &&
                std::count_if(ellipse_cut_topology.begin(), ellipse_cut_topology.end(),
                    [&](const auto& piece) {
                        return piece.geometry_id == ellipse_cut_target;
                    }) == 3,
                "Ellipse did not expose exact trimmable pieces at its crossings");
        static_cast<void>(zima::sketcher::apply_sketch_trim(
            ellipse_cutter, {*upper_ellipse_piece}));
        require(ellipse_cutter.ellipses.empty() &&
                    ellipse_cutter.elliptical_arcs.size() == 1 &&
                    ellipse_cutter.elliptical_arcs.front().id == trim_ellipse_id &&
                    std::abs(ellipse_cutter.elliptical_arcs.front().major_radius - 8.0) <
                        1.0e-9 &&
                    std::abs(ellipse_cutter.elliptical_arcs.front().minor_radius - 4.0) <
                        1.0e-9 &&
                    ellipse_cutter.viewer_mesh().edges.back().reference.semantic_key ==
                        "elliptical_arc:" + trim_ellipse_id,
                "Ellipse trim did not preserve exact parameters and stable identity");
        const auto loaded_trimmed_ellipse = zima::sketcher::Sketch::from_serialized(
            ellipse_cutter.serialized());
        require(loaded_trimmed_ellipse.elliptical_arcs ==
                    ellipse_cutter.elliptical_arcs,
                "Trimmed elliptical arc did not survive Sketch serialization");
        static_cast<void>(ellipse_cutter.add_segment(
            0.0, -6.0, 0.0, 6.0, 1.0e-6, true));
        const auto retrim_topology = zima::sketcher::sketch_trim_topology(
            ellipse_cutter, false);
        const auto right_elliptical_piece =
            zima::sketcher::nearest_sketch_trim_piece(
                retrim_topology, {5.5, -2.8}, 0.5);
        require(right_elliptical_piece &&
                    right_elliptical_piece->geometry_id == trim_ellipse_id,
                "Persisted elliptical arc was not offered for a later trim");
        static_cast<void>(zima::sketcher::apply_sketch_trim(
            ellipse_cutter, {*right_elliptical_piece}));
        require(ellipse_cutter.elliptical_arcs.size() == 1 &&
                    ellipse_cutter.elliptical_arcs.front().id == trim_ellipse_id,
                "Repeated elliptical-arc trim lost stable geometry identity");
        ellipse_cutter.validate();

        auto oblique_ellipse_trim = zima::sketcher::Sketch::create_default();
        const auto oblique_ellipse_id = oblique_ellipse_trim.add_ellipse(
            0.0, 0.0, 8.0, 0.0, 0.0, 4.0);
        constexpr double first_parameter = 0.37;
        constexpr double second_parameter = 2.41;
        const std::array<double, 2> first_ellipse_point{
            8.0 * std::cos(first_parameter), 4.0 * std::sin(first_parameter)};
        const std::array<double, 2> second_ellipse_point{
            8.0 * std::cos(second_parameter), 4.0 * std::sin(second_parameter)};
        const auto oblique_chord = oblique_ellipse_trim.add_segment(
            first_ellipse_point[0], first_ellipse_point[1],
            second_ellipse_point[0], second_ellipse_point[1]);
        const auto oblique_topology = zima::sketcher::sketch_trim_topology(
            oblique_ellipse_trim, false);
        const auto removed_oblique_piece =
            zima::sketcher::nearest_sketch_trim_piece(
                oblique_topology, {-7.5, -1.0}, 2.0);
        require(removed_oblique_piece &&
                    removed_oblique_piece->geometry_id == oblique_ellipse_id,
                "Oblique analytical Ellipse crossing was not refined for Trim");
        static_cast<void>(zima::sketcher::apply_sketch_trim(
            oblique_ellipse_trim, {*removed_oblique_piece}));
        const auto chord_value = std::find_if(
            oblique_ellipse_trim.segments.begin(),
            oblique_ellipse_trim.segments.end(),
            [&](const auto& value) { return value.id == oblique_chord; });
        const auto& oblique_arc = oblique_ellipse_trim.elliptical_arcs.front();
        const std::set<std::string> chord_points{
            chord_value->first_point_id, chord_value->second_point_id};
        const std::set<std::string> arc_points{
            oblique_arc.start_point_id, oblique_arc.end_point_id};
        require(chord_points == arc_points,
                "Refined Ellipse trim did not preserve shared exact chord endpoints");

        auto stale_trim = zima::sketcher::Sketch::create_default();
        static_cast<void>(stale_trim.add_segment(-5.0, 2.0, 5.0, 2.0));
        auto stale_piece = zima::sketcher::sketch_trim_topology(
            stale_trim, false).front();
        stale_piece.start += 0.1;
        const auto stale_before = stale_trim.serialized();
        bool stale_rejected = false;
        try {
            static_cast<void>(zima::sketcher::apply_sketch_trim(
                stale_trim, {stale_piece}));
        } catch (const std::invalid_argument&) {
            stale_rejected = true;
        }
        require(stale_rejected && stale_trim.serialized() == stale_before,
                "Invalid or stale trim piece partially changed the Sketch");

        // Keep the complete create/edit/commit/reload path covered without
        // reconstructing the model through OCCT while loading.
        auto document = zima::document::PartDocument::create_default();
        auto committed_sketch = zima::sketcher::Sketch::create_default();
        committed_sketch.name = "Slice profile";
        committed_sketch.plane = zima::sketcher::SketchPlane::XZ;
        const auto bottom = committed_sketch.add_segment(
            0.0, 0.0, 20.0, 0.0);
        const auto right = committed_sketch.add_segment(
            20.0, 0.0, 20.0, 10.0);
        const auto top = committed_sketch.add_segment(
            20.0, 10.0, 0.0, 10.0);
        const auto left = committed_sketch.add_segment(
            0.0, 10.0, 0.0, 0.0);
        const auto construction = committed_sketch.add_segment(
            0.0, 5.0, 20.0, 5.0, 1.0e-6, true);
        committed_sketch.set_segment_centerline(construction, true);
        const auto circle = committed_sketch.add_circle(
            10.0, 5.0, 2.0, true);
        const auto arc = committed_sketch.add_arc(
            10.0, 5.0, 12.0, 5.0, 10.0, 7.0, true);
        require(committed_sketch.segments.size() == 5 &&
                    committed_sketch.circles.size() == 1 &&
                    committed_sketch.arcs.size() == 1 &&
                    committed_sketch.segments.back().id == construction &&
                    committed_sketch.circles.front().id == circle &&
                    committed_sketch.arcs.front().id == arc &&
                    committed_sketch.segments.front().id == bottom &&
                    committed_sketch.segments[1].id == right &&
                    committed_sketch.segments[2].id == top &&
                    committed_sketch.segments[3].id == left &&
                    committed_sketch.segments.back().construction &&
                    committed_sketch.segments.back().centerline &&
                    committed_sketch.circles.front().construction &&
                    committed_sketch.arcs.front().construction,
                "Sketch slice geometry did not preserve stable IDs and construction state");
        static_cast<void>(committed_sketch.add_segment_constraint(
            bottom, zima::sketcher::ConstraintKind::Horizontal));
        auto profile_dimension = committed_sketch.create_segment_dimension(bottom);
        profile_dimension.driving = false;
        committed_sketch.apply_dimension(profile_dimension);
        auto external = zima::sketcher::Sketch::create_external_reference(
            zima::sketcher::ExternalReferenceKind::Edge);
        external.source_document_id = "source-part";
        external.source_owner_id = "source-feature";
        external.source_semantic_key = "edge:stable-profile";
        external.cached_points = {{0.0, 0.0}, {20.0, 0.0}};
        const auto external_id = external.id;
        committed_sketch.add_external_reference(external);
        committed_sketch.validate();
        const auto centerline_round_trip = zima::sketcher::Sketch::from_serialized(
            committed_sketch.serialized());
        require(centerline_round_trip.segments.back().centerline,
                "Sketch centerline lost its unbounded semantic on round-trip");

        const auto before_cancel = committed_sketch.serialized();
        auto cancelled_edit = committed_sketch;
        static_cast<void>(cancelled_edit.add_point(100.0, 100.0));
        require(committed_sketch.serialized() == before_cancel &&
                    cancelled_edit.serialized() != before_cancel,
                "Sketch Cancel path changed persisted input");
        const auto sketch_id = committed_sketch.id;
        document.sketches.push_back(committed_sketch);
        document.insert_history_entry(
            zima::document::PartHistoryKind::Sketch, sketch_id);
        auto extrusion = zima::document::PartDocument::create_extrusion_container(
            sketch_id);
        extrusion.extrusion.height = 12.0;
        document.history.push_back(extrusion);
        document.insert_history_entry(
            zima::document::PartHistoryKind::Feature, extrusion.id);
        const auto feature_ops = document.kernel_operations();
        require(std::any_of(feature_ops.begin(), feature_ops.end(),
                    [&](const auto& operation) {
                        return operation.owner_id == extrusion.id;
                    }),
                "Extrusion did not consume the committed Sketch profile");

        const auto document_path =
            std::filesystem::path{"zima-cad-cpp-sketch-slice-contract.prt"};
        document.save(document_path);
        const auto reloaded_document =
            zima::document::PartDocument::load(document_path);
        std::filesystem::remove(document_path);
        require(reloaded_document.sketches.size() == 1 &&
                    reloaded_document.sketches.front().id == sketch_id &&
                    reloaded_document.sketches.front().external_references.front().id ==
                        external_id &&
                    reloaded_document.history.size() == 1 &&
                    reloaded_document.history.front().extrusion.sketch_id == sketch_id,
                "Committed Sketch and Extrusion did not round-trip without OCCT");

        std::cout << "C++ Sketcher contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
