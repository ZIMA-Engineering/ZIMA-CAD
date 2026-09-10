#include <zima/viewer/dimension_presentation.hpp>
#pragma once
#include <QPointF>
#include <QRectF>
#include <cmath>
#include <zima/drawing/model_annotations.hpp>
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
  double text_angle{};
  std::vector<QPointF> centers;
};
inline ModelAnnotationLayout
model_annotation_layout(const drawing::DrawingView &view,
                        const drawing::ModelAnnotation &source, QRectF bounds, double text_width = -1) {
  auto item=drawing::project_model_annotation(view,source);
  const auto h=view.camera.horizontal,v=view.camera.vertical;
  const bool same_camera=std::hypot(h.x-item.handle_camera_horizontal[0],h.y-item.handle_camera_horizontal[1],h.z-item.handle_camera_horizontal[2])<1e-6 && std::hypot(v.x-item.handle_camera_vertical[0],v.y-item.handle_camera_vertical[1],v.z-item.handle_camera_vertical[2])<1e-6;
  if(!same_camera)item.paper_handles.clear();
  if(view.show_dimension_guides&&item.dimension_kind==kernel::ViewerDimensionKind::Linear&&item.curves.size()>1&&item.curves[1].size()>1) {
    const auto a=item.curves[1].front(),b=item.curves[1].back();
    if(std::abs(a.x-b.x)>1e-6&&std::abs(a.y-b.y)>1e-6)item.paper_handles.clear();
  }
  ModelAnnotationLayout out;
  if(item.kind==drawing::ModelAnnotationKind::Dimension && item.model_dimension) {
    const auto d=kernel::layout_dimension(*item.model_dimension,item.model_envelope,item.view_layout.value_or(item.model_layout));
    const auto project=[&](kernel::Vec3 p){return QPointF(kernel::dimension_dot(p,view.camera.horizontal)*view.scale,-kernel::dimension_dot(p,view.camera.vertical)*view.scale);};
    const auto layout=viewer::dimension_presentation(d,project,text_width<0?double(item.text.size())*2:text_width,2.5,.75);
    if(!layout.valid)return out;
    const auto paper=[](QPointF p){return QPointF(p.x(),-p.y());};
    for(const auto& curve:layout.curves){std::vector<QPointF> points;for(auto p:curve)points.push_back(paper(p));out.curves.push_back(std::move(points));}
    for(const auto& [tip,direction]:layout.arrows)out.arrows.push_back({paper(tip),paper(direction)});
    out.text=paper(layout.text_baseline);out.text_angle=layout.text_angle;
    out.handles["text"]=paper(layout.handles[0]);out.handles["arrow_first"]=paper(layout.handles[1]);
    if(d.kind!=kernel::ViewerDimensionKind::Radius)out.handles["arrow_second"]=paper(layout.handles[2]);
    return out;
  }
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
    const auto axis=drawing::axis_annotation_geometry(view,item);
    out.curves.clear();
    for(const auto& curve:axis.curves){
      std::vector<QPointF> points;
      for(const auto p:curve)points.push_back({p.x*view.scale,p.y*view.scale});
      out.curves.push_back(std::move(points));
    }
    if(!axis.curves.empty())out.centers.push_back({axis.center.x*view.scale,axis.center.y*view.scale});
    return out;
  }
  if (item.kind != drawing::ModelAnnotationKind::Dimension)
    return out;
  out.handles["text"] = out.text;
  if((item.dimension_kind==kernel::ViewerDimensionKind::Radius||item.dimension_kind==kernel::ViewerDimensionKind::Diameter)&&out.curves.size()>=3&&out.curves[0].size()>1&&out.curves[1].size()>1){
    const auto first=out.curves[0].front(),last=out.curves[0].back(),delta=last-first;const double length=std::hypot(delta.x(),delta.y());
    if(length>1e-9){out.arrows.push_back({last,delta/length});if(item.dimension_kind==kernel::ViewerDimensionKind::Diameter)out.arrows.push_back({first,-delta/length});}
    out.handles["arrow_first"]=out.curves[1].back();out.curves[2].back()=out.text;return out;
  }

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
