#include <zima/drawing/drawing_document.hpp>

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
        zima::kernel::ViewerMesh mesh;
        mesh.edges.push_back({{{0, 0, 0}, {10, 0, 0}}, {"box", "edge:x", ""}});
        auto drawing = zima::drawing::DrawingDocument::create_default();
        auto view = zima::drawing::DrawingDocument::create_view(
            "part-1", "part.prtz", mesh, zima::drawing::ViewOrientation::Front);
        require(view.projected_edges.size() == 1 &&
                    std::abs(view.projected_edges.front().points.back().x + 10.0) < 1e-9,
                "Front Drawing projection does not follow viewer convention");
        zima::kernel::ViewerMesh occluded;
        occluded.edges.push_back({{{0, 0, 0}, {10, 0, 0}}, {"box", "edge:hidden", ""}});
        occluded.vertices = {{-1, -1, -1}, {11, -1, -1}, {5, -1, 1}};
        occluded.triangles = {0, 1, 2};
        const auto hidden = zima::drawing::project_edges(
            occluded, zima::drawing::ViewOrientation::Front);
        require(hidden.size() == 1 && hidden.front().hidden,
                "Drawing hidden-line projection ignored an occluding face");
        const std::string view_id = view.id;
        drawing.sheets.front().views.push_back(std::move(view));
        auto child = zima::drawing::DrawingDocument::create_view(
            "part-1", "part.prtz", mesh, zima::drawing::ViewOrientation::Right);
        child.parent_view_id = view_id;
        const std::string child_id = child.id;
        drawing.sheets.front().views.push_back(std::move(child));
        const auto path = std::filesystem::temp_directory_path() /
            "zima-cad-cpp-drawing-contract.drwz";
        drawing.save(path);
        const auto loaded = zima::drawing::DrawingDocument::load(path);
        std::filesystem::remove(path);
        require(loaded.document_id == drawing.document_id &&
                    loaded.sheets.size() == 1 && loaded.find_view(view_id) != nullptr &&
                    loaded.find_view(child_id) != nullptr &&
                    loaded.find_view(child_id)->parent_view_id == view_id &&
                    loaded.find_view(view_id)->projected_edges.front().source.semantic_key == "edge:x",
                "Drawing save/load lost source identity or projected geometry");
        require(loaded.sheets.front().width_mm() == 210.0 &&
                    loaded.sheets.front().height_mm() == 297.0,
                "A4 Drawing orientation is not portrait");
        std::cout << "C++ Drawing contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
