#pragma once
#include <zima/drawing/drawing_document.hpp>
namespace zima::drawing {
enum class MeasurementState { Resolved, Hidden, Unresolved };
struct DimensionEvaluation {
    MeasurementState state{MeasurementState::Unresolved};
    std::string message;
    std::vector<bool> resolved_attachments;
    bool direction_resolved{true};
    std::vector<kernel::ViewerDimension> presentations;
    std::vector<std::size_t> cached_segment_indices;
};
struct ProjectedMeasurementCurve {
    kernel::EdgeReference source;
    std::vector<Point2> points;
    std::vector<double> parameters;
    double sweep{};
    bool line{}, circular{};
    std::optional<Point2> center;
    Point2 cosine_axis, sine_axis;
};
struct MeasurementPickRequest {
    int mode{-1}; // Automatic, or an explicit DimensionAttachmentKind.
    bool circles_only{}, lines_only{};
    kernel::EdgeReference intersection_first, parallel_line;
    Point2 tangent_direction{1, 0};
    std::optional<Point2> tangent_origin;
};
struct MeasurementCandidate {
    DimensionAttachment attachment;
    Point2 position;
    double distance{};
};
std::vector<MeasurementCandidate> measurement_candidates(const DrawingView &, Point2, double tolerance,
                                                         const MeasurementPickRequest &);
void capture_measurement_geometry(DrawingView &, const kernel::ViewerMesh &);
std::vector<ProjectedMeasurementCurve> projected_measurement_curves(const DrawingView &);
std::optional<Point2> resolve_dimension_attachment(const DrawingView &, const DimensionAttachment &,
                                                   Point2 direction = {1, 0});
std::vector<std::pair<Point2, double>> dimension_intersections(const ProjectedMeasurementCurve &,
                                                               const ProjectedMeasurementCurve &);
DimensionEvaluation evaluate_drawing_dimension(const DrawingView &, const DrawingDimension &);
void refresh_drawing_dimension(const DrawingView &, DrawingDimension &);
void validate_drawing_dimension(const DrawingDimension &);
DrawingDimension make_drawing_dimension(std::string view_id,
                                        DrawingDimensionKind = DrawingDimensionKind::Linear);
void resize_dimension_segments(DrawingDimension &);
void extend_dimension_chain(DrawingDimension &, bool at_first, DimensionAttachment);
std::string drawing_dimension_text(const DrawingDimension &, const kernel::ViewerDimension &,
                                   bool unresolved = false);
void drag_drawing_dimension(const DrawingView &, DrawingDimension &, std::size_t segment, int handle,
                            Point2 delta);
void place_drawing_dimension(const DrawingView &, DrawingDimension &, std::size_t segment, Point2 point);
std::string serialize_drawing_dimensions(const std::vector<DrawingDimension> &);
std::vector<DrawingDimension> deserialize_drawing_dimensions(const std::string &);
std::string serialize_measurement_geometry(const DrawingView &);
void deserialize_measurement_geometry(DrawingView &, const std::string &);
} // namespace zima::drawing
