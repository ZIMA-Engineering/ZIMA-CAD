#pragma once
#include <zima/kernel/dimension_layout.hpp>
#include <zima/document/section.hpp>
#include <zima/document/document_copy.hpp>

#include <zima/kernel/geometry_kernel.hpp>
#include <zima/document/dimension_identifiers.hpp>

#include <zima/sketcher/sketch.hpp>
#include <filesystem>
#include <array>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace zima::drawing {

enum class SheetFormat { A4, A3, A2, A1, A0 };
enum class ProjectionMethod { FirstAngle, ThirdAngle };
enum class ViewOrientation { Front, Back, Left, Right, Top, Bottom, Isometric };
enum class DisplayStyle { VisibleEdges, HiddenEdges, ShadedWithEdges, Shaded };
enum class HiddenEdgeStyle { Dashed, Gray };
enum class TangentEdgeStyle { Visible, Thin, Hidden };
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

enum class ModelAnnotationKind { Dimension, Axis, Construction };
struct ModelAnnotationReference {
    std::string document_id, owner_id, semantic_id, instance_path;
    bool operator==(const ModelAnnotationReference&) const = default;
    auto operator<=>(const ModelAnnotationReference&) const = default;
};
struct ModelAnnotation {
    ModelAnnotationReference source;
    std::array<double,3> plane_normal{0,0,1};
    ModelAnnotationKind kind{ModelAnnotationKind::Dimension};
    kernel::ViewerDimensionKind dimension_kind{kernel::ViewerDimensionKind::Linear};
    // Last explicit projection, in view model units (not paper mm).
    std::vector<std::vector<Point2>> curves;
    Point2 text_anchor;
    std::string text;
    double value{};
    bool visible{}, unresolved{};
    // User placements in paper mm relative to the view origin. Semantic keys
    // include text, arrow_first and arrow_second; never indexed topology.
    std::map<std::string, Point2> paper_handles;
    std::optional<kernel::ViewerDimension> model_dimension;
    std::optional<std::array<kernel::Vec3,2>> model_axis;
    kernel::ModelEnvelope model_envelope;
    kernel::DimensionLayout model_layout;
    std::optional<kernel::DimensionLayout> view_layout;
    std::array<double,3> handle_camera_horizontal{1,0,0},handle_camera_vertical{0,1,0};
    bool operator==(const ModelAnnotation&) const = default;
};

struct ProjectedEdge {
    std::vector<Point2> points;
    zima::kernel::EdgeReference source;
    bool hidden{};
    bool silhouette{};
    bool tangent{};
    bool hatch{};
    int hatch_pattern{};
};

struct ProjectedTriangle {
    std::array<Point2, 3> points;
    zima::kernel::FaceReference source;
    double depth{};
    double light{};
    std::array<double,3> vertex_depths{};
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
    HiddenEdgeStyle hidden_edge_style{HiddenEdgeStyle::Dashed};
    TangentEdgeStyle tangent_edge_style{TangentEdgeStyle::Visible};
    std::string section_id, section_parent_id;
    std::optional<zima::document::SectionDefinition> section_snapshot;
    // Last calculated side, persisted with the projection for trace arrows.
    bool section_display_reversed{};
    // Styles belong to the source Section; only visibility belongs to a view.
    std::set<std::string> hidden_hatch_components;
    double x{100.0};
    double y{100.0};
    double scale{1.0};
    bool use_sheet_scale{true};
    bool show_caption{};
    bool show_section_label{true};
    // Optional paper-mm offsets from the view origin: right/up, independent
    // of model scale. Unset labels follow the top of the calculated bounds.
    std::optional<Point2> caption_position, section_label_position;
    // Explicitly selected cutting traces, snapshotted from this view's source.
    std::vector<zima::document::SectionDefinition> section_markers;
    std::map<std::string,std::array<double,2>> section_marker_offsets;
    bool show_dimension_guides{};
    double dimension_guide_offset{8.0}, dimension_guide_spacing{8.0};
    std::vector<ModelAnnotation> model_annotations;
    std::vector<ProjectedEdge> projected_edges;
    std::vector<ProjectedTriangle> projected_triangles;
    std::set<std::string> value_locks;
};

void refresh_view_geometry(DrawingView&, const zima::kernel::ViewerMesh&);

struct SectionTraceLayout {
    // View-relative paper millimetres, X right and Y up.
    std::vector<std::array<Point2,2>> chain, accents;
    std::array<Point2,2> arrow_tips, arrow_directions;
    std::array<double,2> end_offsets{}, minimum_offsets{};
};
std::optional<SectionTraceLayout> section_trace_layout(const DrawingView&, const zima::document::SectionDefinition&,
    std::span<const DrawingView* const> section_views = {});
// Upright letter bounding box, attached to its outward end. Clearance is in
// paper mm; line obstacles include the model, all traces and their arrows.
Point2 section_letter_position(const SectionTraceLayout&,std::size_t end,Point2 text_size,
    const std::vector<std::array<Point2,2>>& obstacles,double clearance);

// The renderer and dimension picker share the same visibility contract.
inline bool drawing_edge_visible(const DrawingView& view,const ProjectedEdge& edge) {
    if(edge.hatch)return !edge.hidden;
    if(view.display_style==DisplayStyle::Shaded)return false;
    if(edge.tangent&&(edge.hidden||view.tangent_edge_style==TangentEdgeStyle::Hidden))return false;
    return !edge.hidden||view.display_style==DisplayStyle::HiddenEdges;
}

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

