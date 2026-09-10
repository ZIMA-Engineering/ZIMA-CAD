#include <zima/document/dimension_layout_json.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <zima/drawing/model_annotations.hpp>
namespace zima::drawing {
namespace {
using nlohmann::json;
double dot(kernel::Vec3 a, kernel::Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
kernel::Vec3 subtract(kernel::Vec3 a, kernel::Vec3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
kernel::Vec3 cross(kernel::Vec3 a, kernel::Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
void validate(const std::vector<ModelAnnotation> &items) {
  std::set<ModelAnnotationReference> seen;
  for (const auto &item : items) {
    if (item.source.document_id.empty() || item.source.owner_id.empty() ||
        item.source.semantic_id.empty() || !seen.insert(item.source).second)
      throw std::invalid_argument(
          "Invalid or duplicate model annotation identity");
    if (item.kind != ModelAnnotationKind::Dimension &&
        item.kind != ModelAnnotationKind::Axis &&
        item.kind != ModelAnnotationKind::Construction)
      throw std::invalid_argument("Invalid annotation kind");
    if (static_cast<int>(item.dimension_kind) < 0 ||
        static_cast<int>(item.dimension_kind) > 3)
      throw std::invalid_argument("Invalid dimension annotation kind");
    auto point = [](Point2 p) {
      if (!std::isfinite(p.x) || !std::isfinite(p.y))
        throw std::invalid_argument("Invalid annotation position");
    };
    for (auto value : item.plane_normal)
      if (!std::isfinite(value))
        throw std::invalid_argument("Invalid annotation plane normal");
    point(item.text_anchor);
    for (const auto &line : item.curves)
      for (auto p : line)
        point(p);
    if (!std::isfinite(item.value))
      throw std::invalid_argument("Invalid annotation value");
    for (const auto &[key, p] : item.paper_handles) {
      if (key.empty())
        throw std::invalid_argument("Empty annotation handle");
      point(p);
    }
  }
}
} // namespace
ModelAnnotation project_model_annotation(const DrawingView& view,ModelAnnotation item) {
    if(item.model_axis) {
        const auto project=[&](kernel::Vec3 p){return Point2{dot(p,view.camera.horizontal),dot(p,view.camera.vertical)};};
        item.curves={{project((*item.model_axis)[0]),project((*item.model_axis)[1])}};
        return item;
    }
    if(!item.model_dimension)return item;
    const auto d=kernel::layout_dimension(*item.model_dimension,item.model_envelope,item.view_layout.value_or(item.model_layout));
    const auto project=[&](kernel::Vec3 p){return Point2{dot(p,view.camera.horizontal),dot(p,view.camera.vertical)};};
      item.value = d.value;
      item.plane_normal = {d.plane_normal.x, d.plane_normal.y,
                           d.plane_normal.z};
      item.dimension_kind = d.kind;
      item.text_anchor = project(d.label_position.value_or(d.line_second));
      if (!d.display_text_override.empty())
        item.text = d.display_text_override;
      else {
        std::ostringstream text;
        text << d.label_prefix << std::setprecision(12) << d.value
             << kernel::dimension_unit_text(d.unit_suffix);
        item.text = text.str();
      }
      if (d.kind == kernel::ViewerDimensionKind::Angular) {
        const auto u = subtract(d.line_first, d.witness_first);
        const double radius = std::sqrt(dot(u, u)),
                     normal_length =
                         std::sqrt(dot(d.plane_normal, d.plane_normal));
        if (radius <= 1e-12 || normal_length <= 1e-12)
          throw std::invalid_argument("Invalid angular annotation frame");
        auto v = cross(d.plane_normal, u);
        v = {v.x / normal_length, v.y / normal_length, v.z / normal_length};
        if (!std::isfinite(d.sweep_degrees) || std::abs(d.sweep_degrees) > 3600)
          throw std::invalid_argument("Invalid angular annotation sweep");
        std::vector<Point2> arc;
        const double angle = d.sweep_degrees * std::numbers::pi / 180.;
        const int count = std::max(
            2, static_cast<int>(std::ceil(std::abs(d.sweep_degrees) / 3.)));
        if (count > 1200)
          throw std::invalid_argument("Invalid angular annotation sweep");
        for (int i = 0; i <= count; ++i) {
          double t = angle * i / count;
          arc.push_back(project(
              {d.witness_first.x + u.x * std::cos(t) + v.x * std::sin(t),
               d.witness_first.y + u.y * std::cos(t) + v.y * std::sin(t),
               d.witness_first.z + u.z * std::cos(t) + v.z * std::sin(t)}));
        }
        item.curves = {std::move(arc),
                       {project(d.witness_first), project(d.line_first)},
                       {project(d.witness_first), project(d.line_second)}};
      } else
        item.curves = {{project(d.witness_first), project(d.line_first)},
                       {project(d.line_first), project(d.line_second)},
                       {project(d.line_second), project(d.witness_second)}};
    return item;
}
void refresh_model_annotations(DrawingView &view,
                               std::span<const ModelAnnotationSource> sources) {
  auto next = view.model_annotations;
  for (auto &item : next)
    item.unresolved = true;
  std::set<ModelAnnotationReference> incoming;
  const auto project = [&](kernel::Vec3 p) {
    return Point2{dot(p, view.camera.horizontal), dot(p, view.camera.vertical)};
  };
  const auto add = [&](ModelAnnotation item) {
    if (!incoming.insert(item.source).second)
      throw std::invalid_argument(
          "Ambiguous model annotation source: " + item.source.document_id +
          "/" + item.source.instance_path + "/" + item.source.owner_id + "/" +
          item.source.semantic_id);
    const auto old = std::find_if(next.begin(), next.end(), [&](const auto &x) {
      return x.source == item.source;
    });
    if (old != next.end()) {
      if (old->kind != item.kind)
        throw std::invalid_argument("Annotation source changed kind");
      item.visible = old->visible;
      item.paper_handles = old->paper_handles;
      item.view_layout=old->view_layout;
      item.handle_camera_horizontal=old->handle_camera_horizontal;item.handle_camera_vertical=old->handle_camera_vertical;
      item=project_model_annotation(view,std::move(item));
      *old = std::move(item);
    } else
      next.push_back(std::move(item));
  };
  for (const auto &source : sources) {
    const auto identity = [&](const auto &r) {
      if (!r.instance_path.empty() && r.instance_path != source.instance_path)
        throw std::invalid_argument("Annotation occurrence ownership mismatch");
      return ModelAnnotationReference{source.document_id, r.owner_id,
                                      r.semantic_key, source.instance_path};
    };
    for (const auto &d : source.dimensions) {
      ModelAnnotation item;
      item.source = identity(d.reference);
      item.kind = ModelAnnotationKind::Dimension;
      item.model_dimension=d;item.model_envelope=source.envelope;
      if(auto frame=source.object_frames.find({d.reference.owner_id,d.reference.instance_path});frame!=source.object_frames.end()&&frame->second.valid)item.model_envelope=frame->second;
      if(item.model_envelope.valid)item.model_layout.envelope_offset=8.0;
      if(const auto* layout=kernel::find_dimension_layout(source.layouts,d.reference))item.model_layout=*layout;
      item=project_model_annotation(view,std::move(item));
      add(std::move(item));
    }
    for (const auto &edge : source.construction) {
      if (!edge.construction)
        continue;
      ModelAnnotation item;
      item.source = identity(edge.reference);
      item.kind = ModelAnnotationKind::Construction;
      std::vector<Point2> curve;
      for (auto p : edge.points)
        curve.push_back(project(p));
      item.curves.push_back(std::move(curve));
      add(std::move(item));
    }
    for (const auto &axis : source.axes) {
      ModelAnnotation item;
      item.source = identity(axis.reference);
      item.kind = ModelAnnotationKind::Axis;
      item.model_envelope=source.envelope;
      if(auto frame=source.object_frames.find({axis.reference.owner_id,axis.reference.instance_path});
         frame!=source.object_frames.end() && frame->second.valid &&
         kernel::dimension_dot(kernel::dimension_sub(frame->second.maximum,frame->second.minimum),
                               kernel::dimension_sub(frame->second.maximum,frame->second.minimum))>1e-12)
          item.model_envelope=frame->second;
      if(auto frame=source.axis_frames.find({axis.reference.owner_id,axis.reference.semantic_key});
         frame!=source.axis_frames.end() && frame->second.valid)item.model_envelope=frame->second;
      item.text_anchor = project(axis.point);
      const double length = std::sqrt(dot(axis.direction, axis.direction));
      if (length <= 1e-12 || !std::isfinite(axis.display_length) ||
          axis.display_length <= 0)
        throw std::invalid_argument("Invalid model axis");
      auto a = axis.point, b = a;
      const double half = axis.display_length / 2 / length;
      a = {a.x - axis.direction.x * half, a.y - axis.direction.y * half,
           a.z - axis.direction.z * half};
      b = {b.x + axis.direction.x * half, b.y + axis.direction.y * half,
           b.z + axis.direction.z * half};
      item.model_axis=std::array<kernel::Vec3,2>{a,b};
      item.curves = {{project(a), project(b)}};
      add(std::move(item));
    }
  }
  validate(next);
  view.model_annotations = std::move(next);
}
std::string
serialize_model_annotations(const std::vector<ModelAnnotation> &items) {
  validate(items);
  json result = json::array();
  for (const auto &item : items) {
    json curves = json::array(), handles = json::object();
    for (const auto &line : item.curves) {
      json points = json::array();
      for (auto p : line)
        points.push_back({p.x, p.y});
      curves.push_back(std::move(points));
    }
    for (const auto &[key, p] : item.paper_handles)
      handles[key] = {p.x, p.y};
    result.push_back({{"source",
                       {{"document", item.source.document_id},
                        {"owner", item.source.owner_id},
                        {"semantic", item.source.semantic_id},
                        {"instance", item.source.instance_path}}},
                      {"kind", static_cast<int>(item.kind)},
                      {"dimension_kind", static_cast<int>(item.dimension_kind)},
                      {"plane_normal", item.plane_normal},
                      {"curves", curves},
                      {"text_anchor", {item.text_anchor.x, item.text_anchor.y}},
                      {"text", item.text},
                      {"value", item.value},
                      {"visible", item.visible},
                      {"model_axis",item.model_axis?json{document::dimension_vec_json((*item.model_axis)[0]),document::dimension_vec_json((*item.model_axis)[1])}:json(nullptr)},
                      {"unresolved", item.unresolved},
                      {"paper_handles", handles},
                      {"model_dimension",item.model_dimension?document::dimension_geometry_json(*item.model_dimension):json(nullptr)},
                      {"model_envelope",{{"minimum",document::dimension_vec_json(item.model_envelope.minimum)},{"maximum",document::dimension_vec_json(item.model_envelope.maximum)},{"valid",item.model_envelope.valid},{"origin",document::dimension_vec_json(item.model_envelope.origin)},{"axes",{document::dimension_vec_json(item.model_envelope.axes[0]),document::dimension_vec_json(item.model_envelope.axes[1]),document::dimension_vec_json(item.model_envelope.axes[2])}}}},
                      {"model_layout",document::dimension_layout_json(item.model_layout)},
                      {"view_layout",item.view_layout?document::dimension_layout_json(*item.view_layout):json(nullptr)},
                      {"handle_camera_horizontal",item.handle_camera_horizontal},{"handle_camera_vertical",item.handle_camera_vertical}});
  }
  return result.dump();
}
std::vector<ModelAnnotation>
deserialize_model_annotations(const std::string &value) {
  std::vector<ModelAnnotation> result;
  auto root = json::parse(value);
  if (!root.is_array())
    throw std::invalid_argument("Annotation list expected");
  for (const auto &j : root) {
    ModelAnnotation item;
    const auto &r = j.at("source");
    item.source = {r.at("document"), r.at("owner"), r.at("semantic"),
                   r.at("instance")};
    item.kind = static_cast<ModelAnnotationKind>(j.at("kind").get<int>());
    item.dimension_kind =
        static_cast<kernel::ViewerDimensionKind>(j.value("dimension_kind", 0));
    item.plane_normal = j.at("plane_normal").get<std::array<double, 3>>();
    for (const auto &line : j.at("curves")) {
      std::vector<Point2> points;
      for (const auto &p : line)
        points.push_back({p.at(0), p.at(1)});
      item.curves.push_back(std::move(points));
    }
    item.text_anchor = {j.at("text_anchor").at(0), j.at("text_anchor").at(1)};
    item.text = j.at("text");
    item.value = j.at("value");
    item.visible = j.at("visible");
    if(j.contains("model_axis")&&!j.at("model_axis").is_null())
        item.model_axis=std::array<kernel::Vec3,2>{document::dimension_vec_from_json(j.at("model_axis").at(0)),document::dimension_vec_from_json(j.at("model_axis").at(1))};
    item.unresolved = j.at("unresolved");
    for (const auto &[key, p] : j.at("paper_handles").items())
      item.paper_handles[key] = {p.at(0), p.at(1)};
    if(j.contains("model_dimension")&&!j.at("model_dimension").is_null())item.model_dimension=document::dimension_geometry_from_json(j.at("model_dimension"));
    if(j.contains("model_envelope")){const auto& b=j.at("model_envelope");item.model_envelope={document::dimension_vec_from_json(b.at("minimum")),document::dimension_vec_from_json(b.at("maximum")),b.at("valid")};if(b.contains("origin"))item.model_envelope.origin=document::dimension_vec_from_json(b.at("origin"));if(b.contains("axes"))for(int i=0;i<3;++i)item.model_envelope.axes[i]=document::dimension_vec_from_json(b.at("axes").at(i));}
    if(j.contains("model_layout"))item.model_layout=document::dimension_layout_from_json(j.at("model_layout"));
    if(j.contains("view_layout")&&!j.at("view_layout").is_null())item.view_layout=document::dimension_layout_from_json(j.at("view_layout"));
    if(j.contains("handle_camera_horizontal"))item.handle_camera_horizontal=j.at("handle_camera_horizontal").get<std::array<double,3>>();
    if(j.contains("handle_camera_vertical"))item.handle_camera_vertical=j.at("handle_camera_vertical").get<std::array<double,3>>();
    result.push_back(std::move(item));
  }
  validate(result);
  return result;
}
ShowEraseSession::ShowEraseSession(const DrawingView &view)
    : initial_(view.model_annotations) {
  validate(initial_);
}
std::vector<ModelAnnotationReference>
ShowEraseSession::candidates(ShowEraseMode mode,
                             const std::set<ModelAnnotationKind> &kinds) const {
  std::vector<ModelAnnotationReference> result;
  for (const auto &item : initial_)
    if (!item.unresolved &&
        kinds.contains(item.kind) &&
        item.visible == (mode == ShowEraseMode::Erase))
      result.push_back(item.source);
  return result;
}
std::vector<ModelAnnotation> ShowEraseSession::preview(
    ShowEraseMode mode, ShowEraseSelection selection,
    const std::set<ModelAnnotationKind> &kinds,
    const std::set<ModelAnnotationReference> &selected) const {
  const auto offered = candidates(mode, kinds);
  for (const auto &id : selected)
    if (std::find(offered.begin(), offered.end(), id) == offered.end())
      throw std::invalid_argument("Annotation was not offered by the command");
  auto next = initial_;
  for (auto &item : next) {
    if (std::find(offered.begin(), offered.end(), item.source) == offered.end())
      continue;
    const bool keep = selection == ShowEraseSelection::KeepSelected
                          ? selected.contains(item.source)
                          : !selected.contains(item.source);
    item.visible = keep;
  }
  return next;
}
} // namespace zima::drawing
