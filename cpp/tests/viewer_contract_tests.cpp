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
        };
        const auto edges = zima::viewer::ordered_edge_candidates(
            mesh, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.01);
        require(edges.size() == 2 && edges[0].reference.owner_id == "front" &&
                    edges[1].reference.owner_id == "back",
                "Edge candidates do not use stable owners in depth order");
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
                "Edge selection contract did not filter the common candidate list");
        const auto container_contract = zima::viewer::filter_candidates(
            all_candidates, {zima::viewer::CandidateKind::Container});
        require(container_contract.size() == 2 &&
                    container_contract.front().owner_id == "front-container",
                "Default container contract did not preserve leaf-first depth order");
        std::cout << "C++ viewer picking contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
