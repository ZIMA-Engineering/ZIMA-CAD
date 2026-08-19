#pragma once

#include <zima/kernel/geometry_kernel.hpp>

#include <filesystem>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace zima::drawing {

enum class SheetFormat { A4, A3, A2, A1, A0 };
enum class ProjectionMethod { FirstAngle, ThirdAngle };
enum class ViewOrientation { Front, Back, Left, Right, Top, Bottom, Isometric };
enum class DisplayStyle { VisibleEdges, HiddenEdges, ShadedWithEdges };

struct Point2 {
    double x{};
    double y{};
    bool operator==(const Point2&) const = default;
};

struct ProjectedEdge {
    std::vector<Point2> points;
    zima::kernel::EdgeReference source;
    bool hidden{};
};

struct ProjectedTriangle {
    std::array<Point2, 3> points;
    zima::kernel::FaceReference source;
    double depth{};
    double light{};
};

struct DrawingView {
    std::string id;
    std::string name{"Pohled"};
    std::string source_document_id;
    std::filesystem::path source_path;
    std::string parent_view_id;
    ViewOrientation orientation{ViewOrientation::Front};
    DisplayStyle display_style{DisplayStyle::VisibleEdges};
    double x{100.0};
    double y{100.0};
    double scale{1.0};
    std::vector<ProjectedEdge> projected_edges;
    std::vector<ProjectedTriangle> projected_triangles;
};

struct LinearDimension {
    std::string id;
    std::string view_id;
    zima::kernel::EdgeReference first;
    zima::kernel::EdgeReference second;
    Point2 first_point;
    Point2 second_point;
    Point2 label_position;
    double measured_value{};
};

struct DrawingSheet {
    std::string id;
    std::string name{"List 1"};
    SheetFormat format{SheetFormat::A4};
    ProjectionMethod projection_method{ProjectionMethod::FirstAngle};
    double default_scale{1.0};
    std::vector<DrawingView> views;
    std::vector<LinearDimension> dimensions;

    [[nodiscard]] double width_mm() const;
    [[nodiscard]] double height_mm() const;
};

class DrawingDocument {
public:
    std::string document_id;
    std::string name{"Nový výkres"};
    std::vector<DrawingSheet> sheets;

    [[nodiscard]] static DrawingDocument create_default();
    [[nodiscard]] static DrawingView create_view(
        std::string source_document_id, std::filesystem::path source_path,
        const zima::kernel::ViewerMesh& source_mesh,
        ViewOrientation orientation = ViewOrientation::Front);
    [[nodiscard]] DrawingSheet* find_sheet(const std::string& id);
    [[nodiscard]] const DrawingSheet* find_sheet(const std::string& id) const;
    [[nodiscard]] DrawingView* find_view(const std::string& id);
    [[nodiscard]] const DrawingView* find_view(const std::string& id) const;
    void refresh_view(const std::string& view_id,
                      const zima::kernel::ViewerMesh& source_mesh);
    [[nodiscard]] static DrawingDocument load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
};

[[nodiscard]] std::vector<ProjectedEdge> project_edges(
    const zima::kernel::ViewerMesh& mesh, ViewOrientation orientation);
[[nodiscard]] std::vector<ProjectedTriangle> project_triangles(
    const zima::kernel::ViewerMesh& mesh, ViewOrientation orientation);

}  // namespace zima::drawing