enum class DrawingPen { White, Green, Yellow, Red };
struct TemplateLine { Point2 first; Point2 second; DrawingPen pen{DrawingPen::Green}; };
struct TemplateCircle { Point2 center; double radius{}; DrawingPen pen{DrawingPen::Green}; };
struct TemplateText {
    std::string text; Point2 position; double height{2.5};
    DrawingPen pen{DrawingPen::Green}; std::string alignment{"left"};
    std::string vertical_alignment{"bottom"}; double angle{}; bool flipped{true};
    std::string font{"osifont"};
    std::string field_id; // Transient drawing hit identity, assigned by title_block_layout.
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
    bool anchor_position{}; double angle{}; bool flipped{true}; std::string font{"osifont"};
};
struct BomRow {
    int item_number{}; int quantity{1}; std::string name;
    std::string designation; std::string material;
    std::string file_stem;
    std::map<std::string,std::string> parameters;
    std::map<std::string,std::map<std::string,std::string>> parameter_values;
    std::map<std::string,std::string> parameter_aliases;
    std::string mass_unit{"kg"};
    std::string source_document_id;
    std::filesystem::path source_path;
};

struct DrawingSheet {
    std::string id;
    std::string name{"List 1"};
    SheetFormat format{SheetFormat::A4};
    ProjectionMethod projection_method{ProjectionMethod::FirstAngle};
    double default_scale{1.0};
    double thick_line_mm{0.5};
    double thin_line_mm{0.25};
    double red_line_mm{0.7};
    std::vector<DrawingView> views;
    std::vector<LinearDimension> dimensions;
    std::vector<TemplateLine> frame_lines;
    std::vector<TemplateText> frame_texts;
    std::vector<TemplateLine> title_block_lines;
    std::vector<TemplateText> title_block_texts;
    std::vector<TitleBlockField> title_block_fields;
    std::vector<BomRow> bom_rows;
    std::vector<TemplateCircle> frame_circles, title_block_circles;
    std::vector<zima::sketcher::SketchRepeatRegion> repeat_regions;
    std::vector<zima::sketcher::TemplateImage> title_block_images;
    std::string title_block_locale{"cs"};
    std::map<std::string, std::string> local_parameters;

    [[nodiscard]] double width_mm() const;
    [[nodiscard]] double height_mm() const;
};

inline double drawing_pen_width_mm(const DrawingSheet& sheet,DrawingPen pen) {
    if(pen==DrawingPen::White)return sheet.thick_line_mm;
    if(pen==DrawingPen::Red)return sheet.red_line_mm;
    return sheet.thin_line_mm;
}

class DrawingDocument {
public:
    zima::document::DimensionIdentifiers dimension_identifiers;
    [[nodiscard]] std::vector<zima::document::DimensionParameter> dimension_parameters() const;
    void synchronize_dimension_identifiers();
    std::string document_id;
    std::string name{"Nový výkres"};
    // The model document whose Tree-header VÝKRES command created this
    // drawing.  This relationship exists before the first view is inserted,
    // so DÍL/SESTAVA navigation never has to infer ownership from a view.
    std::string source_document_id;
    std::filesystem::path source_path;
    std::string source_name;
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
    void save(const std::filesystem::path& path,
        const zima::document::DocumentCopyIdentity& copy = {}) const;
};

// Mirrors Python's zima_cad.title_block context: the model-level values a
// title-block text field's "&token" expressions may resolve against.
// scope() classifies a token exactly as Python's title_block_token_scope()
// does: "drawing"/"local" prefixes are per-sheet local values, "document"/
// "sheet"/"bom" prefixes are system-computed values, everything else is a
// model (user) parameter.
struct TitleBlockContext {
    std::string file_stem;
    std::string mass_unit{"kg"};
    std::map<std::string, std::string> parameters;
    std::map<std::string, std::map<std::string, std::string>> parameter_values;
    std::map<std::string, std::string> parameter_aliases;
    std::map<std::string, std::string> local_parameters;
    int bom_item_number{};
    int bom_quantity{};
    bool has_bom_row{};
    int sheet_index{};
    int sheet_count{1};
    std::map<std::string,std::map<std::string,std::string>> parameter_labels;
    std::vector<std::string> parameter_order;
};

struct TitleBlockTextTarget {
    std::string expression;
    std::optional<std::size_t> bom_row;
};

struct TemplateLayout {
    // Transient picking targets; edits change source parameters, never the layout.
    std::map<std::string,TitleBlockTextTarget> edit_targets;
    std::vector<zima::sketcher::TemplateImage> images;
    std::vector<TemplateLine> lines;
    std::vector<TemplateText> texts;
    std::vector<TemplateCircle> circles;
};
[[nodiscard]] TemplateLayout title_block_layout(const DrawingSheet&, const TitleBlockContext&);
void load_template_details(DrawingSheet&, const std::filesystem::path&, bool title_block);

[[nodiscard]] std::vector<std::string> title_block_tokens(const std::string& text);
[[nodiscard]] std::string title_block_token_scope(const std::string& token);
[[nodiscard]] std::string title_block_parameter_key(
    const std::string& token, const TitleBlockContext& context);
[[nodiscard]] std::string resolve_title_block_text(
    const TitleBlockField& field, const TitleBlockContext& context,
    const DrawingSheet& sheet);

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
