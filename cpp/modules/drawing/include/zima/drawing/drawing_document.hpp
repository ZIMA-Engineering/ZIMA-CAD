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
enum class ProjectionDirection {
    None, Right, TopRight, Top, TopLeft, Left, BottomLeft, Bottom, BottomRight
};

struct ProjectionCamera {
    zima::kernel::Vec3 horizontal{-1.0, 0.0, 0.0};
    zima::kernel::Vec3 vertical{0.0, 0.0, 1.0};
    zima::kernel::Vec3 depth{0.0, -1.0, 0.0};
};

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
    ProjectionCamera camera;
    ProjectionDirection projection_direction{ProjectionDirection::None};
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
    bool unresolved{};
};

enum class DrawingPen { White, Green, Yellow };
struct TemplateLine { Point2 first; Point2 second; DrawingPen pen{DrawingPen::Green}; };
struct TemplateText {
    std::string text; Point2 position; double height{2.5};
    DrawingPen pen{DrawingPen::Green}; std::string alignment{"left"};
};
struct TitleBlockField {
    std::string id; std::string expression; std::string value;
    Point2 position; double height{2.5}; bool editable{};
    DrawingPen pen{DrawingPen::Green};
    std::string alignment{"left"};
    std::string vertical_alignment{"middle"};
    double box_width{};
    double box_height{};
    std::string format;
    bool write_back{};
};
struct BomRow {
    int item_number{}; int quantity{1}; std::string name;
    std::string designation; std::string material;
};

struct DrawingSheet {
    std::string id;
    std::string name{"List 1"};
    SheetFormat format{SheetFormat::A4};
    ProjectionMethod projection_method{ProjectionMethod::FirstAngle};
    double default_scale{1.0};
    std::vector<DrawingView> views;
    std::vector<LinearDimension> dimensions;
    std::vector<TemplateLine> frame_lines;
    std::vector<TemplateText> frame_texts;
    std::vector<TemplateLine> title_block_lines;
    std::vector<TemplateText> title_block_texts;
    std::vector<TitleBlockField> title_block_fields;
    std::vector<BomRow> bom_rows;

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
[[nodiscard]] ProjectionCamera standard_camera(ViewOrientation orientation);
[[nodiscard]] ProjectionCamera projected_camera(
    const ProjectionCamera& parent, ProjectionDirection direction,
    ProjectionMethod method);
[[nodiscard]] std::vector<ProjectedEdge> project_edges(
    const zima::kernel::ViewerMesh& mesh, const ProjectionCamera& camera);
void load_frame_template(DrawingSheet& sheet, const std::filesystem::path& path);
void load_title_block_template(DrawingSheet& sheet, const std::filesystem::path& path);
[[nodiscard]] std::vector<ProjectedTriangle> project_triangles(
    const zima::kernel::ViewerMesh& mesh, const ProjectionCamera& camera);

}  // namespace zima::drawing
