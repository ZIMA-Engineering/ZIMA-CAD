#pragma once
#include <QPointF>
#include <QRectF>
#include <cmath>
#include <zima/drawing/drawing_document.hpp>
namespace zima::app {
inline std::string
model_annotation_key(const drawing::ModelAnnotationReference &r) {
  std::string s;
  for (const auto *v :
       {&r.document_id, &r.owner_id, &r.semantic_id, &r.instance_path})
    s += std::to_string(v->size()) + ":" + *v;
  return s;
}
struct ModelAnnotationLayout {
  std::vector<std::vector<QPointF>> curves;
  std::map<std::string, QPointF> handles;
  std::vector<std::pair<QPointF, QPointF>> arrows;
  QPointF text;
  std::vector<QPointF> centers;
};
inline ModelAnnotationLayout
model_annotation_layout(const drawing::DrawingView &view,
                        const drawing::ModelAnnotation &item, QRectF bounds) {
  ModelAnnotationLayout out;
  for (const auto &curve : item.curves) {
    std::vector<QPointF> points;
    for (auto p : curve)
      points.push_back({p.x * view.scale, p.y * view.scale});
    out.curves.push_back(std::move(points));
  }
  out.text = {item.text_anchor.x * view.scale, item.text_anchor.y * view.scale};
  if (auto i = item.paper_handles.find("text"); i != item.paper_handles.end())
    out.text = {i->second.x, i->second.y};
  if (item.kind == drawing::ModelAnnotationKind::Axis) {
    if (out.curves.empty() || out.curves[0].size() < 2)
      return out;
    const auto center=(out.curves[0][0]+out.curves[0][1])*.5;
    out.centers.push_back(center);
    auto a = out.curves[0][0], d = out.curves[0][1] - a;
    if(d.manhattanLength()<1e-7) {
      // End-on cylinder/hole: one center mark for this exact source axis.
      constexpr double half=3.0;
      out.curves={{{center.x()-half,center.y()},{center.x()+half,center.y()}},
                  {{center.x(),center.y()-half},{center.x(),center.y()+half}}};
    } else {
      // Preserve the persisted axial span instead of extending every hole's
      // axis across the complete drawing view.
      const double length=std::hypot(d.x(),d.y());
      const auto extension=d/length*2.0;
      out.curves={{a-extension,a+d+extension}};
    }
    return out;
  }
  if (item.kind != drawing::ModelAnnotationKind::Dimension)
    return out;
  out.handles["text"] = out.text;
  const bool angular =
      item.dimension_kind == kernel::ViewerDimensionKind::Angular;
  const std::size_t line = angular ? 0 : 1;
  if (out.curves.size() <= line || out.curves[line].size() < 2)
    return out;
  auto &dimension = out.curves[line];
  if (angular && out.curves.size() >= 3 && !out.curves[1].empty()) {
    const auto center = out.curves[1].front();
    auto i = item.paper_handles.find("arrow_first");
    bool first = true;
    if (i == item.paper_handles.end()) {
      i = item.paper_handles.find("arrow_second");
      first = false;
    }
    if (i != item.paper_handles.end()) {
      const auto old = (first ? dimension.front() : dimension.back()) - center;
      const QPointF requested(i->second.x - center.x(),
                              i->second.y - center.y());
      const double radius = std::hypot(old.x(), old.y());
      if (radius > 1e-9) {
        const double ratio =
            std::max(.1, std::hypot(requested.x(), requested.y())) / radius;
        for (auto &p : dimension)
          p = center + (p - center) * ratio;
        out.curves[1].back() = dimension.front();
        out.curves[2].back() = dimension.back();
      }
    }
  }
  if (!angular) {
    for (const auto &[key, index] :
         std::array<std::pair<const char *, std::size_t>, 2>{
             {{"arrow_first", 0}, {"arrow_second", dimension.size() - 1}}})
      if (auto i = item.paper_handles.find(key); i != item.paper_handles.end())
        dimension[index] = {i->second.x, i->second.y};
    if (out.curves.size() >= 3) {
      out.curves[0].back() = dimension.front();
      out.curves[2].front() = dimension.back();
    }
  }
  out.handles["arrow_first"] = dimension.front();
  out.handles["arrow_second"] = dimension.back();
  const auto arrow = [&](QPointF tip, QPointF delta) {
    const double n = std::hypot(delta.x(), delta.y());
    if (n > 1e-9)
      out.arrows.push_back({tip, delta / n});
  };
  arrow(dimension.front(), dimension.front() - dimension[1]);
  arrow(dimension.back(), dimension.back() - dimension[dimension.size() - 2]);
  return out;
}
} // namespace zima::app
