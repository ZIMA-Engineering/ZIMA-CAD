#include <zima/drawing/drawing_document.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        const auto fixture = zima::drawing::DrawingDocument::load(
            std::filesystem::current_path() / "tests/fixtures/cross_language/drawing.drwz");
        require(fixture.document_id == "drawing-fixture-001" &&
                    fixture.sheets.size() == 1 &&
                    fixture.sheets.front().views.size() == 1 &&
                    fixture.sheets.front().views.front().source_document_id ==
                        "part-fixture-001",
                "Python Drawing fixture lost sheet or view identity");
        zima::kernel::ViewerMesh mesh;
        mesh.edges.push_back({{{0, 0, 0}, {10, 0, 0}}, {"box", "edge:x", ""}});
        mesh.edges.push_back({{{0, 0, 10}, {10, 0, 10}}, {"box", "edge:x-top", ""}});
        auto drawing = zima::drawing::DrawingDocument::create_default();
        auto view = zima::drawing::DrawingDocument::create_view(
            "part-1", "part.prtz", mesh, zima::drawing::ViewOrientation::Front);
        require(view.projected_edges.size() == 2 &&
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
        const auto parent_camera = zima::drawing::standard_camera(
            zima::drawing::ViewOrientation::Front);
        const auto first_angle = zima::drawing::projected_camera(parent_camera,
            zima::drawing::ProjectionDirection::Right,
            zima::drawing::ProjectionMethod::FirstAngle);
        const auto third_angle = zima::drawing::projected_camera(parent_camera,
            zima::drawing::ProjectionDirection::Right,
            zima::drawing::ProjectionMethod::ThirdAngle);
        require(std::abs(first_angle.depth.x - 1.0) < 1e-9 &&
                    std::abs(third_angle.depth.x + 1.0) < 1e-9,
                "First-/third-angle projected cameras did not reverse the view direction");
        const std::string view_id = view.id;
        drawing.sheets.front().views.push_back(std::move(view));
        zima::drawing::LinearDimension dimension;
        dimension.id = "dimension-1"; dimension.view_id = view_id;
        dimension.first = {"box", "edge:x", ""};
        dimension.second = {"box", "edge:x-top", ""};
        dimension.first_point = {-5, 0}; dimension.second_point = {-5, 10};
        dimension.label_position = {-5, 5}; dimension.measured_value = 10;
        drawing.sheets.front().dimensions.push_back(dimension);
        zima::drawing::load_frame_template(
            drawing.sheets.front(), "config/formats/ZE-A4.frmz");
        zima::drawing::load_title_block_template(
            drawing.sheets.front(), "config/formats/ZE-TITLE-BLOCK.tblz");
        require(!drawing.sheets.front().frame_lines.empty() &&
                    !drawing.sheets.front().title_block_lines.empty() &&
                    !drawing.sheets.front().title_block_fields.empty(),
                "C++ Drawing did not embed frame/title-block template geometry");
        auto changed_mesh = mesh;
        changed_mesh.edges[1].points = {{0, 0, 20}, {10, 0, 20}};
        drawing.refresh_view(view_id, changed_mesh);
        require(!drawing.sheets.front().dimensions.front().unresolved &&
                    std::abs(drawing.sheets.front().dimensions.front().measured_value - 20.0) < 1e-9,
                "Drawing regeneration did not update an associative linear dimension");
        auto child = zima::drawing::DrawingDocument::create_view(
            "part-1", "part.prtz", mesh, zima::drawing::ViewOrientation::Right);
        child.parent_view_id = view_id;
        const std::string child_id = child.id;
        drawing.sheets.front().views.push_back(std::move(child));
        const auto path = std::filesystem::current_path() /
            "zima-cad-cpp-drawing-contract.drwz";
        drawing.save(path);
        std::ifstream persisted(path);
        require(static_cast<bool>(persisted), "Drawing contract file was not written");
        const std::string ini((std::istreambuf_iterator<char>(persisted)), {});
        require(ini.find("[Document]\n") != std::string::npos &&
                    ini.find("format_version=11\n") != std::string::npos &&
                    ini.find("type=drawing\n") != std::string::npos &&
                    ini.find("param.cpp_drawing={") != std::string::npos &&
                    ini.find("[Containers]\n") != std::string::npos &&
                    ini.find("items=\n") != std::string::npos,
                "Drawing persistence is not Python-compatible INI");
        const auto loaded = zima::drawing::DrawingDocument::load(path);
        std::filesystem::remove(path);
        require(loaded.document_id == drawing.document_id &&
                    loaded.sheets.size() == 1 && loaded.find_view(view_id) != nullptr &&
                    loaded.find_view(child_id) != nullptr &&
                    loaded.find_view(child_id)->parent_view_id == view_id &&
                    !loaded.sheets.front().frame_lines.empty() &&
                    !loaded.sheets.front().title_block_fields.empty() &&
                    std::abs(loaded.sheets.front().dimensions.front().measured_value - 20.0) < 1e-9 &&
                    loaded.find_view(view_id)->projected_edges.front().source.semantic_key == "edge:x",
                "Drawing save/load lost source identity or projected geometry");
        require(loaded.sheets.front().width_mm() == 210.0 &&
                    loaded.sheets.front().height_mm() == 297.0,
                "A4 Drawing orientation is not portrait");
        require(loaded.name == drawing.name &&
                    loaded.sheets.front().name == drawing.sheets.front().name &&
                    loaded.sheets.front().projection_method ==
                        drawing.sheets.front().projection_method &&
                    loaded.find_view(view_id)->name == drawing.find_view(view_id)->name &&
                    loaded.find_view(view_id)->source_path ==
                        drawing.find_view(view_id)->source_path,
                "Drawing metadata, views, or sheets did not round-trip");
        std::cout << "C++ Drawing contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
