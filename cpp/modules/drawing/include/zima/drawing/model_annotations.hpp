#pragma once
#include <string_view>
#include <zima/drawing/drawing_document.hpp>
namespace zima::drawing {
inline bool non_drawing_axis(std::string_view semantic) {
  return semantic.starts_with("origin:axis:") || semantic.starts_with("sketch_axis:") ||
      semantic=="axis:x" || semantic=="axis:y" || semantic=="axis:z";
}
inline bool origin_annotation(const ModelAnnotationReference& reference) {
  return non_drawing_axis(reference.semantic_id);
}
// Source geometry must already be transformed into the displayed source frame.
// Each packet supplies its actual owning document and exact occurrence path.
struct ModelAnnotationSource {
  std::string document_id, instance_path;
  std::vector<kernel::ViewerDimension> dimensions;
  std::vector<kernel::ViewerEdge> construction;
  std::vector<kernel::ViewerAxis> axes;
  kernel::ModelEnvelope envelope;
  std::vector<kernel::DimensionLayoutEntry> layouts;
  std::map<kernel::ObjectEnvelopeKey,kernel::ModelEnvelope> object_frames;
  // Exact axis reference (owner, semantic); the packet supplies the occurrence.
  std::map<std::pair<std::string,std::string>,kernel::ModelEnvelope> axis_frames;
  // Thread measuring metadata also accompanies child Parts whose modeling
  // dimensions are deliberately excluded from Assembly Show/Erase.
  std::map<std::string,ThreadDesignation> threads;
};
ModelAnnotation project_model_annotation(const DrawingView&,ModelAnnotation);
kernel::ViewerDimension drawing_model_dimension(const DrawingView&,const ModelAnnotation&);
struct AxisAnnotationGeometry {
  Point2 center;
  std::vector<std::vector<Point2>> curves;
};
// Displayed axis strokes in view model units, shared by rendering and picking.
AxisAnnotationGeometry axis_annotation_geometry(const DrawingView &, const ModelAnnotation &);
void refresh_model_annotations(DrawingView &,
                               std::span<const ModelAnnotationSource>);
std::string serialize_model_annotations(const std::vector<ModelAnnotation> &);
std::vector<ModelAnnotation> deserialize_model_annotations(const std::string &);
enum class ShowEraseMode { Show, Erase };
enum class ShowEraseSelection { KeepSelected, RemoveSelected };
// Read-only queries share the picker rules without copying annotation geometry.
std::vector<ModelAnnotationReference> show_erase_candidates(
    std::span<const ModelAnnotation>, ShowEraseMode, const std::set<ModelAnnotationKind>&);
class ShowEraseSession {
public:
  explicit ShowEraseSession(const DrawingView &view);
  std::vector<ModelAnnotationReference>
  candidates(ShowEraseMode, const std::set<ModelAnnotationKind> &) const;
  std::vector<ModelAnnotation>
  preview(ShowEraseMode, ShowEraseSelection,
          const std::set<ModelAnnotationKind> &,
          const std::set<ModelAnnotationReference> &selected) const;

private:
  std::vector<ModelAnnotation> initial_;
};
} // namespace zima::drawing
