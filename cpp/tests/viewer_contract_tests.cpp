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
        const auto tree_point_confirmation = zima::viewer::container_candidate(
            construction_point_mesh, "point-container");
        require(tree_point_confirmation &&
                    tree_point_confirmation->kind ==
                        zima::viewer::CandidateKind::Container &&
                    tree_point_confirmation->owner_id == "point-container",
                "Tree could not confirm the Point container marker");
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
        std::cout << "C++ viewer picking contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
