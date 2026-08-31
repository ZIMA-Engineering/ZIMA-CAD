#include <zima/viewer/picking.hpp>

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        zima::kernel::ViewerMesh mesh;
        mesh.vertices = {
            {-1.0, -1.0, 5.0}, {1.0, -1.0, 5.0}, {0.0, 1.0, 5.0},
            {-1.0, -1.0, 6.0}, {1.0, -1.0, 6.0}, {0.0, 1.0, 6.0},
            {-1.0, -1.0, 10.0}, {1.0, -1.0, 10.0}, {0.0, 1.0, 10.0},
        };
        mesh.triangles = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        mesh.triangle_references = {
            {"front-container", "z_max"},
            {"front-container", "z_max"},
            {"back-container", "z_max"},
        };
        const auto candidates = zima::viewer::ordered_ray_candidates(
            mesh, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0});
        require(candidates.size() == 2,
                "Ray did not deduplicate triangles into two face candidates");
        require(candidates[0].triangle == 0 && candidates[1].triangle == 2,
                "Candidates are not ordered front to back");
        require(candidates[0].reference.owner_id == "front-container" &&
                    candidates[1].reference.owner_id == "back-container",
                "Picking lost persisted face owners");
        require(zima::viewer::next_candidate_index(0, candidates.size()) == 1 &&
                    zima::viewer::next_candidate_index(1, candidates.size()) == 0,
                "RMB candidate cycling does not wrap in the same ordered list");
        const auto miss = zima::viewer::ordered_ray_candidates(
            mesh, {5.0, 5.0, 0.0}, {0.0, 0.0, 1.0});
        require(miss.empty(), "Ray miss returned a candidate");
        mesh.edges = {
            {{{-1.0, 0.0, 5.0}, {1.0, 0.0, 5.0}}, {"front", "edge-a"}},
            {{{-1.0, 0.0, 10.0}, {1.0, 0.0, 10.0}}, {"back", "edge-b"}},
            {{{-1.0, 0.0, 7.0}, {1.0, 0.0, 7.0}}, {}},
        };
        const auto edges = zima::viewer::ordered_edge_candidates(
            mesh, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.01);
        require(edges.size() == 2 && edges[0].reference.owner_id == "front" &&
                    edges[1].reference.owner_id == "back",
                "Edge candidates do not use stable owners in depth order");
        require(mesh.edges.size() == 3,
                "Viewer packet did not retain an unowned display-only result edge");
        const zima::kernel::ViewerEdge first_occurrence_wire{
            {{-1.0, 0.0, 5.0}, {1.0, 0.0, 5.0}},
            {{}, {}, "5:first"}};
        const zima::kernel::ViewerEdge second_occurrence_wire{
            {{-1.0, 0.0, 10.0}, {1.0, 0.0, 10.0}},
            {{}, {}, "6:second"}};
        const zima::viewer::ViewerCandidate first_occurrence{
            zima::viewer::CandidateKind::Occurrence, 0.0, 0, {}, {},
            "5:first", zima::viewer::CandidateGeometry::Display};
        const zima::viewer::ViewerCandidate local_part_body{
            zima::viewer::CandidateKind::Occurrence, 0.0, 0, {}, {}, {},
            zima::viewer::CandidateGeometry::Display};
        require(zima::viewer::candidate_recolors_wire_edge(
                    first_occurrence, first_occurrence_wire) &&
                    !zima::viewer::candidate_recolors_wire_edge(
                        first_occurrence, second_occurrence_wire),
                "Assembly body selection did not recolour only its existing "
                "occurrence wire");
        require(zima::viewer::candidate_recolors_wire_edge(
                    local_part_body, first_occurrence_wire) &&
                    zima::viewer::candidate_recolors_wire_edge(
                        local_part_body, second_occurrence_wire),
                "Part Body Tree selection did not recolour its existing full wire");
        const zima::kernel::ViewerEdge sketch_wire{
            {{-1.0, 0.0, 7.0}, {1.0, 0.0, 7.0}},
            {"sketch", "segment:profile", {}}};
        require(!zima::viewer::candidate_recolors_wire_edge(
                    local_part_body, sketch_wire),
                "Part Body Tree selection also recoloured Sketch geometry");
        const zima::viewer::ViewerCandidate face_overlay{
            zima::viewer::CandidateKind::Face, 0.0, 0,
            "source", "step:face:#42", "5:first",
            zima::viewer::CandidateGeometry::OriginalReference};
        require(!zima::viewer::candidate_recolors_wire_edge(
                    face_overlay, first_occurrence_wire),
                "Face overlay incorrectly entered the cheap whole-body wire path");
        zima::kernel::ViewerMesh infinite_line_mesh;
        infinite_line_mesh.edges.push_back({
            {{-1.0, 0.0, 5.0}, {1.0, 0.0, 5.0}},
            {"sketch", "segment:centerline"}, true, true, true, true});
        const auto infinite_line_candidates =
            zima::viewer::ordered_edge_candidates(infinite_line_mesh,
                {50.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.01);
        require(infinite_line_candidates.size() == 1,
                "Unbounded Sketch centerline remained pickable only between defining points");
        zima::kernel::ViewerMesh ordinary_sketch_mesh;
        ordinary_sketch_mesh.edges.push_back({
            {{-5.0, 0.0, 5.0}, {5.0, 0.0, 5.0}},
            {"sketch-container", "segment:profile"}, false, true});
        const auto ordinary_sketch_candidates =
            zima::viewer::ordered_viewer_candidates(ordinary_sketch_mesh,
                {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.01);
        const auto ordinary_sketch_containers = zima::viewer::filter_candidates(
            ordinary_sketch_candidates,
            {zima::viewer::CandidateKind::Container});
        const auto tree_sketch_candidate = zima::viewer::container_candidate(
            ordinary_sketch_mesh, "sketch-container");
        require(ordinary_sketch_containers.size() == 1 &&
                    ordinary_sketch_containers.front().owner_id ==
                        "sketch-container" &&
                    ordinary_sketch_containers.front().semantic_key == "sketch" &&
                    tree_sketch_candidate &&
                    tree_sketch_candidate->semantic_key == "sketch",
                "Ordinary View did not expose the whole Sketch through the "
                "shared candidate list");
        mesh.points = {
            {{0.0, 0.0, 4.0}, {"front", "vertex-a"}},
            {{0.0, 0.0, 9.0}, {"back", "vertex-b"}},
        };
        const auto points = zima::viewer::ordered_vertex_candidates(
            mesh, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.01);
        require(points.size() == 2 && points[0].reference.semantic_key == "vertex-a" &&
                    points[1].reference.semantic_key == "vertex-b",
                "Vertex candidates do not use stable references in depth order");
        zima::kernel::ViewerMesh construction_point_mesh;
        construction_point_mesh.points.push_back({
            {0.0, 0.0, 5.0}, {"point-container:origin", "point"}, "Bod001"});
        const auto construction_point_candidates =
            zima::viewer::ordered_viewer_candidates(construction_point_mesh,
                {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.01);
        const auto construction_point_containers = zima::viewer::filter_candidates(
            construction_point_candidates,
            {zima::viewer::CandidateKind::Container});
        const auto construction_point_vertices = zima::viewer::filter_candidates(
            construction_point_candidates,
            {zima::viewer::CandidateKind::Vertex});
        require(construction_point_containers.size() == 1 &&
                    construction_point_containers.front().owner_id ==
                        "point-container" &&
                    construction_point_vertices.size() == 1 &&
                    construction_point_vertices.front().owner_id ==
                        "point-container:origin",
                "Point marker did not expose its container and persisted point roles");
        zima::kernel::ViewerMesh solid_origin_mesh;
        solid_origin_mesh.points.push_back({
            {0.0, 0.0, 5.0},
            {"box-container", "container:origin-marker"}});
        const auto solid_origin_candidates = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(solid_origin_mesh,
                {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.01),
            {zima::viewer::CandidateKind::Container});
        require(solid_origin_candidates.size() == 1 &&
                    solid_origin_candidates.front().owner_id ==
                        "box-container" &&
                    solid_origin_candidates.front().semantic_key == "solid",
                "Basic-solid origin marker did not confirm its owning container");
        const auto tree_point_confirmation = zima::viewer::container_candidate(
            construction_point_mesh, "point-container");
        require(tree_point_confirmation &&
                    tree_point_confirmation->kind ==
                        zima::viewer::CandidateKind::Container &&
                    tree_point_confirmation->owner_id == "point-container",
                "Tree could not confirm the Point container marker");
        zima::kernel::ViewerMesh construction_plane_mesh;
        construction_plane_mesh.edges.push_back({
            {{-1.0, 0.0, 5.0}, {1.0, 0.0, 5.0}},
            {"plane-container:entity", "border"}, false, true});
        construction_plane_mesh.original_references.edges.push_back({
            {{-1.0, 0.0, 5.0}, {1.0, 0.0, 5.0}},
            {"plane-container:entity", "border"}, false, true});
        const auto construction_plane_faces = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(construction_plane_mesh,
                {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.01),
            {zima::viewer::CandidateKind::Plane});
        require(construction_plane_faces.size() == 1 &&
                    construction_plane_faces.front().owner_id ==
                        "plane-container:entity" &&
                    construction_plane_faces.front().semantic_key == "plane",
                "Plane rectangle did not expose the stable Plane entity on hover");
        const auto all_candidates = zima::viewer::ordered_viewer_candidates(
            mesh, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.01);
        const auto edge_contract = zima::viewer::filter_candidates(
            all_candidates, {zima::viewer::CandidateKind::Edge});
        require(edge_contract.size() == 2 &&
                    edge_contract.front().kind == zima::viewer::CandidateKind::Edge,
                "Display-only OCCT edge leaked into the selectable candidate list");
        mesh.axes.push_back({
            {0.0, 0.0, 5.0}, {0.0, 0.0, 1.0}, 10.0,
            {"axis-owner", "axis:z", "4:part"}});
        const auto axis_candidates = zima::viewer::ordered_axis_candidates(
            mesh, {0.05, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.1);
        require(axis_candidates.size() == 1 &&
                    axis_candidates.front().reference.semantic_key == "axis:z",
                "Viewer did not pick the exact persisted axis");
        const auto axis_contract = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                mesh, {0.05, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.1),
            {zima::viewer::CandidateKind::Axis});
        require(axis_contract.size() == 1 &&
                    axis_contract.front().kind == zima::viewer::CandidateKind::Axis &&
                    axis_contract.front().instance_path == "4:part",
                "Axis selection contract left the common candidate list");
        zima::kernel::ViewerMesh sketch_origin_priority_mesh;
        sketch_origin_priority_mesh.points.push_back({
            {0.0, 0.0, 5.001},
            {"sketch", "external_point:sketch_origin"}});
        sketch_origin_priority_mesh.axes.push_back({
            {0.0, 0.0, 5.0}, {1.0, 0.0, 0.0}, 10.0,
            {"sketch", "sketch_axis:x"}});
        const auto sketch_origin_priority = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(sketch_origin_priority_mesh,
                {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.01),
            {zima::viewer::CandidateKind::SketchExternalReference,
             zima::viewer::CandidateKind::SketchAxis});
        require(sketch_origin_priority.size() == 2 &&
                    sketch_origin_priority.front().kind ==
                        zima::viewer::CandidateKind::SketchExternalReference &&
                    sketch_origin_priority.front().semantic_key ==
                        "external_point:sketch_origin",
                "Sketch origin point did not take stable priority over its axes");
        mesh.dimensions.push_back({
            {-1.0, 0.0, 5.0}, {1.0, 0.0, 5.0},
            {-1.0, 1.0, 5.0}, {1.0, 1.0, 5.0}, 2.0,
            {"sketch", "dimension:length"}});
        const auto dimension_contract = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                mesh, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, 0.01),
            {zima::viewer::CandidateKind::Dimension});
        require(dimension_contract.size() == 1 &&
                    dimension_contract.front().owner_id == "sketch",
                "Dimension selection contract left the common candidate list");
        zima::kernel::ViewerMesh angular_dimension_mesh;
        zima::kernel::ViewerDimension angular_dimension{
            {0.0, 0.0, 5.0}, {0.0, 0.0, 5.0},
            {1.0, 0.0, 5.0}, {0.0, 1.0, 5.0}, 90.0,
            {"sketch", "dimension:angle"}, "∠ ", "°"};
        angular_dimension.kind =
            zima::kernel::ViewerDimensionKind::Angular;
        angular_dimension.sweep_degrees = 90.0;
        angular_dimension_mesh.dimensions.push_back(angular_dimension);
        const auto angular_dimension_candidates =
            zima::viewer::filter_candidates(
                zima::viewer::ordered_viewer_candidates(
                    angular_dimension_mesh, {0.70710678, 0.70710678, 0.0},
                    {0.0, 0.0, 1.0}, 0.03),
                {zima::viewer::CandidateKind::Dimension});
        require(angular_dimension_candidates.size() == 1 &&
                    angular_dimension_candidates.front().semantic_key ==
                        "dimension:angle",
                "Angular dimension arc did not use the common picker");
        mesh.constraint_markers.push_back({
            {0.0, 2.0, 5.0}, "H", {"sketch", "constraint:horizontal"}});
        const auto constraint_contract = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                mesh, {0.0, 2.0, 0.0}, {0.0, 0.0, 1.0}, 0.01),
            {zima::viewer::CandidateKind::SketchConstraint});
        require(constraint_contract.size() == 1 &&
                    constraint_contract.front().owner_id == "sketch" &&
                    constraint_contract.front().semantic_key ==
                        "constraint:horizontal",
                "Constraint marker did not use the common hover/click candidate list");
        const auto visible_glyph_constraint = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                mesh, {0.015, 2.0, 0.0}, {0.0, 0.0, 1.0}, 0.01),
            {zima::viewer::CandidateKind::SketchConstraint});
        require(visible_glyph_constraint.size() == 1,
            "Visible offset constraint glyph was not offered on hover");
        zima::kernel::ViewerMesh trim_mesh;
        trim_mesh.edges.push_back({
            {{-2.0, 0.0, 5.0}, {2.0, 0.0, 5.0}},
            {"sketch", "trim_piece:7"}, false, true});
        const auto trim_contract = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                trim_mesh, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.01),
            {zima::viewer::CandidateKind::SketchTrimPiece});
        require(trim_contract.size() == 1 &&
                    trim_contract.front().owner_id == "sketch" &&
                    trim_contract.front().semantic_key == "trim_piece:7",
                "Trim hover and click did not use one common viewer candidate");
        const std::vector<zima::viewer::ViewerCandidate> sketch_segments{
            {zima::viewer::CandidateKind::SketchSegment, 1.0, 0,
             "sketch", "segment:profile"},
            {zima::viewer::CandidateKind::SketchSegment, 2.0, 1,
             "sketch", "segment:construction"},
            {zima::viewer::CandidateKind::SketchPoint, 3.0, 2,
             "sketch", "point:a"}};
        const auto construction_contract = zima::viewer::filter_candidates(
            sketch_segments, {zima::viewer::CandidateKind::SketchSegment},
            [](const auto& candidate) {
                return candidate.semantic_key == "segment:construction";
            });
        require(construction_contract.size() == 1 &&
                    construction_contract.front().semantic_key ==
                        "segment:construction" &&
                    construction_contract.front().distance == 2.0,
                "Command candidate filter changed order or offered an invalid segment");
        zima::kernel::ViewerMesh elliptical_arc_mesh;
        elliptical_arc_mesh.edges.push_back({
            {{-2.0, 0.0, 5.0}, {2.0, 0.0, 5.0}},
            {"sketch", "elliptical_arc:arc-a"}, false, true});
        const auto elliptical_arc_contract = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(
                elliptical_arc_mesh, {0.0, 0.0, 0.0},
                {0.0, 0.0, 1.0}, 0.01),
            {zima::viewer::CandidateKind::SketchCurve});
        require(elliptical_arc_contract.size() == 1 &&
                    elliptical_arc_contract.front().semantic_key ==
                        "elliptical_arc:arc-a",
                "Persisted elliptical Arc was not exposed by the SketchCurve contract");
        const auto container_contract = zima::viewer::filter_candidates(
            all_candidates, {zima::viewer::CandidateKind::Container});
        require(container_contract.size() == 2 &&
                    container_contract.front().owner_id == "front-container",
                "Default container contract did not preserve leaf-first depth order");
        const auto tree_confirmation =
            zima::viewer::container_candidate(mesh, "back-container");
        require(tree_confirmation &&
                    tree_confirmation->kind == zima::viewer::CandidateKind::Container &&
                    tree_confirmation->owner_id == "back-container",
                "Tree selection did not resolve the same stable container candidate");
        require(!zima::viewer::container_candidate(mesh, "missing-container"),
                "Tree selection accepted a container absent from viewer data");
        zima::kernel::ViewerMesh separated;
        separated.vertices = {
            {-1.0, -1.0, 5.0}, {1.0, -1.0, 5.0}, {0.0, 1.0, 5.0}};
        separated.triangles = {0, 1, 2};
        separated.triangle_references = {{}};
        separated.original_references.vertices = separated.vertices;
        separated.original_references.triangles = separated.triangles;
        separated.original_references.triangle_references = {
            {"original-box", "z_max", "8:assembly4:part"}};
        separated.triangle_references = {
            {"result-body", "result-face", "8:assembly4:part"}};
        const auto separated_candidates = zima::viewer::ordered_viewer_candidates(
            separated, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.01);
        const auto separated_faces = zima::viewer::filter_candidates(
            separated_candidates, {zima::viewer::CandidateKind::Face});
        require(separated_faces.size() == 1 &&
                    separated_faces.front().owner_id == "original-box" &&
                    separated_faces.front().geometry ==
                        zima::viewer::CandidateGeometry::OriginalReference,
                "Hidden original geometry did not replace result-face picking");
        const auto separated_occurrences = zima::viewer::filter_candidates(
            separated_candidates, {zima::viewer::CandidateKind::Occurrence});
        const auto separated_containers = zima::viewer::filter_candidates(
            separated_candidates, {zima::viewer::CandidateKind::Container});
        require(separated_occurrences.size() == 1 &&
                    separated_occurrences.front().geometry ==
                        zima::viewer::CandidateGeometry::OriginalReference &&
                    separated_containers.size() == 1 &&
                    separated_containers.front().owner_id == "original-box" &&
                    separated_containers.front().geometry ==
                        zima::viewer::CandidateGeometry::OriginalReference,
                "Leaf selection preferred transient result-body topology");
        auto local_part = separated;
        local_part.triangle_references.front().instance_path.clear();
        local_part.original_references.triangle_references.front()
            .instance_path.clear();
        const auto local_part_containers = zima::viewer::filter_candidates(
            zima::viewer::ordered_viewer_candidates(local_part,
                {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.01),
            {zima::viewer::CandidateKind::Container});
        require(local_part_containers.size() == 1 &&
                    local_part_containers.front().owner_id == "original-box" &&
                    local_part_containers.front().geometry ==
                        zima::viewer::CandidateGeometry::OriginalReference,
                "Part hover offered the result Body instead of its leaf Container");
        std::cout << "C++ viewer picking contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
