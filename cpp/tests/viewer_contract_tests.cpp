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
            {"original-box", "z_max"}};
        const auto separated_candidates = zima::viewer::ordered_viewer_candidates(
            separated, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.01);
        const auto separated_faces = zima::viewer::filter_candidates(
            separated_candidates, {zima::viewer::CandidateKind::Face});
        require(separated_faces.size() == 1 &&
                    separated_faces.front().owner_id == "original-box" &&
                    separated_faces.front().geometry ==
                        zima::viewer::CandidateGeometry::OriginalReference,
                "Hidden original geometry did not replace result-face picking");
        std::cout << "C++ viewer picking contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
